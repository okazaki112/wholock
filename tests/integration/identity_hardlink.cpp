// G6 判据（OS 身份语义为尺）：锁硬链接路径 → 按原名扫描必须命中（FileIdInfo 等价类）；
// 负对照：抹掉 identity → 同一扫描必须不命中（证明命中来自身份兜底而非字符串巧合，防恒真断言）。
#include "domain/Normalize.h"
#include "../TestAssert.h"
#include <windows.h>
#include <string>
#include <algorithm>

#include "infrastructure/win32/HandleTableScanner.h"
#include "infrastructure/win32/WindowsPathMap.h"

using namespace who;

namespace {
std::wstring tempBase() {
    wchar_t dir[MAX_PATH]; GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir) + L"wholock-g6-" + std::to_wstring(GetCurrentProcessId());
}
bool hasPid(const std::vector<LockInfo>& v, uint32_t pid) {
    return std::any_of(v.begin(), v.end(), [&](const LockInfo& li) { return li.pid == pid; });
}
} // namespace

int main() {
    win32::WindowsPathMap map;
    const auto base = tempBase();
    const std::wstring orig = base + L"-orig.bin", link = base + L"-link.bin";

    { // 建原名文件
        HANDLE h = CreateFileW(orig.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        CHECK(h != INVALID_HANDLE_VALUE, L"原名文件创建");
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
    CHECK(CreateHardLinkW(link.c_str(), orig.c_str(), nullptr) != FALSE, L"硬链接创建");

    // 以非独占写锁打开硬链接路径（模拟常规占用：share 允许 READ_ATTRIBUTES 探测）
    HANDLE locker = CreateFileW(link.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(locker != INVALID_HANDLE_VALUE, L"硬链接侧持锁");

    std::wstring err;
    FileIdentity identity;
    auto abs = win32::WindowsPathMap::absolutizeExistingFile(orig, &err, &identity);
    CHECK(abs.has_value(), L"原名 absolutize");
    CHECK(identity.valid, L"G6 前置：FileIdInfo 身份可得");

    auto key = normalizePathKey(*abs, &map, &err);
    CHECK(key.has_value(), L"normalize");
    const uint32_t self = GetCurrentProcessId();

    ScanQuery q;
    q.targets.push_back(TargetRef{ *abs, *key, identity });
    q.candidateHints = { self };
    win32::HandleTableScanner ht(&map);
    auto oc = ht.scan(q);
    CHECK(oc.ok, L"HT 扫描成功");
    if (!oc.ok) wprintf(L"err=%s\n", oc.error.c_str());
    if (oc.ok) CHECK(hasPid(oc.locks, self), L"G6: 硬链接锁必须按原名命中（身份兜底）");

    // 负对照：抹掉 identity → 同场景必须不命中（排除字符串巧合，断言不恒真）
    ScanQuery qNeg;
    qNeg.targets.push_back(TargetRef{ *abs, *key, FileIdentity{} });
    qNeg.candidateHints = { self };
    auto ocNeg = ht.scan(qNeg);
    CHECK(ocNeg.ok, L"负对照扫描成功");
    if (ocNeg.ok) CHECK(!hasPid(ocNeg.locks, self), L"负对照: 无身份时字符串不等→不命中");

    CloseHandle(locker);
    DeleteFileW(link.c_str());
    DeleteFileW(orig.c_str());
    return reportAndExit();
}
