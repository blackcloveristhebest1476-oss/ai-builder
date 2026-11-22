#pragma once

#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

namespace fwa {

/// Locale-independent fixed formatting ("1.250"), trimming nothing.
inline std::string fixed(double v, int decimals) {
    if (std::isnan(v)) return "NaN";
    if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    for (char* p = buf; *p; ++p) {
        if (*p == ',') *p = '.';
    }
    return buf;
}

/// Like fixed() but strips trailing zeros ("240", "0.5", "1.25").
inline std::string num(double v, int maxDecimals = 3) {
    std::string s = fixed(v, maxDecimals);
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    return s;
}

/// Significant-digit formatting similar to NaNDL's display (e.g. 12.346, 1.2346e-05).
inline std::string sig(double v, int digits = 5) {
    if (std::isnan(v)) return "NaN";
    if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*g", digits, v);
    for (char* p = buf; *p; ++p) {
        if (*p == ',') *p = '.';
    }
    return buf;
}

inline std::string toLower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

inline std::string trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
    return std::string(s.substr(a, b - a));
}

} // namespace fwa
