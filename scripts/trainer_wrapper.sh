#!/bin/bash
set -euo pipefail

# Wrapper to stop trading manager, run training, then restart trading manager
#!/usr/bin/env bash
set -euo pipefail

# Determine repo root relative to this script's location. This makes the script relocatable.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TM="$REPO_ROOT/trading_manager.sh"
TRAIN_CMD="python3 $REPO_ROOT/python-strategy/regime.py --start-trainer --n-states 4 --trainer-interval-days 30 --data-source binance"
LOG_DIR="$REPO_ROOT/logs"
LOG="$LOG_DIR/trainer_wrapper.log"

mkdir -p "$LOG_DIR"
cd "$REPO_ROOT"
{
  echo "["$(date '+%Y-%m-%d %H:%M:%S')"] Trainer wrapper starting"
  echo "Working dir: $REPO_ROOT"

  # Stop trading manager to free resources
  echo "["$(date '+%Y-%m-%d %H:%M:%S')"] Stopping trading_manager.sh..."
  if "$TM" stop >> "$LOG" 2>&1; then
    echo "["$(date '+%Y-%m-%d %H:%M:%S')"] trading_manager stopped successfully"
  else
    echo "["$(date '+%Y-%m-%d %H:%M:%S')"] trading_manager stop returned non-zero (continuing)"
  fi

  # Run training
  echo "["$(date '+%Y-%m-%d %H:%M:%S')"] Running training: $TRAIN_CMD"
  if eval "$TRAIN_CMD" >> "$LOG" 2>&1; then
    echo "["$(date '+%Y-%m-%d %H:%M:%S')"] Training completed successfully"
  else
    echo "["$(date '+%Y-%m-%d %H:%M:%S')"] Training failed (see $LOG)"
  fi

  # Start trading manager again
  echo "["$(date '+%Y-%m-%d %H:%M:%S')"] Starting trading_manager.sh..."
  if "$TM" start >> "$LOG" 2>&1; then
    echo "["$(date '+%Y-%m-%d %H:%M:%S')"] trading_manager started successfully"
  else
    echo "["$(date '+%Y-%m-%d %H:%M:%S')"] trading_manager failed to start (see logs)"
  fi

  echo "["$(date '+%Y-%m-%d %H:%M:%S')"] Trainer wrapper finished"
} >> "$LOG" 2>&1

exit 0
