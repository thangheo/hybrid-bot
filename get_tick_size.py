import json
import requests

# {
#   "symbols": ["BTCUSDT", "ETHUSDT", "BNBUSDT", "SHIBUSDT"]
# }


def fetch_tick_sizes(symbols):
    url = "https://api.binance.com/api/v3/exchangeInfo"
    response = requests.get(url)
    data = response.json()

    tick_map = {}

    for sym in data["symbols"]:
        symbol_name = sym["symbol"]
        if symbol_name in symbols:
            for f in sym["filters"]:
                if f["filterType"] == "PRICE_FILTER":
                    tick_map[symbol_name] = float(f["tickSize"])
                    break

    # Check missing symbols
    missing = [s for s in symbols if s not in tick_map]
    if missing:
        print("Warning: missing symbols in Binance data:", missing)

    return tick_map

# {
#   "strategy_id": "order_imbalance",
#   "version": "1.2.0",
  
#   "tradingPairs": [
#     {
#       "symbol": "BTC/USDT",
#       "exchange": "BINANCE",
#       "category": "blue_chip",
#       "description": "Bitcoin - Digital gold standard"
#     },
#     {
#       "symbol": "ETH/USDT", 
#       "exchange": "BINANCE",
#       "category": "blue_chip",
#       "description": "Ethereum - Smart contract leader"
#     },


def main():
    # Đọc danh sách symbol
    with open("config/strategy_config.json", "r") as f:
        symbols = json.load(f)["tradingPairs"]

    # Lấy tick size
    tick_sizes = fetch_tick_sizes([p["symbol"].replace("/", "") for p in symbols])
    # Ghi ra file JSON
    with open("tick_sizes.json", "w") as f:
        json.dump(tick_sizes, f, indent=4, sort_keys=True)

    print("✅ Saved tick sizes to tick_sizes.json")
    for k, v in tick_sizes.items():
        print(f"{k}: {v}")

if __name__ == "__main__":
    main()
