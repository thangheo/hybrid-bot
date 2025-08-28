
# ================================================================
# 2. FILE: src/strategy/spy_market_filter.py
# ================================================================

from typing import Dict, Tuple
import pandas as pd
import numpy as np
from enum import Enum

class MarketRegime(Enum):
    BULL = "bull"
    BEAR = "bear" 
    SIDEWAYS = "sideways"
    VOLATILE = "volatile"

class SPYMarketFilter:
    """SPY-based market filter cho BTC trading signals"""
    
    def __init__(self, config: dict):
        self.config = config
        # Correlation-based settings
        self.btc_spy_correlation = 0.50  # From your analysis
        self.momentum_lookback = 5
        self.trend_lookback = 20
        
        # Filter thresholds
        self.bull_momentum_threshold = 0.02  # 2% 5-day gain
        self.bear_momentum_threshold = -0.02  # 2% 5-day loss
        self.high_vol_threshold = 0.02  # 2% daily volatility
        
    def analyze_spy_market(self, spy_data: pd.DataFrame) -> Dict:
        """Phân tích SPY market conditions"""
        
        if spy_data.empty or len(spy_data) < 20:
            return self._default_analysis()
        
        latest = spy_data.iloc[-1]
        
        # 1. Momentum Analysis
        momentum_5d = latest['momentum']  # 5-day momentum
        momentum_strength = abs(momentum_5d)
        
        # 2. Trend Analysis  
        price = latest['close']
        sma_5 = latest['sma_5']
        sma_10 = latest['sma_10'] 
        sma_20 = latest['sma_20']
        
        # Trend signals
        short_term_bullish = price > sma_5 > sma_10
        medium_term_bullish = sma_10 > sma_20
        strong_uptrend = price > sma_5 > sma_10 > sma_20
        
        # 3. Volatility Analysis
        volatility = latest['volatility']
        high_volatility = volatility > self.high_vol_threshold
        
        # 4. Determine Market Regime
        if momentum_5d > self.bull_momentum_threshold and strong_uptrend:
            regime = MarketRegime.BULL
        elif momentum_5d < self.bear_momentum_threshold and not short_term_bullish:
            regime = MarketRegime.BEAR
        elif high_volatility:
            regime = MarketRegime.VOLATILE
        else:
            regime = MarketRegime.SIDEWAYS
        
        # 5. Recent performance (3-day, 7-day)
        recent_3d = spy_data['close'].pct_change(3).iloc[-1]
        recent_7d = spy_data['close'].pct_change(7).iloc[-1]
        
        return {
            'regime': regime,
            'momentum_5d': momentum_5d,
            'momentum_strength': momentum_strength,
            'short_term_bullish': short_term_bullish,
            'medium_term_bullish': medium_term_bullish,
            'strong_uptrend': strong_uptrend,
            'volatility': volatility,
            'high_volatility': high_volatility,
            'recent_3d_return': recent_3d,
            'recent_7d_return': recent_7d,
            'current_price': price,
            'sma_5': sma_5,
            'sma_10': sma_10,
            'sma_20': sma_20,
            'analysis_timestamp': datetime.now()
        }
    
    def get_btc_signal_adjustment(self, spy_analysis: Dict, btc_signal_strength: float) -> Tuple[float, str]:
        """
        Điều chỉnh BTC signal dựa trên SPY analysis
        Returns: (adjusted_signal_strength, reason)
        """
        
        regime = spy_analysis['regime']
        momentum_5d = spy_analysis['momentum_5d']
        
        # Base adjustment = 1.0 (no change)
        adjustment = 1.0
        reasons = []
        
        # === REGIME-BASED ADJUSTMENTS ===
        
        if regime == MarketRegime.BULL:
            if btc_signal_strength > 0:  # BTC BUY signal
                adjustment *= 1.3  # Amplify bullish signals
                reasons.append("SPY bull regime (+30%)")
            else:  # BTC SELL signal  
                adjustment *= 0.7  # Reduce bearish signals
                reasons.append("SPY bull regime - reduce bearish (-30%)")
                
        elif regime == MarketRegime.BEAR:
            if btc_signal_strength > 0:  # BTC BUY signal
                adjustment *= 0.6  # Reduce bullish signals significantly
                reasons.append("SPY bear regime - reduce bullish (-40%)")
            else:  # BTC SELL signal
                adjustment *= 1.2  # Amplify bearish signals
                reasons.append("SPY bear regime (+20%)")
                
        elif regime == MarketRegime.VOLATILE:
            adjustment *= 0.8  # Reduce all signals in volatile market
            reasons.append("High volatility regime (-20%)")
            
        # === MOMENTUM-BASED FINE-TUNING ===
        
        if abs(momentum_5d) > 0.03:  # Strong momentum
            if (momentum_5d > 0 and btc_signal_strength > 0) or \
               (momentum_5d < 0 and btc_signal_strength < 0):
                adjustment *= 1.1  # Same direction bonus
                reasons.append("Strong momentum alignment (+10%)")
        
        # === TREND CONFIRMATION ===
        
        if spy_analysis['strong_uptrend'] and btc_signal_strength > 0:
            adjustment *= 1.15
            reasons.append("SPY strong uptrend (+15%)")
            
        # === APPLY ADJUSTMENT ===
        
        adjusted_strength = btc_signal_strength * adjustment
        
        # Clamp to reasonable bounds
        adjusted_strength = np.clip(adjusted_strength, -2.0, 2.0)
        
        reason_text = "; ".join(reasons) if reasons else "No adjustment"
        
        return adjusted_strength, reason_text
    
    def should_pause_trading(self, spy_analysis: Dict) -> Tuple[bool, str]:
        """
        Kiểm tra có nên pause trading không
        """
        
        # Pause conditions
        if spy_analysis['regime'] == MarketRegime.VOLATILE and spy_analysis['volatility'] > 0.03:
            return True, "Extremely high SPY volatility (>3%)"
        
        # Flash crash detection (>5% drop in 3 days)
        if spy_analysis['recent_3d_return'] < -0.05:
            return True, "SPY flash crash detected (-5% in 3d)"
        
        # Major trend break
        if (spy_analysis['current_price'] < spy_analysis['sma_20'] * 0.95 and 
            spy_analysis['momentum_5d'] < -0.03):
            return True, "SPY major trend break"
            
        return False, "Normal trading conditions"
    
    def _default_analysis(self):
        """Default analysis khi không có SPY data"""
        return {
            'regime': MarketRegime.SIDEWAYS,
            'momentum_5d': 0.0,
            'momentum_strength': 0.0,
            'short_term_bullish': False,
            'medium_term_bullish': False,
            'strong_uptrend': False,
            'volatility': 0.02,
            'high_volatility': False,
            'recent_3d_return': 0.0,
            'recent_7d_return': 0.0,
            'analysis_timestamp': datetime.now(),
            'error': 'No SPY data available'
        }