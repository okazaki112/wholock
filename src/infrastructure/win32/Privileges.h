#pragma once
#include <cstdint>

namespace who::win32 {

// 尽力启用 SeDebugPrivilege：失败不退出（CSDN 165260767 降级纪律），返回 false → HT 结果"同会话可见"
// 成功 → 可跨会话枚举。进程内重复调用安全（幂等）。
bool enableDebugPrivilege();

// 当前进程是否已提权运行（TokenElevation）；UI 用于"以管理员运行结果更全"提示
bool isElevated();

} // namespace who::win32
