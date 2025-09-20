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


if __name__ == "__main__":
    receive_messages()