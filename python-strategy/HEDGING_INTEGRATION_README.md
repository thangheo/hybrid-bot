# BTC Hedging Integration với Regime Publisher

## Tổng quan
Tích hợp tự động hedging BTC futures vào hệ thống regime publisher. Khi phát hiện market dump event (dựa trên p_trend_up threshold), hệ thống sẽ tự động mở short position để hedge và đóng khi market recovery.

## Cách hoạt động

### Logic Hedging
- **Mở Short**: Khi `p_trend_up <= 0.01` (emergency_p_trend_threshold)
- **Đóng Short**: Khi `p_trend_up >= 0.9` (close_p_trend_threshold)
- **Số lượng**: 0.01 BTC với leverage 10x
- **Chỉ cho BTC**: Chỉ hedge BTC/USDT, không áp dụng cho các symbol khác

### Thresholds
```
emergency_p_trend_threshold = 0.01  // Tương đương với C++ code
close_p_trend_threshold = 0.9       // Ngưỡng recovery
```

## Cài đặt

### 1. Cài đặt dependencies
```bash
pip install ccxt
```

### 2. Cấu hình API Keys
```bash
# Set environment variables
export BINANCE_API_KEY="your_binance_api_key"
export BINANCE_API_SECRET="your_binance_secret_key"
```

### 3. Cấu hình hedging
Chỉnh sửa file `src/hedging/hedging_config.json`:

```json
{
  "hedging": {
    "enabled": true,              // Bật hedging
    "api_key": "",               // Để trống, dùng env var
    "api_secret": "",            // Để trống, dùng env var  
    "testnet": true,             // true = testnet, false = mainnet
    "emergency_p_trend_threshold": 0.01,  // Ngưỡng mở short
    "close_p_trend_threshold": 0.9,       // Ngưỡng đóng short
    "hedge_amount": 0.01,        // Số BTC hedge (0.01 BTC)
    "leverage": 10               // Đòn bẩy 10x
  }
}
```

## Sử dụng

### 1. Test với Demo
```bash
cd python-strategy
python demo_hedging_integration.py
```

### 2. Chạy với Regime Publisher
```bash
cd python-strategy  
python regime_publisher.py
```

### 3. Theo dõi logs
```
✅ Hedging Manager initialized (testnet=True)
🛡️ Hedging: ENABLED

BTCUSDT      regime=2 probs=[0.1, 0.2, 0.6, 0.1] trend=0.005 stay=0.8 rec=none
🛡️ HEDGE: open_short - Market dump detected: p_trend_up=0.0050 <= 0.01
🔴 OPENED SHORT HEDGE POSITION
📊 Symbol: BTC/USDT
💰 Amount: 0.01 BTC
💵 Price: $43,250.00
📈 Leverage: 10x
```

## Output ZMQ Message

Regime publisher sẽ thêm thông tin hedging vào ZMQ message:

```json
{
  "ts": "2024-01-01T12:00:00Z",
  "source": "regime_publisher", 
  "regimes": {
    "BTC/USDT": {
      "regime": 2,
      "probs": [0.1, 0.2, 0.6, 0.1],
      "p_trend_up": 0.005,
      "hedging": {
        "action": "open_short",
        "success": true,
        "reason": "Market dump detected: p_trend_up=0.0050 <= 0.01",
        "is_hedged": true,
        "timestamp": "2024-01-01T12:00:00Z"
      }
    }
  }
}
```

## Risk Management

### ⚠️ Quan trọng
1. **Test trước**: Luôn test với testnet trước khi dùng mainnet
2. **Monitor**: Theo dõi logs và positions thường xuyên  
3. **Stop loss**: Cân nhắc thêm stop loss cho safety
4. **Position size**: 0.01 BTC ~ $430 với giá $43k (leverage 10x = $4300 exposure)

### Scenarios
- **Market Dump**: p_trend_up = 0.005 → Mở short hedge
- **Recovery**: p_trend_up = 0.95 → Đóng short hedge  
- **Neutral**: p_trend_up = 0.5 → Giữ nguyên trạng thái

## Troubleshooting

### Lỗi thường gặp
```
❌ Failed to initialize hedging manager: ccxt not available
→ Solution: pip install ccxt

❌ Failed to initialize exchange: Invalid API key
→ Solution: Kiểm tra BINANCE_API_KEY và BINANCE_API_SECRET

ℹ️ Hedging disabled in config  
→ Solution: Set "enabled": true trong hedging_config.json
```

### Kiểm tra trạng thái
```python
# Trong Python console
from src.hedging.regime_hedging_manager import RegimeHedgingManager
manager = RegimeHedgingManager(config)
status = manager.get_status()
print(json.dumps(status, indent=2))
```

## Files Structure
```
python-strategy/
├── regime_publisher.py              # Main publisher với hedging  
├── demo_hedging_integration.py      # Demo test logic
└── src/hedging/
    ├── regime_hedging_manager.py    # Hedging manager class
    ├── hedging_config.json          # Cấu hình hedging
    ├── binance_hedging_bot.py       # Original demo bot
    └── config_example.json          # Example config
```

## Integration với C++ System

Hedging hoạt động song song với hệ thống C++:
- C++ system: HFT trading logic  
- Python system: Regime detection + Auto hedging
- Coordination: Qua ZMQ messages và shared regime state

Khi C++ system detect market conditions tương tự, có thể sync với Python hedging state qua ZMQ.