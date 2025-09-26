# light_sub.py
import zmq, time, sys

ctx = zmq.Context()
s = ctx.socket(zmq.SUB)
s.connect("tcp://127.0.0.1:5557")    # chỉnh endpoint nếu cần
s.setsockopt_string(zmq.SUBSCRIBE, "orderbook.")
count = 0
start = time.time()
report_interval = 5.0
while True:
    topic = s.recv_string()
    msg = s.recv()   # read full message (binary)
    count += 1
    now = time.time()
    if now - start >= report_interval:
        print("rate msg/s:", count / (now - start))
        count = 0
        start = now