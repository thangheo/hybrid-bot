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
    s.setsockopt_string(zmq.SUBSCRIBE, topic_filter)

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

    last_seq = None

    print(f"Connecting to {endpoint!s}, subscribing to '{topic_filter}' for {duration}s (window={window}s)")

    try:
        while time.time() < end_time:
            remaining = max(0, int((end_time - time.time()) * 1000))
            socks = dict(poller.poll(timeout=min(1000, remaining)))
            if s in socks and socks[s] == zmq.POLLIN:
                try:
                    topic = s.recv_string()
                    data = s.recv()
                except Exception:
                    # If multipart recv ordering differs, try safe recv
                    try:
                        topic = s.recv_string()
                        data = s.recv()
                    except Exception:
                        continue

                total += 1
                window_count += 1

                # decode protobuf
                msg = OrderbookMessage()
                try:
                    msg.ParseFromString(data)
                except Exception:
                    decode_errors += 1
                    continue

                seq = 0
                try:
                    seq = int(msg.data.sequence)
                except Exception:
                    # No sequence field or invalid
                    seq = None

                if seq is not None:
                    if last_seq is not None:
                        if seq == last_seq:
                            # duplicate
                            pass
                        elif seq == last_seq + 1:
                            # in order
                            pass
                        elif seq > last_seq + 1:
                            gap = seq - last_seq - 1
                            gaps += gap
                            window_gaps += gap
                        else:
                            # seq < last_seq -> out of order / wrap
                            out_of_order += 1
                    last_seq = seq

            # Window reporting
            now = time.time()
            if now >= window_end:
                interval = window
                rate = window_count / interval if interval > 0 else 0
                print(f"rate msg/s: {rate:.6f}  window_msgs={window_count} window_gaps={window_gaps}")
                window_count = 0
                window_gaps = 0
                window_end = now + window

    except KeyboardInterrupt:
        print("Interrupted by user")

    print("=== Summary ===")
    print(f"Total messages: {total}")
    print(f"Decode errors: {decode_errors}")
    print(f"Total gaps (missing messages): {gaps}")
    print(f"Out of order count: {out_of_order}")
    if last_seq is not None:
        print(f"Last sequence seen: {last_seq}")


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
