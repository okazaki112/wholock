#include "CliArgs.h"
#include <cwctype>

namespace who {
namespace {
bool ieq(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (std::towlower(a[i]) != std::towlower(b[i])) return false;
    return true;
}
bool hasArg(const std::vector<std::wstring>& a, std::wstring_view flag) {
    for (const auto& s : a) if (s == flag) return true;
    return false;
}
} // namespace

bool isCliMode(const std::vector<std::wstring>& args) { return hasArg(args, L"--cli"); }

bool parseCli(const std::vector<std::wstring>& args, CliOptions& out, std::wstring& errOut) {
    out = {};
    for (size_t i = 1; i < args.size(); ++i) {
        const std::wstring& a = args[i];
        if (a == L"-h" || a == L"-?" || a == L"--help") { out.usageRequested = true; continue; }
        if (a == L"--version") { out.versionRequested = true; continue; }
        if (a == L"--cli") { continue; }
        if (a == L"--internal-htfull") { out.mode = CliOptions::Mode::InternalHtfull; continue; } // F6 子进程内部协议
        if (a.rfind(L"--internal-procscan", 0) == 0) { out.mode = CliOptions::Mode::InternalProcscan; continue; } // F7
        if (a == L"--json") { out.json = true; continue; }
        if (a.rfind(L"--engine=", 0) == 0) {
            const std::wstring v = a.substr(9);
            if (ieq(v, L"rm")) out.engineMask = EngineRM;
            else if (ieq(v, L"ht")) out.engineMask = EngineHT;
            else if (ieq(v, L"both")) out.engineMask = EngineRM | EngineHT;
            else if (ieq(v, L"deep")) out.engineMask = EngineDeep; // F6 CLI 深扫
            else { errOut = L"--engine 无效：可选 rm / ht / both / deep（收到 " + v + L"）"; return false; }
            continue;
        }
        if (a.rfind(L"--budget", 0) == 0) { // --budget <ms> 或 --budget=<ms>
            std::wstring v = a.size() > 8 && a[8] == L'=' ? a.substr(9) : L"";
            if (v.empty()) { // 空格分隔：下一个位置参数
                if (++i >= args.size()) { errOut = L"--budget 缺少毫秒数"; return false; }
                v = args[i];
            }
            out.budgetMs = static_cast<std::uint32_t>(wcstoul(v.c_str(), nullptr, 10));
            if (out.budgetMs == 0) { errOut = L"--budget 必须为正毫秒数"; return false; }
            continue;
        }
        if (!a.empty() && a[0] == L'-') { errOut = L"未知选项：" + a; return false; }
        out.paths.push_back(a); // v1.0 F5：位置参数多目标
    }
    if (out.usageRequested || out.versionRequested) return true; // 元信息请求不要求路径参数
    if (out.mode == CliOptions::Mode::InternalProcscan) {
        if (out.paths.empty()) { errOut = L"--internal-procscan 缺少 pid"; return false; }
        out.internalPid = static_cast<std::uint32_t>(wcstoul(out.paths[0].c_str(), nullptr, 10));
        if (out.internalPid == 0) { errOut = L"pid 无效"; return false; }
        return true;
    }
    if (out.mode == CliOptions::Mode::InternalHtfull) return true; // F6：子进程路径经 stdin 传入，argv 不带
    if (out.paths.empty()) { errOut = L"缺少文件/目录路径；用法：WhoLock [选项] <路径...>"; return false; }
    return true;
}

std::wstring engineMaskName(std::uint8_t mask) {
    const bool rm = (mask & EngineRM) != 0, ht = (mask & EngineHT) != 0, st = (mask & EngineStub) != 0;
    std::wstring s;
    if (rm) s += L"RM";
    if (ht) { if (!s.empty()) s += L"+"; s += L"HT"; }
    if (st) { if (!s.empty()) s += L"+"; s += L"Stub"; }
    if (s.empty()) s = L"无";
    return s;
}

} // namespace who
