#include "DeepScanner.h"
#include "DeepScanRunner.h"
#include <vector>

namespace who::win32 {

ScanOutcome DeepScanner::scan(const ScanQuery& q) {
    ScanOutcome oc;
    cancel_.store(false, std::memory_order_relaxed);
    std::vector<std::wstring> paths;
    paths.reserve(q.targets.size());
    for (const auto& t : q.targets) paths.push_back(t.originalPath);

    auto deep = runDeepScan(paths, timeoutMs_, &cancel_);
    lastTimedOut_ = deep.timedOut;
    if (deep.spawnFailed) { oc.ok = false; oc.error = deep.error; return oc; }
    oc.locks = std::move(deep.locks);
    oc.ok = true;
    if (deep.timedOut) oc.error = deep.error; // 部分结果仍有效（协议逐行 flush 设计）
    return oc;
}

} // namespace who::win32
