#!/usr/bin/env python3
"""
Mock Python Execution Engine - Receives trading intents from Python strategy
Acts as the C++ execution engine to test the communication flow
"""
import os
import zmq
import time
import sys
from signal import signal, SIGINT

# Add protobuf path if needed
sys.path.append('generated/python')
import trading_messages_pb2 as tm

class MockExecutionEngine:
    def __init__(self):
        self.context = zmq.Context()
        self.intent_receiver = None
        self.running = True
        self.processed_intents = 0
        
    def start(self):
        """Start mock execution engine server"""
        try:
            # Setup ZMQ REP socket (receives requests, sends replies)
            self.intent_receiver = self.context.socket(zmq.ROUTER)
            self.intent_receiver.bind("tcp://*:5558")
            self.intent_receiver.setsockopt(zmq.RCVTIMEO, 1000)  # 1 second timeout
            
            print("Mock Execution Engine started on tcp://*:5558")
            print("Waiting for trading intents from Python strategy...")
            print("Press Ctrl+C to stop\n")
            return True
            
        except Exception as e:
            print(f"Failed to start mock execution engine: {e}")
            return False
    
    def process_intent_loop(self):
        """Main loop to process incoming trading intents"""
        while self.running:
            try:
                # Receive trading intent
                msg_parts = self.intent_receiver.recv_multipart()
                identity = msg_parts[0]
                request_id = msg_parts[1] 
                data = msg_parts[2]
                # Parse intent
                intent = tm.TradingIntent()
                intent.ParseFromString(data)
                
                # Print received intent details
                self.print_intent_details(intent)
                
                # Create and send acknowledgment
                ack = self.create_acknowledgment(intent)
                self.intent_receiver.send_multipart([
                    identity,
                    request_id,
                    ack.SerializeToString()
                ])
                self.processed_intents += 1
                
            except zmq.Again:
                # Timeout - continue loop
                continue
            except Exception as e:
                print(f"Error processing intent: {e}")
                # Send error acknowledgment if possible
                try:
                    error_ack = tm.ExecutionAcknowledgment()
                    error_ack.intent_id = "unknown"
                    error_ack.accepted = False
                    error_ack.reason = f"Processing error: {str(e)}"
                    self.intent_receiver.send(error_ack.SerializeToString())
                except:
                    pass

    def print_intent_details(self, intent):
        """Print detailed information about received trading intent"""
        print(f"\n{'='*60}")
        print(f"RECEIVED TRADING INTENT #{self.processed_intents + 1}")
        print(f"{'='*60}")
        
        # Basic info
        print(f"Intent ID: {intent.intent_id}")
        print(f"Strategy ID: {intent.strategy_id}")
        print(f"Timestamp: {intent.timestamp}")
        print(f"Signal Timestamp: {intent.signal_timestamp}")
        
        # Trade details
        print(f"\n--- TRADE DETAILS ---")
        print(f"Symbol: {intent.symbol}")
        print(f"Side: {'BUY' if intent.side == tm.SIDE_BUY else 'SELL'}")
        print(f"Target Quantity: {intent.target_quantity}")
        print(f"Max Price: {intent.max_price}")
        print(f"Min Price: {intent.min_price}")
        
        # Market context
        print(f"\n--- MARKET CONTEXT ---")
        print(f"Market Regime: {self.regime_to_string(intent.market_regime)}")
        print(f"Volatility Regime: {self.volatility_to_string(intent.volatility_regime)}")
        print(f"ETF Flow Regime: {self.flow_to_string(intent.etf_flow_regime)}")
        
        # Signal metadata
        print(f"\n--- SIGNAL METADATA ---")
        print(f"Signal Strength: {intent.signal_strength:.3f}")
        print(f"Confidence: {intent.confidence:.3f}")
        
        # Execution preferences
        print(f"\n--- EXECUTION PREFERENCES ---")
        print(f"Execution Style: {self.execution_style_to_string(intent.execution_style)}")
        print(f"Urgency Factor: {intent.urgency_factor:.3f}")
        print(f"Max Slippage Tolerance: {intent.max_slippage_tolerance:.4f}")
        print(f"Allow Partial Fills: {intent.allow_partial_fills}")
        print(f"Max Market Impact: {intent.max_market_impact:.4f}")
        print(f"Max Execution Time: {intent.max_execution_time_ms}ms")
        
        print(f"{'='*60}")

    def create_acknowledgment(self, intent):
        """Create execution acknowledgment response"""
        ack = tm.ExecutionAcknowledgment()
        
        # FIXED: Use direct assignment instead of set_ methods
        ack.intent_id = intent.intent_id
        ack.accepted = True
        ack.execution_id = f"mock_exec_{int(time.time())}_{self.processed_intents}"
        ack.reason = "Mock execution engine - intent accepted for testing"
        
        # Estimate completion time
        current_time = int(time.time() * 1000)
        completion_delay = self.calculate_mock_completion_delay(intent)
        ack.estimated_completion_time = current_time + completion_delay
        
        # Create mock execution parameters
        self.set_mock_execution_parameters(ack.execution_params, intent)
        
        return ack
    
    def calculate_mock_completion_delay(self, intent):
        """Calculate mock completion delay"""
        base_delay = 15000  # 15 seconds
        
        # Adjust by execution style
        if intent.execution_style == tm.EXEC_AGGRESSIVE:
            base_delay = 5000
        elif intent.execution_style == tm.EXEC_STEALTH:
            base_delay = min(60000, intent.max_execution_time_ms)
        elif intent.execution_style == tm.EXEC_PASSIVE:
            base_delay = 30000
        
        # Adjust by urgency
        base_delay = int(base_delay * (1.5 - intent.urgency_factor))
        
        return max(1000, base_delay)  # Minimum 1 second
    
    def set_mock_execution_parameters(self, params, intent):
        """Set mock execution parameters - FIXED to use direct assignment"""
        
        # Base parameters by execution style
        if intent.execution_style == tm.EXEC_AGGRESSIVE:
            params.chunk_size_ratio = 0.4
            params.aggression_level = 0.9
            params.max_chunk_delay_ms = 500
            params.preferred_order_type = tm.ORDER_MARKET
        elif intent.execution_style == tm.EXEC_PASSIVE:
            params.chunk_size_ratio = 0.15
            params.aggression_level = 0.3
            params.max_chunk_delay_ms = 5000
            params.preferred_order_type = tm.ORDER_LIMIT
            params.post_only = True
        elif intent.execution_style == tm.EXEC_STEALTH:
            params.chunk_size_ratio = 0.08
            params.aggression_level = 0.15
            params.max_chunk_delay_ms = 8000
            params.preferred_order_type = tm.ORDER_HIDDEN
            params.use_hidden_orders = True
        else:  # BALANCED
            params.chunk_size_ratio = 0.25
            params.aggression_level = 0.6
            params.max_chunk_delay_ms = 2000
            params.preferred_order_type = tm.ORDER_LIMIT
        
        # Adjust based on intent parameters
        params.aggression_level = params.aggression_level * (0.5 + intent.confidence)
        params.max_participation_rate = 0.15 * intent.urgency_factor
        params.price_improvement_threshold = 0.001 / max(0.1, intent.confidence)
        params.time_in_force_ms = min(30000, intent.max_execution_time_ms // 2)
        
        # Ensure bounds
        params.aggression_level = min(1.0, max(0.1, params.aggression_level))
        params.chunk_size_ratio = min(0.5, max(0.05, params.chunk_size_ratio))
        params.max_participation_rate = min(0.3, max(0.05, params.max_participation_rate))

    # Helper methods for readable output
    def regime_to_string(self, regime):
        regime_map = {
            tm.REGIME_ACCUMULATION: "ACCUMULATION",
            tm.REGIME_EUPHORIA: "EUPHORIA", 
            tm.REGIME_INSTITUTIONAL_FOMO: "INSTITUTIONAL_FOMO",
            tm.REGIME_PANIC: "PANIC",
            tm.REGIME_DISTRIBUTION: "DISTRIBUTION",
            tm.REGIME_SIDEWAYS: "SIDEWAYS",
            tm.REGIME_BALANCED: "BALANCED"  # Now supported
        }
        return regime_map.get(regime, f"UNKNOWN({regime})")
    
    def volatility_to_string(self, vol_regime):
        vol_map = {
            tm.VOL_LOW: "LOW",
            tm.VOL_MEDIUM: "MEDIUM", 
            tm.VOL_HIGH: "HIGH"
        }
        return vol_map.get(vol_regime, f"UNKNOWN({vol_regime})")
    
    def flow_to_string(self, flow_regime):
        flow_map = {
            tm.FLOW_POSITIVE: "POSITIVE",
            tm.FLOW_NEUTRAL: "NEUTRAL",
            tm.FLOW_NEGATIVE: "NEGATIVE"
        }
        return flow_map.get(flow_regime, f"UNKNOWN({flow_regime})")
    
    def execution_style_to_string(self, exec_style):
        style_map = {
            tm.EXEC_AGGRESSIVE: "AGGRESSIVE",
            tm.EXEC_BALANCED: "BALANCED",
            tm.EXEC_PASSIVE: "PASSIVE", 
            tm.EXEC_STEALTH: "STEALTH"
        }
        return style_map.get(exec_style, f"UNKNOWN({exec_style})")
    
    def stop(self):
        """Stop the mock execution engine"""
        self.running = False
        if self.intent_receiver:
            self.intent_receiver.close()
        self.context.term()
        
        print(f"\nMock Execution Engine stopped")
        print(f"Total intents processed: {self.processed_intents}")

def signal_handler(signum, frame):
    """Handle Ctrl+C gracefully and kill all threads/processes"""
    print(f"\nReceived signal {signum}, killing process and all threads...")
    os._exit(0)

def main():
    global running
    running = True
    
    signal(SIGINT, signal_handler)
    
    print("=== MOCK EXECUTION ENGINE ===")
    print("Simulating C++ execution engine to receive Python trading intents")
    
    engine = MockExecutionEngine()
    
    try:
        if not engine.start():
            return 1
        
        # Process intents until stopped
        while running:
            try:
                engine.process_intent_loop()
            except KeyboardInterrupt:
                break
        
    except Exception as e:
        print(f"Engine error: {e}")
        return 1
    finally:
        engine.stop()
    
    return 0

if __name__ == "__main__":
    sys.exit(main())