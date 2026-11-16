#pragma once
// infrastructure/win32：RestartManager 主通道（对标 SO 6931972 / Raymond Chen 8283）
#include "../../domain/LockInfo.h"

namespace who::win32 {

class RmScanner final : public ILockScanner {
public:
    std::uint8_t engineBit() const override { return EngineRM; }
    std::wstring engineName() const override { return L"RestartManager"; }
    ScanOutcome scan(const ScanQuery& q) override;
    // RM 不需要特权，但服务/驱动级引用是它的盲区 → 建议配 HT+管理员
    bool adminRecommended() const override { return false; }
};

} // namespace who::win32
