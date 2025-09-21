import zmq
from trading_messages_pb2 import TradingMessage
from google.protobuf.message import DecodeError

def main():
    context = zmq.Context()
    socket = context.socket(zmq.SUB)
    socket.connect("tcp://localhost:5560")
    socket.setsockopt(zmq.SUBSCRIBE, b"")  # Subscribe to all topics
    print("Subscribed to tcp://localhost:5560")
    while True:
        protobuf_bytes = socket.recv()
        msg = TradingMessage()
        try:
            msg.ParseFromString(protobuf_bytes)
            topic = getattr(msg, "topic", "unknown")
            print("Received message, topic:", topic)
            print("Data:", getattr(msg, "data", None))
        except DecodeError as e:    
            print("Failed to parse protobuf message:", e)

if __name__ == "__main__":
    main()