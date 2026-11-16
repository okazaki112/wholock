#include "Privileges.h"
#include <windows.h>

namespace who::win32 {

bool enableDebugPrivilege() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) return false;
    LUID luid{};
    const BOOL found = LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &luid);
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    const BOOL ok = found && AdjustTokenPrivileges(tok, FALSE, &tp, sizeof(tp), nullptr, nullptr) &&
                    GetLastError() != ERROR_NOT_ALL_ASSIGNED;
    CloseHandle(tok);
    return ok != FALSE;
}

bool isElevated() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    TOKEN_ELEVATION el{};
    DWORD len = sizeof(el);
    const BOOL ok = GetTokenInformation(tok, TokenElevation, &el, sizeof(el), &len);
    CloseHandle(tok);
    return ok != FALSE && el.TokenIsElevated != 0;
}

} // namespace who::win32
