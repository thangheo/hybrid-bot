# src/perception/enhanced_regime_detector.py
import numpy as np
import pandas as pd
from typing import Dict, List, Tuple
from dataclasses import dataclass
from enum import Enum
import logging

class MarketRegime(Enum):
    STRONG_UPTREND = "strong_uptrend"
    WEAK_UPTREND = "weak_uptrend" 
    SIDEWAYS_CHOPPY = "sideways_choppy"
    SIDEWAYS_STABLE = "sideways_stable"
    WEAK_DOWNTREND = "weak_downtrend"
    STRONG_DOWNTREND = "strong_downtrend"
    HIGH_VOLATILITY = "high_volatility"
    MEAN_REVERSION = "mean_reversion"

@dataclass
class RegimeSignal:
    regime: MarketRegime
    confidence: float  # 0-1
    strength: float    # How strong the regime is
    duration: int      # How long this regime has been active
    next_likely_regime: MarketRegime
    transition_probability: float

class MultiTimeframeRegimeDetector:
    """
    Enhanced regime detection using multiple timeframes and indicators
    """
    
    def __init__(self, config: dict):
        self.config = config
        self.lookback_short = config.get('lookback_short', 20)
        self.lookback_medium = config.get('lookback_medium', 50)
        self.lookback_long = config.get('lookback_long', 200)
        
        # Regime transition matrix (learned from historical data)
        self.transition_matrix = self._load_transition_matrix()
        
        # Current regime state
        self.current_regime = None
        self.regime_duration = 0
        self.regime_history = []
        
        logging.basicConfig(level=logging.INFO)
        self.logger = logging.getLogger(__name__)
    
    def detect_regime(self, data: pd.DataFrame) -> RegimeSignal:
        """
        Detect current market regime using multiple indicators
        """
        if len(data) < self.lookback_long:
            return RegimeSignal(
                regime=MarketRegime.SIDEWAYS_STABLE,
                confidence=0.5,
                strength=0.5,
                duration=0,
                next_likely_regime=MarketRegime.SIDEWAYS_STABLE,
                transition_probability=0.3
            )
        
        # Calculate multiple regime indicators
        indicators = self._calculate_regime_indicators(data)
        
        # Multi-factor regime classification
        regime_scores = self._score_regimes(indicators)
        
        # Get primary regime
        primary_regime = max(regime_scores, key=regime_scores.get)
        confidence = regime_scores[primary_regime]
        
        # Calculate regime strength
        strength = self._calculate_regime_strength(indicators, primary_regime)
        
        # Update regime tracking
        if primary_regime == self.current_regime:
            self.regime_duration += 1
        else:
            self.regime_duration = 1
            self.regime_history.append({
                'regime': self.current_regime,
                'duration': self.regime_duration,
                'end_time': data.index[-1]
            })
            self.current_regime = primary_regime
        
        # Predict next likely regime
        next_regime, transition_prob = self._predict_next_regime(primary_regime)
        
        return RegimeSignal(
            regime=primary_regime,
            confidence=confidence,
            strength=strength,
            duration=self.regime_duration,
            next_likely_regime=next_regime,
            transition_probability=transition_prob
        )
    
    def _calculate_regime_indicators(self, data: pd.DataFrame) -> Dict:
        """Calculate various indicators for regime detection"""
        close = data['close']
        high = data['high']
        low = data['low']
        volume = data['volume']
        
        # Trend indicators
        sma_20 = close.rolling(20).mean()
        sma_50 = close.rolling(50).mean()
        sma_200 = close.rolling(200).mean()
        
        # Volatility indicators
        returns = close.pct_change()
        volatility_20 = returns.rolling(20).std() * np.sqrt(252)
        volatility_50 = returns.rolling(50).std() * np.sqrt(252)
        
        # Momentum indicators
        rsi = self._calculate_rsi(close, 14)
        macd, macd_signal = self._calculate_macd(close)
        
        # Volume indicators
        volume_ma = volume.rolling(20).mean()
        volume_ratio = volume / volume_ma
        
        # Price position in range
        price_position = (close - low.rolling(20).min()) / (high.rolling(20).max() - low.rolling(20).min())
        
        return {
            'price': close.iloc[-1],
            'sma_20': sma_20.iloc[-1],
            'sma_50': sma_50.iloc[-1], 
            'sma_200': sma_200.iloc[-1],
            'volatility_20': volatility_20.iloc[-1],
            'volatility_50': volatility_50.iloc[-1],
            'rsi': rsi.iloc[-1],
            'macd': macd.iloc[-1],
            'macd_signal': macd_signal.iloc[-1],
            'volume_ratio': volume_ratio.iloc[-1],
            'price_position': price_position.iloc[-1],
            'trend_strength': self._calculate_trend_strength(close),
            'mean_reversion_strength': self._calculate_mean_reversion_strength(close)
        }
    
    def _score_regimes(self, indicators: Dict) -> Dict[MarketRegime, float]:
        """Score each possible regime based on indicators"""
        scores = {}
        
        # Strong Uptrend
        uptrend_score = 0
        if indicators['price'] > indicators['sma_20'] > indicators['sma_50'] > indicators['sma_200']:
            uptrend_score += 0.4
        if indicators['rsi'] > 50:
            uptrend_score += 0.2
        if indicators['macd'] > indicators['macd_signal']:
            uptrend_score += 0.2
        if indicators['volume_ratio'] > 1.2:  # Above average volume
            uptrend_score += 0.1
        if indicators['trend_strength'] > 0.7:
            uptrend_score += 0.1
            
        scores[MarketRegime.STRONG_UPTREND] = uptrend_score if uptrend_score > 0.7 else 0
        scores[MarketRegime.WEAK_UPTREND] = uptrend_score if 0.4 < uptrend_score <= 0.7 else 0
        
        # Strong Downtrend  
        downtrend_score = 0
        if indicators['price'] < indicators['sma_20'] < indicators['sma_50'] < indicators['sma_200']:
            downtrend_score += 0.4
        if indicators['rsi'] < 50:
            downtrend_score += 0.2
        if indicators['macd'] < indicators['macd_signal']:
            downtrend_score += 0.2
        if indicators['volume_ratio'] > 1.2:
            downtrend_score += 0.1
        if indicators['trend_strength'] < -0.7:
            downtrend_score += 0.1
            
        scores[MarketRegime.STRONG_DOWNTREND] = downtrend_score if downtrend_score > 0.7 else 0
        scores[MarketRegime.WEAK_DOWNTREND] = downtrend_score if 0.4 < downtrend_score <= 0.7 else 0
        
        # High Volatility
        vol_score = 0
        if indicators['volatility_20'] > indicators['volatility_50'] * 1.5:
            vol_score += 0.5
        if indicators['volatility_20'] > 0.4:  # > 40% annualized vol
            vol_score += 0.3
        if abs(indicators['rsi'] - 50) > 30:  # RSI in extreme zones
            vol_score += 0.2
            
        scores[MarketRegime.HIGH_VOLATILITY] = vol_score
        
        # Mean Reversion
        mr_score = indicators['mean_reversion_strength']
        if 0.3 < indicators['volatility_20'] < 0.6:  # Moderate volatility
            mr_score += 0.2
        if 30 < indicators['rsi'] < 70:  # RSI not in extremes
            mr_score += 0.2
            
        scores[MarketRegime.MEAN_REVERSION] = mr_score
        
        # Sideways markets
        sideways_score = 0
        if abs(indicators['trend_strength']) < 0.3:
            sideways_score += 0.4
        if indicators['volatility_20'] < 0.25:
            sideways_score += 0.3
            scores[MarketRegime.SIDEWAYS_STABLE] = sideways_score
        else:
            scores[MarketRegime.SIDEWAYS_CHOPPY] = sideways_score + 0.2
        
        # Normalize scores
        total_score = sum(scores.values())
        if total_score > 0:
            scores = {k: v/total_score for k, v in scores.items()}
        
        return scores
    
    def _calculate_trend_strength(self, close: pd.Series) -> float:
        """Calculate trend strength (-1 to 1)"""
        returns = close.pct_change().dropna()
        if len(returns) < 20:
            return 0
        
        # Use linear regression slope
        x = np.arange(len(returns[-20:]))
        y = returns[-20:].values
        slope = np.polyfit(x, y, 1)[0]
        
        # Normalize slope
        return np.tanh(slope * 100)  # Scale and bound between -1 and 1
    
    def _calculate_mean_reversion_strength(self, close: pd.Series) -> float:
        """Calculate mean reversion tendency (0 to 1)"""
        if len(close) < 50:
            return 0.5
        
        # Calculate Z-score variance - higher variance = more mean reverting
        returns = close.pct_change().dropna()
        z_scores = []
        
        for i in range(20, len(returns)):
            window = returns[i-20:i]
            if len(window) > 0 and window.std() > 0:
                z_score = (returns.iloc[i] - window.mean()) / window.std()
                z_scores.append(abs(z_score))
        
        if len(z_scores) == 0:
            return 0.5
        
        # Higher average Z-score suggests more mean reversion opportunities
        avg_z = np.mean(z_scores)
        return min(avg_z / 3.0, 1.0)  # Normalize
    
    def _calculate_rsi(self, close: pd.Series, period: int = 14) -> pd.Series:
        """Calculate RSI"""
        delta = close.diff()
        gain = (delta.where(delta > 0, 0)).rolling(period).mean()
        loss = (-delta.where(delta < 0, 0)).rolling(period).mean()
        rs = gain / loss
        return 100 - (100 / (1 + rs))
    
    def _calculate_macd(self, close: pd.Series) -> Tuple[pd.Series, pd.Series]:
        """Calculate MACD and signal line"""
        ema_12 = close.ewm(span=12).mean()
        ema_26 = close.ewm(span=26).mean()
        macd = ema_12 - ema_26
        signal = macd.ewm(span=9).mean()
        return macd, signal
    
    def _calculate_regime_strength(self, indicators: Dict, regime: MarketRegime) -> float:
        """Calculate how strong the current regime is"""
        if regime in [MarketRegime.STRONG_UPTREND, MarketRegime.WEAK_UPTREND]:
            return min(indicators['trend_strength'] * 2, 1.0)
        elif regime in [MarketRegime.STRONG_DOWNTREND, MarketRegime.WEAK_DOWNTREND]:
            return min(abs(indicators['trend_strength']) * 2, 1.0)
        elif regime == MarketRegime.HIGH_VOLATILITY:
            return min(indicators['volatility_20'] / 0.5, 1.0)
        elif regime == MarketRegime.MEAN_REVERSION:
            return indicators['mean_reversion_strength']
        else:
            return 0.5
    
    def _predict_next_regime(self, current_regime: MarketRegime) -> Tuple[MarketRegime, float]:
        """Predict next likely regime based on transition matrix"""
        if current_regime in self.transition_matrix:
            transitions = self.transition_matrix[current_regime]
            next_regime = max(transitions, key=transitions.get)
            probability = transitions[next_regime]
            return next_regime, probability
        
        return current_regime, 0.3  # Default low probability
    
    def _load_transition_matrix(self) -> Dict:
        """Load or create regime transition probabilities"""
        # This would ideally be learned from historical data
        # For now, using reasonable defaults
        return {
            MarketRegime.STRONG_UPTREND: {
                MarketRegime.WEAK_UPTREND: 0.4,
                MarketRegime.SIDEWAYS_STABLE: 0.3,
                MarketRegime.HIGH_VOLATILITY: 0.2,
                MarketRegime.STRONG_DOWNTREND: 0.1
            },
            MarketRegime.WEAK_UPTREND: {
                MarketRegime.STRONG_UPTREND: 0.3,
                MarketRegime.SIDEWAYS_CHOPPY: 0.4,
                MarketRegime.MEAN_REVERSION: 0.2,
                MarketRegime.WEAK_DOWNTREND: 0.1
            },
            MarketRegime.SIDEWAYS_STABLE: {
                MarketRegime.WEAK_UPTREND: 0.3,
                MarketRegime.WEAK_DOWNTREND: 0.3,
                MarketRegime.MEAN_REVERSION: 0.3,
                MarketRegime.HIGH_VOLATILITY: 0.1
            },
            MarketRegime.MEAN_REVERSION: {
                MarketRegime.SIDEWAYS_STABLE: 0.4,
                MarketRegime.WEAK_UPTREND: 0.2,
                MarketRegime.WEAK_DOWNTREND: 0.2,
                MarketRegime.HIGH_VOLATILITY: 0.2
            },
            MarketRegime.HIGH_VOLATILITY: {
                MarketRegime.STRONG_DOWNTREND: 0.3,
                MarketRegime.STRONG_UPTREND: 0.2,
                MarketRegime.SIDEWAYS_CHOPPY: 0.3,
                MarketRegime.MEAN_REVERSION: 0.2
            },
            MarketRegime.STRONG_DOWNTREND: {
                MarketRegime.WEAK_DOWNTREND: 0.4,
                MarketRegime.HIGH_VOLATILITY: 0.3,
                MarketRegime.MEAN_REVERSION: 0.2,
                MarketRegime.SIDEWAYS_STABLE: 0.1
            },
            MarketRegime.WEAK_DOWNTREND: {
                MarketRegime.SIDEWAYS_CHOPPY: 0.4,
                MarketRegime.MEAN_REVERSION: 0.3,
                MarketRegime.WEAK_UPTREND: 0.2,
                MarketRegime.STRONG_DOWNTREND: 0.1
            }
        }

# Usage example
if __name__ == "__main__":
    import yfinance as yf
    
    # Test with Bitcoin data
    # btc = yf.download('BTC-USD', period='1y')
    #save to csv
    # btc.to_csv('btc_data.csv')
    btc = pd.read_csv('btc_data.csv', index_col='Date', parse_dates=True)
    # Also need to rename columns to lowercase for your code to work
    btc.columns = btc.columns.str.lower()
    print("Original columns:", btc.columns.tolist())

    detector = MultiTimeframeRegimeDetector({
        'lookback_short': 20,
        'lookback_medium': 50, 
        'lookback_long': 200
    })
    
    regime_signal = detector.detect_regime(btc)
    print(f"Current Regime: {regime_signal.regime}")
    print(f"Confidence: {regime_signal.confidence:.2f}")
    print(f"Strength: {regime_signal.strength:.2f}")
    print(f"Duration: {regime_signal.duration} periods")
    print(f"Next Likely: {regime_signal.next_likely_regime} ({regime_signal.transition_probability:.2f})")
