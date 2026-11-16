#pragma once
// v1.0 F12：Explorer 右键集成（HKCU 当前用户注册表，可逆、免管理员）。
// 写入：Software\Classes\*\shell\WhoLock 与 Software\Classes\Directory\shell\WhoLock
//   默认值=菜单文本、Icon=当前 exe、command="<exe>" "%1"（GUI 启动即自动扫描）。
// 开关状态以注册表实况为准（不进 settings.ini）；exe 移动后取消勾选再勾选即修复指向。
#include <string>

namespace who::win32 {

struct ShellIntegrationState {
    bool fileKey = false;        // 任意文件右键键存在
    bool dirKey = false;         // 目录右键键存在
    std::wstring registeredExe;  // command 指向的 exe（空=未注册）
};

ShellIntegrationState queryShellIntegration();
bool enableShellIntegration(std::wstring* errOut);
bool disableShellIntegration(std::wstring* errOut);

} // namespace who::win32
