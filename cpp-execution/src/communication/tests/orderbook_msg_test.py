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
    parser.add_argument('--uri', default='inproc://orderbook', help='ZMQ endpoint')
    parser.add_argument('--topic-prefix', default='orderbook.', help='Topic prefix to subscribe to')
    parser.add_argument('--print-every', type=int, default=1000, help='Print every N messages')
    parser.add_argument('--timeout', type=int, default=30, help='Timeout in seconds')
    parser.add_argument('--verbose', action='store_true', help='Print debug info for received frames')
    parser.add_argument('--fallback-delay', type=int, default=3, help='Seconds to wait before auto-subscribing to all topics when nothing is received')

    args = parser.parse_args()

    # Setup ZMQ subscriber
    ctx = zmq.Context()
    sock = ctx.socket(zmq.SUB)
    # Subscribe to the provided prefix. For debugging you can pass an empty
    # string to subscribe to all topics: --topic-prefix ""
    sock.setsockopt_string(zmq.SUBSCRIBE, args.topic_prefix)
    sock.connect(args.uri)
    # Use a Poller for reliable event detection
    poller = zmq.Poller()
    poller.register(sock, zmq.POLLIN)

    print(f"Connected to {args.uri}")
    print(f"Subscribed to topics: {args.topic_prefix}*")
    print("Waiting for messages...")
    print("-" * 50)

    total_msgs = 0
    successful_parses = 0
    errors = 0
    start_time = time.time()
    overall_start = start_time
    fallback_done = False

    try:
        while True:
            # Wait for incoming messages with timeout via Poller
            events = dict(poller.poll(1000))  # timeout in ms
            # If we've seen no messages for a short while, fallback to subscribe-all
            overall_elapsed = time.time() - overall_start
            if not fallback_done and overall_elapsed > args.fallback_delay and total_msgs == 0:
                print(f"No messages seen for {args.fallback_delay}s — auto-subscribing to all topics for debugging")
                sock.setsockopt_string(zmq.SUBSCRIBE, "")
                fallback_done = True

            if sock in events and events[sock] & zmq.POLLIN:
                parts = sock.recv_multipart()
                total_msgs += 1

                try:
                    if args.verbose:
                        print(f"DEBUG: received multipart with {len(parts)} frames")
                    # Topic is usually the first frame; payload is the last frame
                    topic = parts[0].decode() if len(parts) >= 2 else ""
                    payload = parts[-1]

                    if args.verbose:
                        sample = payload[:64]
                        # show hex sample for binary safety
                        print(f"DEBUG: topic='{topic}' payload_len={len(payload)} sample={sample.hex()}")

                    # Parse protobuf from payload bytes
                    protobuf_data = payload
                    msg = orderbook_pb2.OrderbookMessage()
                    # ParseFromString does not reliably return a boolean across
                    # implementations, so call it and then check message size.
                    msg.ParseFromString(protobuf_data)
                    if msg.ByteSize() > 0:
                        successful_parses += 1
                        # Print sample messages every N messages
                        if total_msgs % args.print_every == 0:
                            print_orderbook_sample(msg, topic, total_msgs)
                    else:
                        errors += 1
                        print(f"Failed to parse protobuf in message #{total_msgs}")

                except Exception as e:
                    errors += 1
                    print(f"Error processing message #{total_msgs}: {e}")

            # Print stats every 5 seconds (debounced)
            elapsed = time.time() - start_time
            if elapsed >= 5:
                start_time = time.time()
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