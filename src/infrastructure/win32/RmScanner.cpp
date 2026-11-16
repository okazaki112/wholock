#include "RmScanner.h"
#include "ProcessQuery.h"
#include <windows.h>
#include <RestartManager.h>
#include <vector>

namespace who::win32 {
namespace {

std::wstring lastErrorText(DWORD code) {
    LPWSTR buf = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                   FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, code, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring msg = buf ? std::wstring(buf, n) : L"(无消息)";
    if (buf) LocalFree(buf);
    while (!msg.empty() && (msg.back() == L'\r' || msg.back() == L'\n' || msg.back() == L' ')) msg.pop_back();
    return msg;
}

LockKind kindFromAppType(RM_APP_TYPE t) {
    switch (t) {
    case RmService:      return LockKind::ServiceModule;
    case RmExplorer:     return LockKind::ProcessModule;
    case RmMainWindow:   return LockKind::Unknown; // 窗口类应用持有句柄：RM 不区分粒度，由 HT 通道补
    case RmOtherWindow:
    case RmConsole:      return LockKind::Unknown;
    default:             return LockKind::Unknown; // RmUnknownApp / RmCritical 等
    }
}

std::wstring imageQueryPath(DWORD pid) { // 薄封装：共享 ProcessQuery（避免 HT/RM 两处复制）
    HANDLE hp = openProcForDup(pid);
    if (!hp) return {};
    std::wstring img = queryFullImage(hp);
    CloseHandle(hp);
    return img;
}

struct SessionGuard {
    DWORD s; explicit SessionGuard(DWORD sess) : s(sess) {}
    ~SessionGuard() { RmEndSession(s); }
    SessionGuard(const SessionGuard&) = delete;
};

} // namespace

ScanOutcome RmScanner::scan(const ScanQuery& q) {
    ScanOutcome oc;
    DWORD session = 0;
    WCHAR key[CCH_RM_SESSION_KEY + 1] = {};
    DWORD rc = RmStartSession(&session, 0, key);
    if (rc != ERROR_SUCCESS) { oc.error = L"RmStartSession 失败: " + lastErrorText(rc); return oc; }
    SessionGuard guard(session);

    if (q.targets.empty()) { oc.error = L"无扫描目标"; return oc; }
    std::vector<PCWSTR> resources; // v1.0 F4：一次注册全部目标文件（目录展开由 PrepareTargets 完成）
    resources.reserve(q.targets.size());
    for (const auto& t : q.targets) resources.push_back(t.originalPath.c_str());
    rc = RmRegisterResources(session, static_cast<UINT>(resources.size()), resources.data(), 0, nullptr, 0, nullptr);
    if (rc != ERROR_SUCCESS) { oc.error = L"RmRegisterResources 失败: " + lastErrorText(rc); return oc; }

    // SDK 头文件权威原型（旧 MSDN 文档描述有歧义，以 RestartManager.h 为准）：
    // RmGetList(h, [out]UINT* pNeeded, [inout]UINT* pCapacity(input=提供数/output=写入数), array, [out]LPDWORD* pRebootReasons)
    UINT need = 0, capacity = 0;
    DWORD reasons = 0;
    rc = RmGetList(session, &need, &capacity, nullptr, &reasons); // 预查：capacity 输入 0
    if (rc == ERROR_SUCCESS && need == 0u) { oc.ok = true; return oc; } // 无人持有
    if (rc != ERROR_SUCCESS && rc != ERROR_MORE_DATA && need == 0u) {
        oc.error = L"RmGetList 预查失败(code=" + std::to_wstring(rc) + L"): " + lastErrorText(rc);
        return oc;
    }

    std::vector<RM_PROCESS_INFO> arr;
    for (int attempt = 0; attempt < 8; ++attempt) {
        capacity = static_cast<UINT>(need) + 8u;                 // 并发增长余量
        arr.assign(capacity, RM_PROCESS_INFO{});
        rc = RmGetList(session, &need, &capacity, arr.data(), &reasons);
        if (rc == ERROR_SUCCESS) break;
        if (rc != ERROR_MORE_DATA) {
            oc.error = L"RmGetList 取数失败(code=" + std::to_wstring(rc) + L" need=" + std::to_wstring(need) +
                       L"): " + lastErrorText(rc);
            return oc;
        }
    }
    if (rc != ERROR_SUCCESS) { oc.error = L"RmGetList 连续 ERROR_MORE_DATA（8 次重试上限）"; return oc; }

    oc.locks.reserve(capacity); // capacity 出参=实际写入数
    for (UINT i = 0; i < capacity; ++i) {
        LockInfo li;
        li.pid = arr[i].Process.dwProcessId;
        li.name = arr[i].strAppName;
        li.imagePath = imageQueryPath(li.pid);
        li.kind = kindFromAppType(arr[i].ApplicationType);
        li.engines = EngineRM;
        li.seenByRm = true;
        oc.locks.push_back(std::move(li));
    }
    oc.ok = true;
    return oc;
}

} // namespace who::win32
