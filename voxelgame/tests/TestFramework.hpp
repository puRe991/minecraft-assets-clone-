// vg/tests/TestFramework.hpp
//
// A tiny, dependency-free unit-test harness. No GoogleTest, no Catch — just
// enough to register cases, make assertions, and print a summary with a
// process exit code suitable for CI. Kept deliberately small so the test
// build has zero external requirements, matching the engine's philosophy.
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace vg::test {

struct Case { std::string name; std::function<void()> fn; };

inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }
inline int& failures() { static int f = 0; return f; }
inline int& checks() { static int c = 0; return c; }

struct Registrar {
    Registrar(const std::string& name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

inline void reportFail(const char* file, int line, const std::string& msg) {
    ++failures();
    std::printf("    FAIL %s:%d  %s\n", file, line, msg.c_str());
}

inline int runAll() {
    int failedCases = 0;
    for (auto& c : registry()) {
        int before = failures();
        c.fn();
        bool ok = failures() == before;
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", c.name.c_str());
        if (!ok) ++failedCases;
    }
    std::printf("\n%d checks, %d failing case(s)\n",
                checks(), failedCases);
    std::printf("%s\n", failedCases ? "*** TESTS FAILED ***" : "ALL TESTS PASSED");
    return failedCases ? 1 : 0;
}

}  // namespace vg::test

// --- assertion macros ---------------------------------------------------
#define VG_TEST(name)                                                        \
    static void name();                                                      \
    static ::vg::test::Registrar reg_##name(#name, name);                    \
    static void name()

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++::vg::test::checks();                                              \
        if (!(cond))                                                         \
            ::vg::test::reportFail(__FILE__, __LINE__, "CHECK(" #cond ")");  \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                \
    do {                                                                     \
        ++::vg::test::checks();                                              \
        if (std::fabs((a) - (b)) > (eps))                                    \
            ::vg::test::reportFail(__FILE__, __LINE__,                       \
                std::string("CHECK_NEAR(" #a ", " #b ") diff=") +           \
                std::to_string(std::fabs((a) - (b))));                       \
    } while (0)
