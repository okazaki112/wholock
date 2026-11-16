#include "ShellIntegration.h"
#include <windows.h>
#include <algorithm>
#include <string>

namespace who::win32 {
namespace {

constexpr wchar_t kFileKey[] = L"Software\\Classes\\*\\shell\\WhoLock";
constexpr wchar_t kDirKey[]  = L"Software\\Classes\\Directory\\shell\\WhoLock";
constexpr wchar_t kMenuText[] = L"查找占用(&W)";

std::wstring selfExe() {
    wchar_t buf[4096];
    GetModuleFileNameW(nullptr, buf, 4095);
    return buf;
}

std::wstring regGetString(HKEY root, const wchar_t* sub, const wchar_t* name) {
    wchar_t buf[4096] = {};
    DWORD sz = sizeof(buf) - sizeof(wchar_t);
    if (RegGetValueW(root, sub, name, RRF_RT_REG_SZ, nullptr, buf, &sz) != ERROR_SUCCESS) return {};
    return buf;
}

bool keyExists(HKEY root, const wchar_t* sub) {
    HKEY k = nullptr;
    const LSTATUS r = RegOpenKeyExW(root, sub, 0, KEY_QUERY_VALUE, &k);
    if (r == ERROR_SUCCESS) { RegCloseKey(k); return true; }
    return false;
}

bool writeShellKeys(const std::wstring& exe, std::wstring* errOut) {
    struct KeySpec { const wchar_t* key; bool isDir; };
    const KeySpec specs[] = { { kFileKey, false }, { kDirKey, true } };
    for (const auto& sp : specs) {
        HKEY k = nullptr;
        const LSTATUS r = RegCreateKeyExW(HKEY_CURRENT_USER, sp.key, 0, nullptr,
                                          REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &k, nullptr);
        if (r != ERROR_SUCCESS) {
            if (errOut) *errOut = L"RegCreateKey 失败(code=" + std::to_wstring(r) + L"): " + sp.key;
            return false;
        }
        const std::wstring cmd = L"\"" + exe + L"\" \"%1\"";
        RegSetValueExW(k, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(kMenuText),
                       DWORD((wcslen(kMenuText) + 1) * sizeof(wchar_t)));
        RegSetValueExW(k, L"Icon", 0, REG_SZ, reinterpret_cast<const BYTE*>(exe.c_str()),
                       DWORD((exe.size() + 1) * sizeof(wchar_t)));
        RegSetValueExW(k, L"command", 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                       DWORD((cmd.size() + 1) * sizeof(wchar_t))); // command 作为默认值写在子键
        RegCloseKey(k);
        // command 子键（HKCU\...\WhoLock\command 默认值）
        std::wstring cmdKey = std::wstring(sp.key) + L"\\command";
        HKEY ck = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, cmdKey.c_str(), 0, nullptr,
                            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &ck, nullptr) == ERROR_SUCCESS) {
            RegSetValueExW(ck, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                           DWORD((cmd.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(ck);
        }
        (void)sp.isDir;
    }
    return true;
}

} // namespace

ShellIntegrationState queryShellIntegration() {
    ShellIntegrationState s;
    s.fileKey = keyExists(HKEY_CURRENT_USER, kFileKey);
    s.dirKey = keyExists(HKEY_CURRENT_USER, kDirKey);
    s.registeredExe = regGetString(HKEY_CURRENT_USER,
                                   (std::wstring(kFileKey) + L"\\command").c_str(), nullptr);
    return s;
}

bool enableShellIntegration(std::wstring* errOut) {
    // 每次启用都重写为【当前】exe 路径——挪动程序后取消再勾选即修复指向
    return writeShellKeys(selfExe(), errOut);
}

bool disableShellIntegration(std::wstring* errOut) {
    const LSTATUS r1 = RegDeleteTreeW(HKEY_CURRENT_USER, kFileKey);
    const LSTATUS r2 = RegDeleteTreeW(HKEY_CURRENT_USER, kDirKey);
    const bool ok = (r1 == ERROR_SUCCESS || r1 == ERROR_FILE_NOT_FOUND) &&
                    (r2 == ERROR_SUCCESS || r2 == ERROR_FILE_NOT_FOUND);
    if (!ok && errOut) *errOut = L"RegDeleteTree 失败(code=" + std::to_wstring(r1) + L"/" + std::to_wstring(r2) + L")";
    return ok;
}

} // namespace who::win32
