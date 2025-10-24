# Professional Balance Validation System

## Overview

The new balance validation system provides comprehensive checking of account balances before placing orders, eliminating the "Account has insufficient balance" errors you were experiencing.

## Key Features

### 🔍 **Comprehensive Balance Checking**
- Real-time balance validation before order placement
- Exchange-specific trading fee calculations
- Safety buffer to account for price movements and fees
- Support for all major trading pairs and symbols

### 🚀 **Performance Optimized**
- Intelligent balance caching (30-second expiry by default)
- Async validation to avoid blocking order flow
- Automatic cache refresh on insufficient balance errors

### 🛠️ **Professional Features**
- Case-insensitive asset lookup
- Multiple symbol format support (BTCUSDT, BTC/USDT, BTC-USDT)
- Detailed validation results with suggested adjustments
- Configurable per-exchange trading fees

### 📊 **Smart Adjustments**
- Automatic quantity suggestions when balance is insufficient
- Minimum notional value validation
- Buffer percentage to account for market volatility

## Configuration

### Balance Validation Settings (`config/balance_validation.json`)

```json
{
  "balanceValidation": {
    "enabled": true,
    "defaultTradingFee": 0.001,           // 0.1% default fee
    "exchangeTradingFees": {
      "binance": 0.001,                   // Binance: 0.1%
      "ftx": 0.0007,                      // FTX: 0.07%
      "coinbase": 0.005                   // Coinbase: 0.5%
    },
    "balanceBufferPercent": 0.01,         // 1% safety buffer
    "balanceCacheExpiryMs": 30000,        // 30 seconds cache
    "enableRealTimeValidation": true,
    "minNotionalValue": 10.0              // $10 minimum order
  }
}
```

### Key Configuration Options

- **defaultTradingFee**: Fallback trading fee when exchange-specific fee not available
- **exchangeTradingFees**: Per-exchange trading fees for accurate calculations
- **balanceBufferPercent**: Safety buffer (1% = 1% extra balance required)
- **balanceCacheExpiryMs**: How long to cache balance data
- **minNotionalValue**: Minimum order value in quote currency

## How It Works

### 1. **Pre-Order Validation**
```cpp
// Before placing any order, the system:
auto balanceResult = balanceValidator_->validateOrder(order, exchangeId, false);

if (!balanceResult.isValid) {
    // Order is rejected with detailed reason
    // Cache is automatically refreshed for next order
}
```

### 2. **Balance Requirements Calculation**

**For BUY orders:**
```
Required Balance = (Quantity × Price) × (1 + Trading Fee) × (1 + Buffer)
```

**For SELL orders:**
```
Required Balance = Quantity × (1 + Buffer)
```

### 3. **Final Pre-Submit Check**
```cpp
// Just before submitting to exchange, force refresh balances
auto finalCheck = balanceValidator_->validateOrder(order, exchangeId, true);
```

## Error Prevention

### Before (Old System)
```
❌ Execution failed for signal 641e81d448ab7-00003670-2d19: 
HTTP error 400: {"code":-2010,"msg":"Account has insufficient balance for requested action."}
```

### After (New System)
```
❌ Order rejected due to insufficient balance:
   Symbol: ZECUSDT
   Side: SELL
   Quantity: 0.051
   Price: 256.88
   Asset: ZEC
   Required: 0.051515
   Available: 0.030000
   Reason: Insufficient ZEC balance for SELL order
   💡 Suggested quantity: 0.0297
```

## Integration Examples

### Basic Usage
```cpp
// Create validator with config
auto config = BalanceValidatorConfigLoader::loadFromFile("config/balance_validation.json");
auto validator = std::make_unique<BalanceValidator>(config);

// Validate before placing order
auto result = validator->validateOrder(order, exchangeId);
if (result.isValid) {
    // Safe to place order
    placeOrder(order);
} else {
    // Handle insufficient balance
    if (result.canAdjust) {
        // Suggest alternative quantity
        adjustOrderQuantity(order, result.suggestedQuantity);
    }
}
```

### Advanced Usage with Async
```cpp
// Async validation for high-frequency trading
auto future = validator->validateOrderAsync(order, exchangeId, true);
auto result = future.get();
```

## Monitoring and Debugging

### Validation Logs
The system provides detailed logging for troubleshooting:

```
✅ Balance validation passed:
   Asset: USDT
   Required: 256.88
   Available: 1500.00
   Est. fees: 0.26

🔄 Insufficient balance detected, refreshing cache for exchange: binance
✅ Balance cache cleared for exchange: binance
```

### Cache Management
```cpp
// Force refresh for specific exchange
validator->clearBalanceCache(exchangeId);

// Get current cached balances
auto balances = validator->getCachedBalances(exchangeId);

// Refresh all cached balances
validator->refreshAllBalances();
```

## Benefits

1. **Zero Insufficient Balance Errors**: Orders are validated before submission
2. **Improved Trading Performance**: No failed orders due to balance issues
3. **Better Risk Management**: Configurable safety buffers
4. **Cost Optimization**: Accurate fee calculations prevent over-estimation
5. **User Experience**: Clear error messages with suggested fixes
6. **High Performance**: Intelligent caching reduces API calls

## Testing

Run the balance validation tests:
```bash
cd cpp-execution
make test_balance_validator
./test_balance_validator
```

The test suite covers:
- Sufficient/insufficient balance scenarios
- Fee calculations for different exchanges
- Symbol parsing (BTCUSDT, BTC/USDT, etc.)
- Quantity adjustment suggestions
- Cache behavior and refresh logic

## Migration from Old System

The new balance validator is fully integrated into the existing ExecutionService. No changes to your trading logic are required - the validation happens automatically before each order placement.

The old preflight checks have been replaced with this more comprehensive system, providing better accuracy and more detailed feedback when orders cannot be placed due to insufficient balances.