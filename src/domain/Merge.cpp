#include "Merge.h"
#include <algorithm>

namespace who {
namespace {
// v0.1 行键=pid（决策 2026-11-16：RM kind 粒度粗，同 pid 双行对用户是冗余；
// v0.2 若 RM 能区分模块/句柄类别再回到 (pid,kind)，见重要发现.md 打磨循环记录）
bool sameRow(const LockInfo& a, const LockInfo& b) { return a.pid == b.pid; }
std::wstring pickBetterStr(const std::wstring& a, const std::wstring& b) {
    return a.size() >= b.size() ? a : b;
}
LockKind pickKind(LockKind cur, LockKind add) {
    if (cur != add && cur == LockKind::Unknown) return add; // Unknown 让位精确 kind
    return cur;
}
} // namespace

void mergeInto(std::vector<LockInfo>& out, const std::vector<LockInfo>& add) {
    for (const auto& li : add) {
        const auto it = std::find_if(out.begin(), out.end(), [&](const LockInfo& e) { return sameRow(e, li); });
        if (it == out.end()) {
            out.push_back(li);
        } else {
            it->engines |= li.engines;
            it->name = pickBetterStr(it->name, li.name);
            it->imagePath = pickBetterStr(it->imagePath, li.imagePath);
            it->seenByRm = it->seenByRm || li.seenByRm;
            // v1.0 多目标：同进程命中多文件 → 首个保留 + 计数
            if (!li.occupiedFile.empty()) {
                if (it->occupiedFile.empty()) it->occupiedFile = li.occupiedFile;
                else if (it->occupiedFile != li.occupiedFile) ++it->occupiedMore;
            }
            it->occupiedMore += li.occupiedMore;
        }
    }
}

void sortLocks(std::vector<LockInfo>& locks) {
    std::stable_sort(locks.begin(), locks.end(), [](const LockInfo& a, const LockInfo& b) {
        return a.pid < b.pid;
    });
}

} // namespace who
