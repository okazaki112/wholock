#pragma once
#include <cstdint>
#include <string>

namespace who {

// Kill 风险分级（护栏唯一决策点，UI 只允许 Allowed 走 TerminateProcess）
enum class KillRisk : std::uint8_t {
    Allowed,        // 普通进程：允许（仍需确认框）
    OwnProcess,     // 本程序自身
    CriticalSystem, // PID<=4 或关键系统名单
    ShellProcess,   // explorer 等：杀之桌面重启，v0.1 拦下
};

// pid=进程号，nameLower/pathLower 建议取自 LockInfo（已由 domain 归一小写）
KillRisk classifyKill(std::uint32_t pid, const std::wstring& nameLower,
                      const std::wstring& imagePathLower, std::uint32_t selfPid);

std::wstring_view riskText(KillRisk r);

} // namespace who
