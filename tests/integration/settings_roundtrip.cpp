// F3 判据：写→读回等价；损坏 ini（宽度过小）必须判无效几何（防御负样例）。
#include "infrastructure/win32/Settings.h"
#include "../TestAssert.h"
#include <windows.h>

using namespace who::win32;

int main() {
    UiSettings s;
    s.x = 1234; s.y = 222; s.w = 999; s.h = 888; s.hasGeometry = true; s.htFull = true;
    saveUiSettings(s);
    auto r = loadUiSettings();
    CHECK(r.x == 1234 && r.y == 222 && r.w == 999 && r.h == 888, L"几何往返一致");
    CHECK(r.hasGeometry, L"有效几何判定");
    CHECK(r.htFull, L"引擎偏好往返");

    // 负样例：损坏 ini（w 低于下限）→ hasGeometry=false
    const auto p = settingsPath();
    WritePrivateProfileStringW(L"ui", L"w", L"10", p.c_str());
    auto bad = loadUiSettings();
    CHECK(!bad.hasGeometry, L"负样例:损坏几何判无效");

    // 清理测试痕迹（本测试自建的 ini，属 agent 会话产物）
    DeleteFileW(p.c_str());
    return reportAndExit();
}
