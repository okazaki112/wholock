#pragma once
#include <windows.h>
#include <string>

namespace who::win32 {

// 共享进程信息工具：HT/RM 两引擎与 UI 结束进程前展示统一走这里（避免三处复制）
// QUERY_LIMITED | DUP_HANDLE；权限/保护进程失败返回 nullptr
HANDLE openProcForDup(DWORD pid);

// 完整映像路径；失败返回空串。outBaseLower 输出小写基本名（供名单匹配/展示）
std::wstring queryFullImage(HANDLE hProc, std::wstring* outBaseLower = nullptr);

} // namespace who::win32
