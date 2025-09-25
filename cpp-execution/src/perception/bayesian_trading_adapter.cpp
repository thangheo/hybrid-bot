#include "perception/bayesian_trading_adapter.hpp"
#include "perception/bayesian_trading_model.hpp"

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
    return reinterpret_cast<Impl*>(h)->model.save_model(path);
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
    } catch (...) {}
}

} // namespace perception
} // namespace trading
