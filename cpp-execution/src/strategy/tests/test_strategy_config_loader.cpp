#include <gtest/gtest.h>
#include "strategy/config/strategy_config_loader.hpp"
using namespace trading::strategy;

TEST(StrategyConfigLoader, LoadsModeFromJson) {
    auto cfg = loadStrategyConfigFromFile("config/strategy_config.json");
    ASSERT_EQ(cfg.mode, StrategyConfig::Mode::TRAINING);
}
