// G-F12 判据（HKCU 真跑往返）：enable→双键+command 指向当前 exe→disable→双键消失。
// 前置清理保证断言不恒真（若用户实机已启用，先卸载再测）。
#include "../TestAssert.h"
#include <windows.h>
#include <string>

#include "infrastructure/win32/ShellIntegration.h"

using namespace who::win32;

int main() {
    std::wstring err;
    disableShellIntegration(&err); // 前置清理（测试自建键，测试后同样卸载还原）
    auto st0 = queryShellIntegration();
    CHECK(!st0.fileKey && !st0.dirKey, L"前置:未注册状态");

    CHECK(enableShellIntegration(&err), L"enable 成功");
    if (!err.empty()) wprintf(L"enable err=%s\n", err.c_str());
    auto st1 = queryShellIntegration();
    CHECK(st1.fileKey && st1.dirKey, L"F12:文件与目录双键写入");
    wchar_t sp[4096]; GetModuleFileNameW(nullptr, sp, 4095);
    CHECK(st1.registeredExe.find(sp) != std::wstring::npos,
          L"F12:command 指向当前 exe（右键即自动扫描）");

    CHECK(disableShellIntegration(&err), L"disable 成功");
    auto st2 = queryShellIntegration();
    CHECK(!st2.fileKey && !st2.dirKey, L"F12 往返:双键已删除");

    // 负样例：对未注册状态再 disable 必须幂等成功（FILE_NOT_FOUND 视为成功）
    CHECK(disableShellIntegration(&err), L"负样例:幂等 disable");
    return reportAndExit();
}
