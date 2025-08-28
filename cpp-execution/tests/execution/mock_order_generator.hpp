/**
 * @file mock_order_generator.hpp
 * @brief Mock implementation of OrderGenerator
 */
#pragma once

#include <gmock/gmock.h>
#include "execution/order_generator.hpp"

namespace trading {
namespace execution {

class MockOrderGenerator : public OrderGenerator {
public:
    MOCK_METHOD(Order, generateOrder, (const OrderGeneratorParams& params), (override));
};

}  // namespace execution
}  // namespace trading