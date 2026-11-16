#include "DeepScanRunner.h"
#include <windows.h>
#include <algorithm>
#include <functional>
#include <string>

namespace who::win32 {
namespace {

std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring fromUtf8(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(n > 0 ? n : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

std::vector<std::wstring> splitTabs(const std::wstring& line) {
    std::vector<std::wstring> out;
    size_t pos = 0;
    while (true) {
        const size_t t = line.find(L'\t', pos);
        out.push_back(line.substr(pos, t == std::wstring::npos ? std::wstring::npos : t - pos));
        if (t == std::wstring::npos) break;
        pos = t + 1;
    }
    return out;
}

std::wstring selfExePath() {
    wchar_t buf[4096];
    GetModuleFileNameW(nullptr, buf, 4095);
    return buf;
}

// 通用：spawn 自身 + 管道 stdout 行协议 + stdin 可选写入 + deadline 杀
struct SpawnPipes {
    HANDLE childOutR = nullptr, childOutW = nullptr;
    HANDLE childInR = nullptr, childInW = nullptr;
    HANDLE proc = nullptr, thread = nullptr;
    void closeAll(bool parentSide) {
        (void)parentSide; // 预留：子进程侧关闭语义不同，当前仅父侧使用
        if (childOutR) CloseHandle(childOutR);
        if (childOutW) CloseHandle(childOutW);
        if (childInR) CloseHandle(childInR);
        if (childInW) CloseHandle(childInW);
        if (proc) CloseHandle(proc);
        if (thread) CloseHandle(thread);
    }
};

bool spawnSelf(const std::wstring& exePath, const std::wstring& args, SpawnPipes& p, bool withStdin) {
    // 熔断（v1.0 实战教训）：internal 子进程若因任何原因再次走到 spawnSelf，会形成 fork 炸弹
    // （实测单轮炸出 60+ 同名进程占锁 exe）。internal 进程永远禁止再自举。
    if (wcsstr(GetCommandLineW(), L"--internal-") != nullptr) return false;
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    if (!CreatePipe(&p.childOutR, &p.childOutW, &sa, 64 * 1024)) return false;
    SetHandleInformation(p.childOutR, HANDLE_FLAG_INHERIT, 0);
    if (withStdin && !CreatePipe(&p.childInR, &p.childInW, &sa, 64 * 1024)) return false;
    if (withStdin) SetHandleInformation(p.childInW, HANDLE_FLAG_INHERIT, 0);

    std::wstring cmd = L"\"" + (exePath.empty() ? selfExePath() : exePath) + L"\" " + args;
    STARTUPINFOW si{}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = p.childOutW;
    si.hStdError = p.childOutW;
    si.hStdInput = withStdin ? p.childInR : GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return false;
    p.proc = pi.hProcess; p.thread = pi.hThread;
    return true;
}

// 读子进程 stdout 直到 END/超时/取消；每行回调。返回是否见到 END。
bool pumpLines(SpawnPipes& p, DWORD deadlineMs, const std::atomic<bool>* cancel,
               const std::function<bool(const std::wstring&)>& onLine, bool& timedOut) {
    timedOut = false;
    const ULONGLONG end = GetTickCount64() + deadlineMs;
    std::string acc;
    bool sawEnd = false;
    char buf[8192];
    while (!sawEnd) {
        if (cancel && cancel->load(std::memory_order_relaxed)) break;
        DWORD avail = 0;
        if (!PeekNamedPipe(p.childOutR, nullptr, 0, nullptr, &avail, nullptr)) break; // 管道已关（子进程退出）
        if (avail > 0) {
            DWORD got = 0;
            if (!ReadFile(p.childOutR, buf, (DWORD)std::min<DWORD>(sizeof(buf), avail), &got, nullptr) || got == 0) break;
            acc.append(buf, got);
            size_t nl;
            while ((nl = acc.find('\n')) != std::string::npos) {
                std::string line = acc.substr(0, nl);
                acc.erase(0, nl + 1);
                if (line.empty()) continue;
                if (line == "END") { sawEnd = true; break; }
                onLine(fromUtf8(line));
            }
            continue;
        }
        // 无数据：等 100ms 看子进程是否结束/新数据
        if (WaitForSingleObject(p.proc, 100) == WAIT_OBJECT_0) {
            if (!PeekNamedPipe(p.childOutR, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) break; // 排空
        }
        if (GetTickCount64() > end) { timedOut = true; break; }
    }
    if (timedOut || !sawEnd) { TerminateProcess(p.proc, 2); WaitForSingleObject(p.proc, 3000); } // G7：超时/取消均杀兜底（子进程可能挂在句柄解析）
    return sawEnd;
}

} // namespace

DeepScanResult runDeepScan(const std::vector<std::wstring>& originalPaths, DWORD budgetMs,
                           const std::atomic<bool>* cancel, const std::wstring& exePathOverride) {
    DeepScanResult res;
    SpawnPipes p;
    if (!spawnSelf(exePathOverride, L"--internal-htfull --cli --budget " + std::to_wstring(budgetMs), p, true)) {
        res.spawnFailed = true; res.error = L"深扫子进程启动失败"; p.closeAll(true); return res;
    }
    // stdin 传路径（UTF-8 行），写完关闭
    {
        std::string in;
        for (const auto& s : originalPaths) { in += toUtf8(s); in += '\n'; }
        DWORD wr = 0;
        WriteFile(p.childInW, in.data(), DWORD(in.size()), &wr, nullptr);
        CloseHandle(p.childInW); p.childInW = nullptr;
    }
    bool timedOut = false;
    pumpLines(p, budgetMs + 4000, cancel, [&](const std::wstring& line) -> bool {
        const auto f = splitTabs(line);
        if (f.size() >= 2 && f[0] == L"BEGIN") { res.totalHandles = _wcstoi64(f[1].c_str(), nullptr, 10); }
        else if (f.size() >= 2 && f[0] == L"ERR") { res.error = f[1]; } // 子进程诊断透传（ERR 行不再隐身）
        else if (f.size() >= 5 && f[0] == L"HIT") {
            LockInfo li;
            li.pid = static_cast<std::uint32_t>(wcstoul(f[1].c_str(), nullptr, 10));
            li.name = f[2];
            li.imagePath = f[3];
            li.occupiedFile = f[4];
            li.kind = LockKind::FileHandle;
            li.engines = EngineDeep;
            res.locks.push_back(std::move(li));
        }
        return true;
    }, timedOut);
    GetExitCodeProcess(p.proc, &res.exitCode); // v1.0 取证：子进程退出码
    res.timedOut = timedOut;
    if (timedOut) res.error = L"深扫超时，子进程已终止（保留部分结果）";
    if (p.childInW) CloseHandle(p.childInW);
    p.closeAll(true);
    return res;
}

ProcHandleResult runProcessHandleScan(DWORD pid, DWORD budgetMs, const std::wstring& exePathOverride) {
    ProcHandleResult res;
    SpawnPipes p;
    if (!spawnSelf(exePathOverride, L"--internal-procscan " + std::to_wstring(pid) + L" --cli --budget " + std::to_wstring(budgetMs), p, false)) {
        res.spawnFailed = true; res.error = L"句柄明细子进程启动失败"; p.closeAll(true); return res;
    }
    bool timedOut = false;
    pumpLines(p, budgetMs + 4000, nullptr, [&](const std::wstring& line) -> bool {
        const auto f = splitTabs(line);
        if (f.size() >= 4 && f[0] == L"HL") {
            HandleRow r;
            r.handleValue = std::wcstoull(f[1].c_str(), nullptr, 16);
            r.grantedAccess = static_cast<std::uint32_t>(std::wcstoul(f[2].c_str(), nullptr, 16));
            r.type = static_cast<std::uint8_t>(std::wcstoul(f[3].c_str(), nullptr, 10));
            if (f.size() >= 5) r.path = f[4];
            res.rows.push_back(std::move(r));
        }
        return true;
    }, timedOut);
    res.timedOut = timedOut;
    if (timedOut) res.error = L"句柄明细枚举超时，子进程已终止（保留部分结果）";
    p.closeAll(true);
    return res;
}

} // namespace who::win32
