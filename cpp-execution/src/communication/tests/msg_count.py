# light_sub.py
import zmq, time, sys

ctx = zmq.Context()
s = ctx.socket(zmq.SUB)
s.connect("tcp://127.0.0.1:5557")    # chỉnh endpoint nếu cần
# subscribe to topic prefix as bytes-safe (some publishers send non-utf8 topics)
try:
    s.setsockopt_string(zmq.SUBSCRIBE, "orderbook.")
except Exception:
    # fallback: set raw bytes subscription
    s.setsockopt(zmq.SUBSCRIBE, b"orderbook.")

count = 0
start = time.time()
report_interval = 5.0
while True:
    try:
        parts = s.recv_multipart()
    except Exception:
        # transient recv error, continue
        continue

    if not parts:
        continue

    # parts[0] is topic (bytes). Try to decode but tolerate non-utf8
    topic = parts[0]
    try:
        topic_str = topic.decode('utf-8')
    except Exception:
        topic_str = repr(topic)

    # payload may be in parts[1]
    msg = parts[1] if len(parts) > 1 else b''
    count += 1
    now = time.time()
    if now - start >= report_interval:
        print("rate msg/s:", count / (now - start))
        count = 0
        start = now