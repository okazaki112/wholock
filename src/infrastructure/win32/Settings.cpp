#include "Settings.h"
#include <windows.h>

namespace who::win32 {

std::wstring settingsPath() {
    wchar_t appdata[4096];
    if (GetEnvironmentVariableW(L"APPDATA", appdata, 4095) == 0)
        return L"settings.ini"; // 极端环境回落当前目录（不阻断功能）
    std::wstring dir = std::wstring(appdata) + L"\\WhoLock";
    CreateDirectoryW(dir.c_str(), nullptr); // 已存在返回失败但无副作用
    return dir + L"\\settings.ini";
}

UiSettings loadUiSettings() {
    UiSettings s;
    const std::wstring p = settingsPath();
    s.x = GetPrivateProfileIntW(L"ui", L"x", -1, p.c_str());
    s.y = GetPrivateProfileIntW(L"ui", L"y", -1, p.c_str());
    s.w = GetPrivateProfileIntW(L"ui", L"w", 790, p.c_str());
    s.h = GetPrivateProfileIntW(L"ui", L"h", 590, p.c_str());
    s.hasGeometry = s.x >= 0 && s.y >= 0 && s.w >= 640 && s.h >= 460; // 损坏/越界防御
    s.htFull = GetPrivateProfileIntW(L"ui", L"htfull", 0, p.c_str()) != 0;
    return s;
}

void saveUiSettings(const UiSettings& s) {
    const std::wstring p = settingsPath();
    WritePrivateProfileStringW(L"ui", L"x", std::to_wstring(s.x).c_str(), p.c_str());
    WritePrivateProfileStringW(L"ui", L"y", std::to_wstring(s.y).c_str(), p.c_str());
    WritePrivateProfileStringW(L"ui", L"w", std::to_wstring(s.w).c_str(), p.c_str());
    WritePrivateProfileStringW(L"ui", L"h", std::to_wstring(s.h).c_str(), p.c_str());
    WritePrivateProfileStringW(L"ui", L"htfull", s.htFull ? L"1" : L"0", p.c_str());
}

} // namespace who::win32
