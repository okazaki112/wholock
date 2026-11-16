#include "CliMain.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "version.h" // F2 单一事实源
#include "../infrastructure/win32/HandleTableScanner.h" // F6/F7 子进程端
#include "../infrastructure/win32/PrepareTargets.h"

#include "../application/ScanCoordinator.h"
#include "../domain/CliArgs.h"
#include "../domain/Normalize.h"
#include "../infrastructure/win32/Engines.h"
#include "../infrastructure/win32/PrepareTargets.h"
#include "../ui/Theme.h" // kindText/engineText 单一事实源（展示层共享，非 ui 窗口依赖）

namespace who::cli {
namespace {

void writeOut(const std::string& s) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD w = 0; WriteFile(h, s.data(), DWORD(s.size()), &w, nullptr);
}

std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string jsonEscape(const std::wstring& w) {
    std::string out;
    for (const wchar_t c : w) {
        switch (c) {
        case L'"':  out += "\\\""; break;
        case L'\\': out += "\\\\"; break;
        case L'\n': out += "\\n"; break;
        case L'\t': out += "\\t"; break;
        default:
            if (c < 0x20) { char b[10]; sprintf_s(b, "\\u%04x", static_cast<unsigned>(c)); out += b; }
            else out += toUtf8(std::wstring(1, c));
        }
    }
    return out;
}

// ---------- v1.0 F6/F7 子进程内部模式（DeepScanRunner 协议端） ----------
std::wstring readAllStdinUtf8() {
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    std::string acc;
    char buf[8192];
    DWORD got = 0;
    while (ReadFile(h, buf, sizeof(buf), &got, nullptr) && got > 0) acc.append(buf, got);
    if (acc.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, acc.data(), int(acc.size()), nullptr, 0);
    std::wstring w(n > 0 ? n : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, acc.data(), int(acc.size()), w.data(), n);
    return w;
}

void writeLineW(const std::wstring& line) { writeOut(toUtf8(line) + "\n"); } // 管道无用户态缓冲，逐行即时

std::vector<std::wstring> splitLines(const std::wstring& text) {
    std::vector<std::wstring> out;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find(L'\n', pos);
        if (nl == std::wstring::npos) nl = text.size();
        std::wstring line = text.substr(pos, nl - pos);
        pos = nl + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (!line.empty()) out.push_back(std::move(line));
    }
    return out;
}

int runInternalHtfull(const CliOptions& o) {
    { // 诊断：暴露 stdin 句柄状态（定位继承链问题后移除）
        wchar_t dbg[96];
        const HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
        swprintf_s(dbg, L"%p valid=%d", h, h != nullptr && h != INVALID_HANDLE_VALUE);
        writeLineW(std::wstring(L"DBG\tstdin=") + dbg);
    }
    auto paths = splitLines(readAllStdinUtf8());
    if (paths.empty()) { writeLineW(L"ERR\tstdin 无路径"); return 2; }
    win32::WindowsPathMap map;
    win32::PreparedTargets prep;
    std::wstring err;
    if (!win32::prepareScanTargets(paths, map, prep, &err)) { writeLineW(L"ERR\t" + err); return 3; }
    ScanQuery query;
    query.targets = std::move(prep.refs);

    win32::HandleTableScanner ht(&map);
    std::thread timer([&] { Sleep(o.budgetMs); ht.cancel(); }); // 自杀兜底（父进程还有超时杀）
    auto oc = ht.scanFull(query);
    timer.join();

    if (!oc.ok) writeLineW(L"ERR\t" + oc.error); // 诊断透传（此前被吞：父侧只见 BEGIN\t0+END）
    writeLineW(L"BEGIN\t" + std::to_wstring(ht.lastEnumHandleCount()));
    for (const auto& li : oc.locks)
        writeLineW(L"HIT\t" + std::to_wstring(li.pid) + L"\t" + li.name + L"\t" + li.imagePath + L"\t" + li.occupiedFile);
    writeLineW(L"END");
    return oc.ok ? 0 : 2;
}

int runInternalProcscan(const CliOptions& o) {
    std::atomic<bool> cancel{false};
    std::thread timer([&] { Sleep(o.budgetMs); cancel.store(true, std::memory_order_relaxed); });
    auto rows = win32::enumerateProcessHandles(o.internalPid, &cancel);
    timer.join();
    for (const auto& r : rows) {
        wchar_t hb[32], ab[16];
        swprintf_s(hb, L"%llx", static_cast<unsigned long long>(r.handleValue));
        swprintf_s(ab, L"%lx", r.grantedAccess);
        writeLineW(std::wstring(L"HL\t") + hb + L"\t" + ab + L"\t" + std::to_wstring(r.type) + L"\t" + r.path);
    }
    writeLineW(L"END");
    return 0;
}

} // namespace

int run(const std::vector<std::wstring>& argv, win32::WindowsPathMap& map) {
    writeLineW(L"DBG\trun-enter argv=" + std::to_wstring(argv.size())); // 子进程到达性取证
    CliOptions o; std::wstring err;
    if (!parseCli(argv, o, err)) { writeOut(toUtf8(err) + "\n"); return 2; }
    if (o.mode == CliOptions::Mode::InternalHtfull)   return runInternalHtfull(o);  // F6 子进程端
    if (o.mode == CliOptions::Mode::InternalProcscan) return runInternalProcscan(o); // F7 子进程端
    if (o.usageRequested) {
        writeOut("usage: WhoLock.exe [--cli] [--json] [--version] [--engine=rm|ht|both] <file-path>\n");
        return 0;
    }
    if (o.versionRequested) { writeOut("WhoLock v" WHOLOCK_VERSION_STR "\n"); return 0; }
    // 按 --engine 现场装配，开关语义与工厂位一致（GUI 常驻双引擎）
    auto scanners = win32::createEngines(o.engineMask, &map);
    if (scanners.empty()) { writeOut("--engine 结果集为空\n"); return 2; }
    ScanCoordinator coord(std::move(scanners));

    win32::PreparedTargets prep;
    if (!win32::prepareScanTargets(o.paths, map, prep, &err)) { writeOut(toUtf8(err) + "\n"); return 3; }
    const std::size_t targetCount = prep.refs.size(); // use-before-move：摘要计数须在 move 前取
    const std::size_t expandedDirs = prep.expandedDirs;
    const std::wstring prepNote = prep.note;
    ScanQuery query;
    query.targets = std::move(prep.refs);
    auto rep = coord.execute(query);

    if (o.json) {
        std::string j = "[";
        for (size_t i = 0; i < rep.merged.size(); ++i) {
            const auto& li = rep.merged[i];
            if (i) j += ",";
            j += "{\"pid\":" + std::to_string(li.pid) +
                 ",\"name\":\"" + jsonEscape(li.name) +
                 "\",\"imagePath\":\"" + jsonEscape(li.imagePath) +
                 "\",\"kind\":\"" + toUtf8(ui::kindText(li.kind)) +
                 "\",\"engines\":\"" + toUtf8(ui::engineText(li.engines)) +
                 "\",\"occupiedFile\":\"" + jsonEscape(li.occupiedFile) +
                 "\",\"occupiedMore\":" + std::to_string(li.occupiedMore) + "}";
        }
        writeOut(j + "]\n");
    } else {
        writeOut(toUtf8(L"目标: " + std::to_wstring(targetCount) + L" 个文件" +
                        (expandedDirs ? (L"（含目录展开×" + std::to_wstring(expandedDirs) + L"）") : L"")) + "\n");
        if (!prepNote.empty()) writeOut(toUtf8(L"· " + prepNote) + "\n");
        writeOut(toUtf8(L"引擎: " + ui::engineText(o.engineMask)) + "\n");
        for (const auto& n : rep.notes)
            writeOut(toUtf8(n.ok ? (L"  · " + n.engine + L" 命中×" + std::to_wstring(n.count))
                                  : (L"  · " + n.engine + L" 失败: " + n.error)) + "\n");
        for (const auto& li : rep.merged) {
            std::wstring tail = li.occupiedFile.empty() ? std::wstring()
                : (L"  占用: " + li.occupiedFile + (li.occupiedMore ? (L" (+" + std::to_wstring(li.occupiedMore) + L")") : L""));
            writeOut(toUtf8(L"  [" + std::to_wstring(li.pid) + L"] " + li.name + L"  " + ui::kindText(li.kind) +
                             L"  " + (li.imagePath.empty() ? L"" : li.imagePath) + L"  " + ui::engineText(li.engines) + tail) + "\n");
        }
        if (rep.merged.empty() && rep.anyOk) writeOut(toUtf8(L"  （未发现占用者；只读共享句柄属 RM 盲区）\n"));
    }
    return rep.allFailed() && !rep.notes.empty() && rep.merged.empty() ? 3 : 0;
}

} // namespace who::cli
