#!/usr/bin/env python3
"""
Subscribe to internal publisher, decode protobuf OrderbookMessage and check sequence continuity.
Does NOT add or expect any JSON metadata — uses the existing protobuf `OrderbookMessage.sequence` field.

Usage:
  python3 msg_count_seq.py --endpoint tcp://127.0.0.1:5557 --topic orderbook. --duration 30

The script prints per-window rates and reports detected gaps / out-of-order sequences.
"""
import argparse
import time
import zmq
from internal_orderbook_pb2 import OrderbookMessage


def run(endpoint: str, topic_filter: str, duration: float, window: float):
    ctx = zmq.Context()
    s = ctx.socket(zmq.SUB)
    s.connect(endpoint)
    try:
        s.setsockopt_string(zmq.SUBSCRIBE, topic_filter)
    except Exception:
        s.setsockopt(zmq.SUBSCRIBE, topic_filter.encode('utf-8') if isinstance(topic_filter, str) else topic_filter)

    poller = zmq.Poller()
    poller.register(s, zmq.POLLIN)

    end_time = time.time() + duration if duration > 0 else float('inf')
    window_end = time.time() + window

    total = 0
    window_count = 0
    gaps = 0
    window_gaps = 0
    decode_errors = 0
    out_of_order = 0

    # Track last sequence per symbol (messages mix many symbols)
    last_seq_by_symbol = {}
    # per-symbol counters for window reporting
    window_counts_by_symbol = {}
    window_gaps_by_symbol = {}
    out_of_order_by_symbol = {}
    # For sequences that look like timestamps, collect delta stats instead of gap counting
    time_delta_sum_by_symbol = {}
    time_delta_count_by_symbol = {}
    # threshold to consider a delta as a counter increment vs timestamp (tunable)
    COUNTER_DELTA_THRESHOLD = 1000  # if delta <= this, treat as counter; else as timestamp-like

    print(f"Connecting to {endpoint!s}, subscribing to '{topic_filter}' for {duration}s (window={window}s)")

    try:
        while time.time() < end_time:
            remaining = max(0, int((end_time - time.time()) * 1000))
            socks = dict(poller.poll(timeout=min(1000, remaining)))
            if s in socks and socks[s] == zmq.POLLIN:
                try:
                    parts = s.recv_multipart()
                except Exception:
                    # transient recv error
                    continue

                if not parts:
                    continue

                topic = parts[0]
                try:
                    topic_str = topic.decode('utf-8')
                except Exception:
                    topic_str = repr(topic)

                data = parts[1] if len(parts) > 1 else b''

                total += 1
                window_count += 1
                # symbol can be in protobuf or embedded in topic
                symbol = ''
                try:
                    if hasattr(msg.data, 'symbol') and msg.data.symbol:
                        symbol = str(msg.data.symbol)
                except Exception:
                    symbol = ''
                if not symbol:
                    # try to extract from topic (strip prefix)
                    try:
                        ts = topic_str if 'topic_str' in locals() else None
                        if ts:
                            # topic like 'orderbook.SYMBOL' or 'orderbook.SYMBOL.rest'
                            if ts.startswith('orderbook.'):
                                symbol = ts.split('orderbook.', 1)[1]
                            else:
                                symbol = ts
                    except Exception:
                        symbol = 'unknown'

                window_counts_by_symbol[symbol] = window_counts_by_symbol.get(symbol, 0) + 1

                # decode protobuf
                msg = OrderbookMessage()
                try:
                    msg.ParseFromString(data)
                except Exception:
                    decode_errors += 1
                    continue

                seq = None
                try:
                    seq = int(msg.data.sequence)
                except Exception:
                    seq = None

                if seq is not None:
                    last = last_seq_by_symbol.get(symbol)
                    if last is not None:
                        delta = seq - last
                        if delta == 0:
                            # duplicate
                            pass
                        elif delta > 0 and delta <= COUNTER_DELTA_THRESHOLD:
                            # Likely a counter; consider gaps only when delta > 1
                            if delta == 1:
                                pass
                            else:
                                gap = delta - 1
                                gaps += gap
                                window_gaps += gap
                                window_gaps_by_symbol[symbol] = window_gaps_by_symbol.get(symbol, 0) + gap
                        elif delta > COUNTER_DELTA_THRESHOLD:
                            # Probably timestamp-like sequence (large jump). Track average time delta instead of gap count
                            time_delta_sum_by_symbol[symbol] = time_delta_sum_by_symbol.get(symbol, 0) + delta
                            time_delta_count_by_symbol[symbol] = time_delta_count_by_symbol.get(symbol, 0) + 1
                        else:
                            # seq < last -> out of order / wrap
                            out_of_order += 1
                            out_of_order_by_symbol[symbol] = out_of_order_by_symbol.get(symbol, 0) + 1
                    # update last seen
                    last_seq_by_symbol[symbol] = seq

            # Window reporting
            now = time.time()
            if now >= window_end:
                interval = window
                rate = window_count / interval if interval > 0 else 0
                print(f"rate msg/s: {rate:.6f}  window_msgs={window_count} window_gaps={window_gaps}")
                # print top symbols by window messages and gaps
                if window_counts_by_symbol:
                    top_msgs = sorted(window_counts_by_symbol.items(), key=lambda x: x[1], reverse=True)[:5]
                    top_gaps = sorted(window_gaps_by_symbol.items(), key=lambda x: x[1], reverse=True)[:5]
                    print("Top symbols (msgs in window):", top_msgs)
                    if top_gaps:
                        print("Top symbols (gaps in window):", top_gaps)

                # also report timestamp-like average deltas for top symbols in this window
                if time_delta_count_by_symbol:
                    sample = sorted(time_delta_count_by_symbol.items(), key=lambda x: x[1], reverse=True)[:5]
                    avg_deltas = [(sym, time_delta_sum_by_symbol[sym] / time_delta_count_by_symbol[sym]) for sym, _ in sample]
                    print("Top symbols (timestamp-like avg delta):", avg_deltas)

                # reset window counters
                window_count = 0
                window_gaps = 0
                window_counts_by_symbol.clear()
                window_gaps_by_symbol.clear()
                out_of_order_by_symbol.clear()
                window_end = now + window

    except KeyboardInterrupt:
        print("Interrupted by user")

    print("=== Summary ===")
    print(f"Total messages: {total}")
    print(f"Decode errors: {decode_errors}")
    print(f"Total gaps (missing messages): {gaps}")
    print(f"Out of order count: {out_of_order}")
    # top symbols by total messages and gaps
    if last_seq_by_symbol:
        top_msg_symbols = sorted(window_counts_by_symbol.items(), key=lambda x: x[1], reverse=True) if window_counts_by_symbol else []
        # compute top symbols from last_seq_by_symbol sizes (approx)
        top_by_last = sorted(last_seq_by_symbol.items(), key=lambda x: x[1], reverse=True)[:5]
        print("Top last-seen sequences by symbol (sample):", top_by_last)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--endpoint', '-e', default='tcp://127.0.0.1:5557', help='Publisher endpoint to connect to')
    parser.add_argument('--topic', '-t', default='orderbook.', help='Topic filter to subscribe to')
    parser.add_argument('--duration', '-d', type=float, default=30.0, help='Duration in seconds to run (0 for indefinite)')
    parser.add_argument('--window', '-w', type=float, default=5.0, help='Reporting window in seconds')

    args = parser.parse_args()
    run(args.endpoint, args.topic, args.duration, args.window)


if __name__ == '__main__':
    main()
