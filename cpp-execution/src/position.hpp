

#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <algorithm>
#include <cctype>
#include <sstream>
#include "common/logger.hpp"

// Simple in-memory authoritative position cache for debugging / local use.
// Key: symbol.toString(), Value: signed quantity (positive = long, negative = short)
static std::unordered_map<std::string, double> g_localPositions;
static std::mutex g_localPositionsMutex;

// Normalize symbol key for local cache (trim spaces, uppercase) to avoid mismatches
static std::string normalizeLocalPosKey(const std::string &s) {
    std::string k = s;
    // trim leading/trailing spaces
    while (!k.empty() && std::isspace(static_cast<unsigned char>(k.front()))) k.erase(k.begin());
    while (!k.empty() && std::isspace(static_cast<unsigned char>(k.back()))) k.pop_back();
    // uppercase
    std::transform(k.begin(), k.end(), k.begin(), [](unsigned char c){ return std::toupper(c); });
    return k;
}

static void setLocalPosition(const std::string &symbol, double signedQty) {
    try {
        std::lock_guard<std::mutex> lk(g_localPositionsMutex);
        g_localPositions[normalizeLocalPosKey(symbol)] = signedQty;
    } catch (...) {}
}

static void addLocalPosition(const std::string &symbol, double deltaQty) {
    try {
        std::lock_guard<std::mutex> lk(g_localPositionsMutex);
        auto k = normalizeLocalPosKey(symbol);
        double prev = 0.0;
        auto it = g_localPositions.find(k);
        if (it != g_localPositions.end()) prev = it->second;
        g_localPositions[k] = prev + deltaQty;
    } catch (...) {}
}

static std::optional<double> getLocalPosition(const std::string &symbol) {
    try {
        std::lock_guard<std::mutex> lk(g_localPositionsMutex);
        auto k = normalizeLocalPosKey(symbol);
        auto it = g_localPositions.find(k);
        if (it == g_localPositions.end()) return std::nullopt;
        return it->second;
    } catch (...) {
        return std::nullopt;
    }
}

static void dumpLocalPositionsDebug() {
    try {
        std::lock_guard<std::mutex> lk(g_localPositionsMutex);
        std::ostringstream ss;
        ss << "[LOCAL POS DUMP] ";
        for (const auto &p : g_localPositions) {
            ss << p.first << "=" << p.second << " ";
        }
        LOG_INFO("{}", ss.str());
    } catch (...) {}
}