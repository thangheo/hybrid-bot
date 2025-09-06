#!/usr/bin/env python3
"""
Data Ingestion Service Tester
Tests and monitors the C++ DataIngestionService via ZeroMQ
"""

import zmq
import json
import time
import threading
import signal
import sys
from datetime import datetime
from collections import defaultdict, deque
import argparse

class DataIngestionTester:
    def __init__(self, zmq_endpoint="tcp://localhost:5555", verbose=False):
        self.zmq_endpoint = zmq_endpoint
        self.verbose = verbose
        self.running = False
        
        # Statistics
        self.stats = {
            'total_messages': 0,
            'messages_per_exchange': defaultdict(int),
            'messages_per_symbol': defaultdict(int),
            'errors': 0,
            'start_time': None,
            'last_message_time': None
        }
        
        # Performance monitoring
        self.message_timestamps = deque(maxlen=1000)  # Keep last 1000 timestamps
        
        # ZeroMQ setup
        self.context = zmq.Context()
        self.socket = None
        
        # Threading
        self.listener_thread = None
        self.stats_thread = None
        
        # Signal handling
        signal.signal(signal.SIGINT, self.signal_handler)
        signal.signal(signal.SIGTERM, self.signal_handler)
    
    def signal_handler(self, signum, frame):
        print(f"\nReceived signal {signum}, shutting down...")
        self.stop()
        sys.exit(0)
    
    def connect(self):
        """Connect to the ZeroMQ publisher"""
        try:
            self.socket = self.context.socket(zmq.SUB)
            self.socket.connect(self.zmq_endpoint)
            
            # Subscribe to all messages (empty filter)
            self.socket.setsockopt(zmq.SUBSCRIBE, b"")
            
            # Set receive timeout
            self.socket.setsockopt(zmq.RCVTIMEO, 1000)  # 1 second timeout
            
            print(f"✅ Connected to {self.zmq_endpoint}")
            return True
            
        except zmq.ZMQError as e:
            print(f"❌ Failed to connect to {self.zmq_endpoint}: {e}")
            return False
    
    def start(self):
        """Start the tester"""
        if not self.connect():
            return False
        
        self.running = True
        self.stats['start_time'] = time.time()
        
        # Start listener thread
        self.listener_thread = threading.Thread(target=self.message_listener, daemon=True)
        self.listener_thread.start()
        
        # Start stats thread
        self.stats_thread = threading.Thread(target=self.stats_reporter, daemon=True)
        self.stats_thread.start()
        
        print("🚀 DataIngestionTester started")
        return True
    
    def stop(self):
        """Stop the tester"""
        self.running = False
        
        if self.socket:
            self.socket.close()
        
        if self.context:
            self.context.term()
        
        print("🛑 DataIngestionTester stopped")
    
    def message_listener(self):
        """Listen for messages from the C++ service"""
        while self.running:
            try:
                # Receive message with timeout
                message = self.socket.recv_string(zmq.NOBLOCK)
                self.process_message(message)
                
            except zmq.Again:
                # Timeout - no message received
                continue
            except zmq.ZMQError as e:
                if self.running:  # Only log if we're still supposed to be running
                    print(f"❌ ZMQ Error: {e}")
                    self.stats['errors'] += 1
                break
            except Exception as e:
                print(f"❌ Error processing message: {e}")
                self.stats['errors'] += 1
    
    def process_message(self, message):
        """Process a received message"""
        try:
            # Parse the message (assuming JSON format)
            data = json.loads(message)
            
            # Update statistics
            self.stats['total_messages'] += 1
            self.stats['last_message_time'] = time.time()
            self.message_timestamps.append(time.time())
            
            # Extract info from message
            exchange = data.get('exchangeId', 'unknown')
            symbol = data.get('symbol', 'unknown')
            
            self.stats['messages_per_exchange'][exchange] += 1
            self.stats['messages_per_symbol'][symbol] += 1
            
            if self.verbose:
                self.print_message_details(data)
                
        except json.JSONDecodeError:
            # Maybe it's a different format - just count it
            self.stats['total_messages'] += 1
            self.stats['last_message_time'] = time.time()
            self.message_timestamps.append(time.time())
            
            if self.verbose:
                print(f"📦 Raw message: {message[:100]}{'...' if len(message) > 100 else ''}")
        
        except Exception as e:
            print(f"❌ Error processing message: {e}")
            self.stats['errors'] += 1
    
    def print_message_details(self, data):
        """Print detailed message information"""
        timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        
        print(f"\n[{timestamp}] 📨 Order Book Update")
        print(f"  Exchange: {data.get('exchangeId', 'N/A')}")
        print(f"  Symbol: {data.get('symbol', 'N/A')}")
        print(f"  Sequence: {data.get('sequenceNumber', 'N/A')}")
        print(f"  Is Snapshot: {data.get('isSnapshot', 'N/A')}")
        
        bids = data.get('bids', [])
        asks = data.get('asks', [])
        
        print(f"  Bids: {len(bids)}, Asks: {len(asks)}")
        
        if bids and asks:
            best_bid = bids[0] if isinstance(bids[0], dict) else {'price': bids[0], 'quantity': 'N/A'}
            best_ask = asks[0] if isinstance(asks[0], dict) else {'price': asks[0], 'quantity': 'N/A'}
            
            print(f"  Best Bid: {best_bid.get('price', 'N/A')} @ {best_bid.get('quantity', 'N/A')}")
            print(f"  Best Ask: {best_ask.get('price', 'N/A')} @ {best_ask.get('quantity', 'N/A')}")
            
            try:
                spread = float(best_ask.get('price', 0)) - float(best_bid.get('price', 0))
                mid_price = (float(best_ask.get('price', 0)) + float(best_bid.get('price', 0))) / 2
                print(f"  Spread: {spread:.6f}, Mid: {mid_price:.6f}")
            except (ValueError, TypeError):
                print("  Spread/Mid: Unable to calculate")
    
    def calculate_messages_per_second(self):
        """Calculate messages per second based on recent timestamps"""
        if len(self.message_timestamps) < 2:
            return 0.0
        
        # Use timestamps from last 10 seconds or all available
        now = time.time()
        recent_timestamps = [ts for ts in self.message_timestamps if now - ts <= 10.0]
        
        if len(recent_timestamps) < 2:
            return 0.0
        
        time_span = recent_timestamps[-1] - recent_timestamps[0]
        if time_span <= 0:
            return 0.0
        
        return len(recent_timestamps) / time_span
    
    def stats_reporter(self):
        """Periodically report statistics"""
        while self.running:
            time.sleep(10)  # Report every 10 seconds
            
            if not self.running:
                break
            
            self.print_stats()
    
    def print_stats(self):
        """Print current statistics"""
        now = time.time()
        uptime = now - self.stats['start_time'] if self.stats['start_time'] else 0
        
        print(f"\n{'='*50}")
        print(f"📊 STATISTICS ({datetime.now().strftime('%H:%M:%S')})")
        print(f"{'='*50}")
        print(f"Uptime: {uptime:.1f}s")
        print(f"Total Messages: {self.stats['total_messages']}")
        print(f"Errors: {self.stats['errors']}")
        print(f"Messages/sec: {self.calculate_messages_per_second():.2f}")
        
        if self.stats['last_message_time']:
            last_msg_ago = now - self.stats['last_message_time']
            print(f"Last Message: {last_msg_ago:.1f}s ago")
        else:
            print("Last Message: Never")
        
        # Per-exchange stats
        if self.stats['messages_per_exchange']:
            print(f"\n📈 Per Exchange:")
            for exchange, count in sorted(self.stats['messages_per_exchange'].items()):
                percentage = (count / self.stats['total_messages']) * 100
                print(f"  {exchange}: {count} ({percentage:.1f}%)")
        
        # Per-symbol stats (top 10)
        if self.stats['messages_per_symbol']:
            print(f"\n📈 Top Symbols:")
            sorted_symbols = sorted(self.stats['messages_per_symbol'].items(), 
                                  key=lambda x: x[1], reverse=True)
            for symbol, count in sorted_symbols[:10]:
                percentage = (count / self.stats['total_messages']) * 100
                print(f"  {symbol}: {count} ({percentage:.1f}%)")
        
        print(f"{'='*50}\n")
    
    def wait_for_messages(self, timeout=30):
        """Wait for messages with timeout"""
        start_time = time.time()
        initial_count = self.stats['total_messages']
        
        print(f"⏳ Waiting for messages (timeout: {timeout}s)...")
        
        while self.running and (time.time() - start_time) < timeout:
            if self.stats['total_messages'] > initial_count:
                print(f"✅ Received {self.stats['total_messages'] - initial_count} messages!")
                return True
            time.sleep(0.1)
        
        print(f"⏰ Timeout reached. No messages received in {timeout}s")
        return False

def main():
    parser = argparse.ArgumentParser(description='Test C++ DataIngestionService via ZeroMQ')
    parser.add_argument('--endpoint', default='tcp://localhost:5555',
                       help='ZeroMQ endpoint (default: tcp://localhost:5555)')
    parser.add_argument('--verbose', '-v', action='store_true',
                       help='Verbose message details')
    parser.add_argument('--timeout', type=int, default=60,
                       help='Timeout for initial message wait (default: 60s)')
    
    args = parser.parse_args()
    
    print("🔧 Data Ingestion Service Tester")
    print(f"📡 Endpoint: {args.endpoint}")
    print(f"🔊 Verbose: {args.verbose}")
    print(f"⏱️  Timeout: {args.timeout}s")
    
    tester = DataIngestionTester(args.endpoint, args.verbose)
    
    if not tester.start():
        print("❌ Failed to start tester")
        return 1
    
    try:
        # Wait for initial messages
        if tester.wait_for_messages(args.timeout):
            print("🎉 Service is publishing data successfully!")
            
            # Continue monitoring
            print("📡 Monitoring... Press Ctrl+C to stop")
            while tester.running:
                time.sleep(1)
        else:
            print("⚠️  No messages received - check if C++ service is running")
            
    except KeyboardInterrupt:
        pass
    finally:
        tester.stop()
        tester.print_stats()  # Final stats
    
    return 0

if __name__ == "__main__":
    sys.exit(main())