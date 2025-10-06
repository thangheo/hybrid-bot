#include "perception/rules_adapter.hpp"
#include <iostream>
#include <vector>
#include <iomanip>

int main() {
    using namespace trading::perception;

    // Create rules adapter handle
    RulesHandle h = rules_create();
    if (!h) {
        std::cerr << "Failed to create rules handle" << std::endl;
        return 1;
    }

    // Example feature vector (outputs from detectors)
    std::vector<double> features = { 0.15, -0.05, 0.30, 0.0, 0.1 };

    // Predict before any training / updates
    RulesSignal before = rules_predict(h, features);
    std::cout << "Before update:\n";
    std::cout << "  probability: " << std::setprecision(4) << before.probability << "\n";
    std::cout << "  expected_value: " << before.expected_value << "\n";
    std::cout << "  position_size: " << before.position_size << "\n";
    std::cout << "  should_trade: " << (before.should_trade ? "yes" : "no") << "\n";
    std::cout << "  direction: " << (before.direction == trading::core::Direction::BUY ? "BUY" : (before.direction == trading::core::Direction::SELL ? "SELL" : "NEUTRAL")) << "\n";

    // Simulate an observed positive label (market moved up). Use label >=1 for positive
    int label = 1;
    for (int i = 0; i < 20; ++i) {
        rules_update(h, features, label);
    }

    // Predict after a few lightweight updates
    RulesSignal after = rules_predict(h, features);
    std::cout << "\nAfter update:\n";
    std::cout << "  probability: " << std::setprecision(4) << after.probability << "\n";
    std::cout << "  expected_value: " << after.expected_value << "\n";
    std::cout << "  position_size: " << after.position_size << "\n";
    std::cout << "  should_trade: " << (after.should_trade ? "yes" : "no") << "\n";
    std::cout << "  direction: " << (after.direction == trading::core::Direction::BUY ? "BUY" : (after.direction == trading::core::Direction::SELL ? "SELL" : "NEUTRAL")) << "\n";

    // Demonstrate save/load (no-op currently but shows API)
    rules_save(h, "./rules_weights.json");

    // Destroy handle
    rules_destroy(h);

    return 0;
}
