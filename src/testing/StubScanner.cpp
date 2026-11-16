#include "StubScanner.h"

namespace who::testing {

StubScanner::StubScanner(std::vector<LockInfo> canned, std::uint8_t bit, bool ok,
                         std::wstring error, bool adminHint)
    : canned_(std::move(canned)), bit_(bit), ok_(ok), adminHint_(adminHint),
      error_(std::move(error)), name_(L"Stub") {}

ScanOutcome StubScanner::scan(const ScanQuery&) {
    ScanOutcome oc;
    oc.ok = ok_;
    oc.locks = canned_;
    oc.error = error_;
    if (bit_ != 0u) for (auto& li : oc.locks) li.engines |= bit_;
    name_ = (bit_ == EngineStub) ? L"Stub" : (bit_ == EngineRM ? L"Stub-RM" : L"Stub-HT");
    return oc;
}

} // namespace who::testing
