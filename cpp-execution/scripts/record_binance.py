#!/usr/bin/env python3
"""Record a Binance spot order-book feed for deterministic replay.

Default stream is the partial book "<sym>@depth20@100ms" with the 27 pairs from
config/strategy_config.json -- what Trading_system subscribes to (trading_system.cpp:947,
exchange_manager.cpp createStreamNames). Each partial-book message is a full top-20
snapshot, so no REST snapshot is needed.

With --stream "depth@100ms" (diff depth) it also follows Binance's book-sync procedure:
subscribe first, then fetch a REST snapshot per symbol, so the snapshot lands in the
file *after* some buffered events and the replayer must bridge U/u like a live client.

Output format (one record per line, tab-separated):
    S <recv_ns> <SYMBOL> <snapshot json>     REST /api/v3/depth snapshot
    D <recv_ns> <raw combined-stream frame>  {"stream": "...", "data": {...}}

recv_ns is time.time_ns() taken right after the frame was received.

Requires: pip install websockets aiohttp
Usage:    record_binance.py --minutes 15 --out data/feeds/binance_depth20.tsv
"""
import argparse
import asyncio
import time

import aiohttp
import websockets

# Same 27 pairs as config/strategy_config.json "tradingPairs"
DEFAULT_SYMBOLS = [
    "btcusdt", "ethusdt", "bnbusdt", "xrpusdt", "solusdt", "adausdt", "avaxusdt",
    "dogeusdt", "dotusdt", "linkusdt", "uniusdt", "aaveusdt", "arbusdt", "opusdt",
    "fetusdt", "injusdt", "ondousdt", "hbarusdt", "paxgusdt", "sandusdt", "suiusdt",
    "aptusdt", "tiausdt", "shibusdt", "atomusdt", "algousdt", "zecusdt",
]
WS_URL = "wss://stream.binance.com:9443/stream?streams="
REST_URL = "https://api.binance.com/api/v3/depth?symbol={}&limit=1000"


async def fetch_snapshots(symbols, out, delay_s):
    # Let some events buffer first so the replayer has to bridge U/u correctly.
    await asyncio.sleep(delay_s)
    async with aiohttp.ClientSession() as session:
        for sym in symbols:
            async with session.get(REST_URL.format(sym.upper())) as resp:
                body = await resp.text()
            out.write(f"S\t{time.time_ns()}\t{sym.upper()}\t{body}\n")


async def record(symbols, stream, minutes, path):
    streams = "/".join(f"{s}@{stream}" for s in symbols)
    diff_depth = stream.startswith("depth@")
    deadline = time.monotonic() + minutes * 60
    n = 0
    with open(path, "w", buffering=1 << 20) as out:
        async with websockets.connect(WS_URL + streams, max_size=None) as ws:
            snap_task = (asyncio.create_task(fetch_snapshots(symbols, out, 2.0))
                         if diff_depth else None)
            try:
                while time.monotonic() < deadline:
                    frame = await ws.recv()
                    out.write(f"D\t{time.time_ns()}\t{frame}\n")
                    n += 1
                    if n % 10000 == 0:
                        print(f"{n} frames", flush=True)
            except websockets.ConnectionClosed as e:
                # Reconnecting would need fresh snapshots; keep the consistent prefix.
                print(f"connection closed after {n} frames: {e}", flush=True)
            if snap_task:
                await snap_task
    print(f"done: {n} frames -> {path}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--minutes", type=float, default=15)
    ap.add_argument("--symbols", default=",".join(DEFAULT_SYMBOLS))
    ap.add_argument("--stream", default="depth20@100ms",
                    help='stream suffix, e.g. "depth20@100ms" (partial book) or "depth@100ms" (diff)')
    ap.add_argument("--out", default="data/feeds/binance_depth20.tsv")
    args = ap.parse_args()
    asyncio.run(record(args.symbols.split(","), args.stream, args.minutes, args.out))


if __name__ == "__main__":
    main()
