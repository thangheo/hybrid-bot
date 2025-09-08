# ================================================================
# Order Book Imbalance Manager - Free Spot Trading Enhancement
# Direct alternative to CoinGlass for immediate market pressure detection
# ================================================================

import asyncio
import websockets
import json
import pandas as pd
import numpy as np
from typing import Dict, List, Optional, Tuple, Any
from datetime import datetime, timedelta
import logging
from dataclasses import dataclass
from collections import deque
import statistics

logger = logging.getLogger(__name__)

# ================================================================
# Data Classes for Order Book Analysis
# ================================================================

@dataclass
class OrderBookLevel:
    """Single order book level"""
    price: float
    quantity: float
    
@dataclass
class OrderBookSnapshot:
    """Complete order book snapshot"""
    symbol: str
    bids: List[OrderBookLevel]
    asks: List[OrderBookLevel]
    timestamp: datetime
    
@dataclass
class ImbalanceMetrics:
    """Order book imbalance analysis results"""
    symbol: str
    imbalance_ratio: float      # -1 to 1 (negative = ask heavy, positive = bid heavy)
    imbalance_strength: float   # 0 to 1 (how strong the imbalance)
    depthImbalance: float      # Weighted by price levels
    volumeImbalance : float     # Pure volume comparison
    spread_pressure: float      # Pressure on bid-ask spread
    market_depth_score: float   # Overall market depth quality
    spoofing_detected: bool     # Unusual wall detection
    confidence: float           # Signal confidence 0-1
    timestamp: datetime

@dataclass
class ImbalanceTrend:
    """Historical imbalance trend analysis"""
    symbol: str
    trend_direction: str        # 'building_bids', 'building_asks', 'balanced', 'volatile'
    trend_strength: float       # How consistently trending (0-1)
    trend_duration: int         # Seconds the trend has lasted
    momentum_score: float       # Rate of change in imbalance
    reversal_probability: float # Likelihood of trend reversal
    optimal_entry_side: str     # 'buy', 'sell', 'wait'
    confidence: float
    timestamp: datetime

# ================================================================
# Order Book Data Manager
# ================================================================

class BinanceOrderBookManager:
    """Manage real-time order book data from Binance WebSocket"""
    
    def __init__(self, config: dict):
        self.config = config
        self.symbols = config.get('symbols', ['BTCUSDT', 'ETHUSDT'])
        self.depth_levels = config.get('depth_levels', 20)  # Top 20 levels
        self.update_speed = config.get('update_speed', '1000ms')  # 1000ms or 100ms
        
        # Data storage
        self.current_books = {}
        self.book_history = {symbol: deque(maxlen=100) for symbol in self.symbols}
        
        # WebSocket connection
        self.ws_connections = {}
        self.is_running = False
        
        # Analysis parameters
        self.imbalance_window = config.get('imbalance_window', 10)  # Look at last 10 updates
        self.spoofing_threshold = config.get('spoofing_threshold', 10.0)  # 10x normal size
        
        logger.info(f"Order Book Manager initialized for {len(self.symbols)} symbols")
    
    async def start_streams(self):
        """Start WebSocket streams for all symbols"""
        self.is_running = True
        
        tasks = []
        for symbol in self.symbols:
            task = asyncio.create_task(self._start_symbol_stream(symbol))
            tasks.append(task)
        
        logger.info(f"🔄 Started {len(tasks)} order book streams")
        
        # Run all streams concurrently
        try:
            await asyncio.gather(*tasks)
        except Exception as e:
            logger.error(f"❌ Order book stream error: {e}")
            self.is_running = False
    
    async def _start_symbol_stream(self, symbol: str):
        """Start WebSocket stream for single symbol"""
        
        symbol_lower = symbol.lower()
        ws_url = f"wss://stream.binance.com:9443/ws/{symbol_lower}@depth{self.depth_levels}@{self.update_speed}"
        
        while self.is_running:
            try:
                async with websockets.connect(ws_url) as websocket:
                    logger.info(f"✅ Connected to {symbol} order book stream")
                    
                    async for message in websocket:
                        if not self.is_running:
                            break
                            
                        data = json.loads(message)
                        await self._process_orderbook_update(symbol, data)
                        
            except Exception as e:
                logger.error(f"❌ {symbol} stream error: {e}")
                if self.is_running:
                    await asyncio.sleep(5)  # Wait before reconnect
                    logger.info(f"🔄 Reconnecting {symbol} stream...")
    
    async def _process_orderbook_update(self, symbol: str, data: dict):
        """Process incoming order book update"""
        
        try:
            # Parse bid and ask data
            bids = [OrderBookLevel(float(level[0]), float(level[1])) 
                   for level in data.get('bids', [])]
            asks = [OrderBookLevel(float(level[0]), float(level[1])) 
                   for level in data.get('asks', [])]
            
            # Create snapshot
            snapshot = OrderBookSnapshot(
                symbol=symbol,
                bids=bids,
                asks=asks,
                timestamp=datetime.now()
            )
            
            # Store current state
            self.current_books[symbol] = snapshot
            
            # Add to history
            self.book_history[symbol].append(snapshot)
            
            logger.debug(f"📊 {symbol} book updated: {len(bids)} bids, {len(asks)} asks")
            
        except Exception as e:
            logger.error(f"❌ Failed to process {symbol} update: {e}")
    
    def get_current_book(self, symbol: str) -> Optional[OrderBookSnapshot]:
        """Get current order book snapshot"""
        return self.current_books.get(symbol)
    
    def get_book_history(self, symbol: str, count: int = 10) -> List[OrderBookSnapshot]:
        """Get recent order book history"""
        history = list(self.book_history.get(symbol, []))
        return history[-count:] if len(history) >= count else history
    
    async def stop_streams(self):
        """Stop all WebSocket streams"""
        self.is_running = False
        logger.info("🛑 Stopping order book streams...")

# ================================================================
# Imbalance Analysis Engine
# ================================================================

class OrderBookImbalanceAnalyzer:
    """Analyze order book imbalances for trading signals"""
    
    def __init__(self, config: dict):
        self.config = config
        
        # Analysis parameters
        self.depth_levels = config.get('analysis_depth', 10)  # Analyze top 10 levels
        self.volume_threshold = config.get('min_volume_threshold', 1000)  # Min $1000
        self.imbalance_threshold = config.get('imbalance_threshold', 0.6)  # 60% threshold
        
        # Weighted analysis settings
        self.level_weights = self._generate_level_weights(self.depth_levels)
        
        logger.info("Order Book Imbalance Analyzer initialized")
    
    def _generate_level_weights(self, levels: int) -> List[float]:
        """Generate weights for order book levels (closer levels = higher weight)"""
        weights = []
        for i in range(levels):
            # Exponential decay: Level 0 = 1.0, Level 10 = 0.1
            weight = np.exp(-i * 0.23)  # 0.23 gives ~0.1 at level 10
            weights.append(weight)
        
        # Normalize so sum = 1.0
        total = sum(weights)
        return [w / total for w in weights]
    
    def calculate_basic_imbalance(self, snapshot: OrderBookSnapshot) -> float:
        """Calculate basic bid/ask volume imbalance"""
        
        if not snapshot.bids or not snapshot.asks:
            return 0.0
        
        # Sum top N levels
        levels_to_use = min(self.depth_levels, len(snapshot.bids), len(snapshot.asks))
        
        bid_volume = sum(bid.quantity for bid in snapshot.bids[:levels_to_use])
        ask_volume = sum(ask.quantity for ask in snapshot.asks[:levels_to_use])
        
        total_volume = bid_volume + ask_volume
        
        if total_volume < self.volume_threshold:
            return 0.0  # Insufficient volume
        
        # Calculate imbalance ratio
        bid_ratio = bid_volume / total_volume
        imbalance = (bid_ratio - 0.5) * 2  # Convert to -1 to +1 scale
        
        return imbalance
    
    def calculate_weighted_imbalance(self, snapshot: OrderBookSnapshot) -> float:
        """Calculate price-weighted imbalance (closer levels matter more)"""
        
        if not snapshot.bids or not snapshot.asks:
            return 0.0
        
        levels_to_use = min(self.depth_levels, len(snapshot.bids), len(snapshot.asks))
        
        weighted_bid_volume = 0.0
        weighted_ask_volume = 0.0
        
        for i in range(levels_to_use):
            weight = self.level_weights[i]
            
            if i < len(snapshot.bids):
                weighted_bid_volume += snapshot.bids[i].quantity * weight
            
            if i < len(snapshot.asks):
                weighted_ask_volume += snapshot.asks[i].quantity * weight
        
        total_weighted = weighted_bid_volume + weighted_ask_volume
        
        if total_weighted == 0:
            return 0.0
        
        bid_ratio = weighted_bid_volume / total_weighted
        imbalance = (bid_ratio - 0.5) * 2  # -1 to +1 scale
        
        return imbalance
    
    def calculate_depth_imbalance(self, snapshot: OrderBookSnapshot) -> float:
        """Calculate dollar-weighted imbalance (price * quantity)"""
        
        if not snapshot.bids or not snapshot.asks:
            return 0.0
        
        levels_to_use = min(self.depth_levels, len(snapshot.bids), len(snapshot.asks))
        
        bid_value = sum(bid.price * bid.quantity for bid in snapshot.bids[:levels_to_use])
        ask_value = sum(ask.price * ask.quantity for ask in snapshot.asks[:levels_to_use])
        
        total_value = bid_value + ask_value
        
        if total_value == 0:
            return 0.0
        
        bid_ratio = bid_value / total_value
        imbalance = (bid_ratio - 0.5) * 2
        
        return imbalance
    
    def detect_spoofing(self, snapshot: OrderBookSnapshot) -> bool:
        """Detect potential spoofing (fake walls)"""
        
        if not snapshot.bids or not snapshot.asks:
            return False
        
        # Get typical order sizes
        all_sizes = []
        for bid in snapshot.bids[:self.depth_levels]:
            all_sizes.append(bid.quantity)
        for ask in snapshot.asks[:self.depth_levels]:
            all_sizes.append(ask.quantity)
        
        if len(all_sizes) < 5:  # Need enough data
            return False
        
        median_size = statistics.median(all_sizes)
        
        # Check for orders much larger than typical
        for bid in snapshot.bids[:3]:  # Check top 3 levels
            if bid.quantity > median_size * self.spoofing_threshold:
                return True
        
        for ask in snapshot.asks[:3]:
            if ask.quantity > median_size * self.spoofing_threshold:
                return True
        
        return False
    
    def calculate_spread_pressure(self, snapshot: OrderBookSnapshot) -> float:
        """Calculate pressure on the bid-ask spread"""
        
        if not snapshot.bids or not snapshot.asks:
            return 0.0
        
        best_bid = snapshot.bids[0].price
        best_ask = snapshot.asks[0].price
        spread = best_ask - best_bid
        
        if spread <= 0:
            return 0.0
        
        # Calculate size at best levels
        best_bid_size = snapshot.bids[0].quantity
        best_ask_size = snapshot.asks[0].quantity
        
        total_size = best_bid_size + best_ask_size
        
        if total_size == 0:
            return 0.0
        
        # Pressure score: positive = upward pressure, negative = downward pressure
        size_ratio = best_bid_size / total_size
        pressure = (size_ratio - 0.5) * 2
        
        return pressure
    
    def calculate_market_depth_score(self, snapshot: OrderBookSnapshot) -> float:
        """Calculate overall market depth quality (0-1)"""
        
        if not snapshot.bids or not snapshot.asks:
            return 0.0
        
        levels_to_use = min(self.depth_levels, len(snapshot.bids), len(snapshot.asks))
        
        # Calculate total volume
        totalBidVolume  = sum(bid.quantity for bid in snapshot.bids[:levels_to_use])
        totalAskVolume = sum(ask.quantity for ask in snapshot.asks[:levels_to_use])
        total_volume = totalBidVolume  + totalAskVolume
        
        # Calculate spread
        if snapshot.bids and snapshot.asks:
            spread = snapshot.asks[0].price - snapshot.bids[0].price
            mid_price = (snapshot.bids[0].price + snapshot.asks[0].price) / 2
            spread_pct = spread / mid_price if mid_price > 0 else 0
        else:
            spread_pct = 1.0  # Maximum penalty
        
        # Score components
        volume_score = min(total_volume / 10000, 1.0)  # Normalize to 10k volume
        spread_score = max(0, 1.0 - spread_pct * 1000)  # Penalty for wide spreads
        depth_score = min(levels_to_use / self.depth_levels, 1.0)  # Level availability
        
        # Weighted average
        depth_quality = (
            volume_score * 0.5 +
            spread_score * 0.3 +
            depth_score * 0.2
        )
        
        return depth_quality
    
    def analyze_imbalance(self, snapshot: OrderBookSnapshot) -> ImbalanceMetrics:
        """Comprehensive imbalance analysis"""
        
        # Calculate different imbalance measures
        volumeImbalance  = self.calculate_basic_imbalance(snapshot)
        depthImbalance = self.calculate_weighted_imbalance(snapshot)
        value_imbalance = self.calculate_depth_imbalance(snapshot)
        spread_pressure = self.calculate_spread_pressure(snapshot)
        
        # Overall imbalance (weighted combination)
        overall_imbalance = (
            volumeImbalance  * 0.3 +
            depthImbalance * 0.4 +
            value_imbalance * 0.3
        )
        
        # Imbalance strength (how extreme is it)
        imbalance_strength = abs(overall_imbalance)
        
        # Market quality metrics
        market_depth_score = self.calculate_market_depth_score(snapshot)
        spoofing_detected = self.detect_spoofing(snapshot)
        
        # Confidence calculation
        confidence = self._calculate_confidence(
            imbalance_strength, market_depth_score, spoofing_detected
        )
        
        return ImbalanceMetrics(
            symbol=snapshot.symbol,
            imbalance_ratio=overall_imbalance,
            imbalance_strength=imbalance_strength,
            depthImbalance=depthImbalance,
            volumeImbalance =volumeImbalance ,
            spread_pressure=spread_pressure,
            market_depth_score=market_depth_score,
            spoofing_detected=spoofing_detected,
            confidence=confidence,
            timestamp=snapshot.timestamp
        )
    
    def _calculate_confidence(self, strength: float, depth_score: float, spoofing: bool) -> float:
        """Calculate confidence in the imbalance signal"""
        
        # Base confidence from signal strength
        strength_confidence = min(strength * 2, 1.0)  # 0.5 strength = max confidence
        
        # Market quality bonus
        quality_bonus = depth_score * 0.3
        
        # Spoofing penalty
        spoofing_penalty = 0.4 if spoofing else 0.0
        
        confidence = strength_confidence + quality_bonus - spoofing_penalty
        
        return max(0.0, min(1.0, confidence))

# ================================================================
# Trend Analysis Engine
# ================================================================

class ImbalanceTrendAnalyzer:
    """Analyze imbalance trends over time"""
    
    def __init__(self, config: dict):
        self.config = config
        self.trend_window = config.get('trend_window', 20)  # Last 20 updates
        self.momentum_window = config.get('momentum_window', 5)  # Last 5 for momentum
        
    def analyze_trend(self, imbalance_history: List[ImbalanceMetrics]) -> ImbalanceTrend:
        """Analyze trend from imbalance history"""
        
        if len(imbalance_history) < 5:
            return self._neutral_trend(imbalance_history[0].symbol if imbalance_history else "UNKNOWN")
        
        symbol = imbalance_history[0].symbol
        
        # Extract imbalance ratios
        ratios = [m.imbalance_ratio for m in imbalance_history[-self.trend_window:]]
        
        # Determine trend direction
        trend_direction = self._classify_trend_direction(ratios)
        
        # Calculate trend strength (consistency)
        trend_strength = self._calculate_trend_strength(ratios)
        
        # Calculate trend duration
        trend_duration = self._calculate_trend_duration(ratios)
        
        # Calculate momentum
        momentum_score = self._calculate_momentum(ratios)
        
        # Predict reversal probability
        reversal_probability = self._calculate_reversal_probability(ratios)
        
        # Determine optimal entry side
        optimal_entry_side = self._determine_optimal_entry(
            trend_direction, trend_strength, reversal_probability
        )
        
        # Overall confidence
        confidence = self._calculate_trend_confidence(
            trend_strength, len(ratios), imbalance_history[-1].market_depth_score
        )
        
        return ImbalanceTrend(
            symbol=symbol,
            trend_direction=trend_direction,
            trend_strength=trend_strength,
            trend_duration=trend_duration,
            momentum_score=momentum_score,
            reversal_probability=reversal_probability,
            optimal_entry_side=optimal_entry_side,
            confidence=confidence,
            timestamp=datetime.now()
        )
    
    def _classify_trend_direction(self, ratios: List[float]) -> str:
        """Classify the overall trend direction"""
        
        if not ratios:
            return 'balanced'
        
        recent_avg = np.mean(ratios[-5:]) if len(ratios) >= 5 else np.mean(ratios)
        overall_avg = np.mean(ratios)
        
        # Check for consistent direction
        positive_count = sum(1 for r in ratios if r > 0.1)
        negative_count = sum(1 for r in ratios if r < -0.1)
        
        positive_ratio = positive_count / len(ratios)
        negative_ratio = negative_count / len(ratios)
        
        if positive_ratio > 0.6:
            return 'building_bids'
        elif negative_ratio > 0.6:
            return 'building_asks'
        elif abs(recent_avg) > 0.3:
            return 'volatile'
        else:
            return 'balanced'
    
    def _calculate_trend_strength(self, ratios: List[float]) -> float:
        """Calculate how consistent the trend is (0-1)"""
        
        if len(ratios) < 3:
            return 0.0
        
        # Calculate standard deviation (lower = more consistent)
        std_dev = np.std(ratios)
        
        # Convert to consistency score (inverted)
        max_std = 0.5  # Expected max std for random walk
        consistency = max(0, 1.0 - (std_dev / max_std))
        
        # Also consider directional consistency
        direction_changes = 0
        for i in range(1, len(ratios)):
            if np.sign(ratios[i]) != np.sign(ratios[i-1]):
                direction_changes += 1
        
        direction_consistency = max(0, 1.0 - (direction_changes / len(ratios)))
        
        # Combine both measures
        overall_strength = (consistency * 0.6 + direction_consistency * 0.4)
        
        return overall_strength
    
    def _calculate_trend_duration(self, ratios: List[float]) -> int:
        """Calculate how long the current trend has lasted (in periods)"""
        
        if len(ratios) < 2:
            return 0
        
        current_direction = np.sign(ratios[-1])
        duration = 0
        
        # Count backwards while direction is consistent
        for i in range(len(ratios) - 1, -1, -1):
            if np.sign(ratios[i]) == current_direction and abs(ratios[i]) > 0.1:
                duration += 1
            else:
                break
        
        return duration
    
    def _calculate_momentum(self, ratios: List[float]) -> float:
        """Calculate momentum (rate of change in imbalance)"""
        
        if len(ratios) < self.momentum_window:
            return 0.0
        
        recent = ratios[-self.momentum_window:]
        
        # Calculate linear trend slope
        x = np.arange(len(recent))
        slope, _ = np.polyfit(x, recent, 1)
        
        # Normalize momentum to -1 to 1 range
        momentum = np.tanh(slope * 10)  # Scale and bound
        
        return momentum
    
    def _calculate_reversal_probability(self, ratios: List[float]) -> float:
        """Calculate probability of trend reversal"""
        
        if len(ratios) < 5:
            return 0.5  # Neutral
        
        recent_extreme = abs(ratios[-1])
        trend_duration = self._calculate_trend_duration(ratios)
        momentum = abs(self._calculate_momentum(ratios))
        
        # Factors that increase reversal probability:
        # 1. Extreme imbalance values
        # 2. Long trend duration
        # 3. Slowing momentum
        
        extreme_factor = min(recent_extreme * 2, 1.0)  # 0.5 = max factor
        duration_factor = min(trend_duration / 20, 1.0)  # 20 periods = max factor
        momentum_factor = max(0, 1.0 - momentum)  # Lower momentum = higher reversal chance
        
        # Weighted combination
        reversal_prob = (
            extreme_factor * 0.4 +
            duration_factor * 0.3 +
            momentum_factor * 0.3
        )
        
        return reversal_prob
    
    def _determine_optimal_entry(self, direction: str, strength: float, reversal_prob: float) -> str:
        """Determine optimal entry side"""
        
        if strength < 0.3 or reversal_prob > 0.7:
            return 'wait'  # Low conviction or high reversal risk
        
        if direction == 'building_bids' and reversal_prob < 0.4:
            return 'buy'  # Bids building, low reversal risk
        elif direction == 'building_asks' and reversal_prob < 0.4:
            return 'sell'  # Asks building, low reversal risk
        elif direction == 'volatile':
            return 'wait'  # Too choppy
        else:
            return 'wait'  # Default to waiting
    
    def _calculate_trend_confidence(self, strength: float, data_points: int, depth_score: float) -> float:
        """Calculate overall confidence in trend analysis"""
        
        # Data sufficiency
        data_confidence = min(data_points / 20, 1.0)  # 20 points = max confidence
        
        # Trend strength confidence
        strength_confidence = strength
        
        # Market quality confidence
        quality_confidence = depth_score
        
        # Combined confidence
        overall_confidence = (
            data_confidence * 0.3 +
            strength_confidence * 0.4 +
            quality_confidence * 0.3
        )
        
        return overall_confidence
    
    def _neutral_trend(self, symbol: str) -> ImbalanceTrend:
        """Return neutral trend when insufficient data"""
        
        return ImbalanceTrend(
            symbol=symbol,
            trend_direction='balanced',
            trend_strength=0.0,
            trend_duration=0,
            momentum_score=0.0,
            reversal_probability=0.5,
            optimal_entry_side='wait',
            confidence=0.1,
            timestamp=datetime.now()
        )

# ================================================================
# Main Order Book Strategy Manager
# ================================================================

class OrderBookStrategyManager:
    """Main manager combining order book data and analysis"""
    
    def __init__(self, config: dict):
        self.config = config
        
        # Initialize components
        self.book_manager = BinanceOrderBookManager(config)
        self.imbalance_analyzer = OrderBookImbalanceAnalyzer(config)
        self.trend_analyzer = ImbalanceTrendAnalyzer(config)
        
        # Data storage
        self.imbalance_history = {symbol: deque(maxlen=100) for symbol in config.get('symbols', [])}
        
        # Analysis settings
        self.analysis_interval = config.get('analysis_interval', 5)  # Analyze every 5 seconds
        self.is_analyzing = False
        
        logger.info("Order Book Strategy Manager initialized")
    
    async def start(self):
        """Start the complete order book analysis system"""
        
        logger.info("🚀 Starting Order Book Strategy Manager...")
        
        # Start order book streams
        book_task = asyncio.create_task(self.book_manager.start_streams())
        
        # Start analysis loop
        analysis_task = asyncio.create_task(self._analysis_loop())
        
        # Run both concurrently
        try:
            await asyncio.gather(book_task, analysis_task)
        except Exception as e:
            logger.error(f"❌ Strategy manager error: {e}")
            await self.stop()
    
    async def _analysis_loop(self):
        """Main analysis loop"""
        
        self.is_analyzing = True
        
        while self.is_analyzing:
            try:
                # Analyze all symbols
                for symbol in self.config.get('symbols', []):
                    await self._analyze_symbol(symbol)
                
                # Wait for next analysis cycle
                await asyncio.sleep(self.analysis_interval)
                
            except Exception as e:
                logger.error(f"❌ Analysis loop error: {e}")
                await asyncio.sleep(1)
    
    async def _analyze_symbol(self, symbol: str):
        """Analyze single symbol"""
        
        # Get current order book
        current_book = self.book_manager.get_current_book(symbol)
        if not current_book:
            return
        
        # Analyze current imbalance
        imbalance_metrics = self.imbalance_analyzer.analyze_imbalance(current_book)
        
        # Store in history
        self.imbalance_history[symbol].append(imbalance_metrics)
        
        # Analyze trend if we have enough history
        if len(self.imbalance_history[symbol]) >= 5:
            trend_analysis = self.trend_analyzer.analyze_trend(
                list(self.imbalance_history[symbol])
            )
            
            # Log significant signals
            if imbalance_metrics.confidence > 0.6 or trend_analysis.confidence > 0.6:
                self._log_trading_signal(symbol, imbalance_metrics, trend_analysis)
    
    def _log_trading_signal(self, symbol: str, imbalance: ImbalanceMetrics, trend: ImbalanceTrend):
        """Log significant trading signals"""
        
        logger.info(f"🎯 {symbol} SIGNAL:")
        logger.info(f"   Imbalance: {imbalance.imbalance_ratio:.3f} (strength: {imbalance.imbalance_strength:.2f})")
        logger.info(f"   Trend: {trend.trend_direction} (strength: {trend.trend_strength:.2f})")
        logger.info(f"   Entry: {trend.optimal_entry_side} (confidence: {trend.confidence:.2f})")
        logger.info(f"   Market Quality: {imbalance.market_depth_score:.2f}")
        
        if imbalance.spoofing_detected:
            logger.warning(f"   ⚠️ Spoofing detected!")
    
    async def get_trading_signal(self, symbol: str) -> Optional[Dict[str, Any]]:
        """Get current trading signal for symbol"""
        
        if symbol not in self.imbalance_history:
            return None
        
        history = list(self.imbalance_history[symbol])
        if not history:
            return None
        
        # Get latest imbalance analysis
        latest_imbalance = history[-1]
        
        # Get trend analysis if we have enough data
        if len(history) >= 5:
            trend_analysis = self.trend_analyzer.analyze_trend(history)
        else:
            trend_analysis = self.trend_analyzer._neutral_trend(symbol)
        
        # Generate combined trading signal
        trading_signal = self._generate_trading_signal(latest_imbalance, trend_analysis)
        
        return trading_signal
    
    def _generate_trading_signal(self, imbalance: ImbalanceMetrics, trend: ImbalanceTrend) -> Dict[str, Any]:
        """Generate combined trading signal"""
        
        # Base signal from imbalance
        if imbalance.imbalance_ratio > 0.3 and imbalance.confidence > 0.5:
            base_signal = 1  # Buy signal
            signal_strength = imbalance.imbalance_strength
        elif imbalance.imbalance_ratio < -0.3 and imbalance.confidence > 0.5:
            base_signal = -1  # Sell signal
            signal_strength = imbalance.imbalance_strength
        else:
            base_signal = 0  # No signal
            signal_strength = 0.0
        
        # Trend confirmation/modification
        if trend.optimal_entry_side == 'buy' and base_signal >= 0:
            final_signal = 1
            final_strength = min(signal_strength + 0.2, 1.0)
        elif trend.optimal_entry_side == 'sell' and base_signal <= 0:
            final_signal = -1
            final_strength = min(signal_strength + 0.2, 1.0)
        elif trend.optimal_entry_side == 'wait':
            final_signal = 0
            final_strength = 0.0
        else:
            final_signal = base_signal
            final_strength = signal_strength * 0.8  # Reduce strength if trend disagrees
        
        # Combined confidence
        combined_confidence = (imbalance.confidence + trend.confidence) / 2
        
        # Generate signal dictionary
        signal_dict = {
            'symbol': imbalance.symbol,
            'signal': final_signal,
            'strength': final_strength,
            'confidence': combined_confidence,
            'timeframe': 'short_term',  # Order book signals are short-term
            'entry_timing': 'immediate' if final_strength > 0.6 else 'patient',
            
            # Detailed breakdown
            'imbalance_data': {
                'ratio': imbalance.imbalance_ratio,
                'strength': imbalance.imbalance_strength,
                'depth_score': imbalance.market_depth_score,
                'spoofing': imbalance.spoofing_detected
            },
            'trend_data': {
                'direction': trend.trend_direction,
                'strength': trend.trend_strength,
                'momentum': trend.momentum_score,
                'reversal_prob': trend.reversal_probability,
                'optimal_entry': trend.optimal_entry_side
            },
            
            # Risk parameters
            'suggested_stop_loss': 0.005,  # 0.5% (tight for order book signals)
            'suggested_take_profit': 0.015,  # 1.5% (quick profit target)
            'position_size_multiplier': min(combined_confidence, final_strength),
            
            'timestamp': datetime.now()
        }
        
        return signal_dict
    
    async def stop(self):
        """Stop the strategy manager"""
        
        logger.info("🛑 Stopping Order Book Strategy Manager...")
        
        self.is_analyzing = False
        await self.book_manager.stop_streams()
        
        logger.info("✅ Order Book Strategy Manager stopped")
    
    def get_diagnostic_info(self) -> Dict[str, Any]:
        """Get diagnostic information"""
        
        diagnostics = {
            'symbols': self.config.get('symbols', []),
            'connected_streams': len(self.book_manager.current_books),
            'analysis_running': self.is_analyzing,
            'history_lengths': {
                symbol: len(history) 
                for symbol, history in self.imbalance_history.items()
            }
        }
        
        # Add current imbalance states
        current_states = {}
        for symbol in self.config.get('symbols', []):
            if symbol in self.imbalance_history and self.imbalance_history[symbol]:
                latest = self.imbalance_history[symbol][-1]
                current_states[symbol] = {
                    'imbalance_ratio': latest.imbalance_ratio,
                    'confidence': latest.confidence,
                    'depth_score': latest.market_depth_score
                }
        
        diagnostics['current_states'] = current_states
        
        return diagnostics

# ================================================================
# Integration with Bio-Inspired System
# ================================================================

class OrderBookBioIntegration:
    """Integration layer between Order Book analysis and Bio-Inspired system"""
    
    def __init__(self, config: dict):
        self.strategy_manager = OrderBookStrategyManager(config)
        
    async def enhance_perception_layer(self, symbol: str) -> Dict[str, float]:
        """Enhance Perception Layer with order book data"""
        
        signal = await self.strategy_manager.get_trading_signal(symbol)
        
        if not signal:
            return {'orderbook_pressure': 0.0, 'market_quality': 0.5, 'timing_signal': 0.0}
        
        return {
            'orderbook_pressure': signal['imbalance_data']['ratio'],
            'market_quality': signal['imbalance_data']['depth_score'],
            'timing_signal': signal['strength'] * signal['signal'],
            'volatility_indicator': abs(signal['trend_data']['momentum']),
            'reversal_probability': signal['trend_data']['reversal_prob'],
            'confidence_multiplier': signal['confidence']
        }
    
    async def enhance_execution_layer(self, symbol: str) -> Dict[str, Any]:
        """Enhance Execution Layer with optimal timing"""
        
        signal = await self.strategy_manager.get_trading_signal(symbol)
        
        if not signal:
            return {'optimal_timing': 'wait', 'urgency': 0.0}
        
        return {
            'optimal_timing': signal['entry_timing'],
            'urgency': signal['strength'],
            'suggested_stop': signal['suggested_stop_loss'],
            'suggested_target': signal['suggested_take_profit'],
            'position_multiplier': signal['position_size_multiplier'],
            'market_impact_warning': signal['imbalance_data']['spoofing']
        }

# ================================================================
# Usage Example
# ================================================================

async def test_orderbook_manager():
    """Test the complete order book system"""
    
    config = {
        'symbols': ['BTCUSDT', 'ETHUSDT'],
        'depth_levels': 20,
        'update_speed': '1000ms',
        'analysis_interval': 10,  # Analyze every 10 seconds
        'imbalance_threshold': 0.6,
        'min_volume_threshold': 5000
    }
    
    # Initialize system
    manager = OrderBookStrategyManager(config)
    integration = OrderBookBioIntegration(config)
    
    try:
        # Start system (this would run indefinitely)
        logger.info("🚀 Starting Order Book test (Ctrl+C to stop)...")
        await manager.start()
        
    except KeyboardInterrupt:
        logger.info("⏹️ Test interrupted by user")
        await manager.stop()

if __name__ == "__main__":
    # Setup logging
    logging.basicConfig(level=logging.INFO)
    
    # Run test
    asyncio.run(test_orderbook_manager())