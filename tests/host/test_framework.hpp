/*
 * Minimal host test framework for the modulation helpers.
 *
 * Dependency-free on purpose: this must build with plain g++ on a build host
 * with no ESP-IDF toolchain, no hardware, and no network.
 */
#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace testing {

struct TestCase {
    std::string name;
    void (*fn)();
};

inline std::vector<TestCase> &Registry() {
    static std::vector<TestCase> registry;
    return registry;
}

inline int &FailureCount() {
    static int failures = 0;
    return failures;
}

inline std::string &CurrentTest() {
    static std::string current;
    return current;
}

struct Registrar {
    Registrar(const char *name, void (*fn)()) {
        Registry().push_back({name, fn});
    }
};

inline void Fail(const char *file, int line, const std::string &msg) {
    FailureCount()++;
    printf("    FAIL  %s:%d\n          %s\n", file, line, msg.c_str());
}

inline int RunAll() {
    int failed = 0;
    printf("Running %zu tests\n\n", Registry().size());
    for (auto &t : Registry()) {
        CurrentTest() = t.name;
        int before = FailureCount();
        printf("  %s\n", t.name.c_str());
        t.fn();
        if (FailureCount() > before) failed++;
        printf("\n");
    }
    if (failed == 0) {
        printf("All tests passed.\n");
        return 0;
    }
    printf("%d test(s) FAILED.\n", failed);
    return 1;
}

} // namespace testing

#define TEST(name)                                                        \
    static void name();                                                   \
    static testing::Registrar reg_##name(#name, name);                    \
    static void name()

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) testing::Fail(__FILE__, __LINE__, "CHECK(" #cond ")"); \
    } while (0)

#define CHECK_MSG(cond, msg)                                              \
    do {                                                                  \
        if (!(cond))                                                      \
            testing::Fail(__FILE__, __LINE__,                             \
                          std::string("CHECK(" #cond ") -- ") + (msg));   \
    } while (0)

#define CHECK_EQ(a, b)                                                    \
    do {                                                                  \
        auto va_ = (a);                                                   \
        auto vb_ = (b);                                                   \
        if (!(va_ == vb_)) {                                              \
            testing::Fail(__FILE__, __LINE__,                             \
                          std::string(#a " == " #b " (got ") +           \
                              std::to_string(va_) + " vs " +              \
                              std::to_string(vb_) + ")");                 \
        }                                                                 \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                             \
    do {                                                                  \
        double va_ = (double)(a);                                         \
        double vb_ = (double)(b);                                         \
        if (std::fabs(va_ - vb_) > (tol)) {                               \
            testing::Fail(__FILE__, __LINE__,                             \
                          std::string(#a " ~= " #b " (got ") +           \
                              std::to_string(va_) + " vs " +              \
                              std::to_string(vb_) + ", tol " +           \
                              std::to_string((double)(tol)) + ")");       \
        }                                                                 \
    } while (0)

#define CHECK_STREQ(a, b)                                                 \
    do {                                                                  \
        std::string va_ = (a);                                            \
        std::string vb_ = (b);                                            \
        if (va_ != vb_) {                                                 \
            testing::Fail(__FILE__, __LINE__,                             \
                          std::string(#a " == " #b " (got \"") + va_ +   \
                              "\" vs \"" + vb_ + "\")");                  \
        }                                                                 \
    } while (0)