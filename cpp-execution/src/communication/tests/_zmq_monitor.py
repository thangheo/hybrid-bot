import zmq
import time


context = zmq.Context()
intel_socket = context.socket(zmq.SUB)
intel_socket.connect("tcp://localhost:5555")  # Connect to intel endpoint
intel_socket.setsockopt(zmq.SUBSCRIBE, b"MARKET_INTEL")  # Subscribe to intel topic

# Similarly for internal socket
internal_socket = context.socket(zmq.SUB)
internal_socket.connect("tcp://localhost:5557")
internal_socket.setsockopt(zmq.SUBSCRIBE, b"orderbook")  # Or whatever topic orderbook uses


# write a loop to receive messages from both sockets
def receive_messages():
    poller = zmq.Poller()
    poller.register(intel_socket, zmq.POLLIN)
    poller.register(internal_socket, zmq.POLLIN)

    try:
        while True:
            socks = dict(poller.poll(1000))  # 1 second timeout

            if intel_socket in socks and socks[intel_socket] == zmq.POLLIN:
                topic = intel_socket.recv()
                message = intel_socket.recv()
                print(f"Received from Intel: topic={topic}, message={message[:100]}...")

            if internal_socket in socks and socks[internal_socket] == zmq.POLLIN:
                topic = internal_socket.recv()
                message = internal_socket.recv()
                print(f"Received from Internal: topic={topic}, message={message[:100]}...")

    except KeyboardInterrupt:
        print("Interrupted")
    finally:
        intel_socket.close()
        internal_socket.close()
        context.term()
        print("Sockets closed, context terminated")
        print("Done")

# ctx = zmq.Context()
# sock = ctx.socket(zmq.SUB)
# sock.connect("tcp://127.0.0.1:5555")
# # Subscribe to all topics
# sock.setsockopt(zmq.SUBSCRIBE, b"")
# # print("Subscribed to all topics on tcp://127.0.0.1:5557")
# start = time.time()
# count = 0
# try:
#     while time.time() - start < 1111:
#         try:
#             topic = sock.recv(flags=0)
#             data = sock.recv(flags=0)
#             print(f"FRAME #{count}: topic={topic!r} data_len={len(data)}")
#             # show start of data
#             print(data[:200])
#             count += 1
#         except zmq.Again:
#             # no message yet
#             time.sleep(0.1)
# except KeyboardInterrupt:
#     pass
# finally:
#     sock.close()
#     ctx.term()
#     print(f"Done - received {count} frames")
