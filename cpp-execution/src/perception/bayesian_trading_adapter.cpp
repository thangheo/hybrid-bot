#include "perception/bayesian_trading_adapter.hpp"
#include "perception/bayesian_trading_model.hpp"
#include <filesystem>
#include <chrono>
#include <string>

namespace trading {
namespace perception {

struct Impl { trading::perception::BayesianTradingModel model; };

BayesHandle bayes_create() {
    return reinterpret_cast<BayesHandle>(new Impl());
}

void bayes_destroy(BayesHandle h) {
    if (!h) return;
    delete reinterpret_cast<Impl*>(h);
}

bool bayes_load(BayesHandle h, const std::string &path) {
    if (!h) return false;
    return reinterpret_cast<Impl*>(h)->model.load_model(path);
}

bool bayes_save(BayesHandle h, const std::string &path) {
    if (!h) return false;
    try {
        std::filesystem::path fp(path);
        if (fp.has_parent_path()) {
            std::filesystem::create_directories(fp.parent_path());
        }
        // Unique temp file (timestamp suffix) to avoid clobbering concurrent saves
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        std::string tmpSuffix = ".tmp." + std::to_string(now);
        std::filesystem::path tmp = fp;
        tmp += tmpSuffix; // append suffix to filename

        // Save to temp file first
        if (!reinterpret_cast<Impl*>(h)->model.save_model(tmp.string())) {
            // save failed
            try { if (std::filesystem::exists(tmp)) std::filesystem::remove(tmp); } catch(...){}
            return false;
        }

        // Atomically rename temp -> final
        std::filesystem::rename(tmp, fp);
        return true;
    } catch (...) {
        return false;
    }
}

bool bayes_is_ready(BayesHandle h) {
    if (!h) return false;
    try {
        return reinterpret_cast<Impl*>(h)->model.is_ready();
    } catch (...) {
        return false;
    }
}

int bayes_get_num_updates(BayesHandle h) {
    if (!h) return 0;
    try {
        return reinterpret_cast<Impl*>(h)->model.get_stats().num_updates;
    } catch (...) {
        return 0;
    }
}

BayesSignal bayes_predict(BayesHandle h, const std::vector<double>& features) {
    BayesSignal s;
    if (!h) return s;
    auto &m = reinterpret_cast<Impl*>(h)->model;
    try {
        auto ts = m.predict(features);
        s.probability = ts.probability;
        s.expected_value = ts.expected_value;
        s.position_size = ts.position_size;
        s.uncertainty = ts.uncertainty;
        s.should_trade = ts.should_trade;
        s.direction = ts.direction;
    } catch (...) {}
    return s;
}

void bayes_update(BayesHandle h, const std::vector<double>& features, int label) {
    if (!h) return;
    try {
        reinterpret_cast<Impl*>(h)->model.update(features, label);
        // Intentionally do not auto-save here; the engine controls when to persist
    } catch (...) {}
}

} // namespace perception
} // namespace trading
