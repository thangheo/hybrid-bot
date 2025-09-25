#!/usr/bin/env python3
"""
ZMQ subscriber for orderbook protobuf messages.
Receives raw protobuf binary messages from the C++ trading system.

Usage:
  python orderbook_msg_test.py [--uri tcp://localhost:5557] [--print-every 10]
"""
import time
import zmq
import argparse
import internal_orderbook_pb2 as orderbook_pb2


def print_orderbook_sample(msg, topic, msg_num):
    """Print a formatted sample of the orderbook message"""
    print(f"\n=== SAMPLE #{msg_num}: {topic} ===")
    print(f"Exchange: {msg.data.exchange}")
    print(f"Symbol: {msg.data.symbol}")
    print(f"Price: {msg.data.price:.8f}")
    print(f"Bid: {msg.data.bid:.8f} | Ask: {msg.data.ask:.8f}")
    print(f"Volume: {msg.data.volume:.2f}")
    print(f"Sequence: {msg.data.sequence}")
    print(f"Timestamp: {msg.data.timestamp}")

    # Show top bids/asks
    if msg.data.bids:
        print("Top Bids:")
        for i, bid in enumerate(msg.data.bids[:3]):
            print(f"  {bid.price:.8f} @ {bid.quantity:.4f}")

    if msg.data.asks:
        print("Top Asks:")
        for i, ask in enumerate(msg.data.asks[:3]):
            print(f"  {ask.price:.8f} @ {ask.quantity:.4f}")

    if msg.HasField('metadata'):
        print(f"Metadata: {msg.metadata.correlation_id} | {msg.metadata.priority}")


def main():
    parser = argparse.ArgumentParser(description='Orderbook protobuf message subscriber')
    parser.add_argument('--uri', default='tcp://localhost:5557', help='ZMQ endpoint')
    parser.add_argument('--topic-prefix', default='orderbook.', help='Topic prefix to subscribe to')
    parser.add_argument('--print-every', type=int, default=1000, help='Print every N messages')
    parser.add_argument('--timeout', type=int, default=30, help='Timeout in seconds')

    args = parser.parse_args()

    # Setup ZMQ subscriber
    ctx = zmq.Context()
    sock = ctx.socket(zmq.SUB)
    sock.setsockopt(zmq.SUBSCRIBE, args.topic_prefix.encode())
    sock.connect(args.uri)

    print(f"Connected to {args.uri}")
    print(f"Subscribed to topics: {args.topic_prefix}*")
    print("Waiting for messages...")
    print("-" * 50)

    total_msgs = 0
    successful_parses = 0
    errors = 0
    start_time = time.time()

    try:
        while True:
            # Receive message with timeout
            if sock.poll(1000):  # 1 second timeout
                parts = sock.recv_multipart()
                total_msgs += 1

                try:
                    # Extract topic and payload
                    if len(parts) == 2:
                        topic = parts[0].decode()
                        payload = parts[1]
                    else:
                        topic = "unknown"
                        payload = parts[0]

                    # Parse raw protobuf data directly
                    protobuf_data = payload  # payload is already bytes

                    # Parse protobuf
                    msg = orderbook_pb2.OrderbookMessage()
                    if msg.ParseFromString(protobuf_data):
                        successful_parses += 1

                        # Print sample messages every 1000 count
                        if total_msgs % args.print_every == 0:
                            
                            print_orderbook_sample(msg, topic, total_msgs)
                    else:
                        errors += 1
                        print(f"Failed to parse protobuf in message #{total_msgs}")

                except Exception as e:
                    errors += 1
                    print(f"Error processing message #{total_msgs}: {e}")

            # Print stats every 5 seconds
            elapsed = time.time() - start_time
            if int(elapsed) % 5 == 0 and int(elapsed) > 0:
                success_rate = (successful_parses / total_msgs * 100) if total_msgs > 0 else 0
                print(f"Stats: {total_msgs} received, {successful_parses} parsed ({success_rate:.1f}%), {errors} errors")

            # Exit after timeout if no messages
            if elapsed > args.timeout and total_msgs == 0:
                print(f"No messages received after {args.timeout} seconds. Exiting.")
                break

    except KeyboardInterrupt:
        print("\nInterrupted by user")

    finally:
        sock.close()
        ctx.term()

        # Final stats
        if total_msgs > 0:
            success_rate = successful_parses / total_msgs * 100
            print("\nFinal Stats:")
            print(f"  Total messages: {total_msgs}")
            print(f"  Successfully parsed: {successful_parses} ({success_rate:.1f}%)")
            print(f"  Errors: {errors}")


if __name__ == '__main__':
    main()