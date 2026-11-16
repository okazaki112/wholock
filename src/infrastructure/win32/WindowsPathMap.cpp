#include "WindowsPathMap.h"
#include <windows.h>
#include <cstring>
#include <algorithm>
#include <cwchar>

namespace who::win32 {
namespace {

std::wstring sysErrorText() {
    LPWSTR buf = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                   FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, GetLastError(), 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring msg = buf ? std::wstring(buf, n) : L"";
    if (buf) LocalFree(buf);
    while (!msg.empty() && (msg.back() == L'\r' || msg.back() == L'\n' || msg.back() == L' ')) msg.pop_back();
    return msg;
}

bool startsWith(const std::wstring& s, const std::wstring& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

} // namespace

WindowsPathMap::WindowsPathMap() {
    wchar_t buf[512] = {};
    if (GetLogicalDriveStringsW(511, buf)) {
        for (wchar_t* p = buf; *p; p += wcslen(p) + 1) { // 形如 "C:\"
            if (p[0] && p[1] == L':') drives_.emplace_back(p, 2);  // "C:"
        }
    }
    for (const auto& d : drives_) {                      // 查询期间容器不再增长，引用稳定
        wchar_t dev[512] = {};
        if (QueryDosDeviceW(d.c_str(), dev, 511)) devToDrive_.emplace_back(dev, d);
    }
    std::sort(devToDrive_.begin(), devToDrive_.end(),               // 最长设备前缀优先匹配
              [](const auto& a, const auto& b) { return a.first.size() > b.first.size(); });
}

std::optional<std::wstring> WindowsPathMap::toDosPath(const std::wstring& devicePath) const {
    for (const auto& [dev, drive] : devToDrive_) {
        if (!startsWith(devicePath, dev)) continue;
        const wchar_t after = devicePath.size() > dev.size() ? devicePath[dev.size()] : L'\0';
        if (after != L'\\' && after != L'\0') continue; // 前缀必须落在路径分段边界
        std::wstring rest = devicePath.substr(dev.size());
        if (rest.empty()) rest = L"\\";
        return drive + rest;
    }
    return std::nullopt;
}

std::wstring WindowsPathMap::dosFromVolumeGuid(const std::wstring& guidPath) const {
    // 反查：枚举盘符 → 取各盘符的卷 GUID → 与目标比对（GetVolumeNameForVolumeMountPointW 往返即尺子）
    if (guidPath.empty()) return {};
    wchar_t vol[261];
    for (const auto& d : drives_) {
        std::wstring mount = d + L"\\";
        if (GetVolumeNameForVolumeMountPointW(mount.c_str(), vol, 261) &&
            _wcsicmp(vol, guidPath.c_str()) == 0)
            return mount; // "C:\"
    }
    return {};
}

std::optional<std::wstring> WindowsPathMap::absolutizeExistingFile(const std::wstring& raw, std::wstring* errOut,
                                                                   FileIdentity* outIdentity) {
    auto fail = [&](const wchar_t* msg) -> std::optional<std::wstring> {
        if (errOut) *errOut = msg;
        return std::nullopt;
    };
    wchar_t buf[16 + 4096];
    const DWORD n = GetFullPathNameW(raw.c_str(), 4096, buf, nullptr);
    if (n == 0 || n >= 4096) return fail(L"路径解析失败或过长");
    std::wstring full(buf, n);

    const DWORD attr = GetFileAttributesW(full.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return fail((L"文件不存在或无法访问：" + sysErrorText()).c_str());
    if (attr & FILE_ATTRIBUTE_DIRECTORY) return fail(L"目录目标请用扫描目录功能（v1.0 F4；CLI 直接传入目录亦可）");

    // F1 身份 best-effort：READ_ATTRIBUTES + 全共享打开通常不受普通写锁影响；share=0 独占时失败→invalid
    if (outIdentity) {
        *outIdentity = {};
        const HANDLE h = CreateFileW(full.c_str(), FILE_READ_ATTRIBUTES,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            FILE_ID_INFO fii{};
            if (GetFileInformationByHandleEx(h, FileIdInfo, &fii, sizeof(fii))) {
                outIdentity->volumeSerial = fii.VolumeSerialNumber;
                std::memcpy(&outIdentity->fileId, &fii.FileId, sizeof(outIdentity->fileId)); // NTFS FileId64=低8字节
                outIdentity->valid = true;
            }
            CloseHandle(h); // 双路径：成功失败都闭环，防句柄泄漏
        }
    }
    return full; // \\?\ 前缀清理交由 normalizePathKey 统一处理
}

} // namespace who::win32
