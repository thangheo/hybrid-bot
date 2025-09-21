#!/usr/bin/env python3
"""
ZMQ Debug Subscriber for MARKET_INTEL on port 5560
This script helps diagnose why MARKET_INTEL messages aren't being received
"""

import zmq
import time
import json
import threading
import sys
from typing import Optional

def test_basic_connection():
    """Test 1: Basic connection test"""
    print("=== TEST 1: Basic Connection Test ===")
    try:
        context = zmq.Context()
        socket = context.socket(zmq.SUB)
        socket.connect("tcp://localhost:5560")
        socket.setsockopt(zmq.SUBSCRIBE, b"")  # Subscribe to all messages
        socket.setsockopt(zmq.RCVTIMEO, 5000)  # 5 second timeout
        
        print("✓ Connected to tcp://localhost:5560")
        print("✓ Subscribed to all topics")
        print("Waiting 5 seconds for any message...")
        
        try:
            message = socket.recv_multipart(zmq.NOBLOCK)
            print(f"✓ Received message: {len(message)} parts")
            for i, part in enumerate(message):
                print(f"  Part {i}: {part[:100]}...")  # First 100 bytes
        except zmq.Again:
            print("✗ No messages received in 5 seconds")
        
        socket.close()
        context.term()
        
    except Exception as e:
        print(f"✗ Connection test failed: {e}")

def test_market_intel_subscription():
    """Test 2: Specific MARKET_INTEL subscription"""
    print("\n=== TEST 2: MARKET_INTEL Subscription Test ===")
    try:
        context = zmq.Context()
        socket = context.socket(zmq.SUB)
        socket.connect("tcp://localhost:5560")
        
        # Try different subscription patterns
        subscriptions = [
            b"MARKET_INTEL",
            b"MARKET_INTEL",  # Exact match
            b"MARKET",        # Partial match
            b"",              # All messages
        ]
        
        for sub in subscriptions:
            print(f"Testing subscription pattern: {sub}")
            socket.setsockopt(zmq.SUBSCRIBE, sub)
            socket.setsockopt(zmq.RCVTIMEO, 2000)  # 2 second timeout
            
            try:
                message = socket.recv_multipart(zmq.NOBLOCK)
                print(f"✓ Received with pattern '{sub}': {len(message)} parts")
                if len(message) >= 2:
                    topic = message[0].decode('utf-8', errors='ignore')
                    data = message[1].decode('utf-8', errors='ignore')[:200]
                    print(f"  Topic: {topic}")
                    print(f"  Data preview: {data}...")
                break
            except zmq.Again:
                print(f"✗ No messages with pattern '{sub}'")
            
            # Clear subscription
            socket.setsockopt(zmq.UNSUBSCRIBE, sub)
        
        socket.close()
        context.term()
        
    except Exception as e:
        print(f"✗ MARKET_INTEL subscription test failed: {e}")

def test_socket_options():
    """Test 3: Check socket options and states"""
    print("\n=== TEST 3: Socket Options Test ===")
    try:
        context = zmq.Context()
        socket = context.socket(zmq.SUB)
        
        print("Socket options before connect:")
        print(f"  RCVHWM: {socket.getsockopt(zmq.RCVHWM)}")
        print(f"  RCVTIMEO: {socket.getsockopt(zmq.RCVTIMEO)}")
        print(f"  CONFLATE: {socket.getsockopt(zmq.CONFLATE)}")
        
        socket.connect("tcp://localhost:5560")
        socket.setsockopt(zmq.SUBSCRIBE, b"MARKET_INTEL")
        
        print("Socket options after connect:")
        print(f"  RCVHWM: {socket.getsockopt(zmq.RCVHWM)}")
        print(f"  EVENTS: {socket.getsockopt(zmq.EVENTS)}")
        
        # Set more permissive options
        socket.setsockopt(zmq.RCVHWM, 1000)
        socket.setsockopt(zmq.CONFLATE, 0)  # Don't conflate on subscriber
        
        print("Waiting 3 seconds after option changes...")
        time.sleep(3)
        
        try:
            message = socket.recv_multipart(zmq.NOBLOCK)
            print(f"✓ Received after option changes: {len(message)} parts")
        except zmq.Again:
            print("✗ Still no messages after option changes")
        
        socket.close()
        context.term()
        
    except Exception as e:
        print(f"✗ Socket options test failed: {e}")

def test_concurrent_subscriber():
    """Test 4: Long-running subscriber with stats"""
    print("\n=== TEST 4: Long-running Subscriber Test ===")
    
    def subscriber_worker(duration_seconds=10):
        context = zmq.Context()
        socket = context.socket(zmq.SUB)
        socket.connect("tcp://localhost:5560")
        socket.setsockopt(zmq.SUBSCRIBE, b"")  # All messages
        socket.setsockopt(zmq.RCVTIMEO, 1000)  # 1 second timeout
        
        message_count = 0
        market_intel_count = 0
        start_time = time.time()
        
        print(f"Running subscriber for {duration_seconds} seconds...")
        
        while time.time() - start_time < duration_seconds:
            try:
                message = socket.recv_multipart(zmq.NOBLOCK)
                message_count += 1
                
                if len(message) >= 1:
                    topic = message[0].decode('utf-8', errors='ignore')
                    if topic == "MARKET_INTEL":
                        market_intel_count += 1
                        print(f"✓ MARKET_INTEL #{market_intel_count} received")
                        if len(message) >= 2:
                            try:
                                # Try to parse the data
                                data_str = message[1].decode('utf-8', errors='ignore')
                                if data_str.startswith('{'):
                                    data = json.loads(data_str)
                                    print(f"  Parsed JSON keys: {list(data.keys()) if isinstance(data, dict) else 'not dict'}")
                                else:
                                    print(f"  Raw data preview: {data_str[:100]}...")
                            except json.JSONDecodeError:
                                print(f"  Non-JSON data: {message[1][:50]}...")
                    else:
                        print(f"  Other topic: {topic}")
                
            except zmq.Again:
                continue  # Timeout, keep trying
            except Exception as e:
                print(f"Error receiving message: {e}")
        
        print(f"Subscriber stats:")
        print(f"  Total messages: {message_count}")
        print(f"  MARKET_INTEL messages: {market_intel_count}")
        print(f"  Duration: {time.time() - start_time:.1f}s")
        
        socket.close()
        context.term()
    
    subscriber_worker(10)

def test_port_connectivity():
    """Test 5: Basic network connectivity"""
    print("\n=== TEST 5: Port Connectivity Test ===")
    import socket as sock
    
    # Test if port 5560 is open
    try:
        s = sock.socket(sock.AF_INET, sock.SOCK_STREAM)
        s.settimeout(5)
        result = s.connect_ex(('localhost', 5560))
        s.close()
        
        if result == 0:
            print("✓ Port 5560 is open and accepting connections")
        else:
            print("✗ Port 5560 is not accepting TCP connections")
            print("  This might be normal for ZMQ PUB sockets")
    except Exception as e:
        print(f"✗ Port connectivity test failed: {e}")

def test_comparison_with_5557():
    """Test 6: Compare with working port 5557"""
    print("\n=== TEST 6: Comparison with Port 5557 ===")
    
    ports = [5560, 5557]
    for port in ports:
        print(f"\nTesting port {port}:")
        try:
            context = zmq.Context()
            socket = context.socket(zmq.SUB)
            socket.connect(f"tcp://localhost:{port}")
            socket.setsockopt(zmq.SUBSCRIBE, b"")  # All messages
            socket.setsockopt(zmq.RCVTIMEO, 3000)  # 3 second timeout
            
            message_count = 0
            start_time = time.time()
            
            while time.time() - start_time < 3:
                try:
                    message = socket.recv_multipart(zmq.NOBLOCK)
                    message_count += 1
                    if len(message) >= 1:
                        topic = message[0].decode('utf-8', errors='ignore')
                        print(f"  Message {message_count}: {topic}")
                        if message_count >= 5:  # Limit output
                            break
                except zmq.Again:
                    continue
            
            print(f"  Total messages received: {message_count}")
            socket.close()
            context.term()
            
        except Exception as e:
            print(f"  Error on port {port}: {e}")

# ...existing code...
def subscriber_worker(duration_seconds=10):
    context = zmq.Context()
    socket = context.socket(zmq.SUB)
    socket.connect("tcp://localhost:5560")
    socket.setsockopt(zmq.SUBSCRIBE, b"")  # receive everything
    socket.setsockopt(zmq.RCVTIMEO, 1000)

    message_count = 0
    market_intel_count = 0
    start_time = time.time()

    print(f"Running subscriber for {duration_seconds} seconds...")

    while time.time() - start_time < duration_seconds:
        try:
            parts = socket.recv_multipart(flags=0)  # returns list, len==1 for single-frame
            message_count += 1

            def short_hex(b): return b.hex()[:200]

            # Debug info
            print(f"[recv] frames={len(parts)} len0={len(parts[0])} len1={(len(parts[1]) if len(parts)>1 else 0)}")
            print("  part0(hex):", short_hex(parts[0]))
            if len(parts) > 1:
                print("  part1(hex):", short_hex(parts[1]))

            if len(parts) == 1:
                # single-frame: try parsing protobuf, then JSON/text fallback
                payload = parts[0]
                try:
                    intel = tm.MarketIntelligence()
                    intel.ParseFromString(payload)
                    print("  Parsed protobuf, symbol:", getattr(intel, "symbol", None))
                    market_intel_count += 1
                except Exception:
                    try:
                        txt = payload.decode("utf-8", errors="ignore")
                        if txt.startswith("{"):
                            j = json.loads(txt)
                            print("  Parsed JSON keys:", list(j.keys()))
                        else:
                            print("  Raw text preview:", txt[:200])
                    except Exception as e:
                        print("  Unknown payload parse error:", e)

            else:
                # multipart: first part is topic, second is payload
                topic = parts[0].decode("utf-8", errors="ignore")
                payload = parts[1]
                print("  topic:", repr(topic))
                if topic == "MARKET_INTEL":
                    try:
                        intel = tm.MarketIntelligence()
                        intel.ParseFromString(payload)
                        print("  Parsed protobuf for MARKET_INTEL symbol:", getattr(intel, "symbol", None))
                        market_intel_count += 1
                    except Exception:
                        print("  Failed to parse payload for MARKET_INTEL; payload hex preview:", short_hex(payload))
                else:
                    print("  Other topic:", topic)

        except zmq.Again:
            continue
        except Exception as e:
            print("Error receiving message:", e)

    print(f"Subscriber stats: total={message_count} MARKET_INTEL={market_intel_count}")
    socket.close()
    context.term()
# ...existing code...

def main():
    print("ZMQ Debug Tool for MARKET_INTEL Issue")
    print("=" * 50)
    
    if len(sys.argv) > 1:
        test_num = int(sys.argv[1])
        tests = [
            test_basic_connection,
            test_market_intel_subscription, 
            test_socket_options,
            test_concurrent_subscriber,
            test_port_connectivity,
            test_comparison_with_5557,
            subscriber_worker
        ]
        if 1 <= test_num <= len(tests):
            tests[test_num - 1]()
        else:
            print(f"Invalid test number. Choose 1-{len(tests)}")
    else:
        # Run all tests
        test_basic_connection()
        test_market_intel_subscription()
        test_socket_options()
        test_port_connectivity()
        test_comparison_with_5557()
        
        print("\n" + "=" * 50)
        print("All basic tests completed.")
        print("Run with argument '4' for long-running test:")
        print("python zmq_debug_subscriber.py 4")

if __name__ == "__main__":
    main()