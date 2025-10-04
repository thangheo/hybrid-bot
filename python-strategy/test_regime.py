import zmq
ctx = zmq.Context()
sock = ctx.socket(zmq.SUB)
sock.connect("tcp://localhost:5555")
sock.setsockopt_string(zmq.SUBSCRIBE, "regimes_update")
print("Subscribed. Waiting for messages...")
while True:
    msg = sock.recv_string()
    print("RECV:", msg[:200])  # print a snippet