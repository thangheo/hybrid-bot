# 🛡️ Hedging Integration Guide

Hướng dẫn tích hợp hedging BTC tự động dựa trên regime analysis.

## 📋 Tổng quan

Hedging integration cho phép hệ thống tự động mở/đóng vị thế SHORT BTC futures khi:
- **Mở SHORT**: `p_trend_up <= 0.01` (market dump detected)  
- **Đóng SHORT**: `p_trend_up >= 0.9` (market recovery detected)

## 🚀 Quick Start

### 1. Automatic Setup
```bash
./setup_hedging.sh
```

### 2. Manual Setup

1. **Install dependencies:**
```bash
pip install ccxt
sudo apt-get install jq
```

2. **Configure API keys:**
```bash
# Option 1: Environment variables (recommended)
export BINANCE_API_KEY="your_api_key"
export BINANCE_API_SECRET="your_api_secret"

# Option 2: Edit config file
nano python-strategy/hedging_config.json
```

3. **Enable hedging:**
```bash
./trading_manager.sh hedge-enable
```

4. **Start system:**
```bash
./trading_manager.sh start
```

## ⚙️ Configuration

### Hedging Config File: `python-strategy/hedging_config.json`

```json
{
  "hedging": {
    "enabled": true,
    "api_key": "your_binance_api_key",
    "api_secret": "your_binance_api_secret", 
    "testnet": true,
    "emergency_p_trend_threshold": 0.01,
    "close_p_trend_threshold": 0.9,
    "hedge_amount": 0.01,
    "leverage": 10,
    "symbols": ["BTC/USDT"]
  }
}
```

### Parameters Explained

| Parameter | Default | Description |
|-----------|---------|-------------|
| `enabled` | `false` | Enable/disable hedging |
| `testnet` | `true` | Use Binance testnet (recommended for testing) |
| `emergency_p_trend_threshold` | `0.01` | Trigger SHORT when p_trend_up ≤ this value |
| `close_p_trend_threshold` | `0.9` | Close SHORT when p_trend_up ≥ this value |
| `hedge_amount` | `0.01` | BTC amount to hedge (0.01 = ~$600 at $60k BTC) |
| `leverage` | `10` | Futures leverage (10x = $60 margin for 0.01 BTC) |

## 🎮 Trading Manager Commands

### Basic Commands
```bash
./trading_manager.sh start          # Start system with hedging
./trading_manager.sh stop           # Stop system  
./trading_manager.sh status         # Show status
./trading_manager.sh restart        # Restart system
```

### Hedging Commands
```bash
./trading_manager.sh hedge-config   # Show hedging config
./trading_manager.sh hedge-enable   # Enable hedging
./trading_manager.sh hedge-disable  # Disable hedging
```

### Log Commands
```bash
./trading_manager.sh logs hedging   # Show hedging logs only
./trading_manager.sh logs python    # Show all python logs
./trading_manager.sh follow hedging # Follow hedging logs in real-time
```

## 📊 How It Works

### Integration Flow

1. **Regime Publisher** calculates `p_trend_up` every minute based on 4h BTC returns
2. **Hedging Manager** processes the regime signal:
   - `p_trend_up <= 0.01` → Market dump → Open SHORT hedge
   - `p_trend_up >= 0.9` → Market recovery → Close SHORT hedge
   - Otherwise → Hold current position or wait

### Example Log Output
```
BTC/USDT     regime=2 probs=[0.1, 0.2, 0.6, 0.1] trend=0.005 stay=0.8 rec=none
🛡️  HEDGE: open_short - Market dump: p_trend_up=0.0050 <= 0.01
🔴 OPENED SHORT HEDGE POSITION
📊 Symbol: BTC/USDT
💰 Amount: 0.01 BTC  
💵 Price: $67,234.50
📈 Leverage: 10x
```

## ⚠️ Risk Management

### Safety Features

1. **Testnet First**: Always test on Binance testnet before mainnet
2. **Small Position**: Default 0.01 BTC (~$600 exposure)
3. **Automatic Stops**: Closes hedge when market recovers (p_trend_up ≥ 0.9)
4. **Single Position**: Only one hedge position at a time
5. **Error Handling**: Graceful failure if exchange connection fails

### Important Notes

- **Capital Required**: ~$60 margin for 0.01 BTC at 10x leverage
- **Market Risk**: Short positions lose money if BTC price rises
- **Timing Risk**: Regime signals may lag real market movements
- **Exchange Risk**: Binance API/connectivity issues
- **Testnet Limitations**: Testnet behavior may differ from mainnet

## 🔧 Troubleshooting

### Common Issues

1. **"ccxt library not available"**
```bash
pip install ccxt
```

2. **"API credentials invalid"**  
- Check API key/secret in config
- Ensure futures trading is enabled on Binance account
- Verify testnet vs mainnet settings

3. **"Failed to initialize exchange"**
- Check internet connection
- Verify Binance API status
- Check API key permissions (futures trading required)

4. **Hedging not triggering**
- Check if hedging is enabled: `./trading_manager.sh hedge-config`
- Verify BTC regime data in logs: `./trading_manager.sh logs python | grep BTC`
- Check p_trend_up values in logs

### Debug Commands

```bash
# Check hedging configuration
./trading_manager.sh hedge-config

# Test hedging logic (without real trading)
cd python-strategy && python demo_hedging_integration.py

# Monitor regime publisher output
./trading_manager.sh follow python | grep "BTC\|HEDGE"

# Check all hedging-related logs  
./trading_manager.sh logs hedging 50
```

## 🎯 Performance Tuning

### Adjusting Thresholds

- **More Sensitive**: Lower `emergency_p_trend_threshold` (e.g., 0.005)
- **Less Sensitive**: Higher `emergency_p_trend_threshold` (e.g., 0.02)
- **Faster Exit**: Lower `close_p_trend_threshold` (e.g., 0.8)
- **Slower Exit**: Higher `close_p_trend_threshold` (e.g., 0.95)

### Position Sizing

- **Conservative**: 0.001 BTC (~$60 exposure)
- **Standard**: 0.01 BTC (~$600 exposure) 
- **Aggressive**: 0.1 BTC (~$6000 exposure)

**Note**: Always test with small sizes first!

## 📈 Monitoring & Analytics

### Key Metrics to Track

1. **Hedge Trigger Rate**: How often hedges are opened
2. **Hedge Duration**: Average time hedges stay open
3. **P&L per Hedge**: Profit/loss per hedge cycle
4. **False Signals**: Hedges opened then quickly closed
5. **Missed Opportunities**: Major dumps without hedge activation

### Log Analysis

```bash
# Count hedge activations today
./trading_manager.sh logs python | grep "$(date +%Y-%m-%d)" | grep "OPENED SHORT HEDGE" | wc -l

# Show all hedge P&L 
./trading_manager.sh logs python | grep "CLOSED SHORT HEDGE"

# Monitor regime threshold hits
./trading_manager.sh logs python | grep "p_trend_up.*0\.0[0-1]"
```

## 🚨 Production Checklist

Before going live:

- [ ] Tested on Binance testnet successfully
- [ ] Verified API keys and permissions  
- [ ] Set appropriate position size for account
- [ ] Configured conservative thresholds initially
- [ ] Set up monitoring and alerts
- [ ] Tested emergency stop procedures
- [ ] Documented risk limits and procedures
- [ ] Set `testnet: false` in config
- [ ] Double-checked all configuration

---

## 📞 Support

- **Issues**: Check logs first with `./trading_manager.sh logs hedging`
- **Configuration**: Use `./trading_manager.sh hedge-config`
- **Testing**: Run `python demo_hedging_integration.py`

**Remember**: Start with testnet and small positions. Crypto trading involves significant risk!