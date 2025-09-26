#pragma once

#include <vector>
#include <string>

namespace trading {
namespace perception {

struct BayesSignal {
    double probability = 0.0;
    double expected_value = 0.0;
    double position_size = 0.0;
    double uncertainty = 0.0;
    bool should_trade = false;
    int direction = 0; // 1 buy, -1 sell, 0 hold
};

using BayesHandle = void*;

// Create/destroy model
BayesHandle bayes_create();
void bayes_destroy(BayesHandle h);

// Persistence
bool bayes_load(BayesHandle h, const std::string &path);
bool bayes_save(BayesHandle h, const std::string &path);

// Introspection helpers
bool bayes_is_ready(BayesHandle h);
int  bayes_get_num_updates(BayesHandle h);

// Predict/Update
BayesSignal bayes_predict(BayesHandle h, const std::vector<double>& features);
void bayes_update(BayesHandle h, const std::vector<double>& features, int label);

} // namespace perception
} // namespace trading
