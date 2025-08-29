#!/usr/bin/env python3
"""
Complete Python ZMQ Client for Trading System
Handles both market intelligence subscription and trading intent publishing
"""

import zmq
import time
import sys
import threading
import uuid
import logging
from collections import deque
from dataclasses import dataclass
from typing import Dict, List, Optional, Callable
from datetime import datetime

# Import protobuf messages (assuming generated from trading_messages.proto)
try:
    from proto.trading_messages_pb2 import *
except ImportError:
    print("Error: trading_messages_pb2 not found. Generate with: protoc --python_out=. trading_messages.proto")
    sys.exit(1)

# Configure logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

@dataclass
class ConnectionConfig:
    """ZMQ connection configuration"""
    intelligence_endpoint: str = "tcp://localhost:5555"
    intent_endpoint: str = "tcp://localhost:5556"
    connection_timeout: int = 5000  # milliseconds
    max_retries: int = 3

class TradingCommunicationClient:
    """
    Complete ZMQ client for trading system communication
    Handles market intelligence subscription and trading intent publishing
    """
    
    def __init__(self, config: ConnectionConfig = None):
        self.config = config or ConnectionConfig()
        self.context = zmq.Context()
        self._running = False
        self._intelligence_thread = None
        
        # Market intelligence subscriber
        self.intelligence_subscriber = None
        self.intelligence_history = deque(maxlen=1000)
        self.intelligence_callbacks: List[Callable[[MarketIntelligence], None]] = []
        
        # Trading intent client
        self.intent_client = None
        self._intent_lock = threading.Lock()
        
        # Connection status
        self._connected = False
        self._last_intelligence_time = 0
        
        self._setup_connections()
    
    def _setup_connections(self):
        """Initialize ZMQ connections"""
        try:
            # Market intelligence subscriber (SUB socket)
            self.intelligence_subscriber = self.context.socket(zmq.SUB)
            self.intelligence_subscriber.connect(self.config.intelligence_endpoint)
            self.intelligence_subscriber.setsockopt(zmq.SUBSCRIBE, b"MARKET_INTEL")
            self.intelligence_subscriber.setsockopt(zmq.RCVTIMEO, 1000)  # 1 second timeout
            
            # Trading intent client (REQ socket)
            self.intent_client = self.context.socket(zmq.REQ)
            self.intent_client.connect(self.config.intent_endpoint)
            self.intent_client.setsockopt(zmq.RCVTIMEO, self.config.connection_timeout)
            
            logger.info(f"ZMQ client connected to:")
            logger.info(f"  Market Intelligence: {self.config.intelligence_endpoint}")
            logger.info(f"  Trading Intent: {self.config.intent_endpoint}")
            
            self._connected = True
            
        except zmq.ZMQError as e:
            logger.error(f"Failed to setup ZMQ connections: {e}")
            raise
    
    def start(self):
        """Start the client and background threads"""
        if self._running:
            logger.warning("Client already running")
            return
            
        self._running = True
        
        # Start market intelligence subscriber thread
        self._intelligence_thread = threading.Thread(
            target=self._intelligence_loop, 
            daemon=True
        )
        self._intelligence_thread.start()
        
        logger.info("Trading communication client started")
    
    def stop(self):
        """Stop the client and cleanup connections"""
        self._running = False
        
        if self._intelligence_thread and self._intelligence_thread.is_alive():
            self._intelligence_thread.join(timeout=2.0)
        
        # Close sockets
        if self.intelligence_subscriber:
            self.intelligence_subscriber.close()
        if self.intent_client:
            self.intent_client.close()
        
        self.context.term()
        logger.info("Trading communication client stopped")
    
    def add_intelligence_callback(self, callback: Callable[[MarketIntelligence], None]):
        """Add callback for market intelligence updates"""
        self.intelligence_callbacks.append(callback)
    
    def send_trading_intent(self, intent: TradingIntent) -> Optional[ExecutionAcknowledgment]:
        """
        Send trading intent to C++ execution engine
        Returns ExecutionAcknowledgment or None if failed
        """
        if not self._connected:
            logger.error("Client not connected")
            return None
        
        with self._intent_lock:
            for retry in range(self.config.max_retries):
                try:
                    # Send trading intent
                    serialized_intent = intent.SerializeToString()
                    self.intent_client.send(serialized_intent, zmq.NOBLOCK)
                    
                    # Wait for acknowledgment
                    response_data = self.intent_client.recv()
                    
                    # Parse acknowledgment
                    ack = ExecutionAcknowledgment()
                    ack.ParseFromString(response_data)
                    
                    logger.info(f"Intent {intent.intent_id} {'accepted' if ack.accepted else 'rejected'}: {ack.reason}")
                    return ack
                    
                except zmq.Again:
                    logger.warning(f"Timeout sending intent {intent.intent_id}, retry {retry + 1}")
                    if retry == self.config.max_retries - 1:
                        logger.error(f"Failed to send intent {intent.intent_id} after {self.config.max_retries} retries")
                        return None
                    time.sleep(0.1)
                    
                except zmq.ZMQError as e:
                    logger.error(f"ZMQ error sending intent {intent.intent_id}: {e}")
                    return None
    
    def get_latest_intelligence(self, symbol: str = None) -> Optional[MarketIntelligence]:
        """Get most recent market intelligence for symbol (or any symbol if None)"""
        if not self.intelligence_history:
            return None
            
        if symbol is None:
            return self.intelligence_history[-1]
        
        # Find most recent intelligence for specific symbol
        for intel in reversed(self.intelligence_history):
            if intel.symbol == symbol:
                return intel
        return None
    
    def get_intelligence_history(self, symbol: str, count: int = 100) -> List[MarketIntelligence]:
        """Get recent market intelligence history for symbol"""
        return [intel for intel in self.intelligence_history 
                if intel.symbol == symbol][-count:]
    
    def is_connected(self) -> bool:
        """Check if client is connected and receiving data"""
        current_time = time.time()
        return self._connected and (current_time - self._last_intelligence_time < 5.0)
    
    def _intelligence_loop(self):
        """Background thread for receiving market intelligence"""
        logger.info("Market intelligence subscriber started")
        
        while self._running:
            try:
                # Receive market intelligence (non-blocking with timeout)
                topic_data = self.intelligence_subscriber.recv(zmq.NOBLOCK)
                data = self.intelligence_subscriber.recv(zmq.NOBLOCK)
                
                # Parse market intelligence
                intel = MarketIntelligence()
                intel.ParseFromString(data)
                
                # Store in history
                self.intelligence_history.append(intel)
                self._last_intelligence_time = time.time()
                
                # Call registered callbacks
                for callback in self.intelligence_callbacks:
                    try:
                        callback(intel)
                    except Exception as e:
                        logger.error(f"Error in intelligence callback: {e}")
                
            except zmq.Again:
                # No message available, continue
                time.sleep(0.001)  # Small sleep to prevent busy waiting
            except zmq.ZMQError as e:
                logger.error(f"ZMQ error in intelligence loop: {e}")
                time.sleep(0.1)
            except Exception as e:
                logger.error(f"Unexpected error in intelligence loop: {e}")
                time.sleep(0.1)
        
        logger.info("Market intelligence subscriber stopped")

class TradingIntentBuilder:
    """Helper class to build trading intent messages"""
    
    @staticmethod
    def create_intent(symbol: str, side: str, quantity: float, 
                     strategy_id: str = "default_strategy") -> TradingIntent:
        """Create a basic trading intent"""
        intent = TradingIntent()
        
        # Basic information
        intent.intent_id = str(uuid.uuid4())
        intent.timestamp = int(time.time() * 1000)
        intent.strategy_id = strategy_id
        intent.symbol = symbol
        intent.side = SIDE_BUY if side.upper() == 'BUY' else SIDE_SELL
        intent.target_quantity = quantity
        
        # Default values
        intent.signal_strength = 0.7
        intent.confidence = 0.8
        intent.signal_timestamp = intent.timestamp
        intent.execution_style = EXEC_BALANCED
        intent.max_execution_time_ms = 30000  # 30 seconds
        intent.max_slippage_tolerance = 0.002  # 0.2%
        intent.allow_partial_fills = True
        intent.max_market_impact = 0.01  # 1%
        intent.urgency_factor = 0.5
        
        # Default regimes
        intent.market_regime = REGIME_BALANCED
        intent.volatility_regime = VOL_MEDIUM
        intent.etf_flow_regime = FLOW_NEUTRAL
        
        return intent
    
    @staticmethod
    def set_regime_context(intent: TradingIntent, market_regime: int, 
                          volatility_regime: int, etf_flow_regime: int):
        """Set market regime context for trading intent"""
        intent.market_regime = market_regime
        intent.volatility_regime = volatility_regime
        intent.etf_flow_regime = etf_flow_regime
    
    @staticmethod
    def set_execution_preferences(intent: TradingIntent, execution_style: int,
                                max_time_ms: int = 30000, urgency: float = 0.5):
        """Set execution preferences"""
        intent.execution_style = execution_style
        intent.max_execution_time_ms = max_time_ms
        intent.urgency_factor = urgency
        
        # Adjust other parameters based on urgency
        if urgency > 0.8:
            intent.max_slippage_tolerance = 0.005  # More tolerant for urgent trades
        elif urgency < 0.3:
            intent.max_slippage_tolerance = 0.001  # Less tolerant for patient trades
    
    @staticmethod
    def set_risk_parameters(intent: TradingIntent, max_impact: float = 0.01,
                           max_price: float = 0.0, min_price: float = 0.0):
        """Set risk and price parameters"""
        intent.max_market_impact = max_impact
        if max_price > 0:
            intent.max_price = max_price
        if min_price > 0:
            intent.min_price = min_price

class EnhancedTradingStrategy:
    """
    Example strategy that integrates with the communication client
    Demonstrates how to use market intelligence for signal generation
    """
    
    def __init__(self, symbols: List[str]):
        self.symbols = symbols
        self.client = TradingCommunicationClient()
        
        # Strategy state
        self.current_regimes: Dict[str, int] = {}
        self.signal_history = deque(maxlen=100)
        
        # Register for market intelligence updates
        self.client.add_intelligence_callback(self._process_intelligence)
        
        # Start client
        self.client.start()
        
        logger.info(f"Enhanced trading strategy initialized for symbols: {symbols}")
    
    def _process_intelligence(self, intel: MarketIntelligence):
        """Process incoming market intelligence"""
        # Store current regime
        self.current_regimes[intel.symbol] = intel.current_regime
        
        # Example: Log significant regime changes
        if intel.regime_confidence > 0.8:
            regime_name = self._regime_to_string(intel.current_regime)
            logger.info(f"{intel.symbol} regime: {regime_name} (confidence: {intel.regime_confidence:.2f})")
    
    def generate_trading_signal(self, symbol: str) -> Optional[TradingIntent]:
        """
        Generate trading signal based on market intelligence
        This is where you'd integrate your existing strategy logic
        """
        # Get latest market intelligence
        latest_intel = self.client.get_latest_intelligence(symbol)
        if not latest_intel:
            logger.warning(f"No market intelligence available for {symbol}")
            return None
        
        # Example signal generation logic
        signal_strength = self._calculate_signal_strength(symbol, latest_intel)
        
        if signal_strength < 0.6:
            return None  # Signal not strong enough
        
        # Determine trade direction (example logic)
        side = self._determine_trade_side(symbol, latest_intel)
        if not side:
            return None
        
        # Calculate position size
        quantity = self._calculate_position_size(symbol, signal_strength, latest_intel)
        
        # Create trading intent
        intent = TradingIntentBuilder.create_intent(
            symbol=symbol,
            side=side,
            quantity=quantity,
            strategy_id="enhanced_markov_v1"
        )
        
        # Set regime context from market intelligence
        intent.market_regime = self._map_ob_regime_to_market_regime(latest_intel.current_regime)
        intent.volatility_regime = self._determine_volatility_regime(latest_intel)
        intent.etf_flow_regime = self._determine_etf_flow_regime(latest_intel)
        
        # Set signal metadata
        intent.signal_strength = signal_strength
        intent.confidence = min(0.95, latest_intel.regime_confidence * signal_strength)
        
        # Set execution preferences based on market conditions
        execution_style = self._determine_execution_style(latest_intel, signal_strength)
        urgency = self._calculate_urgency(latest_intel, signal_strength)
        
        TradingIntentBuilder.set_execution_preferences(
            intent, execution_style, max_time_ms=30000, urgency=urgency
        )
        
        # Set risk parameters
        max_impact = self._calculate_max_market_impact(latest_intel)
        TradingIntentBuilder.set_risk_parameters(intent, max_impact=max_impact)
        
        return intent
    
    def execute_trading_intent(self, intent: TradingIntent) -> bool:
        """Send trading intent to execution engine"""
        ack = self.client.send_trading_intent(intent)
        
        if ack and ack.accepted:
            logger.info(f"Intent {intent.intent_id} accepted - Execution ID: {ack.execution_id}")
            logger.info(f"Estimated completion: {datetime.fromtimestamp(ack.estimated_completion_time/1000)}")
            
            # Log execution parameters
            params = ack.execution_params
            logger.info(f"Execution parameters: aggression={params.aggression_level:.2f}, "
                       f"chunk_ratio={params.chunk_size_ratio:.2f}")
            
            return True
        else:
            reason = ack.reason if ack else "No response from execution engine"
            logger.error(f"Intent {intent.intent_id} rejected: {reason}")
            return False
    
    def run_strategy_loop(self, interval_seconds: int = 30):
        """Main strategy loop - generates and executes signals"""
        logger.info("Starting strategy loop...")
        
        while self._running:
            try:
                # Wait for client to be connected and receiving data
                if not self.client.is_connected():
                    logger.warning("Waiting for market intelligence connection...")
                    time.sleep(1)
                    continue
                
                # Generate signals for each symbol
                for symbol in self.symbols:
                    try:
                        intent = self.generate_trading_signal(symbol)
                        if intent:
                            success = self.execute_trading_intent(intent)
                            if success:
                                self.signal_history.append({
                                    'timestamp': time.time(),
                                    'symbol': symbol,
                                    'side': intent.side,
                                    'quantity': intent.target_quantity,
                                    'signal_strength': intent.signal_strength
                                })
                    except Exception as e:
                        logger.error(f"Error processing {symbol}: {e}")
                
                # Wait before next iteration
                time.sleep(interval_seconds)
                
            except KeyboardInterrupt:
                logger.info("Strategy loop interrupted by user")
                break
            except Exception as e:
                logger.error(f"Error in strategy loop: {e}")
                time.sleep(5)  # Wait before retrying
    
    def stop_strategy(self):
        """Stop the strategy and cleanup"""
        self._running = False
        self.client.stop()
        logger.info("Strategy stopped")
    
    # Private helper methods for signal generation
    def _calculate_signal_strength(self, symbol: str, intel: MarketIntelligence) -> float:
        """Calculate signal strength based on market intelligence"""
        # Example logic - replace with your actual signal calculation
        
        # Flow-based signal
        flow_signal = 0.5
        if intel.aggressive_buy_ratio > 0.6:
            flow_signal += 0.2
        elif intel.aggressive_sell_ratio > 0.6:
            flow_signal -= 0.2
        
        # Volatility-based adjustment
        vol_factor = 1.0
        if intel.realized_volatility_1m > 0.03:
            vol_factor = 0.8  # Reduce signal in high volatility
        elif intel.realized_volatility_1m < 0.01:
            vol_factor = 1.2  # Increase signal in low volatility
        
        # Liquidity-based adjustment
        liquidity_factor = min(1.0, intel.liquidity_ratio / 0.5)
        
        # Regime confidence factor
        confidence_factor = intel.regime_confidence
        
        signal_strength = flow_signal * vol_factor * liquidity_factor * confidence_factor
        return max(0.0, min(1.0, signal_strength))
    
    def _determine_trade_side(self, symbol: str, intel: MarketIntelligence) -> Optional[str]:
        """Determine trade direction based on market intelligence"""
        # Example logic - integrate with your existing strategy
        
        # Flow-based direction
        if intel.aggressive_buy_ratio > 0.65:
            return "BUY"
        elif intel.aggressive_sell_ratio > 0.65:
            return "SELL"
        
        # Order book imbalance
        if intel.order_book_imbalance > 0.3:
            return "BUY"
        elif intel.order_book_imbalance < -0.3:
            return "SELL"
        
        return None  # No clear signal
    
    def _calculate_position_size(self, symbol: str, signal_strength: float, 
                               intel: MarketIntelligence) -> float:
        """Calculate position size based on signal strength and market conditions"""
        # Base position size (you'd integrate with your portfolio management)
        base_size = 1000.0  # Example base size
        
        # Adjust for signal strength
        size_factor = signal_strength
        
        # Adjust for volatility (smaller size in high volatility)
        if intel.realized_volatility_1m > 0.03:
            size_factor *= 0.7
        elif intel.realized_volatility_1m < 0.01:
            size_factor *= 1.3
        
        # Adjust for liquidity
        liquidity_factor = min(1.0, intel.liquidity_ratio / 0.3)
        
        return base_size * size_factor * liquidity_factor
    
    def _determine_execution_style(self, intel: MarketIntelligence, signal_strength: float) -> int:
        """Determine execution style based on market conditions"""
        if signal_strength > 0.9 and intel.liquidity_ratio > 0.7:
            return EXEC_AGGRESSIVE
        elif intel.liquidity_ratio < 0.3:
            return EXEC_STEALTH
        elif signal_strength < 0.7:
            return EXEC_PASSIVE
        else:
            return EXEC_BALANCED
    
    def _calculate_urgency(self, intel: MarketIntelligence, signal_strength: float) -> float:
        """Calculate execution urgency"""
        urgency = signal_strength * 0.6  # Base urgency from signal strength
        
        # Increase urgency in volatile conditions
        if intel.realized_volatility_1m > 0.025:
            urgency += 0.3
        
        # Increase urgency if strong flow momentum
        if abs(intel.net_flow_5s) > 1.0:
            urgency += 0.2
        
        return min(1.0, urgency)
    
    def _calculate_max_market_impact(self, intel: MarketIntelligence) -> float:
        """Calculate maximum acceptable market impact"""
        base_impact = 0.01  # 1% base
        
        # Allow more impact in liquid markets
        if intel.liquidity_ratio > 0.8:
            base_impact *= 1.5
        elif intel.liquidity_ratio < 0.3:
            base_impact *= 0.5
        
        return base_impact
    
    def _map_ob_regime_to_market_regime(self, ob_regime: int) -> int:
        """Map order book regime to market regime"""
        # Simple mapping - you can enhance this
        mapping = {
            OB_LIQUID: REGIME_BALANCED,
            OB_NORMAL: REGIME_BALANCED,
            OB_THIN: REGIME_UNCERTAIN,
            OB_VOLATILE: REGIME_PANIC
        }
        return mapping.get(ob_regime, REGIME_BALANCED)
    
    def _determine_volatility_regime(self, intel: MarketIntelligence) -> int:
        """Determine volatility regime from market intelligence"""
        vol = intel.realized_volatility_1m
        if vol > 0.03:
            return VOL_HIGH
        elif vol < 0.01:
            return VOL_LOW
        else:
            return VOL_MEDIUM
    
    def _determine_etf_flow_regime(self, intel: MarketIntelligence) -> int:
        """Determine ETF flow regime from market intelligence"""
        # Use net flow as proxy for ETF flow
        net_flow = intel.net_flow_30s
        if net_flow > 0.5:
            return FLOW_POSITIVE
        elif net_flow < -0.5:
            return FLOW_NEGATIVE
        else:
            return FLOW_NEUTRAL
    
    def _regime_to_string(self, regime: int) -> str:
        """Convert regime enum to string for logging"""
        regime_names = {
            OB_LIQUID: "LIQUID",
            OB_NORMAL: "NORMAL", 
            OB_THIN: "THIN",
            OB_VOLATILE: "VOLATILE"
        }
        return regime_names.get(regime, "UNKNOWN")

# Example usage and testing
def main():
    """Main function demonstrating client usage"""
    print("Starting Trading Communication Client Demo...")
    
    try:
        # Initialize strategy with some test symbols
        symbols = ["AAPL", "GOOGL", "MSFT"]
        strategy = EnhancedTradingStrategy(symbols)
        
        # Add custom intelligence callback for monitoring
        def intelligence_monitor(intel: MarketIntelligence):
            if intel.regime_confidence > 0.8:
                regime_name = strategy._regime_to_string(intel.current_regime)
                print(f"[INTEL] {intel.symbol}: {regime_name}, "
                      f"Spread: {intel.bid_ask_spread:.4f}, "
                      f"Liquidity: {intel.liquidity_ratio:.2f}")
        
        strategy.client.add_intelligence_callback(intelligence_monitor)
        
        # Run strategy for demo (or call run_strategy_loop for continuous operation)
        print("Client connected. Monitoring market intelligence...")
        print("Press Ctrl+C to stop...")
        
        # Example: Generate a manual test signal
        test_intent = TradingIntentBuilder.create_intent(
            symbol="AAPL",
            side="BUY", 
            quantity=100.0,
            strategy_id="demo_strategy"
        )
        
        # Wait a bit for market intelligence
        time.sleep(2)
        
        # Send test intent
        print("\nSending test trading intent...")
        success = strategy.execute_trading_intent(test_intent)
        
        if success:
            print("Test intent executed successfully!")
        else:
            print("Test intent failed")
        
        # Monitor for a while
        start_time = time.time()
        while time.time() - start_time < 30:  # Run for 30 seconds
            time.sleep(1)
            
            # Show connection status
            if strategy.client.is_connected():
                intel_count = len(strategy.client.intelligence_history)
                print(f"\rConnected - Intelligence messages received: {intel_count}", end="")
            else:
                print(f"\rDisconnected - Waiting for market intelligence...", end="")
    
    except KeyboardInterrupt:
        print("\nShutting down...")
    except Exception as e:
        logger.error(f"Strategy error: {e}")
    finally:
        if 'strategy' in locals():
            strategy.stop_strategy()

# Utility functions for debugging and monitoring
class TradingSystemMonitor:
    """Monitor and debug trading system communication"""
    
    def __init__(self):
        self.client = TradingCommunicationClient()
        self.client.start()
    
    def monitor_intelligence(self, duration_seconds: int = 60):
        """Monitor market intelligence for specified duration"""
        print(f"Monitoring market intelligence for {duration_seconds} seconds...")
        
        intel_count = 0
        symbols_seen = set()
        
        def counter_callback(intel: MarketIntelligence):
            nonlocal intel_count, symbols_seen
            intel_count += 1
            symbols_seen.add(intel.symbol)
            
            if intel_count % 10 == 0:
                print(f"Received {intel_count} intelligence messages from {len(symbols_seen)} symbols")
        
        self.client.add_intelligence_callback(counter_callback)
        
        time.sleep(duration_seconds)
        
        print(f"\nMonitoring complete:")
        print(f"  Total messages: {intel_count}")
        print(f"  Symbols: {sorted(symbols_seen)}")
        print(f"  Rate: {intel_count/duration_seconds:.1f} messages/second")
        
        self.client.stop()
    
    def test_intent_roundtrip(self, symbol: str = "AAPL"):
        """Test trading intent round-trip communication"""
        print(f"Testing trading intent round-trip for {symbol}...")
        
        # Create test intent
        intent = TradingIntentBuilder.create_intent(
            symbol=symbol,
            side="BUY",
            quantity=50.0,
            strategy_id="test_strategy"
        )
        
        intent.signal_strength = 0.85
        intent.confidence = 0.9
        intent.execution_style = EXEC_BALANCED
        intent.urgency_factor = 0.7
        
        print(f"Sending intent: {intent.intent_id}")
        
        start_time = time.time()
        ack = self.client.send_trading_intent(intent)
        round_trip_time = (time.time() - start_time) * 1000
        
        if ack:
            print(f"Round-trip time: {round_trip_time:.2f}ms")
            print(f"Accepted: {ack.accepted}")
            print(f"Execution ID: {ack.execution_id}")
            print(f"Reason: {ack.reason}")
            
            if ack.execution_params:
                params = ack.execution_params
                print(f"Execution Parameters:")
                print(f"  Aggression Level: {params.aggression_level:.2f}")
                print(f"  Chunk Size Ratio: {params.chunk_size_ratio:.2f}")
                print(f"  Max Participation: {params.max_participation_rate:.2f}")
                print(f"  Order Type: {params.preferred_order_type}")
            
            return True
        else:
            print("No acknowledgment received")
            return False

if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(description='Trading Communication Client')
    parser.add_argument('--mode', choices=['demo', 'monitor', 'test'], default='demo',
                       help='Run mode: demo strategy, monitor intelligence, or test roundtrip')
    parser.add_argument('--duration', type=int, default=60,
                       help='Duration for monitor mode (seconds)')
    parser.add_argument('--symbol', default='AAPL',
                       help='Symbol for test mode')
    
    args = parser.parse_args()
    
    try:
        if args.mode == 'demo':
            main()
        elif args.mode == 'monitor':
            monitor = TradingSystemMonitor()
            monitor.monitor_intelligence(args.duration)
        elif args.mode == 'test':
            monitor = TradingSystemMonitor()
            monitor.test_intent_roundtrip(args.symbol)
            
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)