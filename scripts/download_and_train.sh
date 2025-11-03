#!/usr/bin/env bash
set -euo pipefail

# Download 5000 candles for every symbol listed in config/trading_bot.yaml
# then run quantum_regimes.py to train on all CSVs in python-strategy/data

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CFG="$REPO_ROOT/config/trading_bot.yaml"
#!/usr/bin/env bash
set -euo pipefail

# Download 5000 candles for every symbol listed in config/trading_bot.yaml
# then run quantum_regimes.py to train on all CSVs in python-strategy/data

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CFG="$REPO_ROOT/config/trading_bot.yaml"
DATA_DIR="$REPO_ROOT/python-strategy/data"
DOWNLOAD_SCRIPT="$DATA_DIR/full_tf_download.py"
QUANTUM_SCRIPT="$REPO_ROOT/python-strategy/quantum_regimes.py"

TIMEFRAME="4h"
LENGTH=5000
EXCHANGE="binance"

# source environment variables if needed
source ./venv/bin/activate
if [[ ! -f "$CFG" ]]; then
  echo "Config file not found: $CFG" >&2
  exit 1
fi

if [[ ! -d "$DATA_DIR" ]]; then
  mkdir -p "$DATA_DIR"
fi

if [[ ! -f "$DOWNLOAD_SCRIPT" ]]; then
  echo "Download script not found: $DOWNLOAD_SCRIPT" >&2
  exit 1
fi

echo "Using hard-coded symbol list..."
SYMBOLS=(
  "BTCUSDT" "ETHUSDT" "BNBUSDT" "XRPUSDT" "SOLUSDT" "ADAUSDT" "AVAXUSDT" "DOGEUSDT" "DOTUSDT"
  "LINKUSDT" "UNIUSDT" "AAVEUSDT" "ARBUSDT" "OPUSDT"
  "FETUSDT" "INJUSDT"
  "ONDOUSDT" "HBARUSDT" "PAXGUSDT"
  "SANDUSDT" "GALAUSDT" "IMXUSDT"
  "SUIUSDT" "APTUSDT" "TIAUSDT"
  "SHIBUSDT" "FLOKIUSDT"
  "ATOMUSDT" "ALGOUSDT"
  "ZECUSDT"
)

echo "Found ${#SYMBOLS[@]} symbols. Starting download loop..."

pushd "$DATA_DIR" >/dev/null

for sym in "${SYMBOLS[@]}"; do
  # Normalize symbol token (remove quotes)
  sym_clean=$(echo "$sym" | tr -d '"' | tr -d "'" )
  # Determine quote currency: prefer common 4-letter quotes (USDT, BUSD, USDC), else use last 3 chars
  if [[ ${sym_clean^^} == *USDT ]]; then
    base=${sym_clean:0: -4}
    quote=USDT
  elif [[ ${sym_clean^^} == *BUSD ]]; then
    base=${sym_clean:0: -4}
    quote=BUSD
  elif [[ ${sym_clean^^} == *USDC ]]; then
    base=${sym_clean:0: -4}
    quote=USDC
  else
    # default to last 3 chars as quote
    quote=${sym_clean: -3}
    base=${sym_clean:0: -3}
  fi

  exchange_symbol="${base}/${quote}"
  csv_name="${base}_${quote}_${TIMEFRAME}_${LENGTH}candles.csv"

  echo "Processing symbol: $sym_clean -> $exchange_symbol (output: $csv_name)"

  if [[ -f "$csv_name" ]]; then
    echo "  Skipping: $csv_name already exists"
    continue
  fi

  # Run download script (it must write a CSV to the current dir). Adjust args if your script expects different names.
  echo "  Downloading $exchange_symbol ..."
  python "$DOWNLOAD_SCRIPT" --symbol "$exchange_symbol" --timeframe "$TIMEFRAME" --length $LENGTH --exchange "$EXCHANGE"

  # If the download script writes files with a different naming convention, you may need to rename here.
  # Sleep a bit to avoid hitting rate limits
  sleep 1
done

popd >/dev/null

echo "All downloads attempted. Launching training on $DATA_DIR ..."

# Run training on all CSVs in data dir
python "$QUANTUM_SCRIPT" --input "$DATA_DIR" --outdir "$DATA_DIR"

echo "Download + training complete. Models and results are in $DATA_DIR"

