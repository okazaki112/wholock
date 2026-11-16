#pragma once
#include <optional>
#include <string>
#include "LockInfo.h"

namespace who {

// 归一化比对键：绝对化检查→剥离 \\?\ 与 \??\→UNC 归位→\Device\ 映射盘符
// （map 注入）→'/'→'\' 折叠→解析 . 与 ..→去尾点空格→统一小写。
// 失败返回 nullopt，errOut 给出中文原因。
std::optional<std::wstring> normalizePathKey(const std::wstring& raw,
                                             const IPathMap* map,
                                             std::wstring* errOut = nullptr);

// 取路径基本名并小写（用于进程名比较/风险名单匹配）
std::wstring pathBaseLower(const std::wstring& path);

} // namespace who
