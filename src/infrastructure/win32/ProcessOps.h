#pragma once
#include <cstdint>
#include <string>

namespace who::win32 {

// 结束进程唯一入口：UI/调用方必须先过 domain::classifyKill 护栏（Allowed），此处不再做名单判断（职责单一）。
// 返回 false 时 errOut 带操作系统错误说明。
bool terminateProcess(std::uint32_t pid, std::wstring* errOut);

// v1.0 F8 实装：关闭远端单个句柄（DUPLICATE_CLOSE_SOURCE 技法），不杀进程的释放手段。
// 调用方必须先过危险模式勾选 + 具体句柄确认（UI 护栏），此处不做名单判断（职责单一）。
bool closeRemoteHandle(std::uint32_t ownerPid, std::uint64_t handleValue, std::wstring* errOut);

// v1.0 F11：删除并验证消失（DeleteFileW 后 GetFileAttributes 必须 INVALID；防"假删除"）。
bool deleteFileVerified(const std::wstring& path, std::wstring* errOut);

} // namespace who::win32
