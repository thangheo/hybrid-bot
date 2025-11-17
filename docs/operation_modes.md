# Operation modes

This project supports two operation modes for the C++ trading system:

- TRAINING: enable model training (Bayesian updates) but skip placing real/executed orders. Use this to warm up models offline without risk/execution.
- EXECUTE: normal live trading mode — model training is disabled to avoid further model updates during execution.

How to set the mode

- Environment variable: set `HYBRID_MODE=training` or `HYBRID_MODE=execute` before running the binary.
- CLI flag: pass `--mode=training` or `--mode=execute` to the C++ binary.
 - Config file: set `"mode": "training"` or `"mode": "execute"` in `config/strategy_config.json`.

Programmatic API

You can also programmatically set the mode on the `Trading_system` instance via `setMode(Trading_system::Mode::TRAINING)` or `setMode(Trading_system::Mode::EXECUTE)`.

Notes

- When running in TRAINING mode, the system will:
  - Enable online Bayesian model updates and persistence in `ImbalanceDetectionEngine`.
  - Skip enqueuing/executing orders (ENTRY/EXIT) — the `Trading_system` will log and not place orders.
  - Forwards exit signals to `exitStrategy_->closePosition` only in simulation (no live execution).

- When running in EXECUTE mode, the system will:
  Precedence
   - CLI and environment override the config file. If you set `--mode=training` or `HYBRID_MODE=training`, that will take priority over `strategy_config.json`.
   - If the CLI/env is not used, `strategy_config.json` will be used to determine the default mode.
  - Disable online model training.
  - Allow real orders to be validated by `RiskManager` and sent by `ExecutionService`.

Example

```
# Run in training mode
export HYBRID_MODE=training
./semi_hft --mode=training

# Run in execute mode
./semi_hft --mode=execute
```
