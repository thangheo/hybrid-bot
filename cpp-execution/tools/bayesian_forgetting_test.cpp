#include <iostream>
#include <vector>
#include <random>
#include <cmath>
#include <chrono>
#include <algorithm>

#include "perception/bayesian_trading_model.hpp"

using namespace trading::perception;

// Simple drift simulation: linear score with two regimes
int main() {
    const int dim = 5;
    const int T = 2000;
    const int drift_at = 1000;
    const double noise_std = 0.5;

    // true weight vectors for regime A and B
    std::vector<double> wA(dim), wB(dim);
    for (int i = 0; i < dim; ++i) {
        wA[i] = (i % 2 == 0) ? 1.0 : -0.5;
        wB[i] = -wA[i];
    }

    // create two models: no forgetting and with forgetting
    BayesianTradingModel::ModelConfig cfg1;
    cfg1.beta = 0.2;
    cfg1.forgetting_factor = 1.0; // no forgetting

    BayesianTradingModel::ModelConfig cfg2;
    cfg2.beta = 0.2;
    cfg2.forgetting_factor = 0.95; // moderate forgetting
    cfg2.process_noise = 1e-3;

    BayesianTradingModel model_static(cfg1);
    BayesianTradingModel model_forget(cfg2);

    // RNG
    std::mt19937_64 rng(12345);
    std::normal_distribution<double> nd(0.0, 1.0);
    std::normal_distribution<double> noise(0.0, noise_std);

    // Models will initialize lazily on first predict/update

    // Output CSV header
    std::cout << "t,acc_static,acc_forget,ent_static,ent_forget" << std::endl;

    const int window = 100;
    std::vector<int> hist_static, hist_forget, hist_true;
    hist_static.reserve(T);
    hist_forget.reserve(T);
    hist_true.reserve(T);

    for (int t = 0; t < T; ++t) {
        // pick regime
        const std::vector<double>& w = (t < drift_at) ? wA : wB;

        // sample feature
        std::vector<double> x(dim);
        for (int i = 0; i < dim; ++i) x[i] = nd(rng);

        // true score
        double s = 0.0;
        for (int i = 0; i < dim; ++i) s += w[i] * x[i];
        s += noise(rng);

        // assign label: BUY(2) if s>threshold, SELL(0) if s<-threshold, else NEUTRAL(1)
        double thresh = 0.5;
        int label = 1;
        if (s > thresh) label = 2;
        else if (s < -thresh) label = 0;

        // Predict before update
        auto sig_static = model_static.predict(x);
        auto sig_forget = model_forget.predict(x);

        int pred_static = sig_static.should_trade ? (sig_static.direction == trading::core::Direction::BUY ? 2 : 0) : 1;
        int pred_forget = sig_forget.should_trade ? (sig_forget.direction == trading::core::Direction::BUY ? 2 : 0) : 1;

        hist_static.push_back(pred_static == label ? 1 : 0);
        hist_forget.push_back(pred_forget == label ? 1 : 0);
        hist_true.push_back(label);

        // Update models with label
        model_static.update(x, label);
        model_forget.update(x, label);

        // compute moving-window accuracy and entropy
        auto compute_acc = [&](const std::vector<int>& h) {
            int start = std::max(0, (int)h.size() - window);
            int sum = 0;
            for (int i = start; i < (int)h.size(); ++i) sum += h[i];
            return double(sum) / double((int)h.size() - start + 0.0);
        };

        double accs = compute_acc(hist_static);
        double accf = compute_acc(hist_forget);

        // entropy using last prediction probabilities
        auto probs_from_signal = [&](const BayesianTradingModel::TradingSignal& s)->double{return s.uncertainty;};
        double ent_s = sig_static.uncertainty;
        double ent_f = sig_forget.uncertainty;

        // print trace every step (can be reduced)
        std::cout << t << "," << accs << "," << accf << "," << ent_s << "," << ent_f << std::endl;
    }

    return 0;
}
