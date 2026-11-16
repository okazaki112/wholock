#include "../TestAssert.h"
#include "domain/Normalize.h"
#include "domain/Merge.h"
#include "domain/RiskFlags.h"
#include "domain/CliArgs.h"
#include <optional>

using namespace who;

namespace {
struct FakeMap final : IPathMap {
    std::optional<std::wstring> toDosPath(const std::wstring& d) const override {
        if (d.rfind(L"\\Device\\HarddiskVolume3\\", 0) == 0)
            return L"D:" + d.substr(24);
        return std::nullopt;
    }
};
const std::optional<std::wstring> okv(const wchar_t* s) { return std::wstring(s); }

void testNormalize() {
    FakeMap m;
    std::wstring e;
    CHECK_OPT(normalizePathKey(L"C:\\a\\..\\b.txt", &m, &e), okv(L"c:\\b.txt"), L"点段解析");
    CHECK_OPT(normalizePathKey(L"c:\\Foo Bar\\A. ", &m, &e), okv(L"c:\\foo bar\\a"), L"大小写+尾点空格");
    // （原无意义占位断言已删除）
    CHECK_OPT(normalizePathKey(L"\\\\?\\C:\\x\\y", &m), okv(L"c:\\x\\y"), L"长前缀");
    CHECK_OPT(normalizePathKey(L"\\\\?\\UNC\\srv\\share\\x", &m), okv(L"\\\\srv\\share\\x"), L"UNC长前缀");
    CHECK_OPT(normalizePathKey(L"\\\\srv\\share\\X", &m), okv(L"\\\\srv\\share\\x"), L"UNC 小写");
    CHECK_OPT(normalizePathKey(L"C:/dir/file.txt", &m), okv(L"c:\\dir\\file.txt"), L"正斜杠");
    CHECK_OPT(normalizePathKey(L"C:\\\\a\\\\\\\\b", &m), okv(L"c:\\a\\b"), L"连续斜杠折叠");
    CHECK_OPT(normalizePathKey(L"\\Device\\HarddiskVolume3\\dir\\f.TXT", &m), okv(L"d:\\dir\\f.txt"), L"设备映射");
    CHECK_OPT(normalizePathKey(L"C:\\a\\b\\..", &m), okv(L"c:\\a"), L"末尾..");
    auto root = normalizePathKey(L"C:\\..\\..\\a", &m, &e);
    CHECK(root.has_value() && *root == L"c:\\a", L"根之上归一到根下");

    // --- 负样例 ---
    CHECK(!normalizePathKey(L"", &m, &e).has_value(), L"负样例:空串");
    CHECK_EQ(e, std::wstring(L"空路径"), L"负样例:空串消息");
    CHECK(!normalizePathKey(L"foo.txt", &m, &e).has_value(), L"负样例:相对路径");
    CHECK(!normalizePathKey(L"\\\\bogus", &m, &e).has_value(), L"负样例:UNC 无共享名");
    CHECK(!normalizePathKey(L"\\\\?\\foo", &m, &e).has_value(), L"负样例:剥前缀后仍是相对路径");
    CHECK(!normalizePathKey(L"\\Device\\HarddiskVolume9\\a", &m, &e).has_value(), L"负样例:设备映射失败");
    CHECK(!normalizePathKey(L"\\Device\\HarddiskVolume3\\a", nullptr, &e).has_value(), L"负样例:无映射器");
    CHECK_EQ(pathBaseLower(L"C:\\W\\Notepad.EXE"), std::wstring(L"notepad.exe"), L"基本名小写");
    CHECK_EQ(pathBaseLower(L"bare.txt"), std::wstring(L"bare.txt"), L"基本名无斜杠");
}

void testMerge() {
    std::vector<LockInfo> base{ {44, L"notepad.exe", L"", LockKind::FileHandle, EngineRM, true} };
    mergeInto(base, { {44, L"notepad.exe", L"C:\\Windows\\notepad.exe", LockKind::FileHandle, EngineHT, false} });
    CHECK(base.size() == 1, L"同 pid+kind 合并");
    CHECK(base[0].engines == (EngineRM | EngineHT), L"engines 位或");
    CHECK(!base[0].imagePath.empty(), L"字段取较全侧");
    // v0.1 行键=pid（决策）：kind 不同也并入同一行；精确 kind 优先于 Unknown
    mergeInto(base, { {44, L"notepad.exe", L"", LockKind::MemoryMap, EngineRM, true} });
    CHECK(base.size() == 1, L"同 pid 跨 kind 合并为一行");
    CHECK(base[0].kind == LockKind::FileHandle, L"精确 kind 不被 MemoryMap 覆盖?（FileHandle 保留）");
    mergeInto(base, { {44, L"n.exe", L"", LockKind::Unknown, EngineRM, false},
                      {7, L"sys", L"", LockKind::FileHandle, EngineHT, true} });
    CHECK(base[0].kind == LockKind::FileHandle && base.size() == 2, L"Unknown 不降级精确 kind");
    base.insert(base.end(), { {120, L"svchost.exe", L"", LockKind::ServiceModule, EngineRM, true} });
    sortLocks(base);
    CHECK(base.front().pid == 7 && base.size() == 3, L"pid 升序");
    mergeInto(base, {}); // 幂等空输入
    CHECK(base.size() == 3, L"空增量幂等");
}

void testRisk() {
    CHECK(classifyKill(4900, L"notepad.exe", L"", 4900) == KillRisk::OwnProcess, L"自身");
    CHECK(classifyKill(4, L"system", L"", 1) == KillRisk::CriticalSystem, L"pid<=4");
    CHECK(classifyKill(980, L"svchost.EXE", L"", 1) == KillRisk::CriticalSystem, L"关键名单(大小写)");
    CHECK(classifyKill(1100, L"explorer.exe", L"", 1) == KillRisk::ShellProcess, L"外壳");
    CHECK(classifyKill(1234, L"wmplayer.exe", L"", 1) == KillRisk::Allowed, L"普通允许");
    CHECK(riskText(KillRisk::CriticalSystem).size() > 3, L"riskText 非空");
}

void testIdentity() {
    const FileIdentity a{ 1, 2, true }, b{ 1, 2, true }, c{ 1, 3, true }, d{ 0, 0, false };
    CHECK(identityEqual(a, b), L"同卷同ID相等");
    CHECK(!identityEqual(a, c), L"负样例:fileId 不同");
    CHECK(!identityEqual(a, d), L"负样例:对方 invalid");
    CHECK(!identityEqual(d, b), L"负样例:自身 invalid");
    CHECK(!identityEqual(d, d), L"负样例:双 invalid 不相等");
}

void testCli() {
    CliOptions o; std::wstring e;
    CHECK(parseCli({ L"WhoLock.exe", L"--engine=rm", L"--json", L"D:\\x.txt" }, o, e), L"基础解析");
    CHECK(o.engineMask == EngineRM && o.json && o.paths.size() == 1 && o.paths[0] == L"D:\\x.txt", L"字段落位");
    CHECK(parseCli({ L"x", L"--engine=both", L"a" }, o, e), L"both 与路径");
    CHECK(o.engineMask == (EngineRM | EngineHT), L"both 位集");
    CHECK(!parseCli({ L"x", L"--engine=zz", L"a" }, o, e) && e.find(L"--engine") != std::wstring::npos, L"负样例:坏 engine");
    CHECK(!parseCli({ L"x", L"--json" }, o, e), L"负样例:缺路径");
    CHECK(!parseCli({ L"x", L"-Q", L"a" }, o, e), L"负样例:未知选项");
    CHECK(parseCli({ L"x", L"a.txt", L"b.txt" }, o, e) && o.paths.size() == 2, L"v1.0 多路径合法");
    CHECK(parseCli({ L"x", L"-h" }, o, e) && o.usageRequested, L"usage 不是错误");
    CHECK(engineMaskName(EngineRM | EngineHT) == L"RM+HT", L"mask 名");
    CHECK(isCliMode({ L"x", L"--cli" }), L"--cli 判定");
}
} // namespace

int main() {
    testNormalize(); testMerge(); testRisk(); testIdentity(); testCli();
    return reportAndExit();
}
