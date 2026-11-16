// 集成测试（M1 范围）：可插拔换实现——单 RM 桩 / 双桩合并 / 一引擎失败降级路径
#include "../TestAssert.h"
#include "application/ScanCoordinator.h"
#include "testing/StubScanner.h"

using namespace who;
using who::testing::StubScanner;

namespace {
LockInfo mk(uint32_t pid, const wchar_t* n, LockKind k, uint8_t bits) {
    LockInfo li; li.pid = pid; li.name = n; li.kind = k; li.engines = bits; return li;
}
ScanQuery q() {
    ScanQuery s;
    s.targets.push_back(TargetRef{ L"C:\\Demo\\A.txt", L"c:\\demo\\a.txt", {} });
    return s;
}
} // namespace

int main() {
    {
        std::vector<std::unique_ptr<ILockScanner>> v;
        auto a = std::make_unique<StubScanner>(std::vector<LockInfo>{ mk(235, L"notepad.exe", LockKind::FileHandle, 0) }, EngineRM);
        v.push_back(std::move(a));
        auto rep = ScanCoordinator(std::move(v)).execute(q());
        CHECK(rep.anyOk && !rep.allFailed() && rep.merged.size() == 1, L"单 RM:结果可见");
        CHECK(rep.merged[0].engines == EngineRM, L"单 RM:engines 位");
    }
    {
        std::vector<std::unique_ptr<ILockScanner>> v;
        v.push_back(std::make_unique<StubScanner>(
            std::vector<LockInfo>{ mk(235, L"notepad.exe", LockKind::FileHandle, 0) }, EngineRM));
        v.push_back(std::make_unique<StubScanner>(
            std::vector<LockInfo>{ mk(400, L"svchost.exe", LockKind::ServiceModule, 0),
                                   mk(235, L"notepad.exe", LockKind::FileHandle, 0) }, EngineHT));
        auto rep = ScanCoordinator(std::move(v)).execute(q());
        CHECK(rep.merged.size() == 2, L"双引擎:跨引擎去重合并");
        CHECK(rep.merged.front().pid == 235 && rep.merged.front().engines == (EngineRM | EngineHT), L"双引擎:位合并");
        CHECK(rep.notes.size() == 2 && rep.notes[1].count == 2, L"notes 计数");
    }
    {
        std::vector<std::unique_ptr<ILockScanner>> v;
        v.push_back(std::make_unique<StubScanner>(
            std::vector<LockInfo>{ mk(10, L"a.exe", LockKind::FileHandle, 0) }, EngineRM));
        auto failing = std::make_unique<StubScanner>(std::vector<LockInfo>{}, EngineHT);
        failing->setFail(L"权限不足模拟");
        v.push_back(std::move(failing));
        auto rep = ScanCoordinator(std::move(v)).execute(q());
        CHECK(rep.anyOk && rep.merged.size() == 1, L"负样例:一引擎失败仍出结果");
        CHECK(rep.notes[1].ok == false && !rep.notes[1].error.empty(), L"负样例:note 错误透传");
    }
    {
        auto rep = ScanCoordinator({}).execute(q());
        CHECK(rep.anyOk && rep.merged.empty(), L"负样例:空引擎集合非全失败");
    }
    return reportAndExit();
}
