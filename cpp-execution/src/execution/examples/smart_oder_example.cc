// Example usage
#include "execution/order_generator.hpp"
#include "execution/order_type_selector.hpp"
#include "execution/smart_order_router.hpp"

void example() {
    // Create the components
    OrderGenerator generator;
    OrderTypeSelector selector;
    SmartOrderRouter router;
    
    // Initialize router with exchange information
    std::vector<ExchangeInfo> exchanges = {
        {"binance", 0.001, 0.9, 50.0, 0.99},
        {"coinbase", 0.005, 0.8, 70.0, 0.98},
        {"ftx", 0.002, 0.85, 60.0, 0.97}
    };
    router.initialize(exchanges);
    
    // Create order parameters
    OrderGenerationParams params;
    params.tradingPair = "BTC/USDT";
    params.side = OrderSide::BUY;
    params.quantity = 0.1;
    params.strategy_id = "order_imbalance_v1";
    params.signal_id = "sig_123456";
    
    // Generate the order
    OrderPtr order = generator.createOrder(params);
    
    // Get current market condition
    MarketCondition market = {
        55000.0,  // bidPrice
        55010.0,  // askPrice
        10.0,     // spread
        0.01,     // volatility
        100.0,    // volume
        0.8,      // liquidity_score
        0.005     // market_impact_estimate
    };
    
    // Select optimal order type
    OrderTypeSelection type_selection = selector.selectOrderType(order, market);
    
    // Apply the selection to the order
    order->setType(type_selection.type);
    if (type_selection.price > 0) {
        order->setPrice(type_selection.price);
    }
    order->setTimeInForce(type_selection.time_in_force);
    
    // Route the order to the best exchange
    RoutingResult routing = router.routeOrder(order);
    
    // Apply routing result
    if (!routing.exchangeId.empty()) {
        order->setExchangeId(routing.exchangeId);
        
        // Now the order is ready to be sent to the exchange
        std::cout << "Order routed to " << routing.exchangeId 
                  << " with type " << static_cast<int>(order->getType())
                  << " and price " << order->getPrice() << std::endl;
    } else {
        std::cerr << "Failed to route order: " << routing.reason << std::endl;
    }
}