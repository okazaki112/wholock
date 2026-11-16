#include "ProcessOps.h"
#include <windows.h>

namespace who::win32 {
namespace {
std::wstring lastErrorText(DWORD code) {
    LPWSTR buf = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                   FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, code, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring msg = buf ? std::wstring(buf, n) : L"";
    if (buf) LocalFree(buf);
    while (!msg.empty() && (msg.back() == L'\r' || msg.back() == L'\n' || msg.back() == L' ')) msg.pop_back();
    return msg;
}
} // namespace

bool terminateProcess(std::uint32_t pid, std::wstring* errOut) {
    const HANDLE hp = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!hp) {
        if (errOut) *errOut = L"OpenProcess 失败(code=" + std::to_wstring(GetLastError()) +
                              L"): " + lastErrorText(GetLastError());
        return false;
    }
    const BOOL ok = TerminateProcess(hp, 1u);
    const DWORD err = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(hp);
    if (!ok) {
        if (errOut) *errOut = L"TerminateProcess 失败(code=" + std::to_wstring(err) + L"): " + lastErrorText(err);
        return false;
    }
    return true;
}

// v1.0 F8：关闭远端进程单个句柄——DuplicateHandle 到本进程并带 DUPLICATE_CLOSE_SOURCE，
// 源句柄随复制被系统关闭；不触碰进程本身（比 TerminateProcess 温和，风险=对方逻辑可能依赖该句柄）。
bool closeRemoteHandle(std::uint32_t ownerPid, std::uint64_t handleValue, std::wstring* errOut) {
    const HANDLE hp = OpenProcess(PROCESS_DUP_HANDLE, FALSE, ownerPid);
    if (!hp) {
        if (errOut) { const DWORD e = GetLastError();
            *errOut = L"OpenProcess(PROCESS_DUP_HANDLE) 失败(code=" + std::to_wstring(e) + L"): " + lastErrorText(e); }
        return false;
    }
    HANDLE dup = nullptr;
    const BOOL ok = DuplicateHandle(hp, reinterpret_cast<HANDLE>(static_cast<uintptr_t>(handleValue)),
                                    GetCurrentProcess(), &dup, 0, FALSE,
                                    DUPLICATE_SAME_ACCESS | DUPLICATE_CLOSE_SOURCE);
    const DWORD err = ok ? ERROR_SUCCESS : GetLastError();
    if (ok && dup) CloseHandle(dup); // 复制来的句柄立即关闭；源句柄已被 CLOSE_SOURCE 标志关闭
    CloseHandle(hp);
    if (!ok) {
        if (errOut) *errOut = L"关闭句柄失败(code=" + std::to_wstring(err) + L"): " + lastErrorText(err);
        return false;
    }
    return true;
}

bool deleteFileVerified(const std::wstring& path, std::wstring* errOut) {
    if (DeleteFileW(path.c_str()) == FALSE) {
        const DWORD e = GetLastError();
        if (errOut) *errOut = L"DeleteFile 失败(code=" + std::to_wstring(e) + L"): " + lastErrorText(e);
        return false;
    }
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) { // 假删除防线：删后必须消失
        if (errOut) *errOut = L"删除返回成功但文件仍在（可能被重建或延迟删除待定）";
        return false;
    }
    return true;
}

} // namespace who::win32
