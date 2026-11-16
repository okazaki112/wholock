#pragma once
// v1.0 F6/F7：句柄全表枚举与单进程句柄明细的【子进程隔离运行器】。
// 动机（实测）：无关进程的挂起句柄（断网盘等）会让 GetFinalPathNameByHandleW 永久阻塞——
// 进程内无法安全取消（僵尸线程），故 spawn 自身内部模式经管道通信，父进程超时杀兜底；
// 子进程逐行 flush，被杀时已产出的部分结果仍可用。
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include "../../domain/LockInfo.h"

namespace who::win32 {

struct DeepScanResult {
    std::vector<LockInfo> locks;
    std::uint64_t totalHandles = 0;
    bool timedOut = false;     // G7：超时杀路径可复现
    bool spawnFailed = false;
    std::wstring error;
    DWORD exitCode = (DWORD)-1; // v1.0 取证：子进程退出码（定位 pre-main 崩溃）
};

// spawn: "<target exe>" --internal-htfull --budget <ms>；stdin 传 UTF-8 路径行；stdout 读 HIT 行。
// exePathOverride：深扫子进程必须是有 wWinMain internal 分支的主程序；测试进程必须显式传
// WhoLock.exe 路径——v1.0 实测教训：自举 GetModuleFileName 在测试场景会 spawn 出测试 exe 自身，
// 子进程重新跑测试流程造成 fork 炸弹（60+ 进程占锁）。
DeepScanResult runDeepScan(const std::vector<std::wstring>& originalPaths, DWORD budgetMs,
                           const std::atomic<bool>* cancel, const std::wstring& exePathOverride = L"");

struct ProcHandleResult {
    std::vector<HandleRow> rows;
    bool timedOut = false;
    bool spawnFailed = false;
    std::wstring error;
};

// spawn: "<target exe>" --internal-procscan <pid> --budget <ms>；stdout 读 HL 行。
ProcHandleResult runProcessHandleScan(DWORD pid, DWORD budgetMs, const std::wstring& exePathOverride = L"");

} // namespace who::win32
