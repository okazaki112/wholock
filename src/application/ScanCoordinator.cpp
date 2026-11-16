#include "ScanCoordinator.h"
#include <algorithm>
#include "../domain/Merge.h"

namespace who {

ScanCoordinator::ScanCoordinator(std::vector<std::unique_ptr<ILockScanner>> scanners)
    : scanners_(std::move(scanners)) {}

ScanCoordinator::Report ScanCoordinator::execute(ScanQuery q) const {
    Report rep;
    rep.anyOk = scanners_.empty(); // 空集合视为“无错误发生”，骨架可跑
    // 串行且传递候选 hints：前序引擎命中 PID 喂给后续（RM→HT 主从；HT 借此跳过全表名解析免挂死）
    for (const auto& sc : scanners_) {
        Note n;
        n.engine = sc->engineName();
        n.adminRecommended = sc->adminRecommended();
        ScanOutcome oc = sc->scan(q);
        n.ok = oc.ok;
        n.error = oc.error;
        n.count = static_cast<std::uint32_t>(oc.locks.size());
        if (oc.ok) {
            rep.anyOk = true;
            mergeInto(rep.merged, oc.locks);
            for (const auto& li : oc.locks)
                if (std::find(q.candidateHints.begin(), q.candidateHints.end(), li.pid) == q.candidateHints.end())
                    q.candidateHints.push_back(li.pid);
        }
        rep.notes.push_back(std::move(n));
    }
    sortLocks(rep.merged);
    return rep;
}

} // namespace who
