// Lightweight header-only parsing helpers to safely convert strings to numeric types.
#pragma once

#include <string>
#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>

namespace util {

static inline std::string trim(const std::string &s) {
    size_t a = 0; while (a < s.size() && std::isspace((unsigned char)s[a])) ++a;
    size_t b = s.size(); while (b > a && std::isspace((unsigned char)s[b-1])) --b;
    return s.substr(a, b-a);
}

static inline std::string remove_commas(const std::string &s) {
    std::string out; out.reserve(s.size());
    for (char c : s) if (c != ',') out.push_back(c);
    return out;
}

static inline bool tryParseLongLong(const std::string &s0, long long &out) {
    try {
        std::string s = trim(remove_commas(s0));
        if (s.empty()) return false;
        // If contains decimal point parse as double and cast
        if (s.find('.') != std::string::npos) {
            double d = std::stod(s);
            out = static_cast<long long>(d);
            return true;
        }
        size_t idx = 0;
        long long v = std::stoll(s, &idx);
        (void)idx;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

static inline long long parseLongLongOr(const std::string &s, long long def = 0) {
    long long v = 0;
    if (tryParseLongLong(s, v)) return v; else return def;
}

static inline bool tryParseInt(const std::string &s0, int &out) {
    long long tmp = 0;
    if (!tryParseLongLong(s0, tmp)) return false;
    out = static_cast<int>(tmp);
    return true;
}

static inline int parseIntOr(const std::string &s, int def = 0) {
    int v = 0;
    if (tryParseInt(s, v)) return v; else return def;
}

static inline bool tryParseULL(const std::string &s0, unsigned long long &out) {
    try {
        std::string s = trim(remove_commas(s0));
        if (s.empty()) return false;
        if (s.find('.') != std::string::npos) {
            double d = std::stod(s);
            out = static_cast<unsigned long long>(d);
            return true;
        }
        size_t idx = 0;
        unsigned long long v = std::stoull(s, &idx);
        (void)idx;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

static inline unsigned long long parseULLOr(const std::string &s, unsigned long long def = 0ULL) {
    unsigned long long v = 0;
    if (tryParseULL(s, v)) return v; else return def;
}

static inline double parseDoubleOr(const std::string &s0, double def = 0.0) {
    try {
        std::string s = trim(remove_commas(s0));
        if (s.empty()) return def;
        return std::stod(s);
    } catch (...) { return def; }
}

} // namespace util
