#!/usr/bin/env python3
"""
Capture MARKET_INTEL payloads and save any that fail protobuf parsing.
Usage: python capture_intel_payloads.py [count]
"""
import sys, os, time
import zmq
from trading_messages_pb2 import MarketIntelligence, TradingMessage
from google.protobuf.message import DecodeError

count = int(sys.argv[1]) if len(sys.argv) > 1 else 10
out_dir = '/tmp/zmq_intel_capture'
os.makedirs(out_dir, exist_ok=True)

ctx = zmq.Context()
sock = ctx.socket(zmq.SUB)
sock.connect('tcp://localhost:5560')
sock.setsockopt(zmq.SUBSCRIBE, b'')
sock.setsockopt(zmq.RCVTIMEO, 5000)

saved = 0
received = 0
try:
    while received < count:
        try:
            parts = sock.recv_multipart()
        except Exception as e:
            print('recv error', e)
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
            # Save payload for offline inspection
            fname = os.path.join(out_dir, f'failed_payload_{int(time.time()*1000)}_{received}.bin')
            with open(fname, 'wb') as f:
                f.write(payload)
            print('  Saved failing payload to', fname)
            saved += 1

finally:
    sock.close()
    ctx.term()

print(f'Done. received={received} saved={saved}')
