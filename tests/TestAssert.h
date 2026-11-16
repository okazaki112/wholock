#pragma once
#include <cstdio>
#include <optional>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) { ++g_pass; } else { ++g_fail; std::wprintf(L"FAIL: %s (@%s:%d)\n", (msg), __FILEW__, __LINE__); } } while (0)
#define CHECK_EQ(a, b, msg) do { if ((a) == (b)) { ++g_pass; } else { ++g_fail; std::wprintf(L"FAIL: %s\n  实际=[%s] 期望=[%s] (@%s:%d)\n", (msg), (a).c_str(), (b).c_str(), __FILEW__, __LINE__); } } while (0)
#define CHECK_OPT(actual, expected, msg) do { \
    if ((actual).has_value() && (expected).has_value() && *actual == *expected) { ++g_pass; } \
    else { ++g_fail; std::wprintf(L"FAIL: %s 实际=[%s] 期望=[%s]\n", (msg), \
        (actual) ? actual->c_str() : L"<null>", (expected) ? expected->c_str() : L"<null>"); } } while (0)

static int reportAndExit() {
    std::wprintf(L"== 单测汇总: 通过 %d, 失败 %d ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
