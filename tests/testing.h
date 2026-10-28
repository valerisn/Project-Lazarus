#pragma once
// homemade test harness. gtest for a few hundred CHECKs wasn't worth the dependency

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace lzt {

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}
inline int& failures() {
    static int f = 0;
    return f;
}

struct Registrar {
    Registrar(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); }
};

inline int run_all(const char* filter) {
    int ran = 0, failed_cases = 0;
    for (auto& c : registry()) {
        if (filter && std::string(c.name).find(filter) == std::string::npos)
            continue;
        int before = failures();
        c.fn();
        ++ran;
        bool ok = failures() == before;
        if (!ok) ++failed_cases;
        std::printf("[%s] %s\n", ok ? " OK " : "FAIL", c.name);
    }
    std::printf("\n%d cases, %d failed, %d failed assertions\n", ran, failed_cases, failures());
    return failed_cases ? 1 : 0;
}

}  // namespace lzt

#define LZT_CAT2(a, b) a##b
#define LZT_CAT(a, b) LZT_CAT2(a, b)
#define TEST_CASE(name)                                                                  \
    static void LZT_CAT(lzt_fn_, __LINE__)();                                            \
    static lzt::Registrar LZT_CAT(lzt_reg_, __LINE__)(name, LZT_CAT(lzt_fn_, __LINE__)); \
    static void LZT_CAT(lzt_fn_, __LINE__)()

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("  %s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++lzt::failures();                                                   \
        }                                                                        \
    } while (0)

#define CHECK_EQ(a, b)                                                                     \
    do {                                                                                   \
        if (!((a) == (b))) {                                                               \
            std::printf("  %s:%d: CHECK_EQ failed: %s == %s\n", __FILE__, __LINE__, #a, #b); \
            ++lzt::failures();                                                             \
        }                                                                                  \
    } while (0)
