#include "perception/rules_adapter.hpp"
#include <vector>
#include <memory>
#include <algorithm>
#include <cmath>

namespace trading {
namespace perception {

struct Impl {
    std::vector<double> weights;
    Impl() : weights() {}
};

RulesHandle rules_create() {
    return reinterpret_cast<RulesHandle>(new Impl());
}

void rules_destroy(RulesHandle h) {
    if (!h) return;
    delete reinterpret_cast<Impl*>(h);
}

bool rules_load(RulesHandle h, const std::string &path) {
    // Rules adapter has no persistence by default. Return true for compatibility.
    (void)h; (void)path;
    return true;
}

bool rules_save(RulesHandle h, const std::string &path) {
    (void)h; (void)path;
    return true;
}

RulesSignal rules_predict(RulesHandle h, const std::vector<double>& features) {
    RulesSignal s;
    if (!h) return s;
    auto impl = reinterpret_cast<Impl*>(h);
    // If weights empty, default to uniform weighting
    double score = 0.0;
    if (impl->weights.empty()) {
        for (double v : features) score += v;
    } else {
        size_t n = std::min(features.size(), impl->weights.size());
        for (size_t i = 0; i < n; ++i) score += features[i] * impl->weights[i];
    }
    // Map score to signal fields (simple deterministic mapping)
    s.probability = std::tanh(score);
    s.expected_value = score;
    s.position_size = std::max(0.0, std::abs(score));
    s.uncertainty = 1.0 - std::min(1.0, std::abs(s.probability));
    s.should_trade = (std::abs(score) > 0.1); // small threshold
    s.direction = (score > 0 ? core::Direction::BUY : (score < 0 ? core::Direction::SELL : core::Direction::NEUTRAL));
    return s;
}

void rules_update(RulesHandle h, const std::vector<double>& features, int label) {
    // For rules adapter we treat update as a light weight adjustment to weights
    if (!h) return;
    auto impl = reinterpret_cast<Impl*>(h);
    if (impl->weights.size() < features.size()) impl->weights.resize(features.size(), 0.0);
    // simple perceptron-like update: label should be -1 or 1
    double factor = (label >= 1) ? 0.01 : -0.01;
    for (size_t i = 0; i < features.size(); ++i) impl->weights[i] += factor * features[i];
}

} // namespace perception
} // namespace trading
