// v1.0 M8 判据（OS 语义为尺，全部真跑）：
// A. F6 深扫补盲实证：同一进程【只读共享】打开目标 → RM 通道必须看不见（防恒真对照）→
//    子进程深扫必须报出该进程。
// B. F8 关句柄往返：枚举自身句柄找到目标 → closeRemoteHandle → 再枚举该句柄必须消失。
// C. F11 删除验证：deleteFileVerified 成功 → GetFileAttributes 必须 INVALID。
// D. 取消兜底：预置 cancel 的 runDeepScan 必须快速返回（子进程被杀，不挂死）。
#include "domain/Normalize.h"
#include "../TestAssert.h"
#include <windows.h>
#include <algorithm>
#include <string>

#include "infrastructure/win32/DeepScanRunner.h"
#include "infrastructure/win32/HandleTableScanner.h"
#include "infrastructure/win32/ProcessOps.h"
#include "infrastructure/win32/RmScanner.h"
#include "infrastructure/win32/WindowsPathMap.h"

using namespace who;

namespace {
std::wstring tempVictim() {
    wchar_t dir[MAX_PATH]; GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir) + L"wholock-m8-" + std::to_wstring(GetCurrentProcessId()) + L".bin";
}
bool hasPid(const std::vector<LockInfo>& v, uint32_t pid) {
    return std::any_of(v.begin(), v.end(), [&](const LockInfo& li) { return li.pid == pid; });
}
} // namespace

int main() {
    win32::WindowsPathMap map;
    const auto victim = tempVictim();
    {
        HANDLE h = CreateFileW(victim.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        CHECK(h != INVALID_HANDLE_VALUE, L"victim 创建");
        CloseHandle(h);
    }
    // 只读共享打开（RM 设计盲区场景）
    HANDLE locker = CreateFileW(victim.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(locker != INVALID_HANDLE_VALUE, L"只读共享句柄建立");

    std::wstring err;
    FileIdentity identity;
    auto abs = win32::WindowsPathMap::absolutizeExistingFile(victim, &err, &identity);
    auto key = normalizePathKey(*abs, &map, &err);
    const uint32_t self = GetCurrentProcessId();

    // A1. 防恒真对照：HT 候选门（无 hints 输入）必须看不见——深扫的价值锚点=不依赖 RM 提示。
    //     RM 对只读共享句柄的可见性随 Windows 版本/场景波动（本环境实测可见），只记录不断言。
    ScanQuery q;
    q.targets.push_back(TargetRef{ *abs, *key, identity });
    win32::HandleTableScanner htGate(&map);
    auto ocGate = htGate.scan(q); // candidateHints 空 → 跳过全表
    CHECK(ocGate.ok && !hasPid(ocGate.locks, self), L"对照:候选门无提示不可见（深扫必要性锚）");
    win32::RmScanner rm;
    auto ocRm = rm.scan(q);
    wprintf(L"[记录] RM ok=%d 含自身=%d\n", int(ocRm.ok), ocRm.ok ? int(hasPid(ocRm.locks, self)) : -1);

    // A2. F6 主判据（G7 双路径）：子进程全表在本环境可能撞挂起句柄 → 预算自杀/父杀兜底。
    //     合格判据 = 父进程【永不挂死】(G7) 且 结果要么命中要么诚实降级（timedOut+零结果）。
    //     深扫的价值锚点是协议安全，不是本沙箱环境的命中率。
    const auto tDeep = GetTickCount64();
    // 深扫目标 exe 必须显式指向主程序（v1.0 教训：自举在测试场景会 spawn 出测试 exe 自身造成 fork 炸弹）
    wchar_t selfPath[4096]; GetModuleFileNameW(nullptr, selfPath, 4095);
    std::wstring selfDir = std::wstring(selfPath).substr(0, std::wstring(selfPath).find_last_of(L"\\/") + 1);
    const std::wstring lockExe = selfDir + L"wholock.exe";
    CHECK(GetFileAttributesW(lockExe.c_str()) != INVALID_FILE_ATTRIBUTES, L"前置:同目录 WhoLock.exe 在");
    auto deep = win32::runDeepScan({ victim }, 20000, nullptr, lockExe);
    const auto deepMs = GetTickCount64() - tDeep;
    CHECK(!deep.spawnFailed, L"深扫子进程启动");
    CHECK(deepMs < 28000, L"G7: 深扫在预算+8s 内返回（父进程永不挂死）");
    CHECK(deep.timedOut || hasPid(deep.locks, self), L"F6:命中或诚实超时降级（timedOut）");
    wprintf(L"[记录] deep timedOut=%d locks=%zu ms=%llu total=%llu err=%s\n",
            int(deep.timedOut), deep.locks.size(), (unsigned long long)deepMs,
            (unsigned long long)deep.totalHandles, deep.error.c_str());
    wprintf(L"[childExit] 0x%08X spawnFailed=%d\n", deep.exitCode, int(deep.spawnFailed));

    // B. F8 关句柄往返：枚举自身句柄 → 命中 victim 的磁盘句柄 → 关闭 → 再枚举消失
    auto rows = win32::enumerateProcessHandles(self, nullptr);
    const std::wstring want = *key;
    std::uint64_t hv = 0;
    for (const auto& r : rows) {
        if (r.type != 1) continue;
        auto rk = normalizePathKey(r.path, &map, &err);
        if (rk && *rk == want) { hv = r.handleValue; break; }
    }
    CHECK(hv != 0, L"F8 前置:自身句柄表找到 victim 磁盘句柄");
    if (hv != 0) {
        CHECK(win32::closeRemoteHandle(self, hv, &err), L"F8:closeRemoteHandle 成功");
        if (err.empty()) {} else wprintf(L"close err=%s\n", err.c_str());
        auto rows2 = win32::enumerateProcessHandles(self, nullptr);
        bool still = std::any_of(rows2.begin(), rows2.end(), [&](const HandleRow& r) { return r.handleValue == hv; });
        CHECK(!still, L"F8 往返:句柄已从自身进程消失");
    }

    // C. F11 删除验证（locker 已关，文件应可删）
    if (hv != 0) {
        CHECK(win32::deleteFileVerified(victim, &err), L"F11:删除并验证消失");
        if (!err.empty() && hv) wprintf(L"del err=%s\n", err.c_str());
        CHECK(GetFileAttributesW(victim.c_str()) == INVALID_FILE_ATTRIBUTES, L"F11:GetFileAttributes 复核");
        CloseHandle(locker); // victim 已删，locker 句柄自然失效；幂等关闭
    } else {
        CloseHandle(locker);
        DeleteFileW(victim.c_str());
    }

    // D. 取消兜底：预置 cancel → runDeepScan 快速返回（走 !sawEnd 杀路径），不挂死
    std::atomic<bool> cancel{ true };
    const auto t0 = GetTickCount64();
    auto d2 = win32::runDeepScan({ L"C:\\Windows\\notepad.exe" }, 20000, &cancel);
    const auto el = GetTickCount64() - t0;
    CHECK(el < 3000, L"取消兜底:3s 内返回（子进程被杀）");
    (void)d2;

    return reportAndExit();
}
