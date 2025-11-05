#pragma once

#include <string>
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace execution {

struct SymbolPrecision {
    double stepSize;      // Minimum quantity increment (LOT_SIZE)
    int precision;        // Decimal places for quantity
    double minQty;        // Minimum order quantity
    double maxQty;        // Maximum order quantity
    int pricePrecision;   // Decimal places for price (PRICE_FILTER)
};

class SymbolPrecisionManager {
public:
    // Normalize symbol (remove '/', uppercase)
    static std::string normalizeSymbol(std::string symbol) {
        symbol.erase(std::remove(symbol.begin(), symbol.end(), '/'), symbol.end());
        for (auto& c : symbol) c = std::toupper(c);
        return symbol;
    }

    static const SymbolPrecision& getPrecision(const std::string& symbol) {
        auto key = normalizeSymbol(symbol);
        auto it = BINANCE_PRECISION.find(key);
        if (it != BINANCE_PRECISION.end()) {
            return it->second;
        }
        return DEFAULT_PRECISION;
    }

    static bool hasSymbol(const std::string& symbol) {
        auto key = normalizeSymbol(symbol);
        return BINANCE_PRECISION.find(key) != BINANCE_PRECISION.end();
    }

    // --- Round quantity to nearest valid increment ---
    static double roundQuantity(const std::string& symbol, double quantity) {
        const auto& prec = getPrecision(symbol);
        if (prec.stepSize <= 0.0)
            return quantity;

        // Use more robust rounding to avoid floating-point artifacts
        double steps = std::floor(quantity / prec.stepSize + 0.5);
        double q = steps * prec.stepSize;
        q = std::clamp(q, prec.minQty, prec.maxQty);

        // Apply precision rounding with better floating-point cleanup
        double multiplier = std::pow(10.0, prec.precision);
        double rounded = std::floor(q * multiplier + 0.5) / multiplier;
        
        // Final cleanup to eliminate tiny floating-point errors
        if (prec.precision <= 8) {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%.*f", prec.precision, rounded);
            rounded = std::strtod(buffer, nullptr);
        }
        
        return rounded;
    }

    // --- Validate quantity meets exchange limits and increment size ---
    static bool isValidQuantity(const std::string& symbol, double quantity) {
        const auto& prec = getPrecision(symbol);

        if (quantity < prec.minQty - 1e-12 || quantity > prec.maxQty + 1e-12)
            return false;

        if (prec.stepSize > 0.0) {
            double remainder = std::fmod(quantity, prec.stepSize);
            if (std::fabs(remainder) > 1e-8 && std::fabs(remainder - prec.stepSize) > 1e-8)
                return false;
        }

        return true;
    }

private:
    static inline const std::unordered_map<std::string, SymbolPrecision> BINANCE_PRECISION = {
        // Major Blue Chips
        {"BTCUSDT",   {0.00001, 5, 0.00001, 9000.0, 2}},
        {"ETHUSDT",   {0.0001,  4, 0.0001,  90000.0, 2}},
        {"BNBUSDT",   {0.001,   3, 0.001,   10000.0, 2}},
        {"XRPUSDT",   {0.1,     1, 0.1,     90000000.0, 4}},
        {"SOLUSDT",   {0.01,    2, 0.01,    92000.0, 2}},
        {"ADAUSDT",   {1.0,     0, 1.0,     90000000.0, 5}},
        {"AVAXUSDT",  {0.01,    2, 0.01,    90000.0, 2}},
        {"DOGEUSDT",  {1.0,     0, 1.0,     90000000.0, 5}},
        {"DOTUSDT",   {0.01,    2, 0.01,    160000.0, 3}},

        // DeFi & Layer 2
        {"LINKUSDT",  {0.01,    2, 0.01,    75000.0, 3}},
        {"UNIUSDT",   {0.01,    2, 0.01,    110000.0, 3}},
        {"AAVEUSDT",  {0.001,   3, 0.001,   6200.0, 2}},
        {"ARBUSDT",   {0.1,     1, 0.1,     90000000.0, 4}},
        {"OPUSDT",    {0.1,     1, 0.1,     90000000.0, 4}},

        // AI & Technology
        {"FETUSDT",   {0.1,     1, 0.1,     90000000.0, 4}},
        {"INJUSDT",   {0.01,    2, 0.01,    90000.0, 3}},

        // Real World Assets
        {"ONDOUSDT",  {0.01,    2, 0.01,    90000.0, 3}},
        {"HBARUSDT",  {1.0,     0, 1.0,     90000000.0, 5}},
        {"PAXGUSDT",  {0.00001, 5, 0.00001, 100.0, 1}},

        // Gaming & Metaverse
        {"SANDUSDT",  {0.1,     1, 0.1,     90000000.0, 4}},
        {"GALAUSDT",  {1.0,     0, 1.0,     90000000.0, 5}},
        {"IMXUSDT",   {0.1,     1, 0.1,     90000000.0, 4}},

        // Emerging Altcoins
        {"SUIUSDT",   {0.1,     1, 0.1,     90000000.0, 4}},
        {"APTUSDT",   {0.01,    2, 0.01,    90000.0, 3}},
        {"TIAUSDT",   {0.01,    2, 0.01,    90000.0, 3}},

        // Meme Coins
        {"SHIBUSDT",  {1.0,     0, 1.0,     90000000.0, 8}},
        {"FLOKIUSDT", {1.0,     0, 1.0,     90000000.0, 7}},

        // Infrastructure
        {"ATOMUSDT",  {0.01,    2, 0.01,    90000.0, 3}},
        {"ALGOUSDT",  {1.0,     0, 1.0,     90000000.0, 4}},

        // Privacy
        {"ZECUSDT",   {0.001,   3, 0.001,   10000.0, 2}},
    };

    static inline const SymbolPrecision DEFAULT_PRECISION = {0.001, 3, 0.001, 90000.0, 2};
};

} // namespace execution
