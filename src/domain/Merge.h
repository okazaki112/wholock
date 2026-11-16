#pragma once
#include <vector>
#include "LockInfo.h"

namespace who {

// 行唯一键=(pid,kind)。同键不同引擎：engines 位或合并，信息取字段较全的一侧。
void mergeInto(std::vector<LockInfo>& out, const std::vector<LockInfo>& add);

// pid 升序，其次 kind。稳定排序用于 UI 默认展示。
void sortLocks(std::vector<LockInfo>& locks);

} // namespace who
