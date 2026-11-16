#pragma once
// v1.0 F6：深度扫描引擎——经 DeepScanRunner 子进程隔离调用全表 HT。
#include <windows.h>
#include <atomic>
#include "../../domain/LockInfo.h"

namespace who::win32 {

class DeepScanner final : public ILockScanner {
public:
    std::uint8_t engineBit() const override { return EngineDeep; }
    std::wstring engineName() const override { return L"深度扫描(子进程全表句柄)"; }
    ScanOutcome scan(const ScanQuery& q) override;
    bool adminRecommended() const override { return true; }
    void setTimeoutMs(DWORD ms) noexcept { timeoutMs_ = ms; }
    void cancel() noexcept { cancel_.store(true, std::memory_order_relaxed); }
    bool lastTimedOut() const noexcept { return lastTimedOut_; }

private:
    std::atomic<bool> cancel_{false};
    DWORD timeoutMs_ = 25000;
    bool lastTimedOut_ = false;
};

} // namespace who::win32
