import zmq
import time

ctx = zmq.Context()
sock = ctx.socket(zmq.SUB)
sock.connect("tcp://127.0.0.1:5555")
# Subscribe to all topics
sock.setsockopt(zmq.SUBSCRIBE, b"")
# print("Subscribed to all topics on tcp://127.0.0.1:5557")
start = time.time()
count = 0
try:
    while time.time() - start < 1111:
        try:
            topic = sock.recv(flags=0)
            data = sock.recv(flags=0)
            print(f"FRAME #{count}: topic={topic!r} data_len={len(data)}")
            # show start of data
            print(data[:200])
            count += 1
        except zmq.Again:
            # no message yet
            time.sleep(0.1)
except KeyboardInterrupt:
    pass
finally:
    sock.close()
    ctx.term()
    print(f"Done - received {count} frames")
