#pragma once
#include <string>

namespace who::win32 {

// v1.0 F3：窗口几何 + 引擎偏好持久化（%APPDATA%\WhoLock\settings.ini，明文可查）。
// 安全纪律：危险操作勾选状态【永不】持久化——UiSettings 里没有该字段，接口层面杜绝。
struct UiSettings {
    int x = -1, y = -1, w = 790, h = 590; // -1=未设置（首启用默认）
    bool hasGeometry = false;             // w/h 低于下限视为无效（损坏 ini 防御）
    bool htFull = false;                  // 深度扫描偏好（M8 消费）
};

UiSettings loadUiSettings();
void saveUiSettings(const UiSettings& s);
std::wstring settingsPath();

} // namespace who::win32
