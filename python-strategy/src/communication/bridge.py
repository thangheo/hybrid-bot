#!/usr/bin/env python3
"""
Integrated Python Strategy Engine
Combines existing Markov Chain strategy with real-time C++ market intelligence
"""
from signal import signal, SIGINT, SIGTERM
import zmq
import time
import threading
import uuid
import sys
import os
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple
from collections import deque
import pandas as pd
import numpy as np
import socket  # Standard library socket module

# Add protobuf path
import os 
current_dir = os.path.dirname(os.path.abspath(__file__))
proto_path = os.path.join(current_dir, 'generated', 'python', 'proto')
sys.path.append(proto_path)

import trading_messages_pb2 as tm

@dataclass
class MarketRegimeState:
    """Enhanced market regime state with C++ intelligence"""
    primary_regime: int  # MarketRegime enum value
    confidence: float
    regime_strength: float
    transition_probability: Dict[int, float]
    last_update: float
    
    # Enhanced with C++ intelligence
    order_book_regime: int  # OrderBookRegime enum value
    liquidity_score: float
    volatility_trend: float
    flow_momentum: float

def signalHandler(signum, frame):
    """Handle termination signals gracefully"""
    global g_running
    print(f"\nReceived signal {signum}, shutting down...")
    g_running = False
 
def check_zmq_server_availability():
    """Check if ZMQ servers are actually running"""
    print("=== ZMQ SERVER DIAGNOSTICS ===")
    
    # Check if ports are open
    def check_port(host, port, service_name):
        try:
            # Fixed: Use different variable name to avoid shadowing the socket module
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(2)
            result = sock.connect_ex((host, port))
            sock.close()
            
            if result == 0:
                print(f"✓ {service_name} port {port} is OPEN")
                return True
            else:
                print(f"✗ {service_name} port {port} is CLOSED")
                return False
        except Exception as e:
            print(f"✗ {service_name} port check failed: {e}")
            return False
    
    # Check both ZMQ ports
    intelligence_port_open = check_port("localhost", 5555, "Market Intelligence")
    execution_port_open = check_port("localhost", 5558, "Execution Engine")
    
    # Test ZMQ connection specifically
    if intelligence_port_open:
        print("Testing ZMQ intelligence connection...")
        try:
            context = zmq.Context()
            # Fixed: Use different variable name to avoid conflict with socket module
            zmq_socket = context.socket(zmq.SUB)
            zmq_socket.setsockopt(zmq.SUBSCRIBE, b"MARKET_INTEL")
            zmq_socket.setsockopt(zmq.RCVTIMEO, 2000)
            zmq_socket.connect("tcp://localhost:5555")
            
            # Try to receive a message
            try:
                topic = zmq_socket.recv(zmq.NOBLOCK)
                data = zmq_socket.recv(zmq.NOBLOCK)
                print("✓ Successfully received intelligence data")
            except zmq.Again:
                print("⚠ Connected but no intelligence data flowing")
            
            zmq_socket.close()
            context.term()
            
        except Exception as e:
            print(f"✗ ZMQ intelligence test failed: {e}")
    
    if execution_port_open:
        print("Testing ZMQ execution connection...")
        try:
            context = zmq.Context()
            # Fixed: Use different variable name to avoid conflict with socket module
            zmq_socket = context.socket(zmq.DEALER)
            zmq_socket.setsockopt(zmq.IDENTITY, b"test_client")
            zmq_socket.setsockopt(zmq.RCVTIMEO, 2000)
            zmq_socket.setsockopt(zmq.SNDTIMEO, 2000)
            zmq_socket.connect("tcp://localhost:5558")
            
            # Send a test message
            test_msg = b"test_connection"
            zmq_socket.send_multipart([b"test_id", test_msg])
            
            try:
                response = zmq_socket.recv_multipart()
                print("✓ Execution engine responded to test")
            except zmq.Again:
                print("⚠ Connected but execution engine not responding")
            
            zmq_socket.close()
            context.term()
            
        except Exception as e:
            print(f"✗ ZMQ execution test failed: {e}")
    
    print("==============================")
    return intelligence_port_open and execution_port_open

class MarketIntelligenceIntegrator:
    """Integrates C++ market intelligence with Python strategy"""
    
    def __init__(self, symbols: List[str]):
        self.symbols = symbols
        self.intelligence_history: Dict[str, deque] = {
            symbol: deque(maxlen=1000) for symbol in symbols
        }
        self.current_regimes: Dict[str, MarketRegimeState] = {}
        self.running = False
        
        # ZeroMQ setup
        self.context = zmq.Context()
        self.intelligence_subscriber = None
        self.intent_publisher = None
        
        # Connection tracking
        self.connection_retry_count = 0
        self.max_connection_retries = 5
        self.last_intent_time = 0
        
        # Threading
        self.intelligence_thread = None
        self.intelligence_lock = threading.Lock()
        
        print("Market Intelligence Integrator initialized")

    def start(self):
        """Start market intelligence integration with diagnostics"""
        if self.running:
            return True
        
        try:
            # Setup ZMQ connections with improved error handling
            self.intelligence_subscriber = self.context.socket(zmq.SUB)
            self.intelligence_subscriber.connect("tcp://localhost:5555")
            self.intelligence_subscriber.setsockopt(zmq.SUBSCRIBE, b"MARKET_INTEL")
            self.intelligence_subscriber.setsockopt(zmq.RCVTIMEO, 1000)
            
            # Use DEALER socket for better reliability
            self.intent_publisher = self.context.socket(zmq.DEALER)
            self.intent_publisher.setsockopt(zmq.IDENTITY, b"python_strategy")
            self.intent_publisher.connect("tcp://localhost:5558")
            self.intent_publisher.setsockopt(zmq.RCVTIMEO, 10000)
            self.intent_publisher.setsockopt(zmq.SNDTIMEO, 5000)
            
            # Wait a moment for connections to establish
            time.sleep(0.5)
            
            # Start intelligence monitoring thread
            self.running = True
            self.intelligence_thread = threading.Thread(target=self._intelligence_loop)
            self.intelligence_thread.daemon = True
            self.intelligence_thread.start()
            
            print("Market intelligence integration started")
            return True
            
        except Exception as e:
            print(f"Failed to start intelligence integration: {e}")
            return False
    
    def stop(self):
        """Stop market intelligence integration"""
        self.running = False
        
        if self.intelligence_thread:
            self.intelligence_thread.join(timeout=2)
        
        if self.intelligence_subscriber:
            self.intelligence_subscriber.close()
        if self.intent_publisher:
            self.intent_publisher.close()
            
        print("Market intelligence integration stopped")

    def _intelligence_loop(self):
        """Background thread to receive market intelligence"""
        intelligence_count = 0
        
        while self.running:
            try:
                # Receive market intelligence
                topic = self.intelligence_subscriber.recv()
                data = self.intelligence_subscriber.recv()
                
                # Parse message
                intel = tm.MarketIntelligence()
                intel.ParseFromString(data)
                symbol = intel.symbol.replace('/', '')
                # Update intelligence history
                with self.intelligence_lock:
                    self.intelligence_history[symbol].append({
                        'timestamp': intel.timestamp,
                        'bid_ask_spread': intel.bid_ask_spread,
                        'order_book_imbalance': intel.order_book_imbalance,
                        'liquidity_ratio': intel.liquidity_ratio,
                        'realized_volatility_1m': intel.realized_volatility_1m,
                        'net_flow_5s': intel.net_flow_5s,
                        'net_flow_30s': intel.net_flow_30s,
                        'aggressive_buy_ratio': intel.aggressive_buy_ratio,
                        'current_regime': intel.current_regime,
                        'regime_confidence': intel.regime_confidence
                    })
                
                # Update market regime
                self._update_market_regime(symbol, intel)
                
                intelligence_count += 1
                if intelligence_count % 20 == 0:
                    print(f"Processed {intelligence_count} intelligence updates")
                    
            except zmq.Again:
                # Timeout - continue
                continue
            except Exception as e:
                print(f"Error in intelligence loop: {e}")
                time.sleep(1)

    def _update_market_regime(self, symbol: str, intel: tm.MarketIntelligence):
        """Update market regime based on C++ intelligence"""
        
        # Get historical data for trend analysis
        with self.intelligence_lock:
            history = list(self.intelligence_history[symbol])
        
        if len(history) < 10:
            return
        
        # Calculate regime scores using C++ intelligence
        regime_scores = self._calculate_enhanced_regime_scores(history, intel)
        
        # Determine primary regime
        primary_regime = max(regime_scores, key=regime_scores.get)
        confidence = regime_scores[primary_regime]
        
        # Calculate regime strength
        regime_strength = self._calculate_regime_strength(history, intel)
        
        # Create enhanced regime state
        regime_state = MarketRegimeState(
            primary_regime=primary_regime,
            confidence=confidence,
            regime_strength=regime_strength,
            transition_probability=self._estimate_transitions(history, primary_regime),
            last_update=time.time(),
            order_book_regime=intel.current_regime,
            liquidity_score=intel.liquidity_ratio,
            volatility_trend=intel.realized_volatility_1m,
            flow_momentum=intel.net_flow_5s - intel.net_flow_30s
        )
        
        self.current_regimes[symbol] = regime_state

    def _calculate_enhanced_regime_scores(self, history: List[dict], intel: tm.MarketIntelligence) -> Dict[int, float]:
        """Calculate regime scores enhanced with C++ intelligence"""
        
        scores = {regime: 0.0 for regime in range(9)}  # 9 market regimes
        
        # Current conditions from C++ intelligence
        spread = intel.bid_ask_spread
        imbalance = abs(intel.order_book_imbalance)
        flow_ratio = intel.aggressive_buy_ratio - intel.aggressive_sell_ratio
        volatility = intel.realized_volatility_1m
        liquidity = intel.liquidity_ratio
        
        # Flow acceleration from history
        recent_flow = np.mean([h['net_flow_5s'] for h in history[-5:]])
        older_flow = np.mean([h['net_flow_5s'] for h in history[-15:-5]]) if len(history) >= 15 else recent_flow
        flow_acceleration = recent_flow - older_flow
        
        # ACCUMULATION indicators (enhanced with C++ data)
        if (liquidity > 0.8 and volatility < 0.01 and 
            abs(flow_ratio) < 0.1 and spread < 0.001 and
            intel.current_regime == tm.OB_LIQUID):
            scores[tm.REGIME_ACCUMULATION] += 0.5
            
        # EUPHORIA indicators
        if (abs(flow_ratio) > 0.4 and flow_acceleration > 0.2 and
            intel.aggressive_buy_ratio > 0.7 and
            intel.current_regime in [tm.OB_VOLATILE, tm.OB_NORMAL]):
            scores[tm.REGIME_EUPHORIA] += 0.6
            
        # INSTITUTIONAL_FOMO indicators  
        if (abs(flow_ratio) > 0.3 and liquidity > 0.6 and
            volatility > 0.005 and intel.net_flow_30s > intel.net_flow_5s):
            scores[tm.REGIME_INSTITUTIONAL_FOMO] += 0.5
            
        # PANIC indicators (enhanced with order book regime)
        if (volatility > 0.03 and spread > 0.005 and
            liquidity < 0.3 and abs(flow_ratio) > 0.5 and
            intel.current_regime in [tm.OB_THIN, tm.OB_VOLATILE]):
            scores[tm.REGIME_PANIC] += 0.7
            
        # DISTRIBUTION indicators
        if (intel.aggressive_sell_ratio > 0.6 and
            imbalance > 0.4 and liquidity > 0.4):
            scores[tm.REGIME_DISTRIBUTION] += 0.5
            
        # SIDEWAYS indicators
        if (abs(flow_ratio) < 0.1 and imbalance < 0.2 and
            volatility < 0.01 and intel.current_regime == tm.OB_NORMAL):
            scores[tm.REGIME_SIDEWAYS] += 0.4
        
        # Normalize scores
        total_score = sum(scores.values())
        if total_score > 0:
            scores = {k: v/total_score for k, v in scores.items()}
        else:
            scores[tm.REGIME_BALANCED] = 1.0
            
        return scores

    def _calculate_regime_strength(self, history: List[dict], intel: tm.MarketIntelligence) -> float:
        """Calculate regime strength using C++ intelligence"""
        if len(history) < 20:
            return 0.5
        
        # Coefficient of variation for key metrics
        recent_data = history[-20:]
        
        spreads = [h['bid_ask_spread'] for h in recent_data]
        flows = [h['net_flow_5s'] for h in recent_data]
        imbalances = [h['order_book_imbalance'] for h in recent_data]
        
        spread_cv = np.std(spreads) / (np.mean(spreads) + 0.0001)
        flow_cv = np.std(flows) / (abs(np.mean(flows)) + 0.001)
        imbalance_cv = np.std(imbalances)
        
        # Include C++ order book regime stability
        regime_stability = intel.regime_confidence if hasattr(intel, 'regime_confidence') else 0.5
        
        strength = np.mean([spread_cv, flow_cv, imbalance_cv]) * regime_stability
        return min(1.0, strength * 2)

    def _estimate_transitions(self, history: List[dict], current_regime: int) -> Dict[int, float]:
        """Estimate regime transition probabilities"""
        transitions = {regime: 0.1 for regime in range(9)}
        
        if len(history) < 10:
            return transitions
        
        # Analyze trends
        recent_trend = self._analyze_trends(history)
        
        # Regime-specific transition logic
        if current_regime == tm.REGIME_ACCUMULATION:
            if recent_trend['flow_increasing']:
                transitions[tm.REGIME_INSTITUTIONAL_FOMO] = 0.3
            if recent_trend['volatility_increasing']:
                transitions[tm.REGIME_BALANCED] = 0.2
                
        elif current_regime == tm.REGIME_EUPHORIA:
            if recent_trend['flow_decreasing']:
                transitions[tm.REGIME_DISTRIBUTION] = 0.4
            if recent_trend['volatility_increasing']:
                transitions[tm.REGIME_PANIC] = 0.2
                
        elif current_regime == tm.REGIME_PANIC:
            if recent_trend['volatility_decreasing']:
                transitions[tm.REGIME_ACCUMULATION] = 0.3
                transitions[tm.REGIME_SIDEWAYS] = 0.2
        
        # Normalize
        total = sum(transitions.values())
        return {k: v/total for k, v in transitions.items()}

    def _analyze_trends(self, history: List[dict]) -> Dict[str, bool]:
        """Analyze recent trends in market intelligence"""
        if len(history) < 20:
            return {key: False for key in ['flow_increasing', 'flow_decreasing', 
                                         'volatility_increasing', 'volatility_decreasing']}
        
        recent = history[-10:]
        older = history[-20:-10]
        
        recent_flow = np.mean([h['net_flow_5s'] for h in recent])
        older_flow = np.mean([h['net_flow_5s'] for h in older])
        
        recent_vol = np.mean([h['realized_volatility_1m'] for h in recent])
        older_vol = np.mean([h['realized_volatility_1m'] for h in older])
        
        return {
            'flow_increasing': recent_flow > older_flow * 1.1,
            'flow_decreasing': recent_flow < older_flow * 0.9,
            'volatility_increasing': recent_vol > older_vol * 1.1,
            'volatility_decreasing': recent_vol < older_vol * 0.9,
        }

    def get_current_regime(self, symbol: str) -> Optional[MarketRegimeState]:
        """Get current market regime for symbol"""
        return self.current_regimes.get(symbol)

    def get_execution_urgency(self, symbol: str) -> float:
        """Calculate execution urgency based on market intelligence"""
        regime_state = self.current_regimes.get(symbol)
        if not regime_state:
            return 0.5
        
        urgency = 0.5  # Base urgency
        
        # Regime-based urgency
        if regime_state.primary_regime in [tm.REGIME_EUPHORIA, tm.REGIME_PANIC]:
            urgency += 0.3
        elif regime_state.primary_regime in [tm.REGIME_ACCUMULATION, tm.REGIME_SIDEWAYS]:
            urgency -= 0.2
        
        # Order book regime urgency
        if regime_state.order_book_regime == tm.OB_VOLATILE:
            urgency += 0.2
        elif regime_state.order_book_regime == tm.OB_THIN:
            urgency -= 0.1
        
        # Volatility urgency
        if regime_state.volatility_trend > 0.03:
            urgency += 0.2
        
        # Flow momentum urgency
        if abs(regime_state.flow_momentum) > 0.5:
            urgency += 0.1
        
        return max(0.1, min(1.0, urgency))

    def _reconnect_publisher(self):
        """Reconnect the intent publisher"""
        if self.connection_retry_count >= self.max_connection_retries:
            print(f"Max reconnection attempts reached ({self.max_connection_retries})")
            return False
        
        try:
            print(f"Reconnecting intent publisher (attempt {self.connection_retry_count + 1})...")
            
            if self.intent_publisher:
                self.intent_publisher.close()
            
            self.intent_publisher = self.context.socket(zmq.DEALER)
            self.intent_publisher.setsockopt(zmq.IDENTITY, b"python_strategy")
            self.intent_publisher.connect("tcp://localhost:5558")
            self.intent_publisher.setsockopt(zmq.RCVTIMEO, 10000)
            self.intent_publisher.setsockopt(zmq.SNDTIMEO, 5000)
            
            self.connection_retry_count += 1
            time.sleep(1)  # Brief pause
            
            print("Intent publisher reconnected")
            return True
            
        except Exception as e:
            print(f"Reconnection failed: {e}")
            self.connection_retry_count += 1
            return False


class IntegratedTradingStrategy:
    """Enhanced trading strategy with C++ pipeline integration"""
    
    def __init__(self, symbols: List[str]):
        self.symbols = symbols
        self.intelligence_integrator = MarketIntelligenceIntegrator(symbols)
        
        # Strategy state
        self.active_positions = {}
        self.last_signals = {}
        self.signal_cooldowns = {}
        
        # Performance tracking
        self.trades_executed = 0
        self.trades_rejected = 0
        self.connection_errors = 0
        self.total_pnl = 0.0
        self.start_time = time.time()
        
        print(f"Integrated strategy initialized for {len(symbols)} symbols")

    def start(self):
        """Start the integrated strategy"""
        if not self.intelligence_integrator.start():
            return False
        
        print("Integrated trading strategy started")
        return True
    
    def stop(self):
        """Stop the integrated strategy"""
        self.intelligence_integrator.stop()
        print("Integrated trading strategy stopped")

    def generate_enhanced_signal(self, symbol: str) -> Optional[tm.TradingIntent]:
        """Generate trading signal enhanced with C++ market intelligence"""
        
        # Check cooldown
        if symbol in self.signal_cooldowns:
            if time.time() - self.signal_cooldowns[symbol] < 30:  # 30 second cooldown
                return None
        
        # Get current market regime from C++ intelligence
        regime_state = self.intelligence_integrator.get_current_regime(symbol)
        if not regime_state:
            return None
        
        # Calculate signal strength
        signal_strength = self._calculate_signal_strength(symbol, regime_state)
        
        if signal_strength < 0.7:  # Only trade strong signals
            return None
        
        # Determine trade side based on regime and intelligence
        side = self._determine_trade_side(regime_state)
        if side is None:
            return None
        
        # Calculate position size
        quantity = self._calculate_position_size(symbol, regime_state, signal_strength)
        
        # Create trading intent
        intent = tm.TradingIntent()
        intent.intent_id = f"{symbol}_{int(time.time())}_{str(uuid.uuid4())[:8]}"
        intent.timestamp = int(time.time() * 1000)
        intent.strategy_id = "integrated_strategy_v1"
        
        # Basic trade info
        intent.symbol = symbol
        intent.side = side
        intent.target_quantity = quantity
        intent.max_price = 0  # No price limit for demo
        intent.min_price = 0
        
        # Strategic context from regime analysis
        intent.market_regime = regime_state.primary_regime
        intent.volatility_regime = self._determine_volatility_regime(regime_state)
        intent.etf_flow_regime = self._determine_etf_flow_regime(regime_state)
        
        # Signal metadata
        intent.signal_strength = signal_strength
        intent.confidence = regime_state.confidence
        intent.signal_timestamp = int(time.time() * 1000)
        
        # Execution preferences based on market intelligence
        intent.execution_style = self._determine_execution_style(regime_state)
        intent.urgency_factor = self.intelligence_integrator.get_execution_urgency(symbol)
        intent.max_slippage_tolerance = 0.002 * (1 + regime_state.volatility_trend)
        intent.allow_partial_fills = True
        intent.max_market_impact = 0.01 if regime_state.liquidity_score > 0.5 else 0.005
        
        # Timing based on regime
        if regime_state.primary_regime in [tm.REGIME_EUPHORIA, tm.REGIME_PANIC]:
            intent.max_execution_time_ms = 10000  # Fast execution
        elif regime_state.primary_regime == tm.REGIME_ACCUMULATION:
            intent.max_execution_time_ms = 60000  # Patient execution
        else:
            intent.max_execution_time_ms = 30000  # Balanced
        
        return intent

    def _calculate_signal_strength(self, symbol: str, regime_state: MarketRegimeState) -> float:
        """Calculate signal strength based on regime and market intelligence"""
        
        base_strength = 0.5
        
        # Regime-based strength
        if regime_state.primary_regime in [tm.REGIME_EUPHORIA, tm.REGIME_PANIC]:
            base_strength += 0.3
        elif regime_state.primary_regime in [tm.REGIME_INSTITUTIONAL_FOMO, tm.REGIME_DISTRIBUTION]:
            base_strength += 0.2
        elif regime_state.primary_regime == tm.REGIME_ACCUMULATION:
            base_strength += 0.1
        
        # Confidence adjustment
        base_strength *= regime_state.confidence
        
        # Order book regime adjustment
        if regime_state.order_book_regime == tm.OB_LIQUID:
            base_strength *= 1.1
        elif regime_state.order_book_regime == tm.OB_THIN:
            base_strength *= 0.8
        
        # Flow momentum adjustment
        if abs(regime_state.flow_momentum) > 0.3:
            base_strength += 0.1
        
        return min(1.0, max(0.0, base_strength))

    def _determine_trade_side(self, regime_state: MarketRegimeState) -> Optional[int]:
        """Determine trade side based on regime analysis"""
        
        # Buy signals
        if regime_state.primary_regime in [tm.REGIME_ACCUMULATION, tm.REGIME_INSTITUTIONAL_FOMO]:
            if regime_state.flow_momentum > 0.1:
                return tm.SIDE_BUY
        
        # Sell signals  
        if regime_state.primary_regime in [tm.REGIME_DISTRIBUTION, tm.REGIME_EUPHORIA]:
            if regime_state.flow_momentum < -0.1:
                return tm.SIDE_SELL
        
        # Momentum-based signals
        if abs(regime_state.flow_momentum) > 0.3:
            return tm.SIDE_BUY if regime_state.flow_momentum > 0 else tm.SIDE_SELL
        
        return tm.SIDE_UNKNOWN

    def _calculate_position_size(self, symbol: str, regime_state: MarketRegimeState, signal_strength: float) -> float:
        """Calculate position size based on regime and signal strength"""
        
        base_size = 100.0  # Base position size
        
        # Adjust for signal strength
        size = base_size * signal_strength
        
        # Adjust for regime
        if regime_state.primary_regime == tm.REGIME_PANIC:
            size *= 0.5  # Smaller positions in panic
        elif regime_state.primary_regime == tm.REGIME_EUPHORIA:
            size *= 1.5  # Larger positions in euphoria
        
        # Adjust for liquidity
        size *= regime_state.liquidity_score
        
        # Adjust for volatility
        if regime_state.volatility_trend > 0.02:
            size *= 0.7  # Smaller in high volatility
        
        return max(10.0, min(1000.0, size))  # Bounds check

    def _determine_execution_style(self, regime_state: MarketRegimeState) -> int:
        """Determine execution style based on market conditions"""
        
        # Aggressive in momentum regimes
        if regime_state.primary_regime in [tm.REGIME_EUPHORIA, tm.REGIME_PANIC]:
            return tm.EXEC_AGGRESSIVE
        
        # Passive in accumulation
        if regime_state.primary_regime == tm.REGIME_ACCUMULATION:
            return tm.EXEC_PASSIVE
        
        # Stealth in thin markets
        if regime_state.order_book_regime == tm.OB_THIN:
            return tm.EXEC_STEALTH
        
        return tm.EXEC_BALANCED

    def _determine_volatility_regime(self, regime_state: MarketRegimeState) -> int:
        """Determine volatility regime"""
        if regime_state.volatility_trend > 0.025:
            return tm.VOL_HIGH
        elif regime_state.volatility_trend < 0.01:
            return tm.VOL_LOW
        else:
            return tm.VOL_MEDIUM

    def _determine_etf_flow_regime(self, regime_state: MarketRegimeState) -> int:
        """Determine ETF flow regime"""
        if regime_state.flow_momentum > 0.2:
            return tm.FLOW_POSITIVE
        elif regime_state.flow_momentum < -0.2:
            return tm.FLOW_NEGATIVE
        else:
            return tm.FLOW_NEUTRAL

    def send_trading_intent(self, intent: tm.TradingIntent) -> bool:
        """Send trading intent to C++ execution engine with improved error handling"""
        
        # Rate limiting - prevent spam
        current_time = time.time()
        if current_time - self.intelligence_integrator.last_intent_time < 5:
            print("Rate limit: Skipping intent (too frequent)")
            return False
        
        retry_count = 0
        max_retries = 3
        
        while retry_count < max_retries:
            try:
                # Check if publisher is available
                if not self.intelligence_integrator.intent_publisher:
                    print("No intent publisher available")
                    return False
                
                # Create unique request ID for DEALER socket
                request_id = f"{intent.symbol}_{int(time.time() * 1000)}"
                
                # Serialize intent
                intent_data = intent.SerializeToString()
                
                print(f"Sending intent (attempt {retry_count + 1}): {intent.intent_id}")
                
                # Send with request ID for DEALER socket
                self.intelligence_integrator.intent_publisher.send_multipart([
                    request_id.encode(), 
                    intent_data
                ])
                
                # Try to receive acknowledgment
                try:
                    response_parts = self.intelligence_integrator.intent_publisher.recv_multipart()
                    
                    if len(response_parts) >= 2:
                        response_id = response_parts[0].decode()
                        response_data = response_parts[1]
                        
                        # Parse acknowledgment
                        ack = tm.ExecutionAcknowledgment()
                        ack.ParseFromString(response_data)
                        
                        if ack.accepted:
                            print(f"✓ Intent accepted: {ack.execution_id}")
                            print(f"  Execution ID: {ack.execution_id}")
                            print(f"  Estimated completion: {ack.estimated_completion_time}")
                            print(f"  Reason: {ack.reason}")
                            
                            # Update tracking
                            self.signal_cooldowns[intent.symbol] = current_time
                            self.intelligence_integrator.last_intent_time = current_time
                            self.trades_executed += 1
                            self.intelligence_integrator.connection_retry_count = 0  # Reset on success
                            
                            return True
                        else:
                            print(f"✗ Intent rejected: {ack.reason}")
                            self.trades_rejected += 1
                            return False
                    else:
                        print("Invalid response format")
                        
                except zmq.Again:
                    print("Timeout waiting for acknowledgment")
                    retry_count += 1
                    if retry_count < max_retries:
                        print(f"Retrying in 2 seconds... ({retry_count}/{max_retries})")
                        time.sleep(2)
                    continue
                    
            except zmq.ZMQError as e:
                print(f"ZMQ Error (attempt {retry_count + 1}): {e}")
                self.connection_errors += 1
                
                if e.errno == zmq.EAGAIN:  # Resource temporarily unavailable
                    print("Socket busy, attempting reconnection...")
                    if self.intelligence_integrator._reconnect_publisher():
                        retry_count += 1
                        continue
                    else:
                        break
                else:
                    retry_count += 1
                    if retry_count < max_retries:
                        time.sleep(1)
                    continue
                    
            except Exception as e:
                print(f"Unexpected error sending intent (attempt {retry_count + 1}): {e}")
                retry_count += 1
                if retry_count < max_retries:
                    time.sleep(1)
        
        print(f"Failed to send intent after {max_retries} attempts")
        return False

    def print_strategy_status(self):
        """Print enhanced strategy performance status"""
        elapsed = time.time() - self.start_time
        
        print(f"\n=== STRATEGY STATUS ===")
        print(f"Runtime: {elapsed:.0f}s")
        print(f"Trades executed: {self.trades_executed}")
        print(f"Trades rejected: {self.trades_rejected}")
        print(f"Connection errors: {self.connection_errors}")
        print(f"Success rate: {self.trades_executed/(max(1, self.trades_executed + self.trades_rejected))*100:.1f}%")
        print(f"Active regimes: {len(self.intelligence_integrator.current_regimes)}")
        
        # Show current regimes with details
        for symbol, regime_state in self.intelligence_integrator.current_regimes.items():
            print(f"  {symbol}: Regime {regime_state.primary_regime} "
                  f"(conf: {regime_state.confidence:.2f}, "
                  f"liq: {regime_state.liquidity_score:.2f})")
        
        print("========================")

    def run_strategy_loop(self):
        """Main strategy execution loop with improved error handling"""
        print("Starting strategy loop...")
        
        loop_count = 0
        consecutive_errors = 0
        max_consecutive_errors = 10
        
        # Ensure g_running is defined
        global g_running
        try:
            g_running
        except NameError:
            g_running = True

        while g_running:
            try:
                loop_count += 1
                
                # Check for too many consecutive errors
                if consecutive_errors >= max_consecutive_errors:
                    print(f"Too many consecutive errors ({consecutive_errors}), pausing...")
                    time.sleep(10)
                    consecutive_errors = 0
                
                # Process each symbol
                symbols_processed = 0
                for symbol in self.symbols:
                    try:
                        # Generate signal if conditions are met
                        intent = self.generate_enhanced_signal(symbol)
                        
                        if intent:
                            print(f"\n--- SIGNAL GENERATED ---")
                            print(f"Symbol: {intent.symbol}")
                            print(f"Side: {'BUY' if intent.side == tm.SIDE_BUY else 'SELL'}")
                            print(f"Quantity: {intent.target_quantity:.1f}")
                            print(f"Signal Strength: {intent.signal_strength:.2f}")
                            print(f"Market Regime: {intent.market_regime}")
                            print(f"Execution Style: {intent.execution_style}")
                            print(f"Urgency: {intent.urgency_factor:.2f}")
                            
                            # Send to C++ execution engine
                            if self.send_trading_intent(intent):
                                print("✓ Intent sent successfully")
                                consecutive_errors = 0  # Reset error count on success
                            else:
                                print("✗ Failed to send intent")
                                consecutive_errors += 1
                            print("------------------------")
                            
                        symbols_processed += 1
                        
                    except Exception as e:
                        print(f"Error processing {symbol}: {e}")
                        consecutive_errors += 1
                
                # Print periodic status
                if loop_count % 20 == 0:  # Every ~20 seconds
                    self.print_strategy_status()
                
                # Sleep between iterations
                time.sleep(1)
                
            except KeyboardInterrupt:
                print("Keyboard interrupt received")
                break
            except Exception as e:
                print(f"Error in strategy loop: {e}")
                consecutive_errors += 1
                time.sleep(2)


def main():
    """Main application entry point with better error handling"""
    signal(SIGINT, signalHandler)
    signal(SIGTERM, signalHandler)
    
    print("=== INTEGRATED TRADING STRATEGY ===")
    print("Combining C++ pipeline intelligence with Python strategy")
    
    # Run initial diagnostics
    # if not check_zmq_server_availability():
    #     print("\n❌ CRITICAL: C++ servers not detected!")
    #     print("Please make sure the integrated_trading_server is running:")
    #     print("  ./build/full_bridge")
    #     print("\nWaiting 10 seconds for servers to start...")
    #     time.sleep(10)
        
    #     # Check again
    #     if not check_zmq_server_availability():
    #         print("❌ Servers still not available. Exiting.")
    #         return 1
    
    try:
        # Symbol list
        symbols = ["BTCUSDT", "ETHUSDT", "SOLUSDT", "XRPUSDT", "ADAUSDT",
                   "AVAXUSDT", "DOTUSDT", "LINKUSDT", "UNIUSDT", "ATOMUSDT",
                   "ALGOUSDT", "AAVEUSDT", "ARBUSDT", "OPUSDT", "INJUSDT",
                   "SUIUSDT", "TRBUSDT", "BANDUSDT", "CHZUSDT", "ENJUSDT",
                   "YFIUSDT", "1INCHUSDT", "DOGEUSDT", "WIFUSDT"]

        # Create and start strategy
        strategy = IntegratedTradingStrategy(symbols)
        
        if not strategy.start():
            print("Failed to start strategy")
            return 1
        
        print(f"\n✓ Strategy running with {len(symbols)} symbols...")
        print("✓ Receiving real-time market intelligence from C++ pipeline")
        print("✓ Generating trading signals based on enhanced regime detection")
        print("Press Ctrl+C to stop...")
        
        # Wait a moment for intelligence to start flowing
        print("Waiting for market intelligence...")
        time.sleep(5)
        
        # Check if we're receiving intelligence
        if len(strategy.intelligence_integrator.current_regimes) == 0:
            print("⚠ WARNING: No market intelligence received yet")
            print("  This is normal for the first 30-60 seconds")
        else:
            print(f"✓ Receiving intelligence for {len(strategy.intelligence_integrator.current_regimes)} symbols")
        
        # Run main strategy loop
        strategy.run_strategy_loop()
        
        print("\nStopping strategy...")
        strategy.stop()
        
    except Exception as e:
        print(f"Strategy error: {e}")
        import traceback
        traceback.print_exc()
        return 1
    
    print("Strategy shutdown complete")
    return 0


if __name__ == "__main__":
    sys.exit(main())