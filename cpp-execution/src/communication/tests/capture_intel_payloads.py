#!/usr/bin/env python3
"""
Capture MARKET_INTEL payloads and save any that fail protobuf parsing.
This version is robust and intended to run forever. It will:
 - keep the socket alive and reconnect on errors
 - not exit on receive timeouts (it will continue polling)
 - save failing payloads to files under the output directory
 - print periodic statistics (messages/sec, saved count)

Usage: python capture_intel_payloads.py [--out-dir DIR] [--bind URI]
"""
import argparse
import os
import time
import sys
import zmq
from trading_messages_pb2 import MarketIntelligence, TradingMessage
from google.protobuf.message import DecodeError


def create_socket(ctx, uri):
    sock = ctx.socket(zmq.SUB)
    sock.setsockopt(zmq.SUBSCRIBE, b"")
    sock.connect(uri)
    return sock


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--out-dir', default='/tmp/zmq_intel_capture')
    p.add_argument('--uri', default='tcp://localhost:5560', help='ZMQ connect URI or bind URI')
    p.add_argument('--bind', action='store_true', help='Bind instead of connect (useful for testing)')
    p.add_argument('--stats-interval', type=float, default=5.0, help='seconds between stats prints')
    p.add_argument('--debug', action='store_true', help='Enable verbose debug output')
    args = p.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)

    ctx = zmq.Context()
    sock = None
    received = 0
    saved = 0
    start_time = time.time()
    last_stats_time = start_time
    last_received = 0

    try:
        # create and connect/bind socket
        sock = ctx.socket(zmq.SUB)
        sock.setsockopt(zmq.SUBSCRIBE, b"")
        if args.bind:
            sock.bind(args.uri)
            action = 'bound to'
        else:
            sock.connect(args.uri)
            action = 'connected to'
        print(f'{action} {args.uri}, saving failing payloads to {args.out_dir}')
        if args.debug:
            try:
                # print some socket info for debugging
                last = sock.getsockopt(zmq.LAST_ENDPOINT)
            except Exception:
                last = b''
            try:
                events = sock.getsockopt(zmq.EVENTS)
            except Exception:
                events = 0
            print('DEBUG: LAST_ENDPOINT=', last, 'EVENTS=', events)

        while True:
            # use the old blocking recv method — this special socket needs it
            time.sleep(0.001)
            if args.debug:
                print('DEBUG: calling recv_multipart() at', time.time())
            try:
                parts = sock.recv_multipart()
            except KeyboardInterrupt:
                print('\nInterrupted by user')
                break
            except Exception as e:
                # original behavior was to break on recv errors
                print('recv error', repr(e))
                if args.debug:
                    try:
                        print('DEBUG: socket EVENTS=', sock.getsockopt(zmq.EVENTS))
                    except Exception:
                        pass
                break

            received += 1
            payload = parts[0] if len(parts) == 1 else parts[1]
            print(f"#%d frames=%d len=%d" % (received, len(parts), len(payload)))
            try:
                mi = MarketIntelligence()
                mi.ParseFromString(payload)
                print('  Parsed MarketIntelligence symbol=', repr(mi.symbol))
            except DecodeError:
                print('  Failed to parse as MarketIntelligence')
                # Try wrapper TradingMessage
                try:
                    tm = TradingMessage()
                    tm.ParseFromString(payload)
                    print('  Parsed as TradingMessage, message_type=', tm.message_type)
                    if tm.HasField('market_intelligence'):
                        print('   contains market_intelligence symbol=', repr(tm.market_intelligence.symbol))
                        continue
                except Exception:
                    pass
                # Save failing payload to file
                fname = os.path.join(args.out_dir, f'payload_{received}_{int(time.time()*1000)}.bin')
                try:
                    with open(fname, 'wb') as f:
                        f.write(payload)
                    print('  Saved failing payload to', fname)
                except Exception as e:
                    print('  Failed to save payload to', fname, 'error:', e)
                # print msg/s
                if received > 0:
                    print(f"  Received {received} messages, {saved} saved ({(saved/received)*100:.2f}%)")
                saved += 1

    finally:
        try:
            if sock is not None:
                sock.close()
        except Exception:
            pass
        try:
            ctx.term()
        except Exception:
            pass


if __name__ == '__main__':
    main()

