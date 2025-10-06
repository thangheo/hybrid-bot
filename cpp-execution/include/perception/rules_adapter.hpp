#pragma once

#include <vector>
#include <string>
#include <core/direction.hpp>

namespace trading {
namespace perception {

struct RulesSignal {
    double probability = 0.0;
    double expected_value = 0.0;
    double position_size = 0.0;
    double uncertainty = 0.0;
    bool should_trade = false;
    int confidence = 0; // 0-100
    core::Direction direction = core::Direction::NEUTRAL;
};

using RulesHandle = void*;

// Create/destroy
RulesHandle rules_create();
void rules_destroy(RulesHandle h);

// Persistence (no-op for rules but kept for compatibility)
bool rules_load(RulesHandle h, const std::string &path);
bool rules_save(RulesHandle h, const std::string &path);

// Predict/update
RulesSignal rules_predict(RulesHandle h, const std::vector<double>& features);
void rules_update(RulesHandle h, const std::vector<double>& features, int label);

} // namespace perception
} // namespace trading
