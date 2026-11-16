#pragma once
#include <memory>
#include <vector>
#include "../domain/LockInfo.h"

namespace who {

// application 层编排器：不碰 Win32，只消费 ILockScanner 注入集合
class ScanCoordinator {
public:
    struct Note {
        std::wstring engine;
        bool ok = false;
        std::uint32_t count = 0;
        std::wstring error;
        bool adminRecommended = false;
    };
    struct Report {
        std::vector<LockInfo> merged;
        std::vector<Note> notes;
        bool anyOk = false;
        bool allFailed() const { return !notes.empty() && !anyOk; }
    };

    explicit ScanCoordinator(std::vector<std::unique_ptr<ILockScanner>> scanners);
    // 按传参顺序执行（RM 先 HT 后，工厂保序）；命中 PID 累积进 hints 传给后续引擎
    Report execute(ScanQuery q) const;

private:
    std::vector<std::unique_ptr<ILockScanner>> scanners_;
};

} // namespace who
