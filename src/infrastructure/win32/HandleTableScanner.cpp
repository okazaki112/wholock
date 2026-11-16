#include "HandleTableScanner.h"
#include "Privileges.h"
#include "ProcessQuery.h"
#include "WindowsPathMap.h"

#include <windows.h>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace who::win32 {
namespace {

using NTSTATUS = long; // ntdll 原型，不经 winternl.h（其结构名与此处冲突）
constexpr NTSTATUS kInfoLengthMismatch = static_cast<NTSTATUS>(0xC0000004L);
constexpr ULONG kSystemExtendedHandleInformation = 0x40;

struct SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX {
    PVOID Object;
    ULONG_PTR UniqueProcessId;
    ULONG_PTR HandleValue;
    ULONG GrantedAccess;
    USHORT CreatorBackTraceIndex;
    USHORT ObjectTypeIndex;
    ULONG HandleAttributes;
    ULONG Reserved;
};
struct SYSTEM_HANDLE_INFORMATION_EX {
    ULONG_PTR NumberOfHandles;
    ULONG_PTR Reserved;
    SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX Handles[1];
};

using NtQuerySystemInformationFn = NTSTATUS(NTAPI*)(ULONG, PVOID, ULONG, PULONG);

NtQuerySystemInformationFn resolveNtQuerySystemInformation() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return nullptr;
    return reinterpret_cast<NtQuerySystemInformationFn>(
        GetProcAddress(ntdll, "NtQuerySystemInformation"));
}

struct ProcCache {
    HANDLE h = nullptr;
    std::wstring image;
    std::wstring baseLower;
};

} // namespace

std::wstring HandleTableScanner::keyFromWinPath(std::wstring winPath) const {
    // GetFinalPathNameByHandle(VOLUME_NAME_DOS) 输出 \\?\C:\... 规范形式；与 domain 键对齐：剥前缀/UNC 归位/小写
    if (winPath.rfind(L"\\??\\", 0) == 0) winPath.erase(0, 4);
    else if (winPath.rfind(L"\\\\?\\UNC\\", 0) == 0) { winPath.erase(0, 8); winPath.insert(0, L"\\\\"); }
    else if (winPath.rfind(L"\\\\?\\", 0) == 0) winPath.erase(0, 4);
    // F1：无挂载点卷输出 Volume{guid}\ → 反查盘符；反查不到保持原样（字符串比对自然不中→身份比对兜底）
    if (winPath.rfind(L"Volume{", 0) == 0 && pathmap_) {
        const std::wstring dos = pathmap_->dosFromVolumeGuid(L"\\\\?\\" + winPath);
        if (!dos.empty()) winPath = dos + winPath.substr(winPath.find_last_of(L'}') + 1);
    }
    for (auto& c : winPath) if (c >= L'A' && c <= L'Z') c = wchar_t(c - L'A' + L'a');
    return winPath;
}

ScanOutcome HandleTableScanner::scan(const ScanQuery& q) { return scanImpl(q, /*fullTable=*/false); }
ScanOutcome HandleTableScanner::scanFull(const ScanQuery& q) { return scanImpl(q, /*fullTable=*/true); }

ScanOutcome HandleTableScanner::scanImpl(const ScanQuery& q, bool fullTable) {
    ScanOutcome oc;
    adminOk_ = enableDebugPrivilege(); // 失败=部分进程打不开，标记提示，不退出（降级纪律）

    // 候选门（v0.3 避挂设计）：非全表模式下仅解析前序引擎报出的 PID 句柄。
    // 全表模式（F6）必须由隔离子进程调用——挂死风险由 DeepScanRunner 超时杀兜底。
    if (q.candidateHints.empty() && !fullTable) {
        oc.ok = true;
        oc.error = L"无候选提示（RM 未先跑或未命中），HT 跳过全表名解析以避免死句柄挂起";
        return oc;
    }

    const auto pNtQuery = resolveNtQuerySystemInformation();
    if (!pNtQuery) { oc.ok = false; oc.error = L"未找到 ntdll!NtQuerySystemInformation"; return oc; }

    PVOID buf = nullptr;
    ULONG size = 1u << 20, needed = 0;
    NTSTATUS st = kInfoLengthMismatch;
    for (int attempt = 0; attempt < 8; ++attempt) {
        buf = VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!buf) { oc.ok = false; oc.error = L"VirtualAlloc 失败（枚举缓冲）"; return oc; }
        st = pNtQuery(kSystemExtendedHandleInformation, buf, size, &needed);
        if (st >= 0) break; // NT_SUCCESS：快照读出不挂死，挂死点仅在逐句柄名解析
        VirtualFree(buf, 0, MEM_RELEASE);
        buf = nullptr;
        if (st != kInfoLengthMismatch) {
            wchar_t b[16]; swprintf_s(b, L"%08lx", static_cast<unsigned long>(st));
            oc.ok = false; oc.error = L"NtQuerySystemInformation 失败(STATUS=0x" + std::wstring(b) + L")";
            return oc;
        }
        size = needed ? (needed + (1u << 16)) : size * 2;
        if (size >= (512u << 20)) {
            oc.ok = false; oc.error = L"句柄表超 512MB 缓冲上限，放弃枚举";
            return oc;
        }
    }
    if (st < 0) { // 重试用尽仍 mismatch
        if (buf) VirtualFree(buf, 0, MEM_RELEASE);
        oc.ok = false; oc.error = L"句柄表容量在枚举期间持续变化（8 次重试用尽）";
        return oc;
    }

    auto* info = static_cast<SYSTEM_HANDLE_INFORMATION_EX*>(buf);
    const ULONG_PTR total = info->NumberOfHandles;
    lastEnumCount_ = total;

    std::unordered_set<uint32_t> hints(q.candidateHints.begin(), q.candidateHints.end()); // 解析范围
    std::unordered_map<ULONG_PTR, ProcCache> procs;                                        // pid→缓存
    std::unordered_set<uint32_t> emitted;
    wchar_t pathBuf[4096];

    bool cancelled = false;
    for (ULONG_PTR i = 0; i < total; ++i) {
        if ((i & 0x3FFu) == 0 && cancel_.load(std::memory_order_relaxed)) { cancelled = true; break; }
        const auto& e = info->Handles[i];
        const uint32_t pid = static_cast<uint32_t>(e.UniqueProcessId);
        if ((!fullTable && !hints.contains(pid)) || emitted.contains(pid)) continue; // 核心：候选门外不碰无关进程句柄名

        auto it = procs.find(e.UniqueProcessId);
        if (it == procs.end()) {
            ProcCache pc;
            pc.h = openProcForDup(pid);
            if (pc.h) pc.image = queryFullImage(pc.h, &pc.baseLower);
            it = procs.emplace(e.UniqueProcessId, std::move(pc)).first;
        }
        auto& pc = it->second;
        if (!pc.h) continue;

        HANDLE dup = nullptr;
        if (!DuplicateHandle(pc.h, reinterpret_cast<HANDLE>(e.HandleValue), GetCurrentProcess(),
                             &dup, 0, FALSE, DUPLICATE_SAME_ACCESS))
            continue; // 进程退出/句柄回收：正常噪声
        bool matched = false;
        size_t hitTarget = 0;
        if (GetFileType(dup) == FILE_TYPE_DISK) { // 非磁盘不查名 → 绕开 NtQueryObject 挂死点
            std::wstring handleKey;
            const DWORD n = GetFinalPathNameByHandleW(dup, pathBuf, 4095,
                                                      FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
            if (n != 0 && n < 4095) handleKey = keyFromWinPath(std::wstring(pathBuf, n));
            bool fiiQueried = false; std::uint64_t vol = 0, fid = 0;
            for (size_t ti = 0; ti < q.targets.size(); ++ti) {
                const auto& t = q.targets[ti];
                if (!handleKey.empty() && handleKey == t.normalizedPath) { hitTarget = ti; matched = true; break; }
                if (t.identity.valid) { // F1 身份兜底（8.3/硬链/软链等价类），单句柄一次查询
                    if (!fiiQueried) {
                        fiiQueried = true;
                        FILE_ID_INFO fii{};
                        if (GetFileInformationByHandleEx(dup, FileIdInfo, &fii, sizeof(fii))) {
                            vol = fii.VolumeSerialNumber;
                            memcpy(&fid, &fii.FileId, sizeof(fid)); // NTFS FileId64 = FILE_ID_128 低 8 字节
                        }
                    }
                    if (vol == t.identity.volumeSerial && fid == t.identity.fileId) { hitTarget = ti; matched = true; break; }
                }
            }
        }
        CloseHandle(dup);
        if (matched) {
            LockInfo li;
            li.pid = pid;
            li.name = pc.baseLower.empty() ? (L"pid-" + std::to_wstring(pid)) : pc.baseLower;
            li.imagePath = pc.image;
            li.kind = LockKind::FileHandle;
            li.engines = EngineHT;
            li.occupiedFile = q.targets[hitTarget].originalPath;
            oc.locks.push_back(std::move(li));
            emitted.insert(pid);
        }
    }

    for (auto& [pidKey, pc] : procs)
        if (pc.h) CloseHandle(pc.h);
    VirtualFree(buf, 0, MEM_RELEASE);

    if (cancelled) { oc.ok = false; oc.error = L"枚举被取消"; return oc; }
    oc.ok = true;
    if (!adminOk_ && oc.locks.empty()) oc.error = L"未启用 SeDebugPrivilege：其他会话进程的补盲可能不完整";
    return oc;
}

// v1.0 F7：枚举指定进程句柄明细。同 scanFull——只允许在隔离子进程内调用（挂死风险由超时杀兜底）。
std::vector<HandleRow> enumerateProcessHandles(DWORD pid, const std::atomic<bool>* cancel) {
    std::vector<HandleRow> rows;
    const auto pNtQuery = resolveNtQuerySystemInformation();
    if (!pNtQuery) return rows;

    PVOID buf = nullptr;
    ULONG size = 1u << 20, needed = 0;
    NTSTATUS st = kInfoLengthMismatch;
    for (int attempt = 0; attempt < 8; ++attempt) {
        buf = VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!buf) return rows;
        st = pNtQuery(kSystemExtendedHandleInformation, buf, size, &needed);
        if (st >= 0) break;
        VirtualFree(buf, 0, MEM_RELEASE); buf = nullptr;
        if (st != kInfoLengthMismatch) return rows;
        size = needed ? (needed + (1u << 16)) : size * 2;
        if (size >= (512u << 20)) return rows;
    }
    if (st < 0) { if (buf) VirtualFree(buf, 0, MEM_RELEASE); return rows; }

    auto* info = static_cast<SYSTEM_HANDLE_INFORMATION_EX*>(buf);
    if (const HANDLE hp = openProcForDup(pid)) {
        wchar_t pathBuf[4096];
        for (ULONG_PTR i = 0; i < info->NumberOfHandles; ++i) {
            if ((i & 0x3FFu) == 0 && cancel && cancel->load(std::memory_order_relaxed)) break;
            const auto& e = info->Handles[i];
            if (static_cast<DWORD>(e.UniqueProcessId) != pid) continue;
            HandleRow r;
            r.handleValue = static_cast<std::uint64_t>(static_cast<uintptr_t>(e.HandleValue));
            r.grantedAccess = e.GrantedAccess;
            HANDLE dup = nullptr;
            if (DuplicateHandle(hp, reinterpret_cast<HANDLE>(e.HandleValue), GetCurrentProcess(),
                                &dup, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
                switch (GetFileType(dup)) {
                case FILE_TYPE_DISK: {
                    r.type = 1;
                    const DWORD n = GetFinalPathNameByHandleW(dup, pathBuf, 4095,
                                                              FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
                    if (n != 0 && n < 4095) r.path.assign(pathBuf, n);
                    break;
                }
                case FILE_TYPE_PIPE: r.type = 2; break;
                default:             r.type = 3; break;
                }
                CloseHandle(dup);
            }
            rows.push_back(std::move(r));
        }
        CloseHandle(hp);
    }
    VirtualFree(buf, 0, MEM_RELEASE);
    return rows;
}

} // namespace who::win32
