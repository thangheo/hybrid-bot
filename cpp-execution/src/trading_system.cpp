#include "trading_system.hpp"
#include "strategy/core_analysis/regime_subscriber.hpp"
#include "strategy/exit/adaptive_exit_strategy.hpp"
#include "common/quantity_precision.hpp" // symbol precision helper for exchange-compliant quantities
#include "strategy/pattern/imbalance_detection_engine.hpp"
#include "risk/interfaces/execution_interface.hpp"
#include "risk/risk_types.hpp"
#include "strategy/types/market_regime.hpp"
#include "common/thread_pool.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include "common/logger.hpp"
#include "execution/models/order.hpp"
// position.hpp (local in-memory cache) removed: prefer canonical RiskManager/PositionTracker

#include <memory>
#include <unordered_map>
#include <filesystem>

// Use fully-qualified ::execution::SymbolPrecisionManager to avoid ambiguity
using namespace ::trading::core;
using namespace ::trading::execution;
using namespace ::trading::risk;
using namespace ::trading::common;
// using namespace ::trading::intelligence;
using namespace ::trading;

// Forward-declare AdaptiveExitStrategy and provide a file-scoped pointer to allow legacy code
namespace trading {
namespace strategy {
class AdaptiveExitStrategy;
} // namespace strategy
} // namespace trading

// File-scoped shared_ptr used by Trading_system member functions when the class member
// `exitStrategy_` is not present in the header (provides backward-compatible fallback).
// static std::shared_ptr<trading::strategy::AdaptiveExitStrategy> exitStrategy_;
//use adaptive exit strategy instead
static std::shared_ptr<trading::strategy::AdaptiveExitStrategy> exitStrategy_;

// Global cache for exchange-specific tick sizes loaded from config
static std::unordered_map<std::string, double> g_exchangeTickSize;
static std::unordered_map<std::string, double> g_symbolTickSize;

// Simple PnL statistics for exits (cumulative PnL in basis points and count)
static std::mutex g_exitPnlMutex;
static double g_cumulativeExitPnlBps = 0.0;
static uint64_t g_totalExitCount = 0;
// Separate counters for simulated (non-authoritative) signal PnL so we don't
// mix them with realized/authoritative exit statistics.
static double g_cumulativeSimulatedExitPnlBps = 0.0;
static uint64_t g_totalSimulatedExitCount = 0;

// Load persisted exit PnL stats from disk (JSON). Best-effort, no exceptions thrown
static void loadExitPnlStats(const std::string& path = "logs/exit_pnl_stats.json") {
    try {
        if (!std::filesystem::exists(path)) return;
        std::ifstream ifs(path);
        if (!ifs.is_open()) return;
        nlohmann::json j; ifs >> j;
        double cum = 0.0; uint64_t cnt = 0;
        double sim_cum = 0.0; uint64_t sim_cnt = 0;
        if (j.contains("cumulative_bps")) cum = j["cumulative_bps"].get<double>();
        if (j.contains("total_count")) cnt = j["total_count"].get<uint64_t>();
        if (j.contains("simulated_cumulative_bps")) sim_cum = j["simulated_cumulative_bps"].get<double>();
        if (j.contains("simulated_total_count")) sim_cnt = j["simulated_total_count"].get<uint64_t>();
        {
            std::lock_guard<std::mutex> lk(g_exitPnlMutex);
            g_cumulativeExitPnlBps = cum;
            g_totalExitCount = cnt;
            g_cumulativeSimulatedExitPnlBps = sim_cum;
            g_totalSimulatedExitCount = sim_cnt;
        }
        LOG_INFO("Loaded exit PnL stats from {}: total_count={}, cumulative_bps={}", path, cnt, cum);
    } catch (...) {
        LOG_WARN("Failed to load exit PnL stats from {}", path);
    }
}

// Save exit PnL stats to disk (JSON). Best-effort, no throwing.
static void saveExitPnlStats(const std::string& path = "logs/exit_pnl_stats.json") {
    try {
        // Ensure directory exists
        std::filesystem::path p(path);
        if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
        nlohmann::json j;
        {
            std::lock_guard<std::mutex> lk(g_exitPnlMutex);
            j["cumulative_bps"] = g_cumulativeExitPnlBps;
            j["total_count"] = g_totalExitCount;
            j["simulated_cumulative_bps"] = g_cumulativeSimulatedExitPnlBps;
            j["simulated_total_count"] = g_totalSimulatedExitCount;
        }
        std::ofstream ofs(path, std::ofstream::trunc);
        if (!ofs.is_open()) {
            LOG_WARN("Could not open {} for writing exit PnL stats", path);
            return;
        }
        ofs << j.dump(4);
        LOG_INFO("Saved exit PnL stats to {}: total_count={}, cumulative_bps={}", path, j["total_count"].get<uint64_t>(), j["cumulative_bps"].get<double>());
    } catch (...) {
        LOG_WARN("Failed to save exit PnL stats to {}", path);
    }
}


static void loadExchangeTickSizes(const std::string& path = "config/exchange_specific_params.json") {
    try {
        if (!std::filesystem::exists(path)) return;
        std::ifstream ifs(path);
        if (!ifs.is_open()) return;
        nlohmann::json j; ifs >> j;
        for (auto it = j.begin(); it != j.end(); ++it) {
            const std::string exch = it.key();
            try {
                if (it.value().contains("numeric") && it.value()["numeric"].contains("tick_size")) {
                    double t = it.value()["numeric"]["tick_size"].get<double>();
                    g_exchangeTickSize[exch] = t;
                }
            } catch (...) { /* ignore malformed entries */ }
        }
    } catch (...) {
        // best-effort
    }
}

static double getExchangeTickSize(const core::ExchangeId& exId) {
    auto it = g_exchangeTickSize.find(exId.toString());
    if (it != g_exchangeTickSize.end() && it->second > 0.0) return it->second;
    // fallback to common default
    return 0.01;
}

static void loadSymbolTickSizes(const std::string& path = "config/symbol_tick_sizes.json") {
    try {
        if (!std::filesystem::exists(path)) return;
        std::ifstream ifs(path);
        if (!ifs.is_open()) return;
        nlohmann::json j; ifs >> j;
        for (auto it = j.begin(); it != j.end(); ++it) {
            try {
                double t = it.value().get<double>();
                // Normalize key: remove '/' if present and uppercase
                std::string k = it.key();
                k.erase(std::remove(k.begin(), k.end(), '/'), k.end());
                g_symbolTickSize[k] = t;
            } catch (...) {}
        }
    } catch (...) {}
}

static double getSymbolTickSize(const core::Symbol& sym) {
    std::string k = sym.toString();
    k.erase(std::remove(k.begin(), k.end(), '/'), k.end());
    auto it = g_symbolTickSize.find(k);
    if (it != g_symbolTickSize.end() && it->second > 0.0) return it->second;
    // fallback to exchange-level config
    return 0.0; // let caller combine with exchange fallback
}

Trading_system::Trading_system() : running_(false), systemHealthy_(true) {
    setupConfigurations();
    setupStatistics();
    // Initialize adaptive intelligence
    // adaptiveIntel_ = std::make_unique<trading::intelligence::AdaptiveMarketIntelligence>();
    
    ZmqServerConfig serverConfig;
    //create shared context for server and client
    // use a single shared_context variable for server/client ZMQ usage
    zmq_context_ = std::make_shared<zmq::context_t>(1);
    // zmq_context_ = shared_context;

    serverConfig.pub_endpoint = SERVER_PUBLISHER_ENDPOINT; // internal endpoint for orderbook
    serverConfig.rep_endpoint = SERVER_REQUEST_ENDPOINT; // request endpoint for risk
    serverConfig.io_threads = 1;
    //use default config for now
    zmq_server_ = std::make_shared<GenericZmqServer>(serverConfig, zmq_context_);
    zmq_server_->registerRequestHandler("ping", [](const std::string& request, 
                                            const std::unordered_map<std::string, std::string>& metadata) -> std::string {
        nlohmann::json response;
        response["status"] = "pong";
        response["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return response.dump();
    });
    
    ZmqClientConfig clientConfig;
    //no long use socket but use inproc
    clientConfig.pub_endpoints = {SERVER_PUBLISHER_ENDPOINT};
    clientConfig.req_endpoint = SERVER_REQUEST_ENDPOINT; // request endpoint for risk
    // clientConfig.auto_reconnect = true;
    // clientConfig.request_timeout = std::chrono::milliseconds(30000); // 30 seconds
    zmq_client_ = std::make_unique<GenericZmqClient>(clientConfig, zmq_context_);
    LOG_INFO("Trading_system constructed - ZMQ server and client created (server={}, client={})",
             static_cast<const void*>(zmq_server_.get()), static_cast<const void*>(zmq_client_.get()));
    
    // Initialize MarketDataInterface
    // Subscriber should connect to the server's PUB endpoint; publisher endpoint
    // is the server's REQ/REP endpoint for requests. Ensure these are not swapped.
    riskConfig_.zmqSubscriberEndpoint = SERVER_PUBLISHER_ENDPOINT;
    riskConfig_.zmqPublisherEndpoint = SERVER_REQUEST_ENDPOINT;
    // marketDataInterface_ = std::make_shared<trading::risk::MarketDataInterface>(riskConfig_);

    positionSizer_ = std::make_unique<trading::risk::PositionSizer>(riskConfig_.getPositionSizingConfig());
    
}

void Trading_system::updateSymbolMarketData(
    const Symbol& sym, const ExchangeId& exch,
    std::atomic<int>& debugCounter,
    double& out_vol_1m, double& out_spread_ema,
    double& out_orderbook_imbalance, double& out_trade_intensity,
    double& out_price_return, double& out_mid_price)
{
    std::string symbolStr;
    try {
        symbolStr = sym.toString();

        // Reset outputs
        out_vol_1m = out_spread_ema = out_orderbook_imbalance =
        out_trade_intensity = out_price_return = out_mid_price = 0.0;

        // 1. Ưu tiên dùng cached orderbook metrics
        trading::strategy::OrderBookMetrics obm;
        if (getLatestOrderbookMetrics(sym, exch, obm)) {
            out_vol_1m = obm.realize_volatility_1m;
            out_spread_ema = obm.emaSpread;
            out_orderbook_imbalance = obm.volumeImbalance;
            out_trade_intensity = obm.totalVolume;
            out_mid_price = obm.midPrice;
        }
        else if (marketDataInterface_) {
            try {
                // Lấy spread
                auto sp_opt = marketDataInterface_->getCurrentSpread(sym, exch.toString());
                if (sp_opt.has_value()) out_spread_ema = sp_opt.value();

                // Lấy volatility và mid price
                out_vol_1m = marketDataInterface_->getVolatility(sym, "1m", exch.toString());
                out_mid_price = marketDataInterface_->getCurrentPrice(sym, exch.toString());
            }
            catch (const std::exception& e) {
                LOG_WARN("marketDataInterface error for {}@{}: {}", symbolStr, exch.toString(), e.what());
            }
            catch (...) {
                LOG_WARN("Unknown marketDataInterface error for {}@{}", symbolStr, exch.toString());
            }
        }

        // 2. Tính price return – dùng shared lock
        const std::string key = exch.toString() + ":" + symbolStr;
        double last_price = 0.0;

        {
            std::shared_lock<std::shared_mutex> rlk(lastPriceMutex_);
            auto it = lastMidPrice_.find(key);
            if (it != lastMidPrice_.end()) {
                last_price = it->second;
            }
        }

        if (out_mid_price > 0.0 && last_price > 0.0) {
            out_price_return = (out_mid_price - last_price) / last_price;
            if (!std::isfinite(out_price_return)) {
                out_price_return = 0.0;
            }
        }

        // 3. Cập nhật lastMidPrice_ – chỉ update, không insert (giả định key đã tồn tại)
        if (out_mid_price > 0.0) {
            std::unique_lock<std::shared_mutex> wlk(lastPriceMutex_);
            lastMidPrice_[key] = out_mid_price;  // SAFE: key đã được pre-allocated
        }

        // 4. Debug log
        int cnt = debugCounter.fetch_add(1, std::memory_order_relaxed);
        if (cnt % 50 == 0) {
            LOG_TRACE("Microstructure {}@{}: vol_1m={:.6f}, spread_ema={:.6f}, imbalance={:.3f}, "
                     "intensity={:.0f}, price_return={:.6f}, mid={:.6f}",
                     symbolStr, exch.toString(),
                     out_vol_1m, out_spread_ema, out_orderbook_imbalance,
                     out_trade_intensity, out_price_return, out_mid_price);
        }

    }
    catch (const std::exception& e) {
        LOG_WARN("updateSymbolMarketData exception for {}@{}: {}",
                 symbolStr.empty() ? "<unknown>" : symbolStr,
                 exch.toString(), e.what());
    }
    catch (...) {
        LOG_WARN("Unknown exception in updateSymbolMarketData for {}@{}",
                 symbolStr.empty() ? "<unknown>" : symbolStr,
                 exch.toString());
    }
}
void Trading_system::marketDataUpdaterLoop()
{
    LOG_INFO("Market data updater loop started");

    // Use configured interval (ms) for market data updater to support low-latency operation.
    const int mdIntervalMs = std::max(1, strategyConfig_.marketDataUpdaterIntervalMs);
    const std::chrono::milliseconds targetPeriod{mdIntervalMs};
    const unsigned int maxWorkers = std::max(1u, std::min(std::thread::hardware_concurrency(), 8u));

    // Use a reusable thread pool to avoid spawning short-lived threads each
    // market-data cycle (std::async with launch::async creates threads which
    // under debuggers like gdb produce heavy churn). A static pool here keeps
    // a fixed set of worker threads for the lifetime of the process.
    static ThreadPool pool(std::max(1u, std::min(std::thread::hardware_concurrency(), 8u)));

    std::atomic<int> debugCounter{0};

    while (marketDataUpdaterRunning_.load(std::memory_order_acquire)) {
        auto cycleStart = std::chrono::steady_clock::now();

        // 1. Kiểm tra exitStrategy
        if (!exitStrategy_) {
            // Use a short retry interval while exit strategy is not ready
            std::this_thread::sleep_for(std::chrono::milliseconds(std::min(500, mdIntervalMs)));
            continue;
        }

        // 2. Snapshot trading pairs
        std::vector<std::pair<Symbol, ExchangeId>> pairs;
        pairs.reserve(strategyConfig_.tradingPairs.size());
        for (const auto& p : strategyConfig_.tradingPairs) {
            pairs.emplace_back(p.first, p.second);
        }

        const size_t total = pairs.size();
        if (total == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(mdIntervalMs));
            continue;
        }

        // 3. Chia task
        const size_t chunkSize = (total + maxWorkers - 1) / maxWorkers;
        std::vector<std::future<void>> futures;
        futures.reserve(maxWorkers);

        for (size_t start = 0; start < total; start += chunkSize) {
            size_t end = std::min(total, start + chunkSize);

            // Submit to reusable pool instead of spawning a new thread via std::async
            futures.emplace_back(pool.enqueue([this, &pairs, start, end, &debugCounter]() {
                for (size_t i = start; i < end; ++i) {
                    if (!marketDataUpdaterRunning_.load(std::memory_order_relaxed)) {
                        return;
                    }

                    const auto& [sym, exch] = pairs[i];

                    double vol_1m = 0.0, spread_ema = 0.0, imbalance = 0.0;
                    double intensity = 0.0, price_return = 0.0, mid_price = 0.0;

                    updateSymbolMarketData(sym, exch, debugCounter,
                                          vol_1m, spread_ema, imbalance,
                                          intensity, price_return, mid_price);

                    // Cập nhật exitStrategy
                    try {
                        exitStrategy_->updateMarketData(sym.toString(),
                            vol_1m, spread_ema, imbalance, intensity, price_return);
                        
                        // Update regime cache
                        ::trading::strategy::MarketMicrostructure micro;
                        micro.volatility_1m = vol_1m;
                        micro.spread_ema = spread_ema;
                        micro.orderbook_imbalance = imbalance;
                        micro.trade_intensity = intensity;
                        micro.mid_price = mid_price;
                        micro.tick_size = getExchangeTickSize(exch);
                        // Other fields can be set to 0 or default
                        
                        exitStrategy_->updateRegime(sym.toString(), micro);
                        // sometime print regime for debug
                        int cnt = debugCounter.fetch_add(1, std::memory_order_relaxed);
                        if (cnt % 100 == 0) {
                            LOG_TRACE("Regime updated for {}: vol_1m={:.6f}, spread_ema={:.6f}, imbalance={:.3f}, intensity={:.0f}, mid={:.6f}",
                                     sym.toString(), vol_1m, spread_ema, imbalance, intensity, mid_price);
                        }
                    }
                    catch (const std::exception& e) {
                        LOG_WARN("exitStrategy->updateMarketData failed for {}@{}: {}",
                                 sym.toString(), exch.toString(), e.what());
                    }
                    catch (...) {
                        LOG_WARN("exitStrategy->updateMarketData unknown error for {}@{}",
                                 sym.toString(), exch.toString());
                    }
                }
            }));
        }
        
        // 4. Chờ tất cả worker hoàn thành
        for (auto& f : futures) {
            if (!marketDataUpdaterRunning_.load(std::memory_order_relaxed)) break;
            try {
                f.get();
            }
            catch (const std::exception& e) {
                LOG_WARN("Worker future exception: {}", e.what());
            }
            catch (...) {
                LOG_WARN("Worker future unknown exception");
            }
        }
        futures.clear(); // Giải phóng ngay

        // 5. Duy trì chu kỳ
        auto cycleEnd = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(cycleEnd - cycleStart);

        if (elapsed < targetPeriod) {
            std::this_thread::sleep_for(targetPeriod - elapsed);
        }
        else if (elapsed.count() > 1500) { // >1.5s → cảnh báo
            LOG_DEBUG("marketDataUpdaterLoop slow cycle: {}ms", elapsed.count());
        }
    }

    LOG_INFO("Market data updater loop stopped");
}

void Trading_system::exitMonitorLoop() {
    LOG_INFO("Exit monitor loop started");
    
    while (exitMonitorRunning_.load()) {
        try {
            if (!exitStrategy_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(std::max(1, strategyConfig_.exitMonitorIntervalMs)));
                continue;
            }
            
            // Get all active positions from exit strategy
            auto activeSymbols = exitStrategy_->getActiveSymbols();
            if (activeSymbols.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(std::max(1, strategyConfig_.exitMonitorIntervalMs)));
                continue;
            }
            for (const auto& symbol : activeSymbols) {
                try {
                    // Get current market data
                    core::Symbol sym(symbol);
                    // Resolve exchangeId for this symbol from strategyConfig_.tradingPairs if possible.
                    // Fallback to binance() with a warning only if not found.
                    auto exchangeId = core::ExchangeId::binance();
                    bool exchangeResolved = false;
                    for (const auto &p : strategyConfig_.tradingPairs) {
                        if (p.first.toString() == symbol) {
                            exchangeId = p.second;
                            exchangeResolved = true;
                            break;
                        }
                    }
                    if (!exchangeResolved) {
                        LOG_WARN("Exit monitor: could not resolve exchange for {} - defaulting to {}", symbol, exchangeId.toString());
                    }
                    
                    auto priceOpt = dataIngestionService_->getLastPrice(exchangeId, sym);
                    if (!priceOpt.has_value()) {
                        LOG_TRACE("Exit monitor: No price for {}", symbol);
                        continue;
                    }
                    
                    double currentPrice = priceOpt.value();
                    
                    // Get spread
                    double spreadRatio = 0.0;
                    double spreadEMA = 0.0;
                    double realize_volatility_1m = 0.0;
                    double volatility = 0.0;
                    double bidPrice = 0.0;
                    double askPrice = 0.0;
                    double bidVolume = 0.0;
                    double askVolume = 0.0;
                    double orderbookImbalance = 0.0;
                    double liquidityProfileScore = 0.0;
                    trading::strategy::OrderBookMetrics obMetrics;

                    if (marketDataInterface_) {
                            
                        // Try to fetch latest orderbook metrics cached from EventBus
                        double obVolImbalance = 0.0;
                        double obRV = 0.0;
                        double obMid = 0.0;
                        if (getLatestOrderbookMetrics(sym, exchangeId, obMetrics)) {
                            try {
                                obMid = obMetrics.midPrice;
                                obVolImbalance = obMetrics.volumeImbalance;
                                // Use rv_smoothed if available (field name in metrics)
                                obRV = obMetrics.rv_smoothed;
                                spreadRatio = obMetrics.spread / currentPrice;
                                spreadEMA = obMetrics.emaSpread / currentPrice;
                                realize_volatility_1m = obMetrics.realize_volatility_1m;
                                volatility = obMetrics.realizedVolatility;
                                bidPrice = obMetrics.bidPrice;
                                askPrice = obMetrics.askPrice;
                                orderbookImbalance = obMetrics.volumeImbalance;
                                liquidityProfileScore = obMetrics.liquidityProfileScore;
                                bidVolume = obMetrics.totalBidVolume;
                                askVolume = obMetrics.totalAskVolume;

                                LOG_TRACE("Exit monitor: metrics for {} mid={} volImb={} rv={} vol1m={}", symbol, obMid, obVolImbalance, obRV, realize_volatility_1m);
                            } catch (...) {
                                // ignore malformed metric
                            }
                        } else {
                            LOG_TRACE("Exit monitor: no cached metrics for {}", symbol);
                            //use marketDataInterface_ as fallback
                            auto spreadOpt = marketDataInterface_->getCurrentSpread(sym, exchangeId.toString());
                            if (spreadOpt.has_value() && currentPrice > 0.0) {
                                spreadRatio = spreadOpt.value() / currentPrice;
                            }                            
                            spreadEMA = spreadRatio;
                        }
                    }
                        
                    // Get position details - prefer Risk/Execution position tracker via ExecutionInterface
                    // Fallback to exitStrategy_ stored position info for backward compatibility
                    std::optional<trading::strategy::AdaptiveExitStrategy::TradeState> posInfo;
                    if (exitStrategy_) {
                        posInfo = exitStrategy_->getTradeState(symbol);
                    }
                    
                    if (!posInfo.has_value() && riskManager_) {
                        try {
                            //fallback to riskManager
                            // RiskManager::getCachedPosition prefers PositionTracker then ExecutionInterface
                            std::optional<double> cachedQty = riskManager_->getCachedPosition(exchangeId, sym);
                            if (cachedQty.has_value() && std::abs(*cachedQty) > 1e-12) {
                                trading::strategy::AdaptiveExitStrategy::TradeState info; //why?
                                double qty = *cachedQty;
                                // info.quantity = std::abs(qty);
                                info.entry_direction = (qty >= 0.0) ? core::Direction::BUY : core::Direction::SELL;
                                info.entry_price = currentPrice;
                                info.entry_time = std::chrono::steady_clock::now();
                                LOG_WARN("Exit monitor: fallback entry_price used (from currentPrice) for {} = {}", symbol, currentPrice);
                                info.entry_confidence = 1.0;
                                posInfo = info; 
                            }
                        
                        } catch (const std::exception& e) {
                            LOG_WARN("Exit monitor: RiskManager cached position lookup failed: {}", e.what());
                        }
                    }

                    
                    // Get current signal (optional - can pass NEUTRAL if not available)
                    core::Direction currentSignalDir = core::Direction::NEUTRAL;
                    double currentConfidence = 0.5;
                    MarketMicrostructure currentMicro;
                    currentMicro.spread_ema = spreadEMA;
                    currentMicro.volatility_1m = realize_volatility_1m;
                    currentMicro.orderbook_imbalance = orderbookImbalance;
                    currentMicro.trade_intensity = obMetrics.totalVolume;
                    currentMicro.mid_price = obMetrics.midPrice;
                    // currentMicro.
                    currentMicro.price_impact = volatility;

                    // Populate tick_size from exchange config cache
                    currentMicro.tick_size = getExchangeTickSize(exchangeId);
                    //add microstructure info here later
                    // CRITICAL: Use position direction, not signal direction!
                    auto exitDecision = exitStrategy_->evaluateExit(
                        symbol,
                        currentPrice,
                        currentMicro,
                        currentSignalDir,
                        currentConfidence
                    );

                    if (exitDecision.should_exit and exitDecision.confidence >= 0.8 && posInfo.has_value()) {
                        // Prepare to log exit trigger with holding time (actual logging after we confirm an authoritative qty)
                        auto now = std::chrono::steady_clock::now();
                        // holdingDurationMs is milliseconds; convert to seconds for log
                        auto holdingDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - posInfo->entry_time).count();
                        double holdingSeconds = static_cast<double>(holdingDurationMs) / 1000.0;
                        // Create exit signal
                        execution::TradingSignal exitSignal;
                        exitSignal.id = "EXIT_" + symbol + "_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
                        exitSignal.symbol = sym;
                        exitSignal.exchangeId = exchangeId;
                        
                        // IMPORTANT: Opposite direction of position
                        exitSignal.direction = (posInfo->entry_direction == core::Direction::BUY) ?
                                            core::Direction::SELL : core::Direction::BUY;
                        
                        exitSignal.strength = 1.0;
                        exitSignal.confidence = 0.9;
                        exitSignal.quality = 0.9;
                        exitSignal.targetSize = 1; //full exit
                        exitSignal.strategyId = core::StrategyId("exit_strategy");
                        
                        // Mark as exit signal
                        exitSignal.metadata["is_exit_signal"] = "true";
                        exitSignal.metadata["exit_reason"] = std::to_string(static_cast<int>(exitDecision.reason));
                        exitSignal.metadata["exit_pnl_bps"] = std::to_string(exitDecision.expected_pnl);
                        exitSignal.signalMetrics["spread_ema"] = spreadEMA;
                        exitSignal.signalMetrics["volatility_1m"] = realize_volatility_1m;
                        // meta data
                    
                        exitSignal.signalMetrics["volatility"] = volatility;
                        if (bidPrice > 0.0 && askPrice > 0.0) {
                            double spread = askPrice - bidPrice;
                            exitSignal.signalMetrics["bidAskSpread"] = spread;
                            exitSignal.signalMetrics["bidPrice"] = bidPrice;
                            exitSignal.signalMetrics["askPrice"] = askPrice;
                        }
                        exitSignal.signalMetrics["bidVolume"] = bidVolume;
                        exitSignal.signalMetrics["askVolume"] = askVolume;
                        exitSignal.signalMetrics["liquidityProfileScore"] = liquidityProfileScore;
                    
                        // Determine exit quantity: always try authoritative source first
                        double exitQty = 0.0;
                        bool qtyResolved = false;
                                                   

                        // Prefer authoritative cached position from RiskManager (PositionTracker/PositionSync)
                        try {
                            if (riskManager_) {
                                auto posOpt = riskManager_->getCachedPosition(exchangeId, sym);
                                if (posOpt.has_value() && std::abs(posOpt.value()) > 1e-12) {
                                    exitQty = std::abs(posOpt.value());
                                    qtyResolved = true;
                                    LOG_DEBUG("Exit monitor: riskManager cached position for {}@{} = {}", sym.toString(), exchangeId.toString(), exitQty);
                                } else {
                                    LOG_TRACE("Exit monitor: no cached position in RiskManager for {}@{}", sym.toString(), exchangeId.toString());
                                }
                            }
                        } catch (const std::exception& e) {
                            LOG_WARN("Exit monitor: failed to query RiskManager cached position: {}", e.what());
                        }                                     

                        // If no authoritative position found, record a simulated signal-level exit
                        // so the strategy can close its internal state and we can still observe
                        // the signal PnL. Do NOT issue an execution order here.
                        if (!qtyResolved) {
                            double simulated_pnl_bps = 0.0;
                            if (posInfo.has_value() && posInfo->entry_price > 0.0) {
                                simulated_pnl_bps = trading::strategy::AdaptiveExitStrategy::computeSignalPnlBps(
                                    posInfo->entry_price, currentPrice, posInfo->entry_direction);
                            }

                            // Update simulated counters (thread-safe)
                            double sim_cum = 0.0;
                            uint64_t sim_count = 0;
                            {
                                std::lock_guard<std::mutex> lk(g_exitPnlMutex);
                                g_cumulativeSimulatedExitPnlBps += simulated_pnl_bps;
                                ++g_totalSimulatedExitCount;
                                sim_cum = g_cumulativeSimulatedExitPnlBps;
                                sim_count = g_totalSimulatedExitCount;
                            }
                            // Persist simulated stats immediately so a crash won't lose them
                            try { saveExitPnlStats("logs/exit_pnl_stats.json"); } catch (...) {}

                            double sim_avg = (sim_count > 0) ? (sim_cum / static_cast<double>(sim_count)) : 0.0;
                            LOG_INFO("Exit monitor: no authoritative position for {} - simulated_exit_pnl={:.4f}bps simulated_count={} simulated_cum={:.4f}bps simulated_avg={:.4f}bps",
                                symbol, simulated_pnl_bps, sim_count, sim_cum, sim_avg);

                            // Close the trade in the strategy's internal state using the signal-level PnL
                            try {
                                exitStrategy_->closePosition(symbol, exitDecision.reason, simulated_pnl_bps);
                            } catch (const std::exception& e) {
                                LOG_WARN("Exit monitor: failed to closePosition for {}: {}", symbol, e.what());
                            }
                        } else {
                            // Ensure the exit notional respects minimum notional by increasing quantity if necessary
                            const double minNotionalUsd = 12.0;
                            double finalNotional = currentPrice * exitQty;
                            
                            std::string symStr = sym.toString();
                            int attempts = 0;
                            while (finalNotional < minNotionalUsd && attempts < 5) {
                                // increase by one step using precision manager
                                double bumped = exitQty + std::pow(10.0, -8 + attempts);
                                try {
                                    bumped = ::execution::SymbolPrecisionManager::roundQuantity(symStr, bumped);
                                } catch (...) {}
                                if (bumped <= exitQty) break; // can't bump further
                                exitQty = bumped;
                                finalNotional = currentPrice * exitQty;
                                attempts++;
                            }

                            if (finalNotional < minNotionalUsd) {
                                LOG_WARN("Exit monitor: resolved exit quantity for {} results in notional ${:.2f} < minNotional ${:.2f} - skipping exit", symbol, finalNotional, minNotionalUsd);
                            } else {
                                exitSignal.setQuantity(exitQty);
                                std::lock_guard<std::mutex> lk(execQueueMutex_);
                                if (operationMode_ == OperationMode::TRAINING) {
                                    // Training-only: skip actual exit execution but record simulated closure
                                    LOG_INFO("Trading_system: Training mode - skipping actual exit execution for {}", symbol);
                                    // Close position in strategy internal state so model labels remain consistent
                                    try { exitStrategy_->closePosition(symbol, exitDecision.reason, exitDecision.expected_pnl); } catch (...) {}
                                } else {
                                    execQueue_.push_back(std::move(exitSignal));
                                }
                                execQueueCv_.notify_one();

                                // Update simple PnL statistics (thread-safe) only for authoritative exits that were queued
                                double cumulative_bps = 0.0;
                                uint64_t total_exits = 0;
                                {
                                    std::lock_guard<std::mutex> lk2(g_exitPnlMutex);
                                    g_cumulativeExitPnlBps += exitDecision.expected_pnl;
                                    ++g_totalExitCount;
                                    cumulative_bps = g_cumulativeExitPnlBps;
                                    total_exits = g_totalExitCount;
                                }
                                // Persist authoritative stats immediately as well
                                try { saveExitPnlStats("logs/exit_pnl_stats.json"); } catch (...) {}
                                double avg_bps = (total_exits > 0) ? (cumulative_bps / static_cast<double>(total_exits)) : 0.0;

                                LOG_INFO("EXIT TRIGGERED: {} - {} ({:.4f}bps) after {:.3f}s holding - exit_count={} cum_pnl={:.4f}bps avg_pnl={:.4f}bps",
                                    symbol,
                                    AdaptiveExitStrategy::exitReasonToString(exitDecision.reason),
                                    exitDecision.expected_pnl,
                                    holdingSeconds,
                                    total_exits,
                                    cumulative_bps,
                                    avg_bps);

                                // Mark the exitStrategy closed for this symbol locally. Note: ideally the position
                                // should be closed upon execution/fill confirmation and realized PnL recorded there.
                                exitStrategy_->closePosition(symbol, exitDecision.reason, exitDecision.expected_pnl);
                            }
                        }
                       
                }
                    
                } catch (const std::exception& e) {
                    LOG_ERROR("Exit monitor error for {}: {}", symbol, e.what());
                }
            }
            
            // Sleep based on configured exit monitor interval (default 100ms)
            std::this_thread::sleep_for(std::chrono::milliseconds(std::max(100, strategyConfig_.exitMonitorIntervalMs)));
            
        } catch (const std::exception& e) {
            LOG_ERROR("Exit monitor loop error: {}", e.what());
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
    
    LOG_INFO("Exit monitor loop stopped");
}

Trading_system::~Trading_system() {
    shutdown();
}


bool Trading_system::initialize() {
   
    // First, initialize the execution service
    setupExecutionEngine();
    // Debug: Verify execution service and router
    if (!executionService_) {
        std::cerr << "Execution Service failed to initialize!" << std::endl;
        return false;
    }
    std::cout << "=== INITIALIZING FULL TRADING SYSTEM ===" << std::endl;
    
    // 🚨 EMERGENCY: Load current PnL state to block losing symbols
    loadEmergencyState();
    // Load persisted exit PnL stats so cumulative metrics survive restarts
    loadExitPnlStats("logs/exit_pnl_stats.json");
    // Load exchange-specific tick sizes early so downstream components can use them
    loadExchangeTickSizes("config/exchange_specific_params.json");
    // Load optional per-symbol tick sizes
    loadSymbolTickSizes("config/symbol_tick_sizes.json");
    
    // 1. Initialize event bus first
    common::EventBus::getInstance().start();
    
    // 2. Initialize data ingestion service
    dataIngestionService_ = std::make_unique<data_ingestion::DataIngestionService>(
        "config/data_ingestion.json");
    //set comm server
    dataIngestionService_->setCommServer(zmq_server_);
    // Ensure internal ZMQ server is started/bound before components that create
    // ZMQ clients/subscribers (important for inproc transport which requires
    // the bind to exist prior to connects in the same context).
    if (!zmq_server_->isRunning()) {
        if (!zmq_server_->start()) {
            std::cerr << "Failed to start internal ZMQ server during initialization: " << zmq_server_->getLastError() << std::endl;
            return false;
        }
        // brief pause to let sockets bind
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    // Short startup pause - scale with marketDataUpdaterIntervalMs to avoid long blocking
    int startupWaitMs = std::min(2000, std::max(500, strategyConfig_.marketDataUpdaterIntervalMs * 5));
    std::this_thread::sleep_for(std::chrono::milliseconds(startupWaitMs));
    if (!dataIngestionService_->initialize()) {
        std::cerr << "Failed to initialize data ingestion service" << std::endl;
        return false;
    }
    // 3. Initialize market data interface
    if (!marketDataInterface_) {
        // Obtain the shared singleton instance using the overload that accepts
        // an injected ZMQ client/context so the subscriber is created with the
        // correct shared inproc context (avoid late injection).
        auto mdi_base = MarketDataInterface::createDataInterface(
            riskConfig_, std::move(zmq_client_), zmq_context_);
        marketDataInterface_ = std::dynamic_pointer_cast<MarketDataInterface>(mdi_base);
        if (!marketDataInterface_) {
            std::cerr << "Failed to obtain MarketDataInterface singleton" << std::endl;
            return false;
        }
    }

    if (!marketDataInterface_->initialize()) {
        std::cerr << "Failed to initialize MarketDataInterface" << std::endl;
        return false;
    }
    // Diagnostic: log health & cache stats immediately after initialization
    try {
        auto hs = marketDataInterface_->getHealthStatus();
        LOG_INFO("MarketDataInterface health - cacheEntries={}, validEntries={}, cacheHealthy={}, subscriberHealthy={}",
                 hs.cacheStats.totalEntries, hs.cacheStats.validEntries, hs.cacheHealthy, hs.subscriberHealthy);
        // Also log pointer/address for easier debugging if workers see nullptr
        LOG_INFO("MarketDataInterface pointer={}", static_cast<const void*>(marketDataInterface_.get()));
    } catch (const std::exception& e) {
        LOG_WARN("Failed to query MarketDataInterface health after init: {}", e.what());
    }
    // 3. Initialize risk manager
            
    // ✅ NEW - Use the factory method
    // riskManager_ = RiskManager::create(executionService_, riskConfig_, marketDataInterface_);
    // Create shared PositionTracker for synchronized position tracking
    std::shared_ptr<trading::risk::PositionTracker> sharedPositionTracker = 
        std::make_shared<trading::risk::PositionTracker>(riskConfig_, marketDataInterface_);
    
    riskManager_ = trading::risk::RiskManager::create(
        executionService_, 
        riskConfig_, 
        marketDataInterface_,
        zmq_context_,
        sharedPositionTracker  // Pass shared PositionTracker
    );
    if (!riskManager_) {
        LOG_ERROR("Application", "Failed to initialize risk manager");
        return false;
    }
    if (!riskManager_->initialize()) {
        std::cerr << "Failed to initialize risk manager" << std::endl;
        return false;
    }
    
    // 4. Initialize strategy engine
    std::cout << "Initializing strategy engine..." << std::endl;
    strategyEngine_ = std::make_unique<strategy::StrategyEngine>(StrategyId("integrated_strategy_test_v1"));
    
    if (!strategyEngine_->initialize(strategyConfig_)) {
        std::cerr << "Failed to initialize strategy engine" << std::endl;
        return false;
    }

    // Apply the configured operation mode (training vs execute) to strategy components
    applyOperationModeToComponents();

    // Ensure a single AdaptiveExitStrategy instance is created and registered
    // early so other components (ImbalanceDetectionEngine, monitor threads)
    // can reference it without startup-order races.
    try {
    // Read prediction horizon from loaded strategy config (ms -> seconds). Fall back to 1500ms if not set.
    int predMs = strategyConfig_.momentumCorrelationConfig.predictionHorizonMs;
    if (predMs <= 0) predMs = 1500;
    double predSeconds = static_cast<double>(predMs) / 1000.0;
    auto globalExit = trading::strategy::ensureGlobalAdaptiveExitStrategy(predSeconds);
        if (globalExit) {
            exitStrategy_ = globalExit;
            LOG_INFO("Initialized AdaptiveExitStrategy singleton (via ensureGlobal): {}", static_cast<const void*>(exitStrategy_.get()));
        } else {
            LOG_WARN("AdaptiveExitStrategy: ensureGlobalAdaptiveExitStrategy returned null");
        }
    } catch (const std::exception &e) {
        LOG_WARN("Failed to ensure global AdaptiveExitStrategy: {}", e.what());
    }

    
    // Load symbol volatility configuration
    std::string volatilityConfigPath = "config/symbol_volatility_config.json";
    if (!strategyEngine_->loadSymbolVolatilityConfig(volatilityConfigPath)) {
        std::cout << "Warning: Failed to load symbol volatility config from " << volatilityConfigPath << std::endl;
        std::cout << "Using default volatility thresholds..." << std::endl;
    } else {
        std::cout << "Successfully loaded symbol volatility configuration" << std::endl;
    }
      
    lastPriceUpdate_ = std::chrono::steady_clock::now();
    // 6. Setup component connections
    setupComponentConnections();
    
    std::cout << "System initialization complete!" << std::endl;
    return true;
}

// Private helper to apply operation mode settings to dependent components.
void Trading_system::applyOperationModeToComponents() {
    try {
        if (strategyEngine_) {
            auto imb = strategyEngine_->getImbalanceDetector();
            if (imb) {
                if (operationMode_ == OperationMode::TRAINING) {
                    imb->setTrainingEnabled(true);
                    LOG_INFO("Trading_system: Operation mode TRAINING - enabled model training and disabled live execution");
                } else {
                    imb->setTrainingEnabled(false);
                    LOG_INFO("Trading_system: Operation mode EXECUTE - disabled model training and enabled live execution");
                }
            }
        }
    } catch (const std::exception &e) {
        LOG_WARN("Trading_system: failed to apply operation mode to components: {}", e.what());
    } catch (...) {
        LOG_WARN("Trading_system: unknown error applying operation mode to components");
    }
}

bool Trading_system::start() {
    if (!running_) {
        std::cout << "\n=== STARTING FULL TRADING SYSTEM ===" << std::endl;
        
        running_ = true;
        systemHealthy_ = true;
        startTime_ = std::chrono::steady_clock::now();
        
        if (!zmq_server_->isRunning()) {
            if (!zmq_server_->start()) {
                std::cerr << "Failed to start internal ZMQ server: " << zmq_server_->getLastError() << std::endl;
                return false;
            }
        }

        // CRITICAL DEBUG - Check if threads are actually running
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
 
        //load symbols from strategy config
        std::vector<Symbol> symbols;
        for (const auto& [name, pair] : strategyConfig_.tradingPairs) {
            symbols.push_back(name);
        } 
        // 1. Start data ingestion
        std::cout << "Starting data ingestion with symbols: ";
        for (const auto& symbol : symbols) {
            std::cout << symbol << " ";
        }
        std::cout << std::endl;
        //order book depth from config
        int depth = 20;//strategyConfig_.orderBookDepth;        
        if (!dataIngestionService_->start(symbols,depth)) {
            std::cerr << "Failed to start data ingestion service" << std::endl;
            return false;
        }
        // Start orderbook worker pool after data ingestion starts
        startOrderbookWorkerPool();
    // Start dedicated strategy orderbook worker pool (decoupled)
        startStrategyOrderbookWorkerPool(); 
        
        // 2. Start strategy engine
        if (!strategyEngine_->start()) {
            std::cerr << "Failed to start strategy engine" << std::endl;
            return false;
        }

        // Start signal worker pool
        startSignalWorkerPool();
         
        lastPriceUpdate_ = std::chrono::steady_clock::now();
        // Start exec worker pool
        startExecWorkerPool();
        
        std::cout << "🚀 FULL TRADING SYSTEM IS NOW LIVE!" << std::endl;
        std::cout << "Monitoring system health and processing..." << std::endl;

        // Start exit monitor thread

        // Start exit monitor thread if not already running
        if (!exitMonitorRunning_.load()) {
            exitMonitorRunning_.store(true);
            exitMonitorThread_ = std::thread([this]() { this->exitMonitorLoop(); });
            LOG_INFO("Trading_system: Exit monitor started");
        }

        // Start market microstructure updater thread (calls updateMarketData once per second)
        if (!marketDataUpdaterRunning_.load()) {
            marketDataUpdaterRunning_.store(true);
            marketDataUpdaterThread_ = std::thread([this]() { this->marketDataUpdaterLoop(); });
            LOG_INFO("Trading_system: Market data updater started");
        }

        return true;
    }
    return false;
}
// Or run without time limit:
void Trading_system::runIndefinitely() {
    auto lastStatusTime = std::chrono::steady_clock::now();
    
    while (running_ && systemHealthy_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        auto now = std::chrono::steady_clock::now();
        
        // Print status every 30 seconds - simplified like full_bridge.cpp
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastStatusTime).count() >= 30) {
            printStatus();
            lastStatusTime = now;
        }
        
        if (!checkSystemHealth()) {
            std::cout << "⚠️ System health issues detected!" << std::endl;
            systemHealthy_ = false;
        }
    }
} 
void Trading_system::shutdown() {
    if (running_) {
        std::cout << "\n=== SHUTTING DOWN TRADING SYSTEM ===" << std::endl;
        
        running_ = false;
        // Stop data ingestion FIRST and wait for cleanup
        if (dataIngestionService_) {
            std::cout << "Stopping data ingestion service..." << std::endl;
            dataIngestionService_->stop();
            dataIngestionService_.reset();

            // Give WebSocket connections time to close gracefully
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }        
        // Stop other services
        if (strategyEngine_) {
            std::cout << "Stopping strategy engine..." << std::endl;
            strategyEngine_->stop();
        }
    // Stop exit monitor thread

    // Stop exit monitor thread
    exitMonitorRunning_.store(false);
    if (exitMonitorThread_.joinable()) {
        exitMonitorThread_.join();
    }
    // Stop market data updater thread
    marketDataUpdaterRunning_.store(false);
    if (marketDataUpdaterThread_.joinable()) {
        marketDataUpdaterThread_.join();
    }
    // Stop worker pools
    stopOrderbookWorkerPool();
    // Stop strategy-specific orderbook processing pool
    stopStrategyOrderbookWorkerPool();
    stopSignalWorkerPool();
    stopExecWorkerPool();

    // Stop services in reverse order
    if (zmq_server_) {
        zmq_server_->stop();
    }
        if (riskManager_) {
            std::cout << "Stopping risk manager..." << std::endl;
            // riskManager_->
            riskManager_.reset();
        }
        if (executionService_) {
            std::cout << "Stopping execution service..." << std::endl;
            executionService_->shutdown();
            executionService_.reset();
        }
            
        // // Clear data ingestion service pointer after shutdown
        // std::cout << "Clearing data ingestion service..." << std::endl;
        
        // Unsubscribe from EventBus metrics subscription if present
        try {
            if (metricsSubscriptionId_ != 0) {
                common::EventBus::getInstance().unsubscribe<trading::events::MetricsUpdatedEvent>(metricsSubscriptionId_);
                metricsSubscriptionId_ = 0;
            }
        } catch (...) {}

    // Persist exit PnL stats before shutdown so we can reload on next start
    saveExitPnlStats("logs/exit_pnl_stats.json");

    // Stop event bus last
        std::cout << "Stopping event bus..." << std::endl;
        common::EventBus::getInstance().stop();

        // Join monitoring threads if any - ensure they terminate after running_==false
        for (auto &t : monitoringThreads_) {
            try {
                if (t.joinable()) t.join();
            } catch (...) {}
        }

        std::cout << "System shutdown complete" << std::endl;
    }
}
 

void Trading_system::printStatus() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime_).count();

    std::cout << "\n=== FULL TRADING SYSTEM STATUS ===" << std::endl;
    std::cout << "Runtime: " << elapsed << "s" << std::endl;  //this will be always 0s
    std::cout << "Service Health: " << (dataIngestionService_->isHealthy() ? "HEALTHY" : "UNHEALTHY") << std::endl;
    std::cout << "Order book updates received: " << messagesProcessed_.load() << std::endl;
    std::cout << "Signal queue size: " << signalQueue_.size() << "  Exec queue size: " << execQueue_.size() << std::endl;
    // std::cout << "internal zmq msg sent: " << orderBookUpdatesReceived_.load() << std::endl;
    // List active symbols with intelligence

    // Print publish stats if available
    uint64_t calls = publishCalls_.load();
    if (calls > 0) {
        double avg_ns = static_cast<double>(publishTimeNs_.load()) / static_cast<double>(calls);
        double avg_ms = avg_ns / 1e6;
        std::cout << "Avg publish latency: " << std::fixed << std::setprecision(3) << avg_ms << " ms over " << calls << " calls" << std::endl;
    }
    // Print simple exit PnL statistics
    double cumExitPnl = 0.0;
    uint64_t exitCount = 0;
    {
        std::lock_guard<std::mutex> lk(g_exitPnlMutex);
        cumExitPnl = g_cumulativeExitPnlBps;
        exitCount = g_totalExitCount;
    }
    if (exitCount > 0) {
        double avgExit = cumExitPnl / static_cast<double>(exitCount);
        std::cout << "Exit signals: " << exitCount << "  Cumulative exit PnL: " << std::fixed << std::setprecision(4) << cumExitPnl << " bps  Avg exit PnL: " << avgExit << " bps" << std::endl;
    } else {
        std::cout << "Exit signals: 0" << std::endl;
    }
    // Print process RSS to detect memory growth
    try {
        FILE* f = fopen("/proc/self/statm", "r");
        if (f) {
            long size = 0, resident = 0;
            if (fscanf(f, "%ld %ld", &size, &resident) >= 2) {
                long pageSizeKb = sysconf(_SC_PAGESIZE) / 1024; // in KB
                long rssKb = resident * pageSizeKb;
                std::cout << "RSS: " << (rssKb / 1024) << " MB" << std::endl;
            }
            fclose(f);
        }
    } catch (...) {
        // ignore
    }

                // Warmup diagnostics
                if (isWarmupActive()) {
                    auto now = std::chrono::steady_clock::now();
                    auto remaining = std::chrono::duration_cast<std::chrono::seconds>(warmupDuration_ - (now - startTime_)).count();
                    std::cout << "Warmup active - time remaining: " << remaining << "s" << std::endl;
                    std::cout << "Suppressed orders during warmup: " << suppressedOrdersDuringWarmup_.load() << std::endl;
                }

                std::cout << "=========================================" << std::endl;
}

bool Trading_system::getLatestOrderbookMetrics(const core::Symbol& symbol, const core::ExchangeId& exchangeId, trading::strategy::OrderBookMetrics& out) const {
    std::string key = exchangeId.toString() + ":" + symbol.toString();
    std::lock_guard<std::mutex> lk(metricsMutex_);
    auto it = latestOrderbookMetrics_.find(key);
    if (it == latestOrderbookMetrics_.end()) return false;
    out = it->second;
    return true;
}

bool Trading_system::isWarmupActive() const {
    
    try {
        if (!running_) return false;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime_);
        return elapsed < warmupDuration_;
    } catch (...) {
        return false;
    }
}
 

bool Trading_system::checkSystemHealth() {
    try {
        // Basic health checks
        if (!running_ || !systemHealthy_) {
            return false;
        }
        
        // Check if services are responsive
        bool dataIngestionHealthy = dataIngestionService_ ? dataIngestionService_->isHealthy() : false;
        bool strategyEngineHealthy = strategyEngine_ ? true : false;
        bool riskManagerHealthy = riskManager_ ? true : false;
        bool zmqHealthy = zmq_server_ ? zmq_server_->isRunning() : false;

        bool overallHealthy = dataIngestionHealthy && strategyEngineHealthy &&
                            riskManagerHealthy && zmqHealthy ;
        if (!overallHealthy) {
            std::cerr << "System health check failed:" << std::endl;
            std::cerr << "  Data Ingestion: " << (dataIngestionHealthy ? "HEALTHY" : "UNHEALTHY") << std::endl;
            std::cerr << "  Strategy Engine: " << (strategyEngineHealthy ? "HEALTHY" : "UNHEALTHY") << std::endl;
            std::cerr << "  Risk Manager: " << (riskManagerHealthy ? "HEALTHY" : "UNHEALTHY") << std::endl;
            std::cerr << "  ZMQ Server: " << (zmqHealthy ? "HEALTHY" : "UNHEALTHY") << std::endl;
            return false;
        }        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error in system health check: " << e.what() << std::endl;
        return false;
    }
}


void Trading_system::setupConfigurations() {
    // Create configuration files
    // createDataIngestionConfig();
    createStrategyConfig();
    createRiskConfig();                
    // createPTCAConfig();
}
    
void Trading_system::createStrategyConfig() {
    
    std::string configPath = "config/strategy_config.json";
    strategyConfig_ = loadStrategyConfigFromFile(configPath);
    // Honour the configuration file mode only if it wasn't set via CLI/env
    if (!modeOverridden_) {
        if (strategyConfig_.mode == trading::strategy::StrategyConfig::Mode::TRAINING) {
            setMode(Trading_system::Mode::TRAINING);
            LOG_INFO("Trading_system: strategy_config.json mode=training -> setting system to TRAINING");
        } else {
            setMode(Trading_system::Mode::EXECUTE);
            LOG_INFO("Trading_system: strategy_config.json mode=execute -> setting system to EXECUTE");
        }
    } else {
        LOG_INFO("Trading_system: mode already set externally (CLI/env) - ignoring strategy_config.json mode");
    }
        //  std::map<std::string, core::ExchangeId> tradingPairs;
    for (const auto& [symbol, exchange] : strategyConfig_.tradingPairs) {
        std::cout << "Configured trading pair: " << symbol.toString()
                    << " on exchange " << exchange.toString() << std::endl;            
    }
}

void Trading_system::onMarketPriceUpdate(const core::Symbol& symbol, double price) {
    // ptcaManager_->updateMarketPrice(symbol, price);
}
    // Ad
void Trading_system::onExecutionResponse(const execution::ExecutionResponse& response) {
    std::cout << "📈 Execution: " << response.client_order_id
            << " Status: " << response.order_status  << std::endl;

    ordersExecuted_++;
     
}

 

void Trading_system::createRiskConfig() {
    // Setup risk configuration (similar to risk demo)
    // load config from file
    std::string configPath = "config/risk_config.json";
    riskConfig_.loadFromFile(configPath);
    if (!riskConfig_.validate()) {
        std::cerr << "Invalid risk configuration!" << std::endl;
    }
    // Use `risk_config.json` as the single source of truth for position limits.
    // Historically the code here injected a large list of hard-coded per-asset
    // overrides for testing. That was confusing and prevented the JSON file
    // from being authoritative. Remove those hard-coded overrides so operators
    // can control limits from `config/risk_config.json` instead.
    auto& positionConfig = const_cast<PositionLimitConfig&>(riskConfig_.getPositionLimitConfig());

    // Ensure every configured trading pair has at least a default limit entry
    // (convert pair like "BTC/USDT" -> base asset "BTC") so missing keys in
    // the JSON don't result in absent lookups. We defer to the JSON values if
    // present and only set the defaultLimit when missing.
    const double fallbackDefault = positionConfig.defaultLimit > 0.0 ? positionConfig.defaultLimit : 1000000.0;
    for (const auto& [symbol, exchange] : strategyConfig_.tradingPairs) {
        std::string base = symbol.base;
        if (base.empty()) continue;
        if (positionConfig.assetLimits.find(base) == positionConfig.assetLimits.end()) {
            positionConfig.assetLimits[base] = fallbackDefault;
        }
    }
    positionConfig.defaultLimit = 500000; // dont care because btc =110k while doge or other maybe 0.001,
    positionConfig.enforceStrictly = false;
    
    auto& exposureConfig = const_cast<ExposureLimitConfig&>(riskConfig_.getExposureLimitConfig());
    exposureConfig.maxTotalExposure = 50000.0;
    exposureConfig.maxAssetExposure = 50000.0;
    exposureConfig.maxStrategyExposure = 50000.0;
    exposureConfig.maxExchangeExposure = 50000.0;
}

void Trading_system::setupStatistics() {
    // Reset all counters
    orderBookUpdatesReceived_ = 0;
    signalsGenerated_ = 0;
    signalsValidated_ = 0;
    signalsApproved_ = 0;
    signalsRejected_ = 0;
    ordersExecuted_ = 0;
    riskEventsTriggered_ = 0;
}

void Trading_system::setupExecutionEngine() {
    std::cout << "=== SETTING UP EXECUTION ENGINE ===" << std::endl;
    // Create the authentication service first
    auto authService = std::make_shared<AuthenticationService>();
    // Create API client manager with auth service
    auto api_client_manager = std::make_shared<ApiClientManager>(authService);
    // Set up credentials and clients (your existing code)
    //get from environment .env file
    const char* apiKey = std::getenv("BINANCE_API_KEY");
    const char* apiSecret = std::getenv("BINANCE_API_SECRET");
    if(!apiKey || !apiSecret) {
        std::cerr << "Error: Missing Binance API credentials in environment variables." << std::endl;
        systemHealthy_ = false;
        return;
    }
    auto credentials = ExchangeCredentials{
        apiKey,
        apiSecret,
        "", 
        true
    };
    authService->setCredentials(core::ExchangeId::binance(), credentials);
    api_client_manager->addClient(core::ExchangeId::binance(),
        std::make_shared<BinanceApiClient>(credentials, 
        std::make_shared<RateLimitController>(core::ExchangeId::binance())));
    std::cout << "=== DEBUG API CLIENT MANAGER ===" << std::endl;
    std::cout << "ApiClientManager created, checking status..." << std::endl;
    if (!api_client_manager->initialize()) {
        std::cerr << "Failed to initialize ApiClientManager" << std::endl;
        systemHealthy_ = false;
        return;
    }
    // Create other components
    auto order_generator = std::make_shared<OrderGenerator>();
    auto smart_order_router = std::make_shared<SmartOrderRouter>();
    auto order_type_selector = std::make_shared<OrderTypeSelector>();
    auto execution_algorithm_factory = std::make_shared<ExecutionAlgorithmFactory>();
    auto order_lifecycle_tracker = std::make_shared<OrderLifecycleTracker>();
    auto fill_reconciliation = std::make_shared<FillReconciliation>();
    // **CRITICAL: Initialize the smart order router with exchange information**
    std::vector<ExchangeInfo> exchanges;
    ExchangeInfo binanceInfo;
    binanceInfo.exchangeId = core::ExchangeId::binance();
    binanceInfo.trading_fee = 0.001;
    // removed: binanceInfo.liquidityProfileScore (not present on ExchangeInfo)
    binanceInfo.latency_ms = 50;
    binanceInfo.success_rate = 0.99;
    exchanges.push_back(binanceInfo);
    // After initializing the router, add this debug check:
    smart_order_router->initialize(exchanges);
    // Create or obtain shared MarketDataInterface and pass it into the gateway.
    // Important: use the overload that injects the shared ZMQ context so the
    // inproc://server_pub publisher and subscriber share the same context and
    // messages are delivered. Passing nullptr for injectedClient and the
    // shared zmq_context_ for context.
    std::shared_ptr<trading::risk::DataInterface> sharedMdIface;
    try {
        sharedMdIface = trading::risk::MarketDataInterface::createDataInterface(
            riskConfig_, /*injectedClient=*/std::unique_ptr<trading::communication::GenericZmqClient>(nullptr), zmq_context_);
    } catch (...) {
        sharedMdIface = nullptr;
    }
    exchangeGateway_ = std::make_shared<execution::ExchangeGateway>(api_client_manager, sharedMdIface);
    if (!exchangeGateway_->isHealthy()) {
        std::cout << "ExchangeId Gateway is not healthy!" << std::endl;
        std::cout << "start using fallback ExecutionService" << std::endl;
        executionService_ = std::make_shared<ExecutionService>(
        order_generator,
        smart_order_router,
        order_type_selector,
        execution_algorithm_factory,
        order_lifecycle_tracker,
        fill_reconciliation,
        api_client_manager
        );
        executionService_->setLimitOrderHandling(trading::execution::ExecutionService::LimitOrderHandling::REJECT_IF_NO_PRICE);
    } else {
        std::cout << "ExchangeId Gateway initialized and healthy." << std::endl;
        executionService_ = std::make_shared<ExecutionService>(
        exchangeGateway_,
        order_generator,
        smart_order_router,
        order_type_selector,
        execution_algorithm_factory,
        order_lifecycle_tracker,
        fill_reconciliation
        );
        executionService_->setLimitOrderHandling(trading::execution::ExecutionService::LimitOrderHandling::REJECT_IF_NO_PRICE);
    }
    // Ensure the execution service is initialized and running. If initialization fails,
    // mark system unhealthy so callers do not attempt to send signals to a stopped service.
    if (executionService_) {
        bool init_ok = false;
        try {
            init_ok = executionService_->initialize();
        } catch (const std::exception& e) {
            std::cerr << "ExecutionService::initialize threw exception: " << e.what() << std::endl;
        }
        if (!init_ok) {
            std::cerr << "Failed to initialize ExecutionService - marking system unhealthy." << std::endl;
            systemHealthy_ = false;
            return;
        } else {
            std::cout << "ExecutionService initialized and running." << std::endl;
        }
    }
}

// Orderbook worker pool control
void Trading_system::startOrderbookWorkerPool() {
    if (orderbookWorkersRunning_.exchange(true)) return; // already running
    orderbookWorkers_.clear();
    for (size_t i = 0; i < orderbookWorkerCount_; ++i) {
        orderbookWorkers_.emplace_back([this, i]() {
            this->orderbookWorkerLoop(i);
        });
    }
    std::cout << "Started " << orderbookWorkers_.size() << " orderbook worker(s)" << std::endl;
}

void Trading_system::stopOrderbookWorkerPool() {
    if (!orderbookWorkersRunning_.exchange(false)) return;
    
    orderbookQueueCv_.notify_all();
    
    // Join all threads first
    for (auto &t : orderbookWorkers_) {
        if (t.joinable()) t.join();
    }
    
    // Clear containers and force deallocation
    orderbookWorkers_.clear();
    orderbookWorkers_.shrink_to_fit();
    
    {
        std::lock_guard<std::mutex> lk(orderbookQueueMutex_);
        orderbookQueue_.clear();
        orderbookQueue_.shrink_to_fit(); // Force deallocation
    }
    
    std::cout << "Orderbook worker pool stopped and memory cleared" << std::endl;
}

void Trading_system::startStrategyOrderbookWorkerPool() {
    if (strategyOrderbookWorkersRunning_.exchange(true)) return;
    strategyOrderbookWorkers_.clear();
    for (size_t i = 0; i < strategyOrderbookWorkerCount_; ++i) {
        strategyOrderbookWorkers_.emplace_back([this, i]() { this->strategyOrderbookWorkerLoop(i); });
    }
    std::cout << "Started " << strategyOrderbookWorkers_.size() << " strategy orderbook worker(s)" << std::endl;
}

void Trading_system::stopStrategyOrderbookWorkerPool() {
    if (!strategyOrderbookWorkersRunning_.exchange(false)) return;
    strategyOrderbookQueueCv_.notify_all();
    for (auto &t : strategyOrderbookWorkers_) if (t.joinable()) t.join();
    strategyOrderbookWorkers_.clear();
    {
        std::lock_guard<std::mutex> lk(strategyOrderbookQueueMutex_);
        strategyOrderbookQueue_.clear();
    }
    std::cout << "Strategy orderbook worker pool stopped" << std::endl;
}

void Trading_system::orderbookWorkerLoop(size_t workerId) {
    std::vector<OrderBookUpdate> batch;
    batch.reserve(orderbookBatchSize_);
    std::cout << "Orderbook worker " << workerId << " started" << std::endl;
    while (orderbookWorkersRunning_.load()) {
        // Wait for data
        std::unique_lock<std::mutex> lk(orderbookQueueMutex_);
        orderbookQueueCv_.wait_for(lk, std::chrono::milliseconds(50), [this]() {
            return !orderbookQueue_.empty() || !orderbookWorkersRunning_.load();
        });

        if (!orderbookWorkersRunning_.load() && orderbookQueue_.empty()) break;

        // Pop up to batchSize items
        batch.clear();
        if (batch.capacity() > orderbookBatchSize_ * 2) {
            batch.shrink_to_fit();
            batch.reserve(orderbookBatchSize_);
        }
        for (size_t i = 0; i < orderbookBatchSize_ && !orderbookQueue_.empty(); ++i) {
            batch.push_back(std::move(orderbookQueue_.front()));
            orderbookQueue_.pop_front();
        }
        lk.unlock();

        if (batch.empty()) continue;

        // Process batch
        for (const auto &update : batch) {
            try {
               
                auto orderBook = dataIngestionService_->getOrderBook(update.exchangeId, update.tradingPair);
                
                if (orderBook) {
                    // publishOrderBookForRisk(update);                 
                    // Enqueue a shared ptr to the orderBook for strategy processing
                    try {
                        std::shared_ptr<OrderBook> ob_ptr = orderBook;
                        std::unique_lock<std::mutex> slk(strategyOrderbookQueueMutex_);
                        if (strategyOrderbookQueue_.size() < strategyOrderbookQueueLimit_) {
                            strategyOrderbookQueue_.push_back(ob_ptr);
                        } else {
                            // Drop oldest if full
                            strategyOrderbookQueue_.pop_front();
                            strategyOrderbookQueue_.push_back(ob_ptr);
                        }
                        slk.unlock();
                        strategyOrderbookQueueCv_.notify_one();
                    } catch (const std::exception &e) {
                        std::cerr << "Failed to enqueue orderbook for strategy: " << e.what() << std::endl;
                    }
                    
                }
            } catch (const std::exception &e) {
                std::cerr << "Orderbook worker " << workerId << " error: " << e.what() << std::endl;
            }
        }
    }
}

void Trading_system::strategyOrderbookWorkerLoop(size_t workerId) {
    // Use a local deque to swap the shared queue with -- this minimizes lock hold time
    std::deque<std::shared_ptr<OrderBook>> localDeque;
    std::cout << "Strategy orderbook worker " << workerId << " started" << std::endl;
    while (strategyOrderbookWorkersRunning_.load()) {
        // Wait until there's work or we are shutting down
        std::unique_lock<std::mutex> lk(strategyOrderbookQueueMutex_);
        strategyOrderbookQueueCv_.wait_for(lk, std::chrono::milliseconds(100), [this]() {
            return !strategyOrderbookQueue_.empty() || !strategyOrderbookWorkersRunning_.load();
        });
        if (!strategyOrderbookWorkersRunning_.load() && strategyOrderbookQueue_.empty()) break;

        // Swap the entire shared queue into localDeque. This is O(1) and very cheap.
        localDeque.clear();
        localDeque.swap(strategyOrderbookQueue_);
        lk.unlock();

        if (localDeque.empty()) continue;

        // Process up to the configured batch size from localDeque
        size_t toProcess = std::min(strategyOrderbookBatchSize_, localDeque.size());
        for (size_t i = 0; i < toProcess; ++i) {
            auto ob_ptr = std::move(localDeque.front());
            localDeque.pop_front();
            try {
                if (strategyEngine_ && ob_ptr) {
                    strategyEngine_->processOrderBook(*ob_ptr);
                }
            } catch (const std::exception &e) {
                std::cerr << "Strategy orderbook worker " << workerId << " error: " << e.what() << std::endl;
            }
        }

        // If there are leftovers in localDeque, push them back into the shared queue in a single locked section.
        if (!localDeque.empty()) {
            std::unique_lock<std::mutex> lk2(strategyOrderbookQueueMutex_);
            // Ensure we respect the queue limit: drop oldest when necessary
            while (!localDeque.empty()) {
                if (strategyOrderbookQueue_.size() >= strategyOrderbookQueueLimit_) {
                    strategyOrderbookQueue_.pop_front();
                }
                strategyOrderbookQueue_.push_back(std::move(localDeque.front()));
                localDeque.pop_front();
            }
            // Notify one worker that new items are available again
            lk2.unlock();
            strategyOrderbookQueueCv_.notify_one();
        }
    }
    std::cout << "Strategy orderbook worker " << workerId << " stopped" << std::endl;
}
// Signal worker pool control
void Trading_system::startSignalWorkerPool() {
    if (signalWorkersRunning_.exchange(true)) return;
    signalWorkers_.clear();
    for (size_t i = 0; i < signalWorkerCount_; ++i) {
        signalWorkers_.emplace_back([this, i]() {
            this->signalWorkerLoop(i);
        });
    }
    std::cout << "Started " << signalWorkers_.size() << " signal worker(s)" << std::endl;
}

void Trading_system::stopSignalWorkerPool() {
    if (!signalWorkersRunning_.exchange(false)) return;
    signalQueueCv_.notify_all();
    for (auto &t : signalWorkers_) if (t.joinable()) t.join();
    signalWorkers_.clear();
    {
        std::lock_guard<std::mutex> lk(signalQueueMutex_);
        signalQueue_.clear();
    }
    std::cout << "Signal worker pool stopped" << std::endl;
}

void Trading_system::signalWorkerLoop(size_t workerId) {
    std::vector<strategy::TradingSignal> batch;
    // signal_dedup set; // optional deduplication
    trading::execution::SignalDeduplicator::Config cfg;
    cfg.includeExchangeInKey = true;
    trading::execution::SignalDeduplicator dedup(cfg);

    batch.reserve(signalBatchSize_);
    while (signalWorkersRunning_.load()) {
        // Wait for data
        std::unique_lock<std::mutex> lk(signalQueueMutex_);

        signalQueueCv_.wait_for(lk, std::chrono::milliseconds(100), [this]() {
            return !signalQueue_.empty() || !signalWorkersRunning_.load();
        });
        if (!signalWorkersRunning_.load() && signalQueue_.empty()) break;
        batch.clear();
        for (size_t i = 0; i < signalBatchSize_ && !signalQueue_.empty(); ++i) {
            batch.push_back(std::move(signalQueue_.front()));
            signalQueue_.pop_front();
        }
        lk.unlock();
        if (batch.empty()) continue;

        for (auto &signal : batch) {
            // CRITICAL FIX: Exit immediately if shutdown is in progress
            if (!signalWorkersRunning_.load()) {
                break;
            }
                        
            try {
                //if signal is neutral, skip
                if (signal.direction == core::Direction::NEUTRAL) continue;
                
               
                signalsGenerated_++;
                double adjustedStrength = signal.strength;              

                // convert and validate
                execution::TradingSignal execSignal;
                // Copy needed fields from the strategy signal BEFORE we move it
                execSignal.id = signal.id;
                execSignal.symbol = signal.symbol;
                execSignal.direction = signal.direction;
                execSignal.strength = adjustedStrength;
                // strategy provides targetSize as a fractional multiplier (0..1)
                execSignal.targetSize = signal.targetSize;
                execSignal.strategyId = signal.strategyId;
                execSignal.confidence = signal.confidence;
                execSignal.quality = signal.quality;
                execSignal.expirationTime = signal.expirationTime;
                execSignal.targets = signal.targets;
                execSignal.exchangeId = signal.exchangeId;
                // Preserve strategy-provided metrics and metadata so Execution gets market context
                execSignal.signalMetrics = signal.signalMetrics;
                execSignal.metadata = signal.metadata;
                // execSignal.
                if (!dedup.shouldProcessSignal(execSignal)) {
                    // Duplicate — skip processing, do NOT register the original with PTCA
                    continue;
                }

                // Inform exit strategy about this latest signal so exit logic can
                // consult targets/ttl even when evaluateExit is invoked elsewhere.
                if (exitStrategy_) {
                    try {
                        exitStrategy_->updateLatestSignal(signal.symbol.toString(), execSignal);
                    } catch (...) {
                        LOG_WARN("Failed to update latest signal in exit strategy for {}", signal.symbol.toString());
                    }
                }

                // Basic filtering
                // if (execSignal.direction == core::Direction::BUY) {
                //     // if (execSignal.strength < 0.5) continue;
                //     if (execSignal.confidence < 0.5) continue;
                // } else if (execSignal.direction == core::Direction::SELL) {
                //     // if (execSignal.strength < 0.3) continue;
                //     //wow, this is stupid, why sell need higher confidence than buy
                //     if (execSignal.confidence < 0.3) continue;
                // }
                

                //check if price target percentage > 0.2%
                for (const auto& target : execSignal.targets) {
                    if (target.percentage > 0.2) {
                        std::cout << "  Target size: " << target.size
                                    << " at " << target.price 
                                    << " (" << target.percentage * 100 << "%)" 
                                    << std::endl;
                    }
                }                   

                // CRITICAL FIX: Check if system is shutting down before accessing riskManager_
                if (!signalWorkersRunning_.load()) {
                    break; // Exit immediately if shutdown is in progress
                }
                
                // Validate with risk manager (with additional safety check)
                if (riskManager_ && signalWorkersRunning_.load()) {
                    std::cout << "🔄 Validating signal " << execSignal.id << " with risk manager" << std::endl;
                    auto validationResult = riskManager_->validateSignal(execSignal);
                    std::cout << "🔄 Risk validation result: approved=" << validationResult.approved << std::endl;
                    signalsValidated_++;
                    if (validationResult.approved) {
                        signalsApproved_++;
                        realExecution(execSignal); 
                    } else {
                        signalsRejected_++;
                    }
                }
            } catch (const std::exception &e) {
                std::cerr << "Signal worker " << workerId << " error: " << e.what() << std::endl;
            }
        }
    }
}
// Execution worker pool control
void Trading_system::startExecWorkerPool() {
    if (execWorkersRunning_.exchange(true)) return;
    execWorkers_.clear();
    for (size_t i = 0; i < execWorkerCount_; ++i) {
        execWorkers_.emplace_back([this, i]() { this->execWorkerLoop(i); });
    }
    std::cout << "Started " << execWorkers_.size() << " exec worker(s)" << std::endl;
    LOG_TRACE("Exec workers started (count={})", execWorkers_.size());
}

void Trading_system::stopExecWorkerPool() {
    if (!execWorkersRunning_.exchange(false)) return;
    execQueueCv_.notify_all();
    for (auto &t : execWorkers_) if (t.joinable()) t.join();
    execWorkers_.clear();
    {
        std::lock_guard<std::mutex> lk(execQueueMutex_);
        execQueue_.clear();
    }
    std::cout << "Exec worker pool stopped" << std::endl;
}

void Trading_system::execWorkerLoop(size_t workerId) {
    while (execWorkersRunning_.load()) {
        execution::TradingSignal task;
        {
            std::unique_lock<std::mutex> lk(execQueueMutex_);
            // Diagnostic: Log when waiting so we can see wakeups
            LOG_TRACE("ExecWorker {} waiting. execQueueSize={} execWorkersRunning={}", workerId, execQueue_.size(), execWorkersRunning_.load());
            execQueueCv_.wait_for(lk, std::chrono::milliseconds(50), [this]() {
                return !execQueue_.empty() || !execWorkersRunning_.load();
            });
            if (!execWorkersRunning_.load() && execQueue_.empty()) break;
            if (execQueue_.empty()) continue;
            LOG_TRACE("ExecWorker {} woke up, execQueueSize={}", workerId, execQueue_.size());
            task = std::move(execQueue_.front());
            execQueue_.pop_front();
        }
        
        try {
            // Small delay to simulate processing/network latency for entries
            std::this_thread::sleep_for(50ms);

            auto workerStart = std::chrono::steady_clock::now();
            std::cout << "🔄 ExecWorker processing signal " << task.id << " for " << task.symbol.toString() 
                      << " (execQueueSize=" << execQueue_.size() << ")" << std::endl;

            if (!executionService_) {
                std::cerr << "Execution service not initialized!" << std::endl;
                continue;
            }

            // Determine whether this is an EXIT order (metadata or naming)
            bool isExitOrder = false;
            try {
                // Prefer explicit metadata flag if available
                auto it = task.metadata.find("is_exit_signal");
                if (it != task.metadata.end() && it->second == "true") isExitOrder = true;
                // Fallback to strategyId prefix. This is incorrect bc our strategy name is order_imbalance, and exit strategy is a separate for exit
                if (!isExitOrder && !task.strategyId.toString().empty() && task.strategyId.toString().find("EXIT_") != std::string::npos) isExitOrder = true;
            } catch (...) {
                // ignore metadata parsing errors, treat as ENTRY
            }
            
            std::cout << "🔄 Signal " << task.id << " isExitOrder=" << isExitOrder << ", direction=" << 
                (task.direction == core::Direction::BUY ? "BUY" : (task.direction == core::Direction::SELL ? "SELL" : "NEUTRAL")) << std::endl;

            if (isExitOrder) {
                // EXIT ORDERS: Use actual available wallet balance instead of relying
                // on internal position tracking which may be out-of-sync (double-counted).
                std::optional<double> currentPriceOpt = dataIngestionService_->getLastPrice(task.exchangeId, task.symbol);
                double currentPrice = currentPriceOpt.value_or(0.0);

                // Tracked quantity (from signal or position tracker)
                double trackedQty = task.getQuantity();

                // Query available balance: prefer RiskManager cached position (PositionSync -> PositionTracker -> ExecutionInterface)
                // Fallback to marketDataInterface balance cache if RiskManager has no data.
                double availableQty = trackedQty;
                // Prefer authoritative RiskManager cached position instead of the local ad-hoc cache.
                try {
                    if (riskManager_) {
                        auto pos = riskManager_->getCachedPosition(task.exchangeId, task.symbol);
                        if (pos.has_value()) {
                            availableQty = pos.value();
                            LOG_INFO("Exit: using RiskManager cached position for {}@{} = {}", task.symbol.toString(), task.exchangeId.toString(), availableQty);
                        } else {
                            LOG_TRACE("Exit: no RiskManager cached position for {}@{}", task.symbol.toString(), task.exchangeId.toString());
                            // MarketDataInterface does not expose an available-balance API in this build,
                            // so we cannot query balances here; keep availableQty as-is and rely on
                            // RiskManager / ExecutionInterface or external account service for authoritative values.
                            LOG_TRACE("Exit: skipping MarketDataInterface balance fallback for {}@{} (API not available)", task.symbol.toString(), task.exchangeId.toString());
                        }
                    } else {
                        LOG_TRACE("Exit: riskManager_ not available for {}@{}", task.symbol.toString(), task.exchangeId.toString());
                    }
                } catch (const std::exception &e) {
                    LOG_WARN("Exit: failed to get available balance for {}@{}: {}", task.symbol.toString(), task.exchangeId.toString(), e.what());
                }

                LOG_INFO("[EXIT] Detected exit for {} - trackedQty={} availableQty={}",
                         task.symbol.toString(), trackedQty, availableQty);

                if (availableQty <= 0.0) {
                    LOG_WARN("Exit aborted for {}: no available balance (available={})", task.symbol.toString(), availableQty);
                    // Record attempt for observability and skip
                    std::cout << "❌ EXIT SKIPPED for " << task.symbol.toString() << ": no available balance" << std::endl;
                    continue;
                }

                // Sell at most what is available. If trackedQty is larger, prefer availableQty.
                double finalQty = std::min(trackedQty, availableQty);

                // Apply precision rounding for exit quantity
                try {
                    std::string symStr = task.symbol.toString();
                    finalQty = ::execution::SymbolPrecisionManager::roundQuantity(symStr, finalQty);
                } catch (const std::exception &e) {
                    LOG_WARN("Exit: Precision rounding failed: {}", e.what());
                }

                // --- Enforce minimum notional for exits as well ---
                const double minNotionalUsd = 10.0;
                double finalNotional = currentPrice * finalQty;
                if (finalNotional < minNotionalUsd) {
                    // Try bumping up to a small threshold (e.g., $12) and re-round
                    double minQtyForNotional = 12.0 / std::max(1e-12, currentPrice);
                    try {
                        std::string symStr = task.symbol.toString();
                        double bumped = ::execution::SymbolPrecisionManager::roundQuantity(symStr, minQtyForNotional);
                        // Only accept bumped qty if it's <= availableQty
                        if (bumped <= availableQty && (bumped * currentPrice) >= minNotionalUsd) {
                            finalQty = bumped;
                            finalNotional = currentPrice * finalQty;
                        } else {
                            LOG_WARN("Exec: Exit adjusted quantity still insufficient or exceeds available balance for {}: bumped={} available={}",
                                     task.symbol.toString(), bumped, availableQty);
                            std::cout << "❌ EXIT FAILED for " << task.symbol.toString() << ": Order value below minimum notional requirement or exceeds available balance" << std::endl;
                            continue; // Skip this exit - cannot meet min notional
                        }
                    } catch (const std::exception &e) {
                        LOG_WARN("Exit: precision helper failed during minNotional adjustment: {}", e.what());
                        std::cout << "❌ EXIT FAILED for " << task.symbol.toString() << ": Order value below minimum notional requirement" << std::endl;
                        continue;
                    }
                }

                task.setQuantity(finalQty);

                LOG_INFO("[EXIT] Executing exit: symbol={} qty={} price={} notional=${:.2f}", 
                         task.symbol.toString(), finalQty, currentPrice, finalNotional);

                std::cout << "🔄 Attempting EXIT execution for " << task.symbol.toString() << std::endl;
                ExecutionResponse response = executionService_->executeSignal(task);
                std::cout << "🔄 EXIT execution response: success=" << response.success << ", error='" << response.error_message << "'" << std::endl;

                if (!response.success) {
                    LOG_ERROR("❌ EXIT FAILED: {} - {}", task.symbol.toString(), response.error_message);
                } else {
                    LOG_INFO("✅ EXIT SUCCESS: {} qty={}", task.symbol.toString(), finalQty);
                    // Record as EXIT
                    recordTradeWithType(task.symbol.toString(), "EXIT");
                    // Inform exit strategy that position is closed if we have one
                    try {
                        if (exitStrategy_) {
                            // Calculate realized PnL in basis points for proper position closure
                            double realizedPnlBps = 0.0;
                            try {
                                auto tradeState = exitStrategy_->getTradeState(task.symbol.toString());
                                if (tradeState.is_active && tradeState.entry_price > 0.0) {
                                    realizedPnlBps = trading::strategy::AdaptiveExitStrategy::computeSignalPnlBps(
                                        tradeState.entry_price, currentPrice, tradeState.entry_direction);
                                }
                            } catch (...) {
                                // Fallback: assume neutral PnL if we can't calculate
                                realizedPnlBps = 0.0;
                            }
                            
                            exitStrategy_->closePosition(task.symbol.toString(), 
                                trading::strategy::AdaptiveExitStrategy::ExitReason::PROFIT_TARGET, 
                                realizedPnlBps);
                        }
                    } catch (...) {}
                }

                // Done handling exit
                continue;
            }

            // --- Step 1: Get allowed position size from risk manager ---
            double allowedSize = 0.0;
            if (riskManager_) {
                allowedSize = riskManager_->getAllowedPositionSize(task.symbol);
            }
            // Clamp allowed USD budget to min/max order size
            allowedSize = std::clamp(allowedSize, 10.0, 5000.0);

            // --- Step 2: Apply position sizing (only for BUY orders) ---
            double sizerFraction = 1.0;
            if (positionSizer_ && task.direction == core::Direction::BUY) {
                std::map<std::string, double> metrics;
                metrics["win_rate"] = task.confidence;
                
                // Fetch real volatility from market data
                double vol = 0.05;
                try {
                    if (marketDataInterface_) {
                        vol = marketDataInterface_->getVolatility(task.symbol, "1d", task.exchangeId.toString());
                        vol = std::clamp(vol, 0.0001, 1.0);
                    }
                } catch (const std::exception& e) {
                    LOG_WARN("Failed to get volatility for {}: {}", task.symbol.toString(), e.what());
                    vol = 0.05;
                }
                metrics["volatility"] = vol;
                
                std::map<std::string, double> portfolio;
                sizerFraction = positionSizer_->calculateSize(task.symbol.toString(), metrics, portfolio);
            }

            // --- Step 3: Calculate position size with signal strength tilt ---
            double positionSize = allowedSize * sizerFraction;
            
            // Apply target tilt as a mild multiplier [0.5, 1.0]
            double clampedTarget = std::clamp(task.targetSize, 0.0, 1.0);
            double targetMultiplier = 0.5 + 0.5 * clampedTarget;
            double notionalUsd = positionSize * targetMultiplier;

            //get price from orderbook metric
            trading::strategy::OrderBookMetrics obMetrics;
            double currentPrice = obMetrics.midPrice;
            // --- Step 4: Get current price and convert to quantity ---

            if (currentPrice <= 0.0) {
                //fallback to data ingestion service
                auto priceOpt = dataIngestionService_->getLastPrice(task.exchangeId, task.symbol);
                if (priceOpt.has_value()) {
                    currentPrice = priceOpt.value();
                }
                if (currentPrice <= 0.0) {
                    LOG_WARN("ExecWorker: unable to fetch price for {} - skipping order", task.symbol.toString());
                    std::cout << "❌ Execution skipped for signal " << task.id 
                              << ": Price unavailable" << std::endl;
                    continue;
                }
            }
            
            double finalQty = notionalUsd / currentPrice;
 
            try {
                // precision helper will normalize it internally (remove '/').
                std::string symStr = task.symbol.toString();

                // Round to the nearest valid increment and precision for this symbol
                double roundedQty = ::execution::SymbolPrecisionManager::roundQuantity(symStr, finalQty);

                // If rounding produced an invalid quantity (e.g., below minQty),
                // attempt to bump to minQty if that's acceptable by notional.
                if (!::execution::SymbolPrecisionManager::isValidQuantity(symStr, roundedQty)) {
                   
                    double bumped = roundedQty;
                    bumped = std::max(bumped, 0.0);
                    for (int attempt = 0; attempt < 3 && !::execution::SymbolPrecisionManager::isValidQuantity(symStr, bumped); ++attempt) {
                        // increase by one stepSize worth implicitly by adding a tiny amount
                        bumped = roundedQty + std::pow(10.0, -8 + attempt);
                        bumped = ::execution::SymbolPrecisionManager::roundQuantity(symStr, bumped);
                    }
                    if (::execution::SymbolPrecisionManager::isValidQuantity(symStr, bumped)) {
                        roundedQty = bumped;
                    } else {
                        // Fall back to roundedQty even if invalid; the exchange will
                        // respond and we already log details below.
                        LOG_WARN("Exec", "Rounded qty for {} still invalid (qty={})", symStr, roundedQty);
                    }
                }

                finalQty = roundedQty;
            } catch (const std::exception &e) {
                // Best-effort: keep previous finalQty if helper fails.
                LOG_WARN("Exec: Quantity precision helper failed for {}: {}", task.symbol.toString(), e.what());
            }

            // --- Step 5: Enforce minimum notional AFTER precision rounding ---
            const double minNotionalUsd = 10.0;
            double finalNotionalUsd = currentPrice * finalQty;
            if (finalNotionalUsd < minNotionalUsd) {
                // Recalculate quantity for $12 minimum and apply precision again
                double minQtyForNotional = 12.0 / currentPrice;
                std::string symStr = task.symbol.toString();
                finalQty = ::execution::SymbolPrecisionManager::roundQuantity(symStr, minQtyForNotional);
                finalNotionalUsd = currentPrice * finalQty;
                
                // If still below minimum after rounding, increase by one step
                if (finalNotionalUsd < minNotionalUsd) {
                    LOG_WARN("Exec: Adjusted quantity still insufficient: {}", "Order value below minimum notional requirement");
                    std::cout << "❌ Adjusted quantity still insufficient: Order value below minimum notional requirement" << std::endl;
                    continue; // Skip this order
                }
            }
            
            task.setQuantity(finalQty);

            // --- Step 6: Log resolved sizing before execution ---
            std::cout << "[EXEC] Prepared order: symbol=" << task.symbol.toString()
                      << " qty=" << finalQty 
                      << " price=" << currentPrice
                      << " notionalUsd=" << notionalUsd
                      << " minNotionalUsd=" << minNotionalUsd 
                      << std::endl;

            // --- Step 7: Execute the signal ---
            std::cout << "🔄 Attempting execution for signal " << task.id << " on " << task.symbol.toString() << std::endl;
            auto t0 = std::chrono::steady_clock::now();
            ExecutionResponse response = executionService_->executeSignal(task);
            auto t1 = std::chrono::steady_clock::now();
            auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
            std::cout << "🔄 Execution response received: success=" << response.success << ", error='" << response.error_message << "' (exec_duration_ms=" << dur << ")" << std::endl;

            if (!response.success) {
                // Log a longer duration as warn for backpressure troubleshooting
                if (dur > 500) {
                    LOG_WARN("ExecWorker: executeSignal slow ({} ms) for {}: {}", dur, task.symbol.toString(), response.error_message);
                }

                std::cout << "❌ Execution failed for signal " << task.id 
                          << ": " << response.error_message << std::endl;
                std::cout << "Signal details - Symbol: " << task.symbol.toString()
                          << ", Direction: " << (task.direction == core::Direction::BUY ? "BUY" : 
                                                 (task.direction == core::Direction::SELL ? "SELL" : "NEUTRAL"))
                          << ", Strength: " << task.strength
                          << ", Confidence: " << task.confidence
                          << ", Quality: " << task.quality
                          << ", TargetSize: " << task.targetSize
                          << ", StrategyId: " << task.strategyId
                          << ", ExchangeId: " << task.exchangeId.toString()
                          << std::endl;
                        // Print local position cache for this symbol to help debugging         
                continue;
            }
 
            std::cout << "📈 Executed: " << task.symbol.toString() << " " 
                      << (task.direction == core::Direction::BUY ? "BUY" : "SELL") 
                      << " qty=" << finalQty
                      << " notional=$" << (currentPrice * finalQty)
                      << std::endl;

            // 🚨 RECORD TRADE FOR EMERGENCY FREQUENCY TRACKING
            recordTradeWithType(task.symbol.toString(), "ENTRY");
            // trading::strategy::OrderBookMetrics obMetrics;

            // Start tracking position in the exit strategy if we successfully executed an ENTRY
            try {
                if (exitStrategy_) {
                    double spread = 0.0;
                    // Try to populate microstructure from cached orderbook metrics (thread-safe cache)
                    MarketMicrostructure micro;

                    // Attempt to read latest cached metrics (populated from EventBus subscription)
                    bool haveMetrics = false;
                    try {
                        haveMetrics = getLatestOrderbookMetrics(task.symbol, task.exchangeId, obMetrics);
                    } catch (...) { haveMetrics = false; }

                    if (haveMetrics) {
                        // Map available OrderBookMetrics fields to MarketMicrostructure
                        micro.volatility_1m = obMetrics.realize_volatility_1m; // 1m realized vol
                        micro.spread_ema = obMetrics.emaSpread;                 // ema spread
                        micro.orderbook_imbalance = obMetrics.volumeImbalance;  // use volume imbalance as proxy
                        micro.trade_intensity = obMetrics.totalVolume;         // total volume as intensity
                        micro.price_impact = obMetrics.liquidityPressure;      // proxy for price impact
                        micro.mid_price = obMetrics.midPrice;
                    } else {
                        LOG_DEBUG("Exec worker: no cached orderbook metrics for {}@{} - using fallback microstructure",
                                  task.symbol.toString(), task.exchangeId.toString());

                        // Fallback: estimate spread ratio from market data if available
                        try {
                            if (marketDataInterface_) {
                                auto sp = marketDataInterface_->getCurrentSpread(task.symbol, task.exchangeId.toString());
                                if (sp.has_value() && currentPrice > 0.0) spread = sp.value() ;
                            }
                        } catch (...) {}

                        micro.volatility_1m = 0.0;
                        micro.spread_ema = spread;
                        micro.orderbook_imbalance = 0.0;
                        micro.trade_intensity = 0.0;
                        micro.price_impact = 0.0;
                    }

                    micro.tick_size = getExchangeTickSize(task.exchangeId);

                    exitStrategy_->startPosition(task.symbol.toString(), currentPrice, task.direction, task.confidence, micro);
                    // Update local authoritative cache for executed ENTRY
                    try {
                        double signedQty = (task.direction == core::Direction::BUY) ? finalQty : -finalQty;
                        // Query authoritative position before and after; PositionTracker should be updated
                        double prev = 0.0;
                        double now = 0.0;
                        try {
                            if (riskManager_) {
                                auto prevOpt = riskManager_->getCachedPosition(task.exchangeId, task.symbol);
                                prev = prevOpt.has_value() ? prevOpt.value() : 0.0;
                            }
                        } catch (...) {}

                        // Do not mutate a local cache here. Position updates are handled by RiskManager/PositionTracker
                        // Attempt to read updated position from RiskManager (best-effort, may be async)
                        try {
                            if (riskManager_) {
                                auto nowOpt = riskManager_->getCachedPosition(task.exchangeId, task.symbol);
                                now = nowOpt.has_value() ? nowOpt.value() : prev;
                            }
                        } catch (...) { now = prev; }

                        LOG_INFO("Exec worker: authoritative cached position for {} @ {} prev={} -> now={} (delta approx={})",
                                 task.symbol.toString(), task.exchangeId.toString(), prev, now, signedQty);
                    } catch (...) {
                        LOG_WARN("Exec worker: failed to update local position cache for {}", task.symbol.toString());
                    }
                }
            } catch (...) {}
                    // (subscription moved to setupComponentConnections to ensure a single global subscription)
                      
        } catch (const std::exception &e) {
            std::cerr << "Exec worker " << workerId << " error: " << e.what() << std::endl;
        }
    }
}


void Trading_system::setupComponentConnections() {        
    // Minimal callback: validate & enqueue update to worker queue
    dataIngestionService_->setOrderBookCallback([this](auto update) {
        // Quick checks only - avoid heavy work here
        std::string symbolStr = update.tradingPair.toString();
        if (strategyConfig_.tradingPairs.find(symbolStr) == strategyConfig_.tradingPairs.end()) {
            return; // ignore unconfigured symbols early
        }

        // Enqueue (bounded)
        std::unique_lock<std::mutex> lk(orderbookQueueMutex_);
        // Count incoming messages
        messagesProcessed_.fetch_add(1);

        if (orderbookQueue_.size() >= orderbookQueueLimit_) {
            // Drop oldest to make space (or simply drop this update)
            orderbookQueue_.pop_front();
        }
        // Move the update into the queue to avoid copying large OrderBookUpdate objects.
        orderbookQueue_.push_back(std::move(update));
        lk.unlock();
        orderbookQueueCv_.notify_one();
    });
        
    // 2. Connect strategy engine to risk manager
    // Enqueue signals into bounded queue; processing done by worker pool
    strategyEngine_->registerSignalCallback([this](const strategy::TradingSignal& signal) {
        // quick enqueue
        signalsGenerated_++;
        std::unique_lock<std::mutex> lk(signalQueueMutex_);
        // std::cout << "Enqueuing signal " << signal.id << " from strategy " << signal.strategyId << std::endl;
        //print debug info for every 20th signal
        static std::atomic<int> signalCount{0};
        int count = signalCount.fetch_add(1);
      
        if (signalQueue_.size() >= signalQueueLimit_) {
            // drop oldest to make room
            signalQueue_.pop_front();
        }
        signalQueue_.push_back(signal);
        lk.unlock();
        signalQueueCv_.notify_one();
    });
    
    // 3. Connect risk manager events
    riskManager_->registerRiskEventCallback([this](const risk::RiskEvent& event) {
        riskEventsTriggered_++;
        std::cout << "🚨 Risk Event: " << event.description 
                    << " (Severity: " << event.severity << ")" << std::endl;
    });

    // 3.a Register ExecutionService callback to forward canonical execution
    // reports to RiskManager. This centralizes emission and prevents duplicates
    // caused by multiple components creating their own reports.
    if (executionService_ && riskManager_) {
        try {
            executionService_->registerExecutionCallback([this](const execution::ExecutionResponse& resp) {
                try {
                    // Best-effort: build a rich ExecutionReport from the underlying Order
                    // so downstream consumers (PositionTracker, PortfolioManager)
                    // receive quantity/filledQuantity and fills instead of zeros.
                    execution::models::ExecutionReport report;
                    report.setOrderId(resp.client_order_id);
                    report.setSymbol(resp.symbol);        // NEW
                    report.setExchangeId(core::ExchangeId::fromString(resp.exchange_id)); // NEW


                    if (resp.success) {
                        // Try to fetch the authoritative order state from the execution service
                        try {
                            auto status = executionService_->getOrderStatus(resp.client_order_id);
                            if (status.found && status.order) {
                                // Create report from order to capture fills/quantities
                                auto rpt_ptr = execution::models::ExecutionReport::createFromOrder(status.order);
                                if (rpt_ptr) {
                                    report = *rpt_ptr; // copy
                                } else {
                                    // Fallback to mark as FILLED if we couldn't create from order
                                    report.setStatus(execution::models::OrderState::FILLED);
                                }
                            } else {
                                // Order not found in tracker; still mark as FILLED so risk sees it,
                                // but quantities will remain empty (best-effort). This path should be
                                // rare; consider improving ExecutionResponse to carry quantities.
                                report.setStatus(execution::models::OrderState::FILLED);
                            }
                        } catch (const std::exception& e) {
                            // If fetching status failed, still set FILLED to avoid losing event
                            std::cerr << "Warning: failed to enrich ExecutionResponse with order status: " << e.what() << std::endl;
                            report.setStatus(execution::models::OrderState::FILLED);
                        }
                    } else {
                        report.setStatus(execution::models::OrderState::REJECTED);
                        report.setRejectReason(resp.error_message);
                    }

                    // Forward to RiskManager
                    if (riskManager_) {
                        riskManager_->processExecutionReport(report);
                    }
                } catch (...) {
                    // swallow - reporting must not crash execution thread
                }
            });
            LOG_INFO("Trading_system", "Registered execution callback to forward reports to RiskManager");
        } catch (...) {
            LOG_WARN("Trading_system", "Failed to register execution callback on ExecutionService");
        }
    }

    // Register post-trade signal checker so PostTradeMonitor can consult live strategy signals
    if (riskManager_ && strategyEngine_) {
    riskManager_->registerPostTradeSignalChecker([this](const core::ExchangeId& exchangeId, const core::Symbol& symbol) -> bool {
            try {
                auto signals = strategyEngine_->getActiveSignals();
                auto now = std::chrono::system_clock::now();
                for (const auto& s : signals) {
                    if (s.exchangeId == exchangeId && s.symbol == symbol && s.isBuySignal()) {
                        if (s.isValid(now)) return true;
                    }
                }
            } catch (...) {
                // swallow - return false on error
            }

            // No active buy signal found; consult RegimeSubscriber diagnostics (log-only)
            // try {
            //     trading::strategy::core_analysis::RegimeInfo rinfo;
            //     if (strategyEngine_->getRegimeInfo(symbol.toString(), rinfo)) {
            //         if (!rinfo.recommended_action.empty()) {
            //             // Log the recommendation for observability; do not change behavior
            //             LOG_INFO("SlowGate: Regime recommendation for {}: recommended={} stay_prob={} reason={}",
            //                      symbol.toString(), rinfo.recommended_action,
            //                      rinfo.stay_prob.has_value() ? std::to_string(rinfo.stay_prob.value()) : std::string("n/a"),
            //                      rinfo.recommendation_reason);
            //         }
            //     }
            // } catch (...) {
            //     // best-effort logging only
            // }

            return false;
        });
    }

    // 4. Register exit callback for AdaptiveExitStrategy
    LOG_INFO("ExitCallback: 🔧 Setting up AdaptiveExitStrategy callback...");
    if (riskManager_) {
        LOG_INFO("ExitCallback: ✅ RiskManager is available");
        auto executionInterface = riskManager_->getExecutionInterface();
        if (executionInterface) {
            LOG_INFO("ExitCallback: ✅ ExecutionInterface is available");
            LOG_INFO("ExitCallback: Registering exit callback for AdaptiveExitStrategy");

            // Capture weak_ptrs to avoid extending lifetimes
            std::weak_ptr<trading::risk::ExecutionInterface> execWeak = executionInterface;
            std::weak_ptr<trading::risk::RiskManager> riskWeak = riskManager_;
            
            LOG_INFO("ExitCallback: ✅ AdaptiveExitStrategy exit callback registered successfully");
        } else {
            LOG_ERROR("ExitCallback: ❌ Cannot register exit callback: ExecutionInterface is null");
        }
    } else {
        LOG_ERROR("ExitCallback: ❌ Cannot register exit callback: RiskManager is null");
    }
    LOG_INFO("ExitCallback: 🔧 AdaptiveExitStrategy setup completed");

    // 5. Subscribe to OrderBook metrics updates so exitMonitorLoop can use them
    if (metricsSubscriptionId_ == 0) {
        metricsSubscriptionId_ = common::EventBus::getInstance().subscribe<trading::events::MetricsUpdatedEvent>(
            [this](const trading::events::MetricsUpdatedEvent& evt) {
                try {
                    const auto& m = evt.metrics;
                    std::string key = m.exchangeId.toString() + ":" + m.symbol.toString();
                    std::lock_guard<std::mutex> lk(metricsMutex_);
                    latestOrderbookMetrics_[key] = m; // copy
                } catch (...) {
                    // swallow - caching is best-effort
                }
            }
        );
        LOG_INFO("Metrics subscription registered with id {}", metricsSubscriptionId_);
    }

    // Wire position lookup callback into ImbalanceDetectionEngine so strategy
    // can consult authoritative cached positions before emitting ENTRY signals.
    if (strategyEngine_ && riskManager_) {
        try {
            auto detector = strategyEngine_->getImbalanceDetector();
            if (detector) {
                // Capture weak_ptr to riskManager_ to avoid ownership cycles
                std::weak_ptr<trading::risk::RiskManager> riskWeakLocal = riskManager_;
                detector->setPositionLookupCallback(
                    [riskWeakLocal](const core::ExchangeId& exch, const core::Symbol& sym) -> std::optional<double> {
                        try {
                            auto rm = riskWeakLocal.lock();
                            if (!rm) return std::nullopt;
                            return rm->getCachedPosition(exch, sym);
                        } catch (...) {
                            return std::nullopt;
                        }
                    }
                );
                LOG_INFO("Wired positionLookup callback into ImbalanceDetectionEngine");
            }
        } catch (const std::exception& e) {
            LOG_WARN("Failed to wire position lookup into ImbalanceDetectionEngine: {}", e.what());
        }
    }
}

void Trading_system::realExecution(const execution::TradingSignal& signal) {
    std::cout << "🔄 realExecution called for signal " << signal.id << std::endl;
    // If we're in the warmup period, suppress sending real orders. Count them
    // for diagnostics and return early. This prevents large orders at startup.
    if (isWarmupActive()) {
        std::cout << "🔄 Warmup active - suppressing execution" << std::endl;
        suppressedOrdersDuringWarmup_.fetch_add(1);
        LOG_INFO("Warmup", "Suppressing execution for signal {} (warmup active)", signal.id);
        return;
    }

    // If we are running in training mode, do not execute orders
    if (operationMode_ == OperationMode::TRAINING) {
        std::cout << "⚠️ Training mode active - skipping execution for signal " << signal.id << std::endl;
        LOG_INFO("Trading_system: Training mode active - skipping execution for signal {}", signal.id);
        return;
    }

    std::cout << "🔄 Enqueuing signal " << signal.id << " for execution" << std::endl;
    // Diagnostic: print current exec queue size and worker status to help track stuck signals
    std::cout << "🔍 ExecQueue size=" << execQueue_.size() << " execWorkersRunning=" << execWorkersRunning_.load() << std::endl;
    // Enqueue execution task to worker pool to avoid thread explosion
    ordersExecuted_++;
    std::unique_lock<std::mutex> lk(execQueueMutex_);
    if (execQueue_.size() >= execQueueLimit_) {
        // drop oldest
        execQueue_.pop_front();
    }
    execQueue_.push_back(signal);
    lk.unlock();
    execQueueCv_.notify_one();
}

// 🚨 EMERGENCY RISK & FREQUENCY CONTROLS IMPLEMENTATION

bool Trading_system::emergencyFrequencyCheck(const std::string& symbol) {
    return emergencyFrequencyCheckWithType(symbol, "ENTRY");
}

bool Trading_system::emergencyFrequencyCheckWithType(const std::string& symbol, const std::string& orderType) {
    std::lock_guard<std::mutex> lock(emergencyMutex_);
    
    if (emergencyStop_.load()) {
        std::cout << "🚫 EMERGENCY STOP ACTIVE - Blocking all signals for " << symbol << std::endl;
        return false;
    }
    
    auto now = std::chrono::system_clock::now();
    
    // 1. Check minimum gap between trades - RELAXED for EXIT orders
    auto lastTradeIt = lastTradeTime_.find(symbol);
    if (lastTradeIt != lastTradeTime_.end()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastTradeIt->second);
        
        // EXIT orders have relaxed timing constraints (risk management priority)
        int requiredGap = (orderType == "EXIT") ? MIN_GAP_SECONDS_FOR_EXIT : MIN_GAP_SECONDS_FOR_ENTRY;
        
        if (elapsed.count() < requiredGap) {
            if (orderType != "EXIT") { // Don't block exit orders for frequency
                std::cout << "🚫 FREQUENCY BLOCK (" << orderType << "): " << symbol << " - " << elapsed.count() 
                          << "s < " << requiredGap << "s required gap" << std::endl;
                return false;
            } else {
                std::cout << "⚠️ EXIT ORDER frequency warning: " << symbol << " - " << elapsed.count() 
                          << "s gap (allowed for risk management)" << std::endl;
            }
        }
    }
    
    // 2. Check hourly trade limit - SEPARATE counters for ENTRY vs EXIT
    auto currentHour = std::chrono::duration_cast<std::chrono::hours>(now.time_since_epoch()).count();
    std::string hourKey = symbol + "_" + orderType + "_" + std::to_string(currentHour);
    
    // EXIT orders have higher hourly limit (risk management priority)
    int maxTradesThisType = (orderType == "EXIT") ? (MAX_TRADES_PER_HOUR * 2) : MAX_TRADES_PER_HOUR;
    
    if (hourlyTradeCount_[hourKey] >= maxTradesThisType) {
        if (orderType != "EXIT") { // Don't block exit orders for hourly limits
            std::cout << "🚫 HOURLY LIMIT (" << orderType << "): " << symbol << " - " << hourlyTradeCount_[hourKey] 
                      << "/" << maxTradesThisType << " trades this hour" << std::endl;
            return false;
        } else {
            std::cout << "⚠️ EXIT ORDER hourly limit warning: " << symbol << " - " << hourlyTradeCount_[hourKey] 
                      << "/" << maxTradesThisType << " (allowed for risk management)" << std::endl;
        }
    }
    
    // 3. Check symbol PnL limit
    auto pnlIt = symbolPnL_.find(symbol);
    if (pnlIt != symbolPnL_.end() && pnlIt->second < MAX_LOSS_PER_SYMBOL) {
        std::cout << "🚫 LOSS LIMIT: " << symbol << " - Current PnL " << pnlIt->second 
                  << " below limit " << MAX_LOSS_PER_SYMBOL << std::endl;
        return false;
    }
    
    // 4. Check total portfolio loss
    double totalLoss = 0.0;
    for (const auto& [sym, pnl] : symbolPnL_) {
        if (pnl < 0) totalLoss += pnl;
    }
    if (totalLoss < MAX_TOTAL_DAILY_LOSS) {
        emergencyStop_.store(true);
        std::cout << "🚨 EMERGENCY STOP TRIGGERED: Total loss " << totalLoss 
                  << " exceeds limit " << MAX_TOTAL_DAILY_LOSS << std::endl;
        return false;
    }
    
    return true; // All checks passed
}

bool Trading_system::emergencyRiskCheck(const std::string& symbol, double currentPnL) {
    std::lock_guard<std::mutex> lock(emergencyMutex_);
    
    symbolPnL_[symbol] = currentPnL;
    
    if (currentPnL < MAX_LOSS_PER_SYMBOL) {
        std::cout << "🚫 RISK BLOCK: " << symbol << " PnL " << currentPnL 
                  << " below safety limit " << MAX_LOSS_PER_SYMBOL << std::endl;
        return false;
    }
    
    return true;
}

void Trading_system::recordTrade(const std::string& symbol) {
    recordTradeWithType(symbol, "ENTRY");
}

void Trading_system::recordTradeWithType(const std::string& symbol, const std::string& orderType) {
    std::lock_guard<std::mutex> lock(emergencyMutex_);
    
    auto now = std::chrono::system_clock::now();
    lastTradeTime_[symbol] = now;
    
    // Update hourly counter with order type
    auto currentHour = std::chrono::duration_cast<std::chrono::hours>(now.time_since_epoch()).count();
    std::string hourKey = symbol + "_" + orderType + "_" + std::to_string(currentHour);
    hourlyTradeCount_[hourKey]++;
    
    // Also update general counter for backward compatibility
    std::string generalKey = symbol + "_" + std::to_string(currentHour);
    hourlyTradeCount_[generalKey]++;
    
    int maxForType = (orderType == "EXIT") ? (MAX_TRADES_PER_HOUR * 2) : MAX_TRADES_PER_HOUR;
    std::cout << "📊 TRADE RECORDED (" << orderType << "): " << symbol << " (Count: " 
              << hourlyTradeCount_[hourKey] << "/" << maxForType << ")" << std::endl;
}

void Trading_system::loadEmergencyState() {
    // Load current PnL state from analysis file if available
    try {
        std::ifstream file("/home/e8/works/hybrid-bot/tools/pnl_analyzer/trade_analysis.json");
        if (file.is_open()) {
            nlohmann::json data;
            file >> data;
            
            if (data.contains("pairs")) {
                std::lock_guard<std::mutex> lock(emergencyMutex_);
                
                for (auto& [symbol, pairData] : data["pairs"].items()) {
                    double totalPnl = pairData.value("total_pnl", 0.0);
                    symbolPnL_[symbol] = totalPnl;
                    
                    std::cout << "💰 LOADED PnL: " << symbol << " = $" << totalPnl << std::endl;
                    
                    // Auto-block symbols already at loss limit
                    if (totalPnl < MAX_LOSS_PER_SYMBOL) {
                        std::cout << "🚫 AUTO-BLOCKED: " << symbol << " due to existing losses" << std::endl;
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        std::cout << "⚠️  Could not load PnL state: " << e.what() << std::endl;
    }
}

