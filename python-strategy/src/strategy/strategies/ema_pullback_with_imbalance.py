# ================================================================
# src/strategy/strategies/ema_pullback_with_imbalance.py
# Hybrid strategy: EMA/RSI pullback + Orderbook Imbalance confirmation
# ================================================================

import pandas as pd
import numpy as np
import time
from typing import Dict, Optional
from ..base_strategy import BaseStrategy
from ..indicators.technical_indicators import TechnicalIndicators
from ..filters.simple_trend_filter import SimpleTrendFilter

class EMAPullbackImbalanceStrategy(BaseStrategy):
    """
    Multi-timeframe EMA pullback strategy with orderbook imbalance confirmation
    
    Logic:
    1. H1 timeframe: Confirm trend direction (EMA20 > EMA50 for uptrend)
    2. M5 timeframe: Wait for pullback to EMA zone
    3. Orderbook: Confirm entry with bid/ask imbalance
    4. Enter on bullish/bearish pattern confirmation
    """
    
    def __init__(self, config: dict):
        """Constructor accepts either a strategy-specific config dict or the full
        `trading_bot.yaml` top-level config. If the latter is passed, it will
        extract the nested `ema_pullback_with_imbalance` section automatically.
        """
        # Allow callers to pass the entire loaded YAML or just the strategy section
        if config is None:
            config = {}

        # If the full config file was passed, read nested section
        # prefer explicit `ema_pullback_with_imbalance` section if present
        if isinstance(config, dict) and 'ema_pullback_with_imbalance' in config:
            cfg = config.get('ema_pullback_with_imbalance', {}) or {}
        else:
            cfg = config

        super().__init__(cfg)

        # === EMA Parameters ===
        self.ema_short = cfg.get('ema_short', 20)
        self.ema_long = cfg.get('ema_long', 50)
        
        # === RSI Parameters ===
        self.rsi_period = cfg.get('rsi_period', 14)
        # Normalize RSI ranges: allow YAML to specify tuples, lists or strings
        def _normalize_range(val, default):
            if isinstance(val, (list, tuple)) and len(val) == 2:
                return (float(val[0]), float(val[1]))
            try:
                # handle string '(30, 50)' or '30,50'
                if isinstance(val, str):
                    parsed = val.strip(' ()')
                    parts = [p.strip() for p in parsed.split(',')]
                    if len(parts) == 2:
                        return (float(parts[0]), float(parts[1]))
            except Exception:
                pass
            return default

        self.rsi_buy_range = _normalize_range(cfg.get('rsi_buy_range', (30, 50)), (30.0, 50.0))
        self.rsi_sell_range = _normalize_range(cfg.get('rsi_sell_range', (50, 70)), (50.0, 70.0))  # Sell when RSI in this range
        
        # === Pullback Zone Parameters ===
        self.pullback_tolerance = cfg.get('pullback_tolerance', 0.02)  # 2% penetration allowed beyond EMA50
        
        # === Orderbook Imbalance Parameters (NEW!) ===
        self.use_imbalance = cfg.get('use_imbalance', True)  # Can disable for pure EMA strategy
        self.imbalance_threshold_strong = cfg.get('imbalance_threshold_strong', 0.65)  # 65% for high confidence
        self.imbalance_threshold_weak = cfg.get('imbalance_threshold_weak', 0.55)     # 55% for medium confidence
        self.volume_surge_threshold = cfg.get('volume_surge_threshold', 2.0)  # 2x average volume
        
        # === ATR for volatility filter ===
        self.atr_period = cfg.get('atr_period', 14)
        self.atr_threshold_ratio = cfg.get('atr_threshold_ratio', 0.4)  # ATR must be > 40% of 50-period average
        
        # === Risk Management ===
        self.risk_reward_tp1 = cfg.get('risk_reward_tp1', 1.0)  # TP1 at 1:1
        self.risk_reward_tp2 = cfg.get('risk_reward_tp2', 2.0)  # TP2 at 1:2
        self.sl_atr_multiplier = cfg.get('sl_atr_multiplier', 1.5)  # SL = 1.5 * ATR
        
        # === Multi-timeframe support ===
        self.primary_tf = cfg.get('primary_timeframe', '5m')   # Entry timeframe
        self.trend_tf = cfg.get('trend_timeframe', '1h')       # Trend timeframe

        # How many bars to skip due to warmup. YAML key: min_periods
        self.min_periods = int(cfg.get('min_periods', 50))

        # Is the strategy enabled in the config? Useful for higher-level code
        self.enabled = bool(cfg.get('enabled', True))

        # Store trend data separately if provided
        self.trend_data = None
        self.trend_indicators = None

        # === Simple Trend Filter (SIMPLIFIED!) ===
        self.use_trend_filter = cfg.get('use_trend_filter', True)
        filter_config = cfg.get('filter_config', {})
        self.trend_filter = SimpleTrendFilter(filter_config)

        # === C++ Market Intelligence Integration ===
        self.intelligence_bridge = None  # Will be set by main.py
        
    def set_intelligence_bridge(self, bridge):
        """Set C++ market intelligence bridge"""
        self.intelligence_bridge = bridge

    def set_trend_data(self, trend_data: pd.DataFrame):
        """Set higher timeframe data for trend confirmation"""
        if trend_data is not None and not trend_data.empty:
            self.trend_data = trend_data
            # Pre-calculate trend indicators
            close = trend_data['close']
            self.trend_indicators = {
                'ema_short': TechnicalIndicators.ema(close, self.ema_short),
                'ema_long': TechnicalIndicators.ema(close, self.ema_long),
                'rsi': TechnicalIndicators.rsi(close, self.rsi_period)
            }

    def get_cpp_orderbook_snapshot(self, symbol: str) -> Optional[Dict]:
        """
        Extract current orderbook intelligence from C++ bridge

        Returns:
            Dict with orderbook metrics or None if unavailable
        """
        if not self.intelligence_bridge or not self.intelligence_bridge.is_available():
            return None

        # Get intelligence state from bridge
        intel = self.intelligence_bridge.get_intelligence(symbol)
        if not intel:
            return None

        # Map C++ MarketIntelligence fields to strategy requirements
        # order_book_imbalance: positive means more bids, negative means more asks
        # Convert to separate bid/ask imbalance ratios (0-1 scale)

        if intel.order_book_imbalance >= 0:
            # More bid pressure
            bid_imbalance = 0.5 + (intel.order_book_imbalance * 0.5)  # Scale to 0.5-1.0
            ask_imbalance = 1.0 - bid_imbalance
        else:
            # More ask pressure
            ask_imbalance = 0.5 + (abs(intel.order_book_imbalance) * 0.5)
            bid_imbalance = 1.0 - ask_imbalance

        # Estimate volume surge from aggressive buy ratio vs sell ratio
        # High aggressive_buy_ratio indicates strong buying pressure
        bid_volume_surge = intel.liquidity_ratio * (1.0 + intel.order_book_imbalance)

        # Detect large walls from depth_imbalance (if available)
        # Large positive depth_imbalance suggests bid walls, negative suggests ask walls
        has_large_bid_walls = False
        has_large_ask_walls = False

        if hasattr(intel, 'spread'):
            # Use spread as proxy: tight spread + high liquidity = potential walls
            if intel.spread < 0.001 and intel.liquidity_ratio > 0.7:
                if intel.order_book_imbalance > 0.3:
                    has_large_bid_walls = True
                elif intel.order_book_imbalance < -0.3:
                    has_large_ask_walls = True

        return {
            'bid_imbalance': bid_imbalance,
            'ask_imbalance': ask_imbalance,
            'bid_volume_surge': bid_volume_surge,
            'has_large_bid_walls': has_large_bid_walls,
            'has_large_ask_walls': has_large_ask_walls,
            # Additional context for debugging
            '_raw_order_book_imbalance': intel.order_book_imbalance,
            '_liquidity_ratio': intel.liquidity_ratio,
            '_regime': intel.current_regime,
            '_data_age': time.time() - intel.last_update
        }
    
    def calculate_indicators(self, data: pd.DataFrame) -> Dict[str, pd.Series]:
        """Calculate all required indicators for M5 timeframe"""
        close = data['close']
        high = data['high']
        low = data['low']
        volume = data.get('volume', pd.Series(1.0, index=data.index))
        
        indicators = {
            # === Core EMA indicators ===
            'ema_short': TechnicalIndicators.ema(close, self.ema_short),
            'ema_long': TechnicalIndicators.ema(close, self.ema_long),
            
            # === RSI ===
            'rsi': TechnicalIndicators.rsi(close, self.rsi_period),
            
            # === ATR for volatility and stop loss ===
            'atr': TechnicalIndicators.atr(high, low, close, self.atr_period),
            
            # === Volume indicators ===
            'volume_ma': volume.rolling(20).mean(),
            'volume_ratio': volume / volume.rolling(20).mean(),
            
            # === Price action patterns ===
            'body_size': abs(data['close'] - data['open']),
            'upper_wick': data['high'] - data[['open', 'close']].max(axis=1),
            'lower_wick': data[['open', 'close']].min(axis=1) - data['low'],
            
            # === Orderbook placeholders (will be populated externally) ===
            'bid_imbalance': pd.Series(0.5, index=data.index),  # Default neutral
            'ask_imbalance': pd.Series(0.5, index=data.index),
            'bid_volume_surge': pd.Series(1.0, index=data.index),
            'has_bid_walls': pd.Series(False, index=data.index),
            'has_ask_walls': pd.Series(False, index=data.index),
        }
        
        # === Derived indicators ===
        indicators['ema_ratio'] = indicators['ema_short'] / indicators['ema_long']
        indicators['atr_ma50'] = indicators['atr'].rolling(50).mean()
        indicators['atr_ratio'] = indicators['atr'] / indicators['atr_ma50']
        
        # === RSI momentum ===
        indicators['rsi_change'] = indicators['rsi'].diff()
        
        return indicators
    
    def update_orderbook_data(self, signals: pd.DataFrame, orderbook_snapshot: Dict):
        """
        Update the latest row with real-time orderbook data from C++ module
        
        Args:
            signals: DataFrame with indicator columns
            orderbook_snapshot: Dict with keys:
                - 'bid_imbalance': float (0-1)
                - 'ask_imbalance': float (0-1)  
                - 'bid_volume_surge': float (ratio vs average)
                - 'has_large_bid_walls': bool
                - 'has_large_ask_walls': bool
        """
        if orderbook_snapshot and not signals.empty:
            last_idx = signals.index[-1]
            signals.loc[last_idx, 'bid_imbalance'] = orderbook_snapshot.get('bid_imbalance', 0.5)
            signals.loc[last_idx, 'ask_imbalance'] = orderbook_snapshot.get('ask_imbalance', 0.5)
            signals.loc[last_idx, 'bid_volume_surge'] = orderbook_snapshot.get('bid_volume_surge', 1.0)
            signals.loc[last_idx, 'has_bid_walls'] = orderbook_snapshot.get('has_large_bid_walls', False)
            signals.loc[last_idx, 'has_ask_walls'] = orderbook_snapshot.get('has_large_ask_walls', False)
    
    def check_trend_alignment(self, current_time: pd.Timestamp) -> int:
        """
        Check H1 trend direction
        Returns: 1 (uptrend), -1 (downtrend), 0 (neutral/no data)
        """
        if self.trend_indicators is None or self.trend_data is None:
            return 0  # No trend data, allow any trade
        
        # Get the most recent trend bar
        trend_ema_short = self.trend_indicators['ema_short']
        trend_ema_long = self.trend_indicators['ema_long']
        
        if trend_ema_short.empty or trend_ema_long.empty:
            return 0
        
        last_ema_short = trend_ema_short.iloc[-1]
        last_ema_long = trend_ema_long.iloc[-1]
        last_close = self.trend_data['close'].iloc[-1]
        
        # Uptrend: EMA20 > EMA50 AND price > EMA20
        if last_ema_short > last_ema_long and last_close > last_ema_short:
            return 1
        
        # Downtrend: EMA20 < EMA50 AND price < EMA20
        if last_ema_short < last_ema_long and last_close < last_ema_short:
            return -1
        
        return 0  # Neutral
    
    def detect_candlestick_pattern(self, data: pd.DataFrame, indicators: Dict[str, pd.Series], 
                                   idx: int, direction: str) -> bool:
        """
        Detect bullish/bearish candlestick patterns
        
        Args:
            data: OHLCV data
            indicators: Calculated indicators
            idx: Current candle index
            direction: 'bullish' or 'bearish'
        """
        if idx < 1:
            return False
        
        # Get current and previous candle
        curr = data.iloc[idx]
        prev = data.iloc[idx - 1]
        
        body_size = indicators['body_size'].iloc[idx]
        upper_wick = indicators['upper_wick'].iloc[idx]
        lower_wick = indicators['lower_wick'].iloc[idx]
        
        if direction == 'bullish':
            # Bullish Pin Bar (Hammer)
            pin_bar = (
                lower_wick > 2 * body_size and
                upper_wick < 0.3 * lower_wick and
                curr['close'] > curr['open']
            )
            
            # Bullish Engulfing
            engulfing = (
                prev['close'] < prev['open'] and  # Previous red
                curr['close'] > curr['open'] and  # Current green
                curr['close'] > prev['open'] and  # Engulfs top
                curr['open'] < prev['close']      # Engulfs bottom
            )
            
            return pin_bar or engulfing
        
        elif direction == 'bearish':
            # Bearish Pin Bar (Shooting Star)
            pin_bar = (
                upper_wick > 2 * body_size and
                lower_wick < 0.3 * upper_wick and
                curr['close'] < curr['open']
            )
            
            # Bearish Engulfing
            engulfing = (
                prev['close'] > prev['open'] and  # Previous green
                curr['close'] < curr['open'] and  # Current red
                curr['close'] < prev['open'] and  # Engulfs bottom
                curr['open'] > prev['close']      # Engulfs top
            )
            
            return pin_bar or engulfing
        
        return False
    
    def generate_signals(self, data: pd.DataFrame, indicators: Dict[str, pd.Series]) -> pd.DataFrame:
        """
        Generate trading signals with multi-stage filtering:
        1. Simple trend filter (SIMPLIFIED!)
        2. Trend alignment (H1)
        3. Pullback to EMA zone (M5)
        4. RSI confirmation
        5. Candlestick pattern
        6. Orderbook imbalance confirmation
        7. Volatility filter
        """
        signals = pd.DataFrame(index=data.index)

        # Initialize columns
        signals['signal'] = 0
        signals['signal_strength'] = 0.0
        signals['confidence'] = 'none'  # 'high', 'medium', 'low', 'none'

        # Context columns for debugging
        signals['trend_aligned'] = 0
        signals['in_pullback_zone'] = False
        signals['rsi_ok'] = False
        signals['pattern_detected'] = False
        signals['imbalance_strong'] = False
        signals['volatile_enough'] = False

        # Copy orderbook data
        signals['bid_imbalance'] = indicators['bid_imbalance']
        signals['ask_imbalance'] = indicators['ask_imbalance']

        # Stop loss and take profit levels
        signals['stop_loss'] = 0.0
        signals['take_profit_1'] = 0.0
        signals['take_profit_2'] = 0.0

        # === STAGE 0: Simple Trend Filter (SIMPLIFIED!) ===
        if self.use_trend_filter:
            filter_results = self.trend_filter.apply_filters(
                data,
                indicators['ema_short'],
                indicators['ema_long']
            )

            # Add filter columns to signals
            signals['is_tradeable'] = filter_results['is_tradeable']
            signals['filter_reason'] = filter_results['filter_reasons']

            # Debug columns
            signals['adx'] = filter_results['adx']
            signals['ema_separation'] = filter_results['ema_separation']
            if self.trend_filter.use_ema_curvature:
                signals['ema_curvature'] = filter_results['ema_curvature']
        else:
            # If filter disabled, allow all trades
            signals['is_tradeable'] = True
            signals['filter_reason'] = 'Filter disabled'
        
        # Check trend alignment once (H1 doesn't change every M5 bar)
        trend_direction = self.check_trend_alignment(data.index[-1] if not data.empty else None)
        
        # Iterate through each bar (skip first 50 for indicator warmup)
        for i in range(self.min_periods, len(data)):
            idx = data.index[i]
            
            # === STAGE 1: Trend Filter (H1) ===
            signals.loc[idx, 'trend_aligned'] = trend_direction
            
            # === STAGE 2: Pullback Zone Check (M5) ===
            current_price = data.loc[idx, 'close']
            ema_short = indicators['ema_short'].loc[idx]
            ema_long = indicators['ema_long'].loc[idx]
            
            if pd.isna(ema_short) or pd.isna(ema_long):
                continue
            
            # Allow price to be between EMA20 and EMA50 (with tolerance)
            ema_min = min(ema_short, ema_long)
            ema_max = max(ema_short, ema_long)
            
            # For buy: price should pull back to EMA zone from above
            # For sell: price should pull back to EMA zone from below
            tolerance = current_price * self.pullback_tolerance
            
            in_buy_pullback_zone = (
                (ema_min - tolerance) <= current_price <= ema_max and
                trend_direction == 1
            )
            
            in_sell_pullback_zone = (
                ema_min <= current_price <= (ema_max + tolerance) and
                trend_direction == -1
            )
            
            signals.loc[idx, 'in_pullback_zone'] = in_buy_pullback_zone or in_sell_pullback_zone
            
            # === STAGE 3: RSI Check ===
            rsi = indicators['rsi'].loc[idx]
            rsi_change = indicators['rsi_change'].loc[idx]
            
            rsi_buy_ok = (
                self.rsi_buy_range[0] < rsi < self.rsi_buy_range[1] and
                rsi_change > 0  # RSI turning up
            )
            
            rsi_sell_ok = (
                self.rsi_sell_range[0] < rsi < self.rsi_sell_range[1] and
                rsi_change < 0  # RSI turning down
            )
            
            signals.loc[idx, 'rsi_ok'] = rsi_buy_ok or rsi_sell_ok
            
            # === STAGE 4: Candlestick Pattern ===
            bullish_pattern = self.detect_candlestick_pattern(data, indicators, i, 'bullish')
            bearish_pattern = self.detect_candlestick_pattern(data, indicators, i, 'bearish')
            
            signals.loc[idx, 'pattern_detected'] = bullish_pattern or bearish_pattern
            
            # === STAGE 5: Orderbook Imbalance (if enabled) ===
            bid_imb = indicators['bid_imbalance'].loc[idx]
            ask_imb = indicators['ask_imbalance'].loc[idx]
            bid_surge = indicators['bid_volume_surge'].loc[idx]
            has_bid_walls = indicators['has_bid_walls'].loc[idx]
            has_ask_walls = indicators['has_ask_walls'].loc[idx]
            
            # Strong imbalance for high confidence
            strong_buy_imbalance = (
                bid_imb > self.imbalance_threshold_strong and
                bid_surge > self.volume_surge_threshold and
                not has_ask_walls  # No resistance above
            )
            
            strong_sell_imbalance = (
                ask_imb > self.imbalance_threshold_strong and
                bid_surge < (1.0 / self.volume_surge_threshold) and
                not has_bid_walls  # No support below
            )
            
            # Weak imbalance for medium confidence
            weak_buy_imbalance = bid_imb > self.imbalance_threshold_weak
            weak_sell_imbalance = ask_imb > self.imbalance_threshold_weak
            
            signals.loc[idx, 'imbalance_strong'] = strong_buy_imbalance or strong_sell_imbalance
            
            # === STAGE 6: Volatility Filter ===
            atr_ratio = indicators['atr_ratio'].loc[idx]
            volatile_enough = atr_ratio > self.atr_threshold_ratio
            signals.loc[idx, 'volatile_enough'] = volatile_enough
            
            # === FINAL SIGNAL GENERATION ===
            atr = indicators['atr'].loc[idx]
            
            # BUY SIGNAL
            if (trend_direction == 1 and
                in_buy_pullback_zone and
                rsi_buy_ok and
                bullish_pattern and
                volatile_enough and
                signals.loc[idx, 'is_tradeable']):  # ← SIMPLIFIED: Only check tradeable

                if not self.use_imbalance:
                    # Pure EMA strategy
                    signals.loc[idx, 'signal'] = 1
                    signals.loc[idx, 'confidence'] = 'medium'
                    signals.loc[idx, 'signal_strength'] = 0.6

                elif strong_buy_imbalance:
                    # High confidence with strong orderbook
                    signals.loc[idx, 'signal'] = 1
                    signals.loc[idx, 'confidence'] = 'high'
                    signals.loc[idx, 'signal_strength'] = 0.9

                elif weak_buy_imbalance:
                    # Medium confidence with weak orderbook
                    signals.loc[idx, 'signal'] = 1
                    signals.loc[idx, 'confidence'] = 'medium'
                    signals.loc[idx, 'signal_strength'] = 0.6
                
                # Calculate risk management levels
                if signals.loc[idx, 'signal'] == 1:
                    # Stop loss below recent low or EMA50
                    recent_low = data.loc[data.index[max(0, i-5):i+1], 'low'].min()
                    sl_ema = ema_long - (self.sl_atr_multiplier * atr)
                    signals.loc[idx, 'stop_loss'] = min(recent_low, sl_ema)
                    
                    risk = current_price - signals.loc[idx, 'stop_loss']
                    signals.loc[idx, 'take_profit_1'] = current_price + (risk * self.risk_reward_tp1)
                    signals.loc[idx, 'take_profit_2'] = current_price + (risk * self.risk_reward_tp2)
            
            # SELL SIGNAL
            elif (trend_direction == -1 and
                  in_sell_pullback_zone and
                  rsi_sell_ok and
                  bearish_pattern and
                  volatile_enough and
                  signals.loc[idx, 'is_tradeable']):  # ← SIMPLIFIED: Only check tradeable

                if not self.use_imbalance:
                    signals.loc[idx, 'signal'] = -1
                    signals.loc[idx, 'confidence'] = 'medium'
                    signals.loc[idx, 'signal_strength'] = 0.6

                elif strong_sell_imbalance:
                    signals.loc[idx, 'signal'] = -1
                    signals.loc[idx, 'confidence'] = 'high'
                    signals.loc[idx, 'signal_strength'] = 0.9

                elif weak_sell_imbalance:
                    signals.loc[idx, 'signal'] = -1
                    signals.loc[idx, 'confidence'] = 'medium'
                    signals.loc[idx, 'signal_strength'] = 0.6
                
                # Calculate risk management levels
                if signals.loc[idx, 'signal'] == -1:
                    recent_high = data.loc[data.index[max(0, i-5):i+1], 'high'].max()
                    sl_ema = ema_long + (self.sl_atr_multiplier * atr)
                    signals.loc[idx, 'stop_loss'] = max(recent_high, sl_ema)
                    
                    risk = signals.loc[idx, 'stop_loss'] - current_price
                    signals.loc[idx, 'take_profit_1'] = current_price - (risk * self.risk_reward_tp1)
                    signals.loc[idx, 'take_profit_2'] = current_price - (risk * self.risk_reward_tp2)
        
        return signals

    def get_strategy_info(self) -> Dict:
        """Return strategy metadata"""
        info = super().get_strategy_info()
        info.update({
            'strategy_type': 'Multi-timeframe EMA Pullback with Orderbook Imbalance',
            'primary_timeframe': self.primary_tf,
            'trend_timeframe': self.trend_tf,
            'uses_orderbook': self.use_imbalance,
            'uses_trend_filter': self.use_trend_filter,
            'parameters': {
                'ema_short': self.ema_short,
                'ema_long': self.ema_long,
                'rsi_period': self.rsi_period,
                'imbalance_threshold_strong': self.imbalance_threshold_strong,
                'imbalance_threshold_weak': self.imbalance_threshold_weak,
                'atr_threshold_ratio': self.atr_threshold_ratio,
                'filter_adx_min': self.trend_filter.adx_min if self.use_trend_filter else None,
                'filter_ema_sep_min': self.trend_filter.ema_sep_min if self.use_trend_filter else None
            }
        })
        return info