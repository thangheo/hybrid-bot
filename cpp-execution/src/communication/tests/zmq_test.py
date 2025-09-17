#!/usr/bin/env python3
"""
Modular ZMQ Test Client for Trading System Communication
Refactored for easier modification and extension
"""

import zmq
import time
import sys
import os
import json
import threading
from typing import Optional, Dict, List, Callable, Any
from dataclasses import dataclass
from abc import ABC, abstractmethod
import base64

# Add generated protobuf path
current_dir = os.path.dirname(os.path.abspath(__file__))

# Try to locate the generated protobuf module (trading_messages_pb2.py) in a few
# sensible places. Projects sometimes put generated Python files in
# generated/python/proto, next to the test, or under the python-strategy tree.
def _locate_and_add_pb2_path(start_dir: str) -> bool:
    p = start_dir
    # Walk up the directory tree up to 6 levels
    for _ in range(6):
        # Common candidate locations relative to p
        candidates = [
            os.path.join(p, 'trading_messages_pb2.py'),
            os.path.join(p, 'generated', 'python', 'proto', 'trading_messages_pb2.py'),
            os.path.join(p, 'generated', 'python', 'trading_messages_pb2.py'),
            os.path.join(p, 'generated', 'python'),
            p,
        ]
        for candidate in candidates:
            if os.path.isfile(candidate):
                # If the candidate is the file itself, add its directory
                dir_to_add = candidate if os.path.isdir(candidate) else os.path.dirname(candidate)
                if dir_to_add not in sys.path:
                    sys.path.insert(0, dir_to_add)
                return True
        # Move one level up
        parent = os.path.abspath(os.path.join(p, '..'))
        if parent == p:
            break
        p = parent
    return False

if not _locate_and_add_pb2_path(current_dir):
    # Last resort: also try to add the repo-relative generated paths
    fallback_paths = [
        os.path.join(current_dir, '..', '..', 'generated', 'python', 'proto'),
        os.path.join(current_dir, '..', '..', '..', 'python-strategy', 'src', 'communication'),
    ]
    added = False
    for fp in fallback_paths:
        fp = os.path.abspath(fp)
        if os.path.isdir(fp) and fp not in sys.path:
            sys.path.insert(0, fp)
            added = True
            break
    if not added:
        print("✗ Could not locate generated protobuf (trading_messages_pb2.py).")
        print("Run: protoc --python_out=generated/python --proto_path=proto proto/trading_messages.proto")
        print("Or move the generated trading_messages_pb2.py next to this test or into generated/python/proto.")
        sys.exit(1)

try:
    import trading_messages_pb2 as tm
    print("✓ Protobuf imported successfully")
except ImportError as e:
    print(f"✗ Failed to import protobuf after adding candidate paths: {e}")
    print("Ensure the generated Python module 'trading_messages_pb2.py' exists and is discoverable on PYTHONPATH.")
    sys.exit(1)

@dataclass
class ConnectionConfig:
    """Configuration for ZMQ connections"""
    pub_endpoint: str = "tcp://localhost:5555"
    req_endpoint: str = "tcp://localhost:5556"
    recv_timeout: int = 2000  # milliseconds
    send_timeout: int = 5000  # milliseconds
    linger: int = 1000

@dataclass
class TestConfig:
    """Configuration for test parameters"""
    intelligence_duration: int = 10  # seconds
    ping_pong_iterations: int = 5
    enable_verbose: bool = True
    enable_stats: bool = True

class ZMQConnection:
    """Manages ZMQ connections with error handling and reconnection"""
    
    def __init__(self, config: ConnectionConfig):
        self.config = config
        self.context = zmq.Context()
        self.subscriber = None
        self.client = None
        self.connected = False
        
    def connect(self) -> bool:
        """Establish ZMQ connections"""
        try:
            # Market intelligence subscriber
            self.subscriber = self.context.socket(zmq.SUB)
            self.subscriber.connect(self.config.pub_endpoint)
            self.subscriber.setsockopt(zmq.RCVTIMEO, self.config.recv_timeout)
            self.subscriber.setsockopt(zmq.LINGER, self.config.linger)
            
            # Trading intent client
            self.client = self.context.socket(zmq.REQ)
            self.client.connect(self.config.req_endpoint)
            self.client.setsockopt(zmq.RCVTIMEO, self.config.recv_timeout)
            self.client.setsockopt(zmq.SNDTIMEO, self.config.send_timeout)
            self.client.setsockopt(zmq.LINGER, self.config.linger)
            
            self.connected = True
            print(f"✓ Connected to PUB: {self.config.pub_endpoint}")
            print(f"✓ Connected to REQ: {self.config.req_endpoint}")
            return True
            
        except zmq.ZMQError as e:
            print(f"✗ Connection failed: {e}")
            return False
    
    def subscribe_to_topic(self, topic: str):
        """Subscribe to a specific topic"""
        if self.subscriber:
            self.subscriber.setsockopt(zmq.SUBSCRIBE, topic.encode())
            print(f"✓ Subscribed to topic: {topic}")
    
    def disconnect(self):
        """Clean disconnect"""
        if self.subscriber:
            self.subscriber.close()
        if self.client:
            self.client.close()
        self.context.term()
        self.connected = False

class TestResult:
    """Container for test results"""
    
    def __init__(self, name: str):
        self.name = name
        self.success = False
        self.message = ""
        self.metrics = {}
        self.start_time = None
        self.end_time = None
        
    def start(self):
        self.start_time = time.time()
        
    def finish(self, success: bool, message: str = "", metrics: Dict = None):
        self.end_time = time.time()
        self.success = success
        self.message = message
        if metrics:
            self.metrics.update(metrics)
            
    @property
    def duration(self) -> float:
        if self.start_time and self.end_time:
            return self.end_time - self.start_time
        return 0.0
        
    def __str__(self):
        status = "✓ PASS" if self.success else "✗ FAIL"
        duration_str = f" ({self.duration:.2f}s)" if self.duration > 0 else ""
        return f"{self.name}: {status}{duration_str}"

class BaseTest(ABC):
    """Base class for all tests"""
    
    def __init__(self, connection: ZMQConnection, config: TestConfig):
        self.connection = connection
        self.config = config
        self.result = TestResult(self.__class__.__name__)
        
    @abstractmethod
    def run(self) -> TestResult:
        """Run the test and return results"""
        pass
        
    def log(self, message: str):
        """Log message if verbose enabled"""
        if self.config.enable_verbose:
            print(message)

class MarketIntelligenceTest(BaseTest):
    """Test for receiving market intelligence messages"""
    
    def __init__(self, connection: ZMQConnection, config: TestConfig, topics: List[str] = None):
        super().__init__(connection, config)
        self.topics = topics or ["MARKET_INTEL"]
        self.received_messages = []
        
    def run(self) -> TestResult:
        self.result.start()
        self.log(f"\n--- Testing Market Intelligence Reception ({self.config.intelligence_duration}s) ---")
        
        # Subscribe to topics
        for topic in self.topics:
            self.connection.subscribe_to_topic(topic)
        
        received_count = 0
        symbols_seen = set()
        start_time = time.time()
        
        while time.time() - start_time < self.config.intelligence_duration:
            try:
                topic = self.connection.subscriber.recv()
                data = self.connection.subscriber.recv()
                
                # Try to parse as protobuf first (raw bytes)
                intel = tm.MarketIntelligence()
                parsed = False
                try:
                    if intel.ParseFromString(data):
                        parsed = True
                except Exception:
                    parsed = False

                # If raw protobuf failed, attempt JSON wrapper extraction
                if not parsed:
                    try:
                        s = data.decode('utf-8')
                        j = json.loads(s)
                        payload = None
                        # Common wrapper shapes: {"data": <base64 or string> , "metadata": {...}}
                        if isinstance(j, dict) and 'data' in j:
                            data_field = j['data']
                            if isinstance(data_field, str):
                                # Try base64 decode first, fall back to raw utf-8 bytes
                                try:
                                    payload = base64.b64decode(data_field)
                                except Exception:
                                    payload = data_field.encode('utf-8')
                            elif isinstance(data_field, list):
                                # e.g., list of integers representing bytes
                                payload = bytes(data_field)

                        if payload:
                            try:
                                intel2 = tm.MarketIntelligence()
                                if intel2.ParseFromString(payload):
                                    intel = intel2
                                    parsed = True
                            except Exception:
                                parsed = False
                    except Exception:
                        parsed = False

                if parsed:
                    received_count += 1
                    symbols_seen.add(intel.symbol)
                    self.received_messages.append(intel)

                    if received_count % 5 == 0:
                        self.log(f"Received {received_count} messages from {len(symbols_seen)} symbols")
                        self.log(f"  Latest: {intel.symbol} - Spread: {intel.bid_ask_spread:.4f}")
                else:
                    # Treat as raw string/JSON if parsing failed
                    try:
                        topic_str = topic.decode() if isinstance(topic, (bytes, bytearray)) else str(topic)
                    except Exception:
                        topic_str = str(topic)
                    self.log(f"Received raw message on {topic_str}: {len(data)} bytes")
                    received_count += 1
                
            except zmq.Again:
                self.log("No message received (timeout)")
                time.sleep(0.1)
            except Exception as e:
                self.log(f"Error receiving message: {e}")
                break
        
        metrics = {
            "messages_received": received_count,
            "symbols_count": len(symbols_seen),
            "rate_per_second": received_count / self.config.intelligence_duration,
            "symbols": sorted(symbols_seen)
        }
        
        success = received_count > 0
        message = f"Received {received_count} messages from {len(symbols_seen)} symbols"
        
        self.result.finish(success, message, metrics)
        return self.result

class TradingIntentTest(BaseTest):
    """Test for trading intent round-trip communication"""
    
    def create_test_intent(self, intent_id: str, symbol: str = "AAPL") -> tm.TradingIntent:
        """Create a test trading intent"""
        intent = tm.TradingIntent()
        intent.intent_id = intent_id
        intent.symbol = symbol
        intent.side = tm.SIDE_BUY
        intent.target_quantity = 100.0
        intent.signal_strength = 0.8
        intent.confidence = 0.9
        intent.signal_timestamp = int(time.time() * 1000)
        intent.strategy_id = "test_strategy"
        intent.execution_style = tm.EXEC_BALANCED
        intent.max_execution_time_ms = 30000
        intent.max_slippage_tolerance = 0.002
        intent.allow_partial_fills = True
        intent.urgency_factor = 0.5
        intent.market_regime = tm.REGIME_BALANCED
        intent.volatility_regime = tm.VOL_MEDIUM
        intent.etf_flow_regime = tm.FLOW_NEUTRAL
        return intent
    
    def send_intent(self, intent: tm.TradingIntent) -> Optional[tm.ExecutionAcknowledgment]:
        """Send intent and receive acknowledgment"""
        try:
            start_time = time.time()
            self.connection.client.send(intent.SerializeToString())
            response = self.connection.client.recv()
            latency = (time.time() - start_time) * 1000
            
            ack = tm.ExecutionAcknowledgment()
            if ack.ParseFromString(response):
                ack.round_trip_ms = latency  # Add custom field for tracking
                return ack
            else:
                self.log("Failed to parse acknowledgment")
                return None
                
        except zmq.Again:
            self.log("Timeout waiting for acknowledgment")
            return None
        except Exception as e:
            self.log(f"Error sending intent: {e}")
            return None
    
    def run(self) -> TestResult:
        self.result.start()
        self.log("\n--- Testing Trading Intent Round-trip ---")
        
        intent = self.create_test_intent(f"test_intent_{int(time.time())}")
        self.log(f"Sending intent: {intent.intent_id}")
        self.log(f"  Symbol: {intent.symbol}, Side: {'BUY' if intent.side == tm.SIDE_BUY else 'SELL'}")
        
        ack = self.send_intent(intent)
        
        if ack:
            metrics = {
                "round_trip_ms": getattr(ack, 'round_trip_ms', 0),
                "accepted": ack.accepted,
                "execution_id": ack.execution_id,
                "reason": ack.reason
            }
            
            self.log(f"✓ Acknowledgment received: {ack.accepted}")
            self.log(f"  Execution ID: {ack.execution_id}")
            self.log(f"  Round-trip: {metrics['round_trip_ms']:.2f}ms")
            
            self.result.finish(True, "Intent acknowledged", metrics)
        else:
            self.result.finish(False, "No acknowledgment received")
            
        return self.result

class PingPongTest(BaseTest):
    """Multiple ping-pong test iterations"""
    
    def __init__(self, connection: ZMQConnection, config: TestConfig, symbols: List[str] = None):
        super().__init__(connection, config)
        self.symbols = symbols or ["AAPL", "GOOGL", "MSFT", "NVDA", "TSLA"]
        
    def run(self) -> TestResult:
        self.result.start()
        self.log(f"\n--- Running Ping-Pong Test ({self.config.ping_pong_iterations} iterations) ---")
        
        success_count = 0
        total_latency = 0
        results = []
        
        for i in range(self.config.ping_pong_iterations):
            symbol = self.symbols[i % len(self.symbols)]
            intent_id = f"ping_{i}_{int(time.time())}"
            
            intent = tm.TradingIntent()
            intent.intent_id = intent_id
            intent.symbol = symbol
            intent.side = tm.SIDE_BUY if i % 2 == 0 else tm.SIDE_SELL
            intent.target_quantity = 50.0 + i * 25
            intent.signal_strength = 0.7 + i * 0.05
            intent.confidence = 0.8
            intent.strategy_id = "ping_test"
            intent.execution_style = tm.EXEC_BALANCED
            intent.urgency_factor = 0.5
            intent.market_regime = tm.REGIME_BALANCED
            
            try:
                start_time = time.time()
                self.connection.client.send(intent.SerializeToString())
                response = self.connection.client.recv()
                latency = (time.time() - start_time) * 1000
                
                ack = tm.ExecutionAcknowledgment()
                ack.ParseFromString(response)
                
                ping_result = {
                    "iteration": i + 1,
                    "symbol": symbol,
                    "latency_ms": latency,
                    "accepted": ack.accepted,
                    "reason": ack.reason
                }
                results.append(ping_result)
                
                if ack.accepted:
                    self.log(f"  ✓ Ping {i+1}: {symbol} - {latency:.2f}ms")
                    success_count += 1
                    total_latency += latency
                else:
                    self.log(f"  ✗ Ping {i+1}: {symbol} - Rejected: {ack.reason}")
                    
            except zmq.Again:
                self.log(f"  ✗ Ping {i+1}: Timeout")
                results.append({"iteration": i + 1, "symbol": symbol, "timeout": True})
            except Exception as e:
                self.log(f"  ✗ Ping {i+1}: Error: {e}")
                results.append({"iteration": i + 1, "symbol": symbol, "error": str(e)})
        
        metrics = {
            "success_rate": f"{success_count}/{self.config.ping_pong_iterations}",
            "average_latency_ms": total_latency / success_count if success_count > 0 else 0,
            "results": results
        }
        
        success = success_count == self.config.ping_pong_iterations
        message = f"Success rate: {success_count}/{self.config.ping_pong_iterations}"
        
        self.result.finish(success, message, metrics)
        return self.result

class CustomMessageTest(BaseTest):
    """Test for custom message types (raw strings, JSON, etc.)"""
    
    def __init__(self, connection: ZMQConnection, config: TestConfig, topics: List[str] = None):
        super().__init__(connection, config)
        self.topics = topics or ["CUSTOM_TOPIC"]
        
    def run(self) -> TestResult:
        self.result.start()
        self.log("\n--- Testing Custom Message Reception ---")
        
        # Subscribe to custom topics
        for topic in self.topics:
            self.connection.subscribe_to_topic(topic)
        
        received_count = 0
        start_time = time.time()
        timeout = 5  # seconds
        
        while time.time() - start_time < timeout:
            try:
                topic = self.connection.subscriber.recv()
                data = self.connection.subscriber.recv()
                
                topic_str = topic.decode()
                data_str = data.decode()
                
                self.log(f"Received on {topic_str}: {data_str[:100]}...")
                received_count += 1
                
                # Try to parse as JSON
                try:
                    json_data = json.loads(data_str)
                    self.log(f"  JSON parsed successfully: {type(json_data)}")
                except json.JSONDecodeError:
                    self.log("  Not JSON format")
                
            except zmq.Again:
                self.log("No custom message received (timeout)")
                break
            except Exception as e:
                self.log(f"Error receiving custom message: {e}")
                break
        
        metrics = {"messages_received": received_count}
        success = received_count > 0
        message = f"Received {received_count} custom messages"
        
        self.result.finish(success, message, metrics)
        return self.result

class TestRunner:
    """Main test runner that orchestrates all tests"""
    
    def __init__(self, connection_config: ConnectionConfig = None, test_config: TestConfig = None):
        self.connection_config = connection_config or ConnectionConfig()
        self.test_config = test_config or TestConfig()
        self.connection = ZMQConnection(self.connection_config)
        self.test_results = []
        
    def add_test(self, test_class, *args, **kwargs):
        """Add a test to the runner"""
        test = test_class(self.connection, self.test_config, *args, **kwargs)
        return test
        
    def run_all_tests(self) -> bool:
        """Run all standard tests"""
        print("=" * 60)
        print("ZMQ COMMUNICATION TEST SUITE")
        print("=" * 60)
        
        if not self.connection.connect():
            print("✗ Failed to establish connections")
            return False
        
        try:
            # Test 1: Market Intelligence
            intel_test = self.add_test(MarketIntelligenceTest)
            intel_result = intel_test.run()
            self.test_results.append(intel_result)
            
            # Test 2: Trading Intent Round-trip
            intent_test = self.add_test(TradingIntentTest)
            intent_result = intent_test.run()
            self.test_results.append(intent_result)
            
            # Test 3: Ping-Pong Test
            ping_test = self.add_test(PingPongTest)
            ping_result = ping_test.run()
            self.test_results.append(ping_result)
            
            # Test 4: Custom Messages
            custom_test = self.add_test(CustomMessageTest)
            custom_result = custom_test.run()
            self.test_results.append(custom_result)
            
            return self.print_summary()
            
        except KeyboardInterrupt:
            print("\nTest interrupted by user")
            return False
        except Exception as e:
            print(f"\nUnexpected error: {e}")
            return False
        finally:
            self.connection.disconnect()
    
    def run_single_test(self, test_class, *args, **kwargs) -> TestResult:
        """Run a single test"""
        if not self.connection.connect():
            result = TestResult(test_class.__name__)
            result.finish(False, "Connection failed")
            return result
            
        try:
            test = self.add_test(test_class, *args, **kwargs)
            return test.run()
        finally:
            self.connection.disconnect()
    
    def print_summary(self) -> bool:
        """Print test summary"""
        print("\n" + "=" * 60)
        print("TEST SUMMARY")
        print("=" * 60)
        
        all_passed = True
        for result in self.test_results:
            print(str(result))
            if result.message:
                print(f"  → {result.message}")
            if self.test_config.enable_stats and result.metrics:
                for key, value in result.metrics.items():
                    if key != "results":  # Skip detailed results in summary
                        print(f"    {key}: {value}")
            all_passed &= result.success
        
        print(f"\nOVERALL: {'✓ ALL TESTS PASSED' if all_passed else '✗ SOME TESTS FAILED'}")
        
        if all_passed:
            print("\nCommunication layer is working correctly!")
        else:
            print("\nCommunication issues detected. Check server logs.")
            
        return all_passed

def main():
    """Main function with configuration options"""
    
    # Parse command line arguments for configuration
    import argparse
    parser = argparse.ArgumentParser(description="ZMQ Communication Test Client")
    parser.add_argument("--pub-endpoint", default="tcp://localhost:5555", help="Publisher endpoint")
    parser.add_argument("--req-endpoint", default="tcp://localhost:5556", help="Request endpoint")
    parser.add_argument("--intelligence-duration", type=int, default=10, help="Intelligence test duration (seconds)")
    parser.add_argument("--ping-iterations", type=int, default=5, help="Number of ping-pong iterations")
    parser.add_argument("--verbose", action="store_true", help="Enable verbose output")
    parser.add_argument("--no-stats", action="store_true", help="Disable statistics output")
    parser.add_argument("--test", choices=["all", "intel", "intent", "ping", "custom"], default="all", help="Specific test to run")
    
    args = parser.parse_args()
    
    # Create configurations
    conn_config = ConnectionConfig(
        pub_endpoint=args.pub_endpoint,
        req_endpoint=args.req_endpoint
    )
    
    test_config = TestConfig(
        intelligence_duration=args.intelligence_duration,
        ping_pong_iterations=args.ping_iterations,
        enable_verbose=args.verbose,
        enable_stats=not args.no_stats
    )
    
    # Create and run tests
    runner = TestRunner(conn_config, test_config)
    
    if args.test == "all":
        success = runner.run_all_tests()
    elif args.test == "intel":
        result = runner.run_single_test(MarketIntelligenceTest)
        success = result.success
        print(str(result))
    elif args.test == "intent":
        result = runner.run_single_test(TradingIntentTest)
        success = result.success
        print(str(result))
    elif args.test == "ping":
        result = runner.run_single_test(PingPongTest)
        success = result.success
        print(str(result))
    elif args.test == "custom":
        result = runner.run_single_test(CustomMessageTest)
        success = result.success
        print(str(result))
    
    return 0 if success else 1

if __name__ == "__main__":
    sys.exit(main())