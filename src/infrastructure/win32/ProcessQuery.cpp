#include "ProcessQuery.h"

namespace who::win32 {

HANDLE openProcForDup(DWORD pid) {
    return OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_DUP_HANDLE, FALSE, pid);
}

std::wstring queryFullImage(HANDLE hProc, std::wstring* outBaseLower) {
    if (!hProc) return {};
    wchar_t buf[4096];
    DWORD sz = 4096;
    if (!QueryFullProcessImageNameW(hProc, 0, buf, &sz)) return {};
    std::wstring img(buf, sz);
    if (outBaseLower) {
        const auto p = img.find_last_of(L"\\/");
        std::wstring baseName = (p == std::wstring::npos) ? img : img.substr(p + 1);
        for (auto& c : baseName) if (c >= L'A' && c <= L'Z') c = wchar_t(c - L'A' + L'a');
        *outBaseLower = std::move(baseName);
    }
    return img;
}

} // namespace who::win32
