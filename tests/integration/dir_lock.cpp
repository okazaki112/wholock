// G4 判据（v1.0 F4 目录目标）：锁 dir\sub\a.bin → 以目录为输入 prepareScanTargets 必须展开命中该文件；
// RM 多目标注册报出持锁进程；HT(hints=self) 命中且 occupiedFile 定位到具体文件。
// 负样例：空目录 / 不存在目录 必须被拒。
#include "domain/Normalize.h"
#include "../TestAssert.h"
#include <windows.h>
#include <algorithm>
#include <string>

#include "infrastructure/win32/PrepareTargets.h"
#include "infrastructure/win32/RmScanner.h"
#include "infrastructure/win32/HandleTableScanner.h"
#include "infrastructure/win32/WindowsPathMap.h"

using namespace who;

namespace {
std::wstring tempRoot() {
    wchar_t dir[MAX_PATH]; GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir) + L"wholock-dir-" + std::to_wstring(GetCurrentProcessId());
}
bool hasPid(const std::vector<LockInfo>& v, uint32_t pid) {
    return std::any_of(v.begin(), v.end(), [&](const LockInfo& li) { return li.pid == pid; });
}
} // namespace

int main() {
    win32::WindowsPathMap map;
    const auto root = tempRoot();
    const auto sub = root + L"\\sub";
    CreateDirectoryW(root.c_str(), nullptr);
    CreateDirectoryW(sub.c_str(), nullptr);
    const auto victim = sub + L"\\a.bin";
    {
        HANDLE h = CreateFileW(victim.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        CHECK(h != INVALID_HANDLE_VALUE, L"目录内目标创建");
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
    // 非独占写锁（share RW|D：模拟常规占用，READ_ATTRIBUTES 可探测）
    HANDLE locker = CreateFileW(victim.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(locker != INVALID_HANDLE_VALUE, L"目录内文件持锁");

    // 正判据 1：目录输入展开
    std::wstring err;
    win32::PreparedTargets prep;
    CHECK(win32::prepareScanTargets({ root }, map, prep, &err), L"目录 prepare 成功");
    CHECK(prep.expandedDirs == 1, L"目录计数");
    bool hasVictim = std::any_of(prep.refs.begin(), prep.refs.end(), [&](const TargetRef& t) {
        return t.originalPath.size() >= victim.size() &&
               _wcsicmp(t.originalPath.c_str(), victim.c_str()) == 0;
    });
    CHECK(hasVictim, L"G4: 递归展开命中 sub\\a.bin");

    ScanQuery q;
    q.targets = prep.refs;
    q.candidateHints = { GetCurrentProcessId() };

    // 正判据 2：RM 多目标注册命中持锁进程
    win32::RmScanner rm;
    auto ocRm = rm.scan(q);
    CHECK(ocRm.ok, L"RM 多目标扫描成功");
    if (!ocRm.ok) wprintf(L"RM err=%s\n", ocRm.error.c_str());
    if (ocRm.ok) CHECK(hasPid(ocRm.locks, GetCurrentProcessId()), L"G4: RM 目录模式报出持锁者");

    // 正判据 3：HT 命中且 occupiedFile 定位到具体文件
    win32::HandleTableScanner ht(&map);
    auto ocHt = ht.scan(q);
    CHECK(ocHt.ok, L"HT 目录扫描成功");
    if (ocHt.ok) {
        const auto it = std::find_if(ocHt.locks.begin(), ocHt.locks.end(),
                                     [&](const LockInfo& li) { return li.pid == GetCurrentProcessId(); });
        CHECK(it != ocHt.locks.end(), L"G4: HT 报出持锁者");
        if (it != ocHt.locks.end()) {
            std::wstring occ = it->occupiedFile;
            for (auto& c : occ) if (c >= L'A' && c <= L'Z') c = wchar_t(c - L'A' + L'a');
            std::wstring vic = victim;
            for (auto& c : vic) if (c >= L'A' && c <= L'Z') c = wchar_t(c - L'A' + L'a');
            CHECK(occ == vic, L"G4: occupiedFile 精确到被锁文件");
        }
    }

    CloseHandle(locker);
    // 负样例：空目录与不存在目录
    const auto emptyDir = root + L"\\empty";
    CreateDirectoryW(emptyDir.c_str(), nullptr);
    win32::PreparedTargets p2;
    CHECK(!win32::prepareScanTargets({ emptyDir }, map, p2, &err), L"负样例:空目录被拒");
    win32::PreparedTargets p3;
    CHECK(!win32::prepareScanTargets({ root + L"\\no-such-dir" }, map, p3, &err), L"负样例:不存在目录被拒");

    DeleteFileW(victim.c_str());
    RemoveDirectoryW(emptyDir.c_str());
    RemoveDirectoryW(sub.c_str());
    RemoveDirectoryW(root.c_str());
    return reportAndExit();
}
