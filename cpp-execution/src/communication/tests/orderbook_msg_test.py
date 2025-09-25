#!/usr/bin/env python3
"""
Simple ZMQ subscriber for topics starting with 'orderbook.' on port 5557.
Counts messages and prints occasional example messages so the screen doesn't flood.

Usage:
  python orderbook_msg_test.py
  python orderbook_msg_test.py --uri tcp://localhost:5557 --print-every 200 --stats-interval 1
"""
import time
import zmq
import argparse


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--uri', default='tcp://localhost:5557', help='ZMQ endpoint to connect to')
    p.add_argument('--topic-prefix', default='orderbook.', help='Topic prefix to subscribe to')
    p.add_argument('--stats-interval', type=float, default=1.0, help='Seconds between printed stats')
    p.add_argument('--print-every', type=int, default=100, help='Print one full message every N messages')
    args = p.parse_args()

    ctx = zmq.Context()
    sock = ctx.socket(zmq.SUB)
    sock.setsockopt(zmq.SUBSCRIBE, args.topic_prefix.encode())
    sock.connect(args.uri)

    print(f"Subscribed to '{args.topic_prefix}*' on {args.uri}")

    total = 0
    last_total = 0
    last_time = time.time()

    try:
        while True:
            # blocking receive; message format may be [topic, payload] or single frame
            parts = sock.recv_multipart()
            total += 1

            # occasionally print one message to keep the screen alive
            if total % args.print_every == 0:
                try:
                    if len(parts) == 1:
                        topic = b''
                        payload = parts[0]
                    else:
                        topic = parts[0]
                        payload = parts[1]
                    topic_s = topic.decode(errors='replace') if topic else '<no-topic>'
                    # print short payload preview
                    payload_preview = repr(payload[:200])
                    payload_full = repr(payload)
                    if len(payload) > 200:
                        payload_preview += '...'
                    # print(f"SAMPLE #{total}: topic={topic_s} len={len(payload)} payload_preview={payload_preview}")
                    print(f"SAMPLE #{total}: topic={topic_s} len={len(payload)} payload={payload_full}")
                except Exception as e:
                    print('Error printing sample message:', e)

            # print per-interval stats (messages per second)
            now = time.time()
            if now - last_time >= args.stats_interval:
                interval = now - last_time
                delta = total - last_total
                mps = delta / interval if interval > 0 else 0.0
                # print a short single-line status
                print(f"stats: total={total} mps_recent={mps:.2f}")
                last_time = now
                last_total = total

    except KeyboardInterrupt:
        print('\nInterrupted by user')
    finally:
        try:
            sock.close()
        except Exception:
            pass
        try:
            ctx.term()
        except Exception:
            pass


if __name__ == '__main__':
    main()
