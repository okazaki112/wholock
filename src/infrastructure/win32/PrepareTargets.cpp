#include "PrepareTargets.h"
#include "WindowsPathMap.h"
#include "../../domain/Normalize.h"
#include <windows.h>
#include <algorithm>

namespace who::win32 {
namespace {

// 目录递归展开：显式栈（免深递归爆栈）、深度上限、重解析点目录跳过（防软链成环）
void expandDirectory(const std::wstring& dir, std::vector<std::wstring>& files,
                     std::size_t maxFiles, std::size_t& capped, int depth = 0) {
    if (depth > 8 || files.size() >= maxFiles) { if (files.size() >= maxFiles) ++capped; return; }
    WIN32_FIND_DATAW fd{};
    const HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    struct Entry { std::wstring name; bool isDir; bool isReparse; };
    std::vector<Entry> entries;
    do {
        if (fd.cFileName[0] == L'.' &&
            (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0))) continue;
        entries.push_back({ fd.cFileName,
                            (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0,
                            (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 });
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    for (const auto& e : entries) {
        const std::wstring full = dir + L"\\" + e.name;
        if (!e.isDir) {
            if (files.size() >= maxFiles) { ++capped; continue; }
            files.push_back(full);
        } else if (!e.isReparse) { // 重解析目录跳过（限制已披露）
            expandDirectory(full, files, maxFiles, capped, depth + 1);
        }
    }
}

} // namespace

bool prepareScanTargets(const std::vector<std::wstring>& inputs, const WindowsPathMap& map,
                        PreparedTargets& out, std::wstring* errOut, std::size_t maxFiles) {
    out = {};
    std::vector<std::wstring> files;
    std::size_t capped = 0;
    bool anyInputOk = false;
    std::wstring lastErr;

    for (const auto& raw : inputs) {
        if (raw.empty()) continue;
        std::wstring err;
        const DWORD attr = GetFileAttributesW(raw.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) {
            const auto abs = WindowsPathMap::absolutizeExistingFile(raw, &err);
            if (!abs) { lastErr = err; continue; } // 不存在等：absolutize 已给原因
            anyInputOk = true;
            files.push_back(*abs);
            continue;
        }
        anyInputOk = true;
        if (attr & FILE_ATTRIBUTE_DIRECTORY) {
            ++out.expandedDirs;
            std::wstring d = raw;
            wchar_t buf[4096];
            if (GetFullPathNameW(raw.c_str(), 4096, buf, nullptr) > 0) d = buf;
            expandDirectory(d, files, maxFiles, capped);
        } else {
            files.push_back(raw);
        }
    }

    if (!anyInputOk) { if (errOut) *errOut = lastErr.empty() ? L"无有效输入路径" : lastErr; return false; }
    if (files.empty()) {
        const std::wstring msg = L"目标内无可枚举文件（空目录？）";
        out.note = msg;
        if (errOut) *errOut = msg;
        return false;
    }
    if (capped > 0) {
        out.cappedAt = maxFiles;
        out.note = L"文件数超上限 " + std::to_wstring(maxFiles) + L"，已截断（深层/超大目录建议单文件精确查）";
    }

    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end()); // 去重（多目录交叠输入）
    for (const auto& f : files) {
        std::wstring err;
        FileIdentity identity;
        const auto abs = WindowsPathMap::absolutizeExistingFile(f, &err, &identity);
        if (!abs) continue; // 枚举后文件被删等瞬态：跳过（note 已含截断信息）
        const auto key = normalizePathKey(*abs, &map, &err);
        if (!key) continue;
        out.refs.push_back(TargetRef{ *abs, *key, identity });
    }
    if (out.refs.empty()) { if (errOut) *errOut = L"目标全部无效或已消失"; return false; }
    return true;
}

} // namespace who::win32
