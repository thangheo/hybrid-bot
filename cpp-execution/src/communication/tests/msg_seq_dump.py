#!/usr/bin/env python3
"""
Lightweight debug subscriber: prints symbol, sequence, delta, and flags for large jumps/restarts.
Use this to inspect how `sequence` behaves per-symbol (counter vs timestamp) and spot restarts.

Example:
  python3 msg_seq_dump.py --endpoint tcp://127.0.0.1:5557 --topic orderbook. --count 200
"""
import argparse
import time
import zmq
from internal_orderbook_pb2 import OrderbookMessage


def run(endpoint, topic_filter, count):
    ctx = zmq.Context()
    s = ctx.socket(zmq.SUB)
    s.connect(endpoint)
    try:
        s.setsockopt_string(zmq.SUBSCRIBE, topic_filter)
    except Exception:
        s.setsockopt(zmq.SUBSCRIBE, topic_filter.encode('utf-8') if isinstance(topic_filter, str) else topic_filter)

    seen = 0
    last_seq = {}
    print("symbol,seq,delta,flag,topic")
    while seen < count:
        try:
            parts = s.recv_multipart()
        except Exception:
            continue
        if not parts:
            continue
        topic = parts[0]
        data = parts[1] if len(parts) > 1 else b''
        try:
            topic_str = topic.decode('utf-8')
        except Exception:
            topic_str = repr(topic)

        msg = OrderbookMessage()
        try:
            msg.ParseFromString(data)
        except Exception:
            print(f"<decode error>,, , ,{topic_str}")
            continue

        symbol = ''
        try:
            symbol = msg.data.symbol if msg.data.symbol else ''
        except Exception:
            symbol = ''
        if not symbol:
            # fallback to topic suffix
            if topic_str.startswith('orderbook.'):
                symbol = topic_str.split('orderbook.',1)[1]
            else:
                symbol = topic_str

        seq = None
        try:
            seq = int(msg.data.sequence)
        except Exception:
            seq = None

        flag = ''
        delta = ''
        if seq is not None:
            last = last_seq.get(symbol)
            if last is None:
                flag = 'first'
            else:
                delta_val = seq - last
                delta = str(delta_val)
                # heuristics
                if delta_val == 0:
                    flag = 'dup'
                elif delta_val == 1:
                    flag = 'ok'
                elif delta_val > 1 and delta_val < 1000:
                    flag = 'gap'
                elif delta_val >= 1000:
                    flag = 'largejump'
                else:
                    flag = 'back'
            last_seq[symbol] = seq

        print(f"{symbol},{seq if seq is not None else ''},{delta},{flag},{topic_str}")
        seen += 1


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--endpoint','-e', default='tcp://127.0.0.1:5557')
    p.add_argument('--topic','-t', default='orderbook.')
    p.add_argument('--count','-n', type=int, default=200)
    args = p.parse_args()
    run(args.endpoint, args.topic, args.count)

if __name__ == '__main__':
    main()
