#pragma once

// Tiny self-contained test harness (no third-party dependencies).

#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace tf {

struct Case {
    char const* name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failures() {
    static int f = 0;
    return f;
}

inline int& checks() {
    static int c = 0;
    return c;
}

struct Registrar {
    Registrar(char const* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

inline void report(char const* file, int line, std::string const& msg) {
    failures()++;
    std::printf("    FAIL %s:%d: %s\n", file, line, msg.c_str());
}

template <class A, class B>
std::string show(A const& a, B const& b) {
    std::ostringstream ss;
    ss.precision(17);
    ss << "got [" << a << "] expected [" << b << "]";
    return ss.str();
}

inline bool closeRel(double a, double b, double rel, double abs = 0.0) {
    if (std::isinf(a) || std::isinf(b)) return a == b;
    if (std::isnan(a) || std::isnan(b)) return false;
    double diff = std::fabs(a - b);
    return diff <= abs || diff <= rel * std::max(std::fabs(a), std::fabs(b));
}

} // namespace tf

#define TF_CAT2(a, b) a##b
#define TF_CAT(a, b) TF_CAT2(a, b)
#define TEST(name) \
    static void TF_CAT(test_fn_, __LINE__)(); \
    static tf::Registrar TF_CAT(test_reg_, __LINE__)(name, TF_CAT(test_fn_, __LINE__)); \
    static void TF_CAT(test_fn_, __LINE__)()

#define CHECK(cond) do { tf::checks()++; if (!(cond)) tf::report(__FILE__, __LINE__, "CHECK(" #cond ")"); } while (0)
#define CHECK_EQ(a, b) do { tf::checks()++; auto _a = (a); auto _b = (b); if (!(_a == _b)) tf::report(__FILE__, __LINE__, std::string(#a " == " #b ": ") + tf::show(_a, _b)); } while (0)
#define CHECK_NEAR(a, b, rel) do { tf::checks()++; double _a = (a); double _b = (b); if (!tf::closeRel(_a, _b, rel)) tf::report(__FILE__, __LINE__, std::string(#a " ~= " #b ": ") + tf::show(_a, _b)); } while (0)
#define CHECK_ABS(a, b, tol) do { tf::checks()++; double _a = (a); double _b = (b); if (!(std::fabs(_a - _b) <= (tol))) tf::report(__FILE__, __LINE__, std::string(#a " ~= " #b ": ") + tf::show(_a, _b)); } while (0)
#define REQUIRE(cond) do { tf::checks()++; if (!(cond)) { tf::report(__FILE__, __LINE__, "REQUIRE(" #cond ")"); return; } } while (0)
#define CHECK_CONTAINS(str, part) do { tf::checks()++; std::string _s = (str); if (_s.find(part) == std::string::npos) tf::report(__FILE__, __LINE__, "expected \"" + _s + "\" to contain \"" + std::string(part) + "\""); } while (0)
