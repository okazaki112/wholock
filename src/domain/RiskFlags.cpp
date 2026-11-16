#include "RiskFlags.h"
#include <string_view>

namespace who {
namespace {
constexpr std::wstring_view kCriticalNames[] = {
    L"wininit", L"csrss", L"services", L"svchost", L"lsass", L"smss", L"winlogon", L"fontdrvhost",
};
constexpr std::wstring_view kShellNames[] = { L"explorer", L"explorer.exe" };

wchar_t lowerW(wchar_t c) { return (c >= L'A' && c <= L'Z') ? wchar_t(c + 32) : c; }

// 归一化：取基本名 → 去最后一个点之后的扩展 → ASCII 小写（护栏不信任调用方口径）
std::wstring baseNameNoExtLower(std::wstring_view name) {
    auto slash = name.find_last_of(L"\\/");
    std::wstring_view base = (slash == std::wstring_view::npos) ? name : name.substr(slash + 1);
    auto dot = base.find_last_of(L'.');
    if (dot != std::wstring_view::npos && dot != 0) base = base.substr(0, dot);
    std::wstring r(base);
    for (auto& c : r) c = lowerW(c);
    return r;
}

} // namespace

KillRisk classifyKill(std::uint32_t pid, const std::wstring& nameLower,
                      const std::wstring& imagePathLower, std::uint32_t selfPid) {
    if (pid == selfPid) return KillRisk::OwnProcess; // 自比优先（自 pid<=4 也算自身）
    if (pid <= 4u) return KillRisk::CriticalSystem;  // System/Idle 等
    (void)imagePathLower; // 预留：镜像路径白/黑名单增强（v0.2）
    const auto base = baseNameNoExtLower(nameLower);
    for (auto n : kCriticalNames)
        if (base == baseNameNoExtLower(std::wstring_view(n))) return KillRisk::CriticalSystem;
    for (auto n : kShellNames)
        if (base == baseNameNoExtLower(std::wstring_view(n))) return KillRisk::ShellProcess;
    return KillRisk::Allowed;
}

std::wstring_view riskText(KillRisk r) {
    switch (r) {
    case KillRisk::Allowed:        return L"可结束（需确认）";
    case KillRisk::OwnProcess:     return L"本程序自身：禁止结束";
    case KillRisk::CriticalSystem: return L"关键系统进程：禁止结束";
    case KillRisk::ShellProcess:   return L"桌面外壳：禁止结束（v0.1）";
    }
    return L"未知";
}

} // namespace who
