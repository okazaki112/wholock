#pragma once
// infrastructure/win32：句柄表补盲通道（M3 实装）
// 链路：SeDebugPrivilege(失败降级) → NtQuerySystemInformation(SystemExtendedHandleInformation)
//       → 按 pid 缓存 OpenProcess(DUP) → DuplicateHandle → GetFileType==DISK 过滤（不碰 NtQueryObject 挂死点）
//       → GetFinalPathNameByHandleW 与归一键比对 → 按 pid 去重产出。
// 已知限制（README 披露）：无 Volume GUID→盘符反查（未装盘卷目标比对不到）；8.3/CJK 重解析点等价类未全覆盖。
#include <windows.h>
#include <atomic>
#include <cstdint>
#include "../../domain/LockInfo.h"

namespace who::win32 {

class WindowsPathMap; // 同层依赖：Volume GUID 反查（v1.0 F1）

class HandleTableScanner final : public ILockScanner {
public:
    explicit HandleTableScanner(const WindowsPathMap* pathmap = nullptr) noexcept : pathmap_(pathmap) {}
    std::uint8_t engineBit() const override { return EngineHT; }
    std::wstring engineName() const override { return L"句柄表(NtQuerySystemInformation)"; }
    ScanOutcome scan(const ScanQuery& q) override;                      // 候选门模式（进程内安全）
    // v1.0 F6：全表名解析模式——挂死风险由 OS 句柄枚举固有，【只允许在隔离子进程内调用】，
    // 由父进程超时杀兜底（DeepScanRunner 协议）。进程内直接调用属违规（README 已披露）。
    ScanOutcome scanFull(const ScanQuery& q);
    bool adminRecommended() const override { return true; }             // M3：缺 SeDebugPrivilege → true
    uint64_t lastEnumHandleCount() const { return lastEnumCount_; }      // M3：诊断用
    void cancel() noexcept { cancel_.store(true, std::memory_order_relaxed); }   // UI 工作线程取消
    void resetCancel() noexcept { cancel_.store(false, std::memory_order_relaxed); }

private:
    ScanOutcome scanImpl(const ScanQuery& q, bool fullTable);
    std::wstring keyFromWinPath(std::wstring winPath) const; // 剥前缀/UNC 归位/小写 + Volume GUID→盘符
    const WindowsPathMap* pathmap_;
    std::atomic<bool> cancel_{false};
    bool adminOk_ = false;             // 每次 scan 重估（失败不退出，仅标记）
    std::uint64_t lastEnumCount_ = 0;
};

// v1.0 F7：枚举指定进程全部句柄明细（同样【只允许隔离子进程调用】——挂死风险同源）
std::vector<HandleRow> enumerateProcessHandles(DWORD pid, const std::atomic<bool>* cancel);

} // namespace who::win32
