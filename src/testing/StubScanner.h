#pragma once
// 测试桩：ILockScanner 的第二实现（R8-b 可插拔证据；非产品代码）
#include <vector>
#include "../domain/LockInfo.h"

namespace who::testing {

class StubScanner final : public ILockScanner {
public:
    StubScanner(std::vector<LockInfo> canned, std::uint8_t bit, bool ok = true,
                std::wstring error = L"", bool adminHint = false);
    std::uint8_t engineBit() const override { return bit_; }
    std::wstring engineName() const override { return name_; }
    ScanOutcome scan(const ScanQuery&) override;
    bool adminRecommended() const override { return adminHint_; }
    void setCanned(std::vector<LockInfo> v) { canned_ = std::move(v); }
    void setFail(std::wstring err) { ok_ = false; error_ = std::move(err); }

private:
    std::vector<LockInfo> canned_;
    std::uint8_t bit_;
    bool ok_;
    bool adminHint_;
    std::wstring error_;
    std::wstring name_;
};

} // namespace who::testing
