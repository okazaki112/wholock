// G1 判据（OS 语义为尺子）：子进程以 FileStream(share=None) 持写锁 5s，被测 RM 必须报出其 PID；子进程退出后必须消失。
// 同文件另做"自锁诊断"（RM 忽略调用方进程本身 → 仅记录不断言，反向证明补盲通道必要性）。
#include "domain/Normalize.h"
#include "../TestAssert.h"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <string>
#include <thread>

#include "infrastructure/win32/HandleTableScanner.h"
#include "infrastructure/win32/RmScanner.h"
#include "infrastructure/win32/WindowsPathMap.h"

using namespace who;
using namespace std::chrono_literals;

namespace {
std::wstring tempTarget() {
    wchar_t dir[MAX_PATH];
    GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir) + L"wholock-selflock-" + std::to_wstring(GetCurrentProcessId()) + L".bin";
}
bool hasPid(const std::vector<LockInfo>& v, uint32_t pid) {
    return std::any_of(v.begin(), v.end(), [&](const LockInfo& li) { return li.pid == pid; });
}

// 启动持锁子进程：powershell 打开目标文件(ReadWrite, share=None) 持 5 秒
DWORD spawnLocker(const std::wstring& target) {
    const std::wstring ps = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "
        L"$s=[IO.File]::Open('" + target + L"', 'Open', 'ReadWrite', 'None'); Start-Sleep -Seconds 12; $s.Close()";
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring mutableCmd = ps;
    if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return 0;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return pi.dwProcessId;
}
} // namespace

int main() {
    win32::WindowsPathMap map;
    const auto target = tempTarget();

    // 1) 建文件
    {
        HANDLE h = CreateFileW(target.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        CHECK(h != INVALID_HANDLE_VALUE, L"目标文件创建");
        CloseHandle(h);
    }

    // 2) 自锁诊断（RM 忽略查询发起者时此路为空——记录原因，不作断言）
    std::wstring err;
    FileIdentity identity;
    auto abs = win32::WindowsPathMap::absolutizeExistingFile(target, &err, &identity);
    CHECK(abs.has_value(), L"absolutize 自身文件");
    auto key = normalizePathKey(*abs, &map, &err);
    CHECK(key.has_value(), L"normalize 键生成");
    ScanQuery q;
    q.targets.push_back(TargetRef{ *abs, *key, identity });
    win32::RmScanner rm;
    {
        HANDLE exclusive = CreateFileW(target.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        CHECK(exclusive != INVALID_HANDLE_VALUE, L"自锁诊断句柄");
        if (exclusive != INVALID_HANDLE_VALUE) { // probe：OS 层确实锁住
            HANDLE p2 = CreateFileW(target.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            CHECK(p2 == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION, L"OS 独占语义成立(前置)");
            auto diag = rm.scan(q);
            wprintf(L"[诊断] 自锁 RM scan ok=%d 含自身=%d\n", int(diag.ok),
                    diag.ok ? int(hasPid(diag.locks, GetCurrentProcessId())) : -1);
            if (!diag.ok) wprintf(L"       err=%s\n", diag.error.c_str());
            CloseHandle(exclusive);
        }
    }

    // 3) 正式 G1：子进程持锁 → 必须被报出
    const DWORD lockerPid = spawnLocker(target);
    CHECK(lockerPid != 0, L"锁子进程启动(powershell)");
    if (lockerPid == 0) return reportAndExit(); // 环境不允许则如实失败退出
    std::this_thread::sleep_for(1200ms);
    auto oc = rm.scan(q);
    CHECK(oc.ok, L"RM scan 调用成功");
    if (!oc.ok) wprintf(L"scan err: %s\n", oc.error.c_str());
    if (oc.ok) CHECK(hasPid(oc.locks, lockerPid), L"G1: 子进程写锁必须被 RM 报出");

    // 3b) HT 通道一致性（G5 换实现仍须通过）：候选门模式——模拟 coordinator 把 RM 命中 PID 回喂
    win32::HandleTableScanner ht;
    q.candidateHints = { lockerPid, GetCurrentProcessId() };
    auto ocH = ht.scan(q);
    CHECK(ocH.ok, L"HT scan 完成");
    if (!ocH.ok) wprintf(L"HT err: %s\n", ocH.error.c_str());
    if (ocH.ok) CHECK(hasPid(ocH.locks, lockerPid), L"G5 补盲: HT 报出子进程写锁");
    if (ocH.ok) wprintf(L"[HT] 枚举句柄数=%llu admin=%d\n", (unsigned long long)ht.lastEnumHandleCount(), int(!ht.adminRecommended()));

    // 4) 反向：子进程退出（持锁 12s）后，第 ~19s 再扫，该 PID 必须消失
    std::this_thread::sleep_for(11800ms);
    auto oc2 = rm.scan(q);
    CHECK(oc2.ok, L"释放后再扫不崩");
    if (oc2.ok) CHECK(!hasPid(oc2.locks, lockerPid), L"G1 反向: 子进程退出后消失");
    auto oc2H = ht.scan(q);
    CHECK(oc2H.ok, L"HT 释放后再扫不崩");
    if (oc2H.ok) CHECK(!hasPid(oc2H.locks, lockerPid), L"G5 反向: HT 同样确认释放");
    // 收尾：确认锁已完全释放（文件能再被排他打开=证据）
    {
        HANDLE rel = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        CHECK(rel != INVALID_HANDLE_VALUE, L"收尾: 文件应已解锁");
        if (rel != INVALID_HANDLE_VALUE) CloseHandle(rel);
    }

    // 5) 负样例：不存在的路径与目录都必须被 absolutize 拦下
    CHECK(!win32::WindowsPathMap::absolutizeExistingFile(target + L"-nope", &err).has_value(), L"负样例:不存在文件被拦");
    CHECK(!win32::WindowsPathMap::absolutizeExistingFile(target.substr(0, 3), &err).has_value(), L"负样例:目录被拦(仅支持文件)");

    DeleteFileW(target.c_str());
    return reportAndExit();
}
