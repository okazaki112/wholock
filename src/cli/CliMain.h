#pragma once
#include <string>
#include <vector>
#include "../infrastructure/win32/WindowsPathMap.h"

namespace who::cli {

// CLI 模式入口（进程码：0 成功 / 2 参数错 / 3 路径错）。输出 stdout：人读 UTF-8，--json 为单行数组。
// 返回后程序即退出，无窗口。
int run(const std::vector<std::wstring>& argv, win32::WindowsPathMap& map);

} // namespace who::cli
