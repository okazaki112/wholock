#include "Normalize.h"
#include <algorithm>
#include <cwchar>
#include <vector>

namespace who {
namespace {

constexpr wchar_t kLowerA = L'a', kUpperA = L'A', kUpperZ = L'Z';

wchar_t lowerW(wchar_t c) {
    if (c >= kUpperA && c <= kUpperZ) return wchar_t(c - kUpperA + kLowerA);
    return c; // 非 ASCII 原样保留（比对键只做 ASCII 小写；NTFS 大小写不敏感仍成立）
}

std::wstring toLowerAscii(const std::wstring& s) {
    std::wstring r = s;
    std::transform(r.begin(), r.end(), r.begin(), lowerW);
    return r;
}

bool startsWith(const std::wstring& s, const wchar_t* p) {
    return s.size() >= wcslen(p) && s.compare(0, wcslen(p), p) == 0;
}

bool isAlpha(wchar_t c) { return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z'); }

bool drivePrefix(const std::wstring& s, size_t at) {
    return at + 2 <= s.size() && isAlpha(s[at]) && s[at + 1] == L':';
}

void fail(std::wstring* out, const wchar_t* msg) { if (out) *out = msg; }

} // namespace

std::optional<std::wstring> normalizePathKey(const std::wstring& raw,
                                             const IPathMap* map,
                                             std::wstring* errOut) {
    if (raw.empty()) { fail(errOut, L"空路径"); return std::nullopt; }

    std::wstring s = raw;

    // 1) 设备路径换成盘符形式（映射器由 infrastructure 注入；单测用 Fake）
    if (startsWith(s, L"\\Device\\")) {
        auto dos = map ? map->toDosPath(s) : std::nullopt;
        if (!dos) { fail(errOut, L"设备路径无法映射到盘符（缺少映射器或盘未挂载）"); return std::nullopt; }
        s = *dos;
    }

    // 2) 长前缀剥离：去除 win32 命名空间前缀，UNC/普通路径各归其位
    if (startsWith(s, L"\\\\?\\")) {
        if (startsWith(s, L"\\\\?\\UNC\\")) { s.erase(0, 8); s.insert(0, L"\\\\"); }
        else s.erase(0, 4);
    } else if (startsWith(s, L"\\??\\")) {
        if (startsWith(s, L"\\??\\UNC\\")) { s.erase(0, 8); s.insert(0, L"\\\\"); }
        else s.erase(0, 4);
    }

    // 3) 正斜杠统一；判定 UNC / 盘符绝对路径
    std::replace(s.begin(), s.end(), L'/', L'\\');
    const bool unc = startsWith(s, L"\\\\");
    const bool rootedDrive = drivePrefix(s, 0);
    if (!rootedDrive && !unc) { fail(errOut, L"需要绝对路径（盘符 X: 或双斜杠服务器共享）"); return std::nullopt; }

    // 4) 拆 header（UNC=服务器+共享两级 / 非UNC=盘符），再分段解析 . 与 ..
    std::wstring header;
    size_t pos = 0;
    if (unc) {
        std::wstring parts[2];
        size_t p = 2;
        for (int field = 0; field < 2; ++field) {
            if (p > s.size()) { fail(errOut, L"UNC 路径必须含服务器与共享名"); return std::nullopt; } // 修复 substr 越界抛异常（0xc0000409）
            const size_t slash = s.find(L'\\', p);
            std::wstring token = (slash == std::wstring::npos) ? s.substr(p) : s.substr(p, slash - p);
            if (token.empty() || drivePrefix(token, 0)) { fail(errOut, L"UNC 路径必须含服务器与共享名"); return std::nullopt; }
            parts[field] = std::move(token);
            p = (slash == std::wstring::npos) ? s.size() + 1 : slash + 1;
        }
        header = L"\\\\" + parts[0] + L"\\" + parts[1] + L"\\";
        pos = p;
    } else {
        header = s.substr(0, 2) + L"\\";
        pos = 2;
        if (s.size() > 2 && s[2] == L'\\') pos = 3;
    }

    std::vector<std::wstring> segs;
    while (pos < s.size()) {
        const size_t slash = s.find(L'\\', pos);
        std::wstring seg = (slash == std::wstring::npos) ? s.substr(pos) : s.substr(pos, slash - pos);
        pos = (slash == std::wstring::npos) ? s.size() : slash + 1;
        if (seg.empty() || seg == L".") continue;
        if (seg == L"..") { if (!segs.empty()) segs.pop_back(); continue; } // 不越过根
        segs.push_back(std::move(seg));
    }

    std::wstring out = header;
    for (size_t i = 0; i < segs.size(); ++i) { if (i) out += L'\\'; out += segs[i]; }
    if (out.size() > 3 && out.back() == L'\\') out.pop_back();

    // 5) 去尾随点/空格（末端；中间段列入 v0.1 已知限制）
    while (out.size() > 3 && (out.back() == L' ' || out.back() == L'.')) out.pop_back();

    // 6) 比对键统一 ASCII 小写
    return toLowerAscii(out);
}

std::wstring pathBaseLower(const std::wstring& path) {
    const auto p = path.find_last_of(L"\\/");
    return toLowerAscii(p == std::wstring::npos ? path : path.substr(p + 1));
}

} // namespace who
