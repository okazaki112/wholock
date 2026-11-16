#pragma once
// infrastructure/win32：扫描目标准备（v1.0 F4/F5）——目录递归展开 + 逐文件 absolutize/normalize/identity。
#include <string>
#include <vector>
#include "../../domain/LockInfo.h"

namespace who::win32 {

class WindowsPathMap;

struct PreparedTargets {
    std::vector<TargetRef> refs;   // 展开后的文件级目标
    std::size_t expandedDirs = 0;  // 目录数（UI 摘要展示）
    std::size_t cappedAt = 0;      // 触发上限截断时的文件数（0=未截断）
    std::wstring note;             // 截断/跳过提示（可空）
};

// 输入任意条数路径（文件/目录混合）；目录递归展开（深度上限 8、总文件上限 maxFiles，超限截断并 note）。
// 全部输入都无效时 errOut 给原因并返回 false。
bool prepareScanTargets(const std::vector<std::wstring>& inputs, const WindowsPathMap& map,
                        PreparedTargets& out, std::wstring* errOut, std::size_t maxFiles = 2000);

} // namespace who::win32
