# hybrid-bot

A crypto trading system I built and ran against Binance spot: a C++23 market-data, order-book
and execution core, with a Python layer for regime detection and strategy research. It
consumes Binance `@depth20@100ms` order-book streams for 27 pairs, computes order-book
microstructure metrics (imbalance, liquidity flow, volume profile), turns them into signals,
and routes them through pre-trade risk checks to execution.

**Status:** archived. I am no longer trading with it. The repository is public as a record of
the engineering, including a latency audit of its own hot path (see
[Performance audit](#performance-audit)).

---

## Architecture

The C++ core can run in two modes.

### 1. Hybrid mode: `tactical` + Python strategy (`trading_manager.sh`)

```
Binance WS ──► C++ tactical (cpp-execution/src/communication/full_bridge.cpp)
                 ├─ DataIngestionService: WebSocket client, parser, order-book reconstruction
                 ├─ OrderBookAnalyzer: per-symbol microstructure metrics
                 ├─ AdaptiveMarketIntelligence
                 └─ ZMQ PUB (market data / metrics) ───────────────┐
                                                                   ▼
               Python strategy (python-strategy/main.py)
                 ├─ regime detection (Markov / Bayesian models)
                 ├─ EMA pull-back + order-book imbalance strategy
                 └─ TradingIntent (protobuf) ── ZMQ REQ/REP ──► C++ tactical → execution
```

### 2. Full C++ mode: `semi_hft` (`cpp-execution/src/trading_system.cpp`)

The whole pipeline runs in one C++ process. Python only publishes market regimes over ZMQ
(`python-strategy/regime_publisher.py` → `RegimeSubscriber`).

```
WS io thread → ExchangeManager (parse, normalize) → OrderBookReconstructor / OrderBook
  → StrategyEngine (OrderBookAnalyzer, VolumeProfile, MomentumCorrelation)
  → ImbalanceDetectionEngine (detector pool) → signal
  → RiskManager (pre-trade: position limits, exposure, fat-finger, order validation)
  → ExecutionService (Binance REST, order lifecycle, fill reconciliation)
```

Operation modes (`TRAINING` / `EXECUTE`) are described in [docs/operation_modes.md](docs/operation_modes.md).

## What's in the C++ core

| Module | Contents |
|---|---|
| `data_ingestion` | Boost.Beast WebSocket client with reconnect, Binance adapter with a SAX JSON parser, order-book manager and reconstructor, protobuf/ZMQ publishing |
| `strategy` | order-book analyzer, imbalance detection engine, volume profile, momentum correlation, adaptive exit strategy, signal generation |
| `risk` | pre-trade filters (position limits, exposure, fat-finger, order validation), post-trade monitoring (drawdown, circuit breaker, stop loss), position tracking |
| `execution` | execution service, order lifecycle tracking, fill reconciliation, rate limiting, TWAP/VWAP/POV/iceberg algorithms, balance validation |
| `communication` | generic ZMQ server/client, C++/Python bridge, protobuf messages |

Around 130k lines of C++ (C++23, CMake). Dependencies: Boost (Beast/Asio), ZeroMQ + cppzmq,
Protobuf, gRPC, nlohmann_json, jsoncpp, OpenSSL, Eigen3, GoogleTest.

## Build and run

```bash
cd cpp-execution
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"      # binaries are written to cpp-execution/bin/
```

```bash
# Hybrid mode (C++ tactical + Python strategy); needs Binance credentials in .env
./trading_manager.sh start

# Full C++ mode
HYBRID_MODE=training ./cpp-execution/bin/semi_hft
```

Python dependencies: `pip install -r requirements.txt`.

## Performance audit

Before archiving, I audited the hot path of the full C++ mode (tick → signal). The full report,
with `file:line` references, is in
[cpp-execution/docs/perf/01_audit.md](cpp-execution/docs/perf/01_audit.md). Main findings:

- **Polling sleep in the first queue.** The processing thread sleeps `100 ms` whenever its
  input queue is empty, adding up to 100 ms of queueing delay to messages that arrive during
  the sleep. This dwarfs every CPU cost on the path.
- **The delay is invisible to internal metrics**, because `receivedTimestamp` is re-stamped
  at parse time, after the queue wait.
- **Every update is processed twice**: the normalizer forwards it to the callback, and the
  worker calls the same callback directly.
- **Parse, re-serialize, parse again**: the WebSocket client builds a JSON DOM and `dump()`s
  the payload, and the adapter parses it again with a SAX parser.
- **Six thread hand-offs per tick** through mutex/condition-variable queues. The multi-worker
  stages do not preserve per-symbol ordering, so stateful detectors can see ticks out of order.
- **Data races** in the strategy stage when it runs with 2 workers (shared `static` maps,
  an iterator used after its lock is released).

The audit method, benchmark plan and Binance feed recorder are in
[cpp-execution/docs/perf/](cpp-execution/docs/perf/) and
[cpp-execution/scripts/record_binance.py](cpp-execution/scripts/record_binance.py).

## Results

<!-- TODO(author): live/paper trading period, number of trades, PnL, and what you learned
     about the strategy. Remove this section if you prefer not to share. -->

## Repository layout

```
cpp-execution/     C++ core (src/, include/, tests/, proto/, docs/)
python-strategy/   Python strategy, regime models, research notebooks and scripts
config/            runtime configuration (pairs, strategy, risk, data ingestion)
docs/              operation modes
deployment/, docker-compose.yml, trading_manager.sh   deployment and process management
```

## License

Copyright (c) 2025 thangheo. All rights reserved (see `cpp-execution/LICENSE`). The source is
published for reference only.
