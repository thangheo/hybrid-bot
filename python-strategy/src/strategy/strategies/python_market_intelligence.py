import zmq
import time
import pandas as pd
import numpy as np
from collections import deque
from dataclasses import dataclass
from typing import Dict, List, Optional
import sys
import os 
current_dir = os.path.dirname(os.path.abspath(__file__))
proto_path = os.path.join(current_dir, 'generated', 'python', 'proto')
sys.path.append(proto_path)

from trading_messages_pb2 import MarketIntelligence, TradingIntent, MarketRegime

@dataclass
class MarketRegimeState:
    """Current market regime assessment"""
    primary_regime: MarketRegime
    confidence: float
    regime_strength: float
    transition_probability: Dict[MarketRegime, float]
    last_update: float

class MarketIntelligenceProcessor:
    """Process real-time market intelligence from C++ execution engine"""
    
    def __init__(self, symbols: List[str]):
        self.symbols = symbols
        self.intelligence_history: Dict[str, deque] = {
            symbol: deque(maxlen=1000) for symbol in symbols
        }
        self.current_regimes: Dict[str, MarketRegimeState] = {}
        
        # ZeroMQ subscriber for market intelligence
        self.context = zmq.Context()
        self.subscriber = self.context.socket(zmq.SUB)
        self.subscriber.connect("tcp://localhost:5555")
        self.subscriber.setsockopt(zmq.SUBSCRIBE, b"MARKET_INTEL")
        
        # Regime detection parameters
        self.regime_thresholds = {
            'spread_tight': 0.001,      # Tight spread threshold
            'spread_wide': 0.005,       # Wide spread threshold
            'flow_strong': 0.3,         # Strong flow threshold
            'imbalance_high': 0.4,      # High imbalance threshold
            'volatility_low': 0.01,     # Low volatility threshold
            'volatility_high': 0.03     # High volatility threshold
        }

    def receive_market_intelligence(self) -> Optional[MarketIntelligence]:
        """Receive market intelligence from C++ execution engine"""
        try:
            # Non-blocking receive
            topic, data = self.subscriber.recv_multipart(zmq.NOBLOCK)
            intel = MarketIntelligence()
            intel.ParseFromString(data)
            
            # Store in history
            self.intelligence_history[intel.symbol].append({
                'timestamp': intel.timestamp,
                'bid_ask_spread': intel.bid_ask_spread,
                'order_book_imbalance': intel.order_book_imbalance,
                'depth_imbalance': intel.depth_imbalance,
                'liquidity_ratio': intel.liquidity_ratio,
                'net_flow_5s': intel.net_flow_5s,
                'net_flow_30s': intel.net_flow_30s,
                'aggressive_buy_ratio': intel.aggressive_buy_ratio,
                'aggressive_sell_ratio': intel.aggressive_sell_ratio,
                'realized_volatility_1m': intel.realized_volatility_1m,
                'price_impact_estimate': intel.price_impact_estimate,
                'current_regime': intel.current_regime,
                'regime_confidence': intel.regime_confidence
            })
            
            return intel
            
        except zmq.Again:
            return None  # No message available

    def update_market_regime(self, symbol: str, intel: MarketIntelligence) -> MarketRegimeState:
        """Update market regime based on market intelligence"""
        
        # Get recent history for trend analysis
        history = list(self.intelligence_history[symbol])
        if len(history) < 10:
            return self.get_default_regime_state()
        
        recent_data = pd.DataFrame(history[-50:])  # Last 50 updates
        
        # Analyze multiple regime indicators
        regime_scores = self._calculate_regime_scores(recent_data, intel)
        
        # Determine primary regime
        primary_regime = max(regime_scores, key=regime_scores.get)
        confidence = regime_scores[primary_regime]
        
        # Calculate regime strength (how pronounced the signals are)
        regime_strength = self._calculate_regime_strength(recent_data, intel)
        
        # Estimate transition probabilities
        transition_probs = self._estimate_transition_probabilities(
            recent_data, current_regime=primary_regime
        )
        
        regime_state = MarketRegimeState(
            primary_regime=primary_regime,
            confidence=confidence,
            regime_strength=regime_strength,
            transition_probability=transition_probs,
            last_update=time.time()
        )
        
        self.current_regimes[symbol] = regime_state
        return regime_state

    def _calculate_regime_scores(self, history_df: pd.DataFrame, current_intel: MarketIntelligence) -> Dict[MarketRegime, float]:
        """Calculate scores for each possible market regime"""
        
        scores = {regime: 0.0 for regime in MarketRegime.values()}
        
        # Current market conditions
        spread = current_intel.bid_ask_spread
        imbalance = abs(current_intel.order_book_imbalance)
        flow_ratio = current_intel.aggressive_buy_ratio - current_intel.aggressive_sell_ratio
        volatility = current_intel.realized_volatility_1m
        liquidity = current_intel.liquidity_ratio
        
        # Flow trends
        flow_acceleration = current_intel.net_flow_5s - history_df['net_flow_5s'].iloc[-10:].mean()
        
        # ACCUMULATION indicators
        if (liquidity > 0.8 and volatility < self.regime_thresholds['volatility_low'] and
            abs(flow_ratio) < 0.1 and spread < self.regime_thresholds['spread_tight']):
            scores[MarketRegime.ACCUMULATION] += 0.4
            
        # EUPHORIA indicators  
        if (abs(flow_ratio) > 0.4 and flow_acceleration > 0.2 and
            current_intel.aggressive_buy_ratio > 0.7):
            scores[MarketRegime.EUPHORIA] += 0.5
            
        # INSTITUTIONAL_FOMO indicators
        if (abs(flow_ratio) > 0.3 and liquidity > 0.6 and
            volatility > self.regime_thresholds['volatility_low'] and
            current_intel.net_flow_30s > current_intel.net_flow_5s):
            scores[MarketRegime.INSTITUTIONAL_FOMO] += 0.4
            
        # PANIC indicators
        if (volatility > self.regime_thresholds['volatility_high'] and
            spread > self.regime_thresholds['spread_wide'] and
            liquidity < 0.3 and abs(flow_ratio) > 0.5):
            scores[MarketRegime.PANIC] += 0.6
            
        # DISTRIBUTION indicators
        if (current_intel.aggressive_sell_ratio > 0.6 and
            imbalance > self.regime_thresholds['imbalance_high'] and
            liquidity > 0.4):
            scores[MarketRegime.DISTRIBUTION] += 0.4
            
        # SIDEWAYS indicators
        if (abs(flow_ratio) < 0.1 and imbalance < 0.2 and
            volatility < self.regime_thresholds['volatility_low']):
            scores[MarketRegime.SIDEWAYS] += 0.3
            
        # Normalize scores
        total_score = sum(scores.values())
        if total_score > 0:
            scores = {k: v/total_score for k, v in scores.items()}
        else:
            scores[MarketRegime.BALANCED] = 1.0
            
        return scores

    def _calculate_regime_strength(self, history_df: pd.DataFrame, intel: MarketIntelligence) -> float:
        """Calculate how strong/pronounced the current regime signals are"""
        
        # Volatility of key metrics indicates regime strength
        spread_cv = history_df['bid_ask_spread'].std() / history_df['bid_ask_spread'].mean()
        flow_cv = history_df['net_flow_5s'].std() / (abs(history_df['net_flow_5s'].mean()) + 0.001)
        imbalance_cv = history_df['order_book_imbalance'].std()
        
        # Higher coefficient of variation = stronger regime signals
        strength = np.mean([spread_cv, flow_cv, imbalance_cv])
        return min(1.0, strength * 2)  # Scale to 0-1

    def _estimate_transition_probabilities(self, history_df: pd.DataFrame, 
                                         current_regime: MarketRegime) -> Dict[MarketRegime, float]:
        """Estimate probabilities of transitioning to other regimes"""
        
        # Simple transition model based on current trajectory
        transitions = {regime: 0.1 for regime in MarketRegime.values()}  # Base probability
        
        # Analyze trends to predict likely transitions
        recent_trend = self._analyze_recent_trends(history_df)
        
        if current_regime == MarketRegime.ACCUMULATION:
            if recent_trend['flow_increasing']:
                transitions[MarketRegime.INSTITUTIONAL_FOMO] = 0.3
            if recent_trend['volatility_increasing']:
                transitions[MarketRegime.BALANCED] = 0.2
                
        elif current_regime == MarketRegime.EUPHORIA:
            if recent_trend['flow_decreasing']:
                transitions[MarketRegime.DISTRIBUTION] = 0.4
            if recent_trend['volatility_increasing']:
                transitions[MarketRegime.PANIC] = 0.2
                
        elif current_regime == MarketRegime.PANIC:
            if recent_trend['volatility_decreasing']:
                transitions[MarketRegime.ACCUMULATION] = 0.3
                transitions[MarketRegime.SIDEWAYS] = 0.2
                
        # Normalize
        total = sum(transitions.values())
        return {k: v/total for k, v in transitions.items()}

    def _analyze_recent_trends(self, history_df: pd.DataFrame) -> Dict[str, bool]:
        """Analyze recent trends in key metrics"""
        if len(history_df) < 20:
            return {key: False for key in ['flow_increasing', 'flow_decreasing', 
                                         'volatility_increasing', 'volatility_decreasing']}
        
        recent = history_df.tail(10)
        older = history_df.tail(20).head(10)
        
        return {
            'flow_increasing': recent['net_flow_5s'].mean() > older['net_flow_5s'].mean(),
            'flow_decreasing': recent['net_flow_5s'].mean() < older['net_flow_5s'].mean(),
            'volatility_increasing': recent['realized_volatility_1m'].mean() > older['realized_volatility_1m'].mean(),
            'volatility_decreasing': recent['realized_volatility_1m'].mean() < older['realized_volatility_1m'].mean(),
        }

    def get_execution_urgency(self, symbol: str) -> float:
        """Determine execution urgency based on market intelligence"""
        if symbol not in self.current_regimes:
            return 0.5  # Default moderate urgency
            
        regime_state = self.current_regimes[symbol]
        recent_intel = list(self.intelligence_history[symbol])[-1] if self.intelligence_history[symbol] else None
        
        if not recent_intel:
            return 0.5
            
        urgency = 0.5  # Base urgency
        
        # High urgency regimes
        if regime_state.primary_regime in [MarketRegime.EUPHORIA, MarketRegime.PANIC]:
            urgency += 0.3
            
        # Volatility adjustment
        if recent_intel['realized_volatility_1m'] > 0.03:
            urgency += 0.2
            
        # Liquidity adjustment  
        if recent_intel['liquidity_ratio'] < 0.3:
            urgency += 0.2
            
        # Flow momentum adjustment
        if abs(recent_intel['net_flow_5s']) > 0.5:
            urgency += 0.1
            
        return min(1.0, urgency)

    def get_market_impact_tolerance(self, symbol: str) -> float:
        """Determine acceptable market impact based on conditions"""
        if symbol not in self.current_regimes:
            return 0.01  # Default conservative
            
        regime_state = self.current_regimes[symbol]
        recent_intel = list(self.intelligence_history[symbol])[-1] if self.intelligence_history[symbol] else None
        
        if not recent_intel:
            return 0.01
            
        base_tolerance = 0.01
        
        # Higher tolerance in liquid conditions
        if recent_intel['liquidity_ratio'] > 0.8:
            base_tolerance *= 2
            
        # Lower tolerance in thin markets
        if recent_intel['liquidity_ratio'] < 0.3:
            base_tolerance *= 0.5
            
        # Regime-based adjustments
        if regime_state.primary_regime == MarketRegime.EUPHORIA:
            base_tolerance *= 1.5  # Accept higher impact to capture momentum
        elif regime_state.primary_regime == MarketRegime.PANIC:
            base_tolerance *= 0.7  # Be more careful in panic
        elif regime_state.primary_regime == MarketRegime.ACCUMULATION:
            base_tolerance *= 0.5  # Very careful in accumulation
            
        return min(0.05, base_tolerance)  # Cap at 5%

    def get_default_regime_state(self) -> MarketRegimeState:
        """Default regime state when insufficient data"""
        return MarketRegimeState(
            primary_regime=MarketRegime.BALANCED,
            confidence=0.5,
            regime_strength=0.5,
            transition_probability={regime: 1.0/len(MarketRegime.values()) 
                                  for regime in MarketRegime.values()},
            last_update=time.time()
        )

class EnhancedTradingStrategy:
    """Trading strategy enhanced with real-time market intelligence"""
    
    def __init__(self, symbols: List[str]):
        self.symbols = symbols
        self.market_intel_processor = MarketIntelligenceProcessor(symbols)
        
        # ZeroMQ client for sending trading intents
        self.context = zmq.Context()
        self.execution_client = self.context.socket(zmq.REQ)
        self.execution_client.connect("tcp://localhost:5556")

    def generate_trading_signal(self, symbol: str) -> Optional[TradingIntent]:
        """Generate trading signal enhanced with market intelligence"""
        
        # Receive latest market intelligence
        intel = self.market_intel_processor.receive_market_intelligence()
        if intel and intel.symbol == symbol:
            # Update market regime based on intelligence
            regime_state = self.market_intel_processor.update_market_regime(symbol, intel)
            
            # Your existing signal generation logic here...
            # But now enhanced with market intelligence:
            
            signal_strength = self.calculate_signal_strength(symbol)
            if signal_strength < 0.3:
                return None
                
            # Create trading intent with market intelligence
            intent = TradingIntent()
            intent.symbol = symbol
            intent.side = self.determine_side(symbol)  # Your logic
            intent.target_quantity = self.calculate_position_size(symbol)  # Your logic
            
            # Enhanced with market intelligence
            intent.market_regime = regime_state.primary_regime
            intent.volatility_regime = self.determine_volatility_regime(intel)
            intent.etf_flow_regime = self.determine_etf_flow_regime(intel)
            
            intent.signal_strength = signal_strength
            intent.confidence = regime_state.confidence
            intent.signal_timestamp = int(time.time() * 1000)
            intent.strategy_id = "imbalance_strategy_v2"
            
            # Market intelligence informed execution preferences
            intent.execution_style = self.determine_execution_style(regime_state, intel)
            intent.urgency_factor = self.market_intel_processor.get_execution_urgency(symbol)
            intent.max_market_impact = self.market_intel_processor.get_market_impact_tolerance(symbol)
            
            # Dynamic timing based on market conditions
            if regime_state.primary_regime in [MarketRegime.EUPHORIA, MarketRegime.PANIC]:
                intent.max_execution_time_ms = 10000  # Fast execution
            elif regime_state.primary_regime == MarketRegime.ACCUMULATION:
                intent.max_execution_time_ms = 60000  # Patient execution
            else:
                intent.max_execution_time_ms = 30000  # Balanced
                
            intent.max_slippage_tolerance = 0.002 * (1 + intel.realized_volatility_1m)
            intent.allow_partial_fills = True
            
            return intent
            
        return None

    def determine_execution_style(self, regime_state: MarketRegimeState, 
                                intel: MarketIntelligence) -> int:
        """Determine execution style based on market regime and conditions"""
        
        if regime_state.primary_regime in [MarketRegime.EUPHORIA, MarketRegime.PANIC]:
            return 0  # AGGRESSIVE
        elif regime_state.primary_regime == MarketRegime.ACCUMULATION:
            return 2  # PASSIVE
        elif intel.liquidity_ratio < 0.3:
            return 3  # STEALTH
        else:
            return 1  # BALANCED

    # Your existing strategy methods...
    def calculate_signal_strength(self, symbol: str) -> float:
        # Your existing implementation
        pass
    
    def determine_side(self, symbol: str) -> int:
        # Your existing implementation  
        pass
    
    def calculate_position_size(self, symbol: str) -> float:
        # Your existing implementation
        pass