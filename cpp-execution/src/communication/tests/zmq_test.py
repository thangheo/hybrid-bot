#!/usr/bin/env python3
"""
Simple ping-pong test for Python-C++ communication
Tests basic ZMQ connectivity and protobuf serialization
"""

import zmq
import time
import sys
import os

# Add generated protobuf path
import os 
current_dir = os.path.dirname(os.path.abspath(__file__))
proto_path = os.path.join(current_dir, 'generated', 'python', 'proto')
sys.path.append(proto_path)


try:
    import trading_messages_pb2 as tm
    print("✓ Protobuf imported successfully")
except ImportError as e:
    print(f"✗ Failed to import protobuf: {e}")
    print("Run: protoc --python_out=generated/python --proto_path=proto proto/trading_messages.proto")
    sys.exit(1)

class SimplePingTest:
    def __init__(self):
        self.context = zmq.Context()
        self.intelligence_sub = None
        self.intent_client = None
        
    def setup_connections(self):
        """Setup ZMQ connections"""
        try:
            # Market intelligence subscriber
            self.intelligence_sub = self.context.socket(zmq.SUB)
            self.intelligence_sub.connect("tcp://localhost:5555")
            self.intelligence_sub.setsockopt(zmq.SUBSCRIBE, b"MARKET_INTEL")
            self.intelligence_sub.setsockopt(zmq.RCVTIMEO, 2000)  # 2 second timeout
            
            # Trading intent client
            self.intent_client = self.context.socket(zmq.REQ)
            self.intent_client.connect("tcp://localhost:5556")
            self.intent_client.setsockopt(zmq.RCVTIMEO, 5000)  # 5 second timeout
            
            print("✓ ZMQ connections established")
            return True
            
        except zmq.ZMQError as e:
            print(f"✗ ZMQ connection failed: {e}")
            return False
    
    def test_intelligence_reception(self, duration=10):
        """Test receiving market intelligence from C++ server"""
        print(f"\n--- Testing Market Intelligence Reception ({duration}s) ---")
        
        received_count = 0
        symbols_seen = set()
        start_time = time.time()
        
        while time.time() - start_time < duration:
            try:
                # Receive market intelligence
                topic = self.intelligence_sub.recv()
                data = self.intelligence_sub.recv()
                
                # Parse protobuf message
                intel = tm.MarketIntelligence()
                intel.ParseFromString(data)
                
                received_count += 1
                symbols_seen.add(intel.symbol)
                
                if received_count % 5 == 0:
                    print(f"Received {received_count} messages from {len(symbols_seen)} symbols")
                    print(f"  Latest: {intel.symbol} - Spread: {intel.bid_ask_spread:.4f}, "
                          f"Liquidity: {intel.liquidity_ratio:.2f}")
                
            except zmq.Again:
                print("No message received (timeout)")
                time.sleep(0.1)
            except Exception as e:
                print(f"Error receiving intelligence: {e}")
                break
        
        print(f"\nIntelligence Test Results:")
        print(f"  Messages received: {received_count}")
        print(f"  Symbols: {sorted(symbols_seen)}")
        print(f"  Rate: {received_count/duration:.1f} msg/s")
        
        return received_count > 0
    
    def test_intent_roundtrip(self):
        """Test trading intent round-trip communication"""
        print("\n--- Testing Trading Intent Round-trip ---")
        
        # Create simple test intent
        intent = tm.TradingIntent()
        intent.intent_id = f"test_intent_{int(time.time())}"
        intent.symbol = "AAPL"
        intent.side = tm.SIDE_BUY
        intent.target_quantity = 100.0
        intent.signal_strength = 0.8
        intent.confidence = 0.9
        intent.signal_timestamp = int(time.time() * 1000)
        intent.strategy_id = "ping_test"
        intent.execution_style = tm.EXEC_BALANCED
        intent.max_execution_time_ms = 30000
        intent.max_slippage_tolerance = 0.002
        intent.allow_partial_fills = True
        intent.max_market_impact = 0.01
        intent.urgency_factor = 0.5
        intent.market_regime = tm.REGIME_BALANCED
        intent.volatility_regime = tm.VOL_MEDIUM
        intent.etf_flow_regime = tm.FLOW_NEUTRAL
        
        print(f"Sending intent: {intent.intent_id}")
        print(f"  Symbol: {intent.symbol}")
        print(f"  Side: {'BUY' if intent.side == tm.SIDE_BUY else 'SELL'}")
        print(f"  Quantity: {intent.target_quantity}")
        
        try:
            # Send intent
            start_time = time.time()
            self.intent_client.send(intent.SerializeToString())
            
            # Receive acknowledgment
            response = self.intent_client.recv()
            round_trip_ms = (time.time() - start_time) * 1000
            
            # Parse acknowledgment
            ack = tm.ExecutionAcknowledgment()
            ack.ParseFromString(response)
            
            print(f"\nRound-trip time: {round_trip_ms:.2f}ms")
            print(f"Acknowledgment received:")
            print(f"  Intent ID: {ack.intent_id}")
            print(f"  Accepted: {ack.accepted}")
            print(f"  Execution ID: {ack.execution_id}")
            print(f"  Reason: {ack.reason}")
            
            if ack.execution_params:
                params = ack.execution_params
                print(f"  Execution Parameters:")
                print(f"    Aggression: {params.aggression_level:.2f}")
                print(f"    Chunk Ratio: {params.chunk_size_ratio:.2f}")
                print(f"    Max Participation: {params.max_participation_rate:.2f}")
            
            return ack.accepted
            
        except zmq.Again:
            print("✗ Timeout waiting for acknowledgment")
            return False
        except Exception as e:
            print(f"✗ Error in round-trip test: {e}")
            return False
    
    def run_ping_pong_test(self):
        """Run multiple ping-pong iterations"""
        print("\n--- Running Ping-Pong Test (5 iterations) ---")
        
        success_count = 0
        total_time = 0
        
        for i in range(5):
            intent = tm.TradingIntent()
            intent.intent_id = f"ping_{i}_{int(time.time())}"
            intent.symbol = ["AAPL", "GOOGL", "MSFT", "NVDA", "TSLA"][i]
            intent.side = tm.SIDE_BUY if i % 2 == 0 else tm.SIDE_SELL
            intent.target_quantity = 50.0 + i * 25
            intent.signal_strength = 0.7 + i * 0.05
            intent.confidence = 0.8
            intent.strategy_id = "ping_test"
            intent.execution_style = tm.EXEC_BALANCED
            intent.max_execution_time_ms = 30000
            intent.urgency_factor = 0.5
            intent.market_regime = tm.REGIME_BALANCED
            
            try:
                print(f"\nPing {i+1}: {intent.symbol} {intent.target_quantity}")
                
                start_time = time.time()
                self.intent_client.send(intent.SerializeToString())
                response = self.intent_client.recv()
                elapsed_ms = (time.time() - start_time) * 1000
                
                ack = tm.ExecutionAcknowledgment()
                ack.ParseFromString(response)
                
                if ack.accepted:
                    print(f"  ✓ Pong received: {elapsed_ms:.2f}ms")
                    success_count += 1
                    total_time += elapsed_ms
                else:
                    print(f"  ✗ Rejected: {ack.reason}")
                
            except zmq.Again:
                print(f"  ✗ Timeout on ping {i+1}")
            except Exception as e:
                print(f"  ✗ Error on ping {i+1}: {e}")
        
        print(f"\nPing-Pong Results:")
        print(f"  Success rate: {success_count}/5")
        if success_count > 0:
            print(f"  Average latency: {total_time/success_count:.2f}ms")
        
        return success_count == 5
    
    def cleanup(self):
        """Cleanup connections"""
        if self.intelligence_sub:
            self.intelligence_sub.close()
        if self.intent_client:
            self.intent_client.close()
        self.context.term()

def main():
    """Main test function"""
    print("=" * 50)
    print("HYBRID TRADING SYSTEM - COMMUNICATION TEST")
    print("=" * 50)
    
    # Check if C++ server is likely running
    test = SimplePingTest()
    
    try:
        # Setup connections
        if not test.setup_connections():
            print("\n✗ Failed to setup connections")
            print("Make sure C++ server is running: ./build/trading_server")
            return False
        
        # Test 1: Market Intelligence Reception
        intelligence_ok = test.test_intelligence_reception(duration=5)
        
        # Test 2: Trading Intent Round-trip
        roundtrip_ok = test.test_intent_roundtrip()
        
        # Test 3: Multiple Ping-Pong
        pingpong_ok = test.run_ping_pong_test()
        
        # Summary
        print("\n" + "=" * 50)
        print("TEST SUMMARY")
        print("=" * 50)
        print(f"Market Intelligence:  {'✓ PASS' if intelligence_ok else '✗ FAIL'}")
        print(f"Intent Round-trip:    {'✓ PASS' if roundtrip_ok else '✗ FAIL'}")  
        print(f"Ping-Pong Test:       {'✓ PASS' if pingpong_ok else '✗ FAIL'}")
        
        all_passed = intelligence_ok and roundtrip_ok and pingpong_ok
        print(f"\nOVERALL: {'✓ ALL TESTS PASSED' if all_passed else '✗ SOME TESTS FAILED'}")
        
        if all_passed:
            print("\nCommunication layer is working correctly!")
            print("Ready to proceed to Phase 3: Strategy Integration")
        else:
            print("\nCommunication issues detected. Check C++ server logs.")
        
        return all_passed
        
    except KeyboardInterrupt:
        print("\nTest interrupted by user")
        return False
    except Exception as e:
        print(f"\nUnexpected error: {e}")
        return False
    finally:
        test.cleanup()

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
      