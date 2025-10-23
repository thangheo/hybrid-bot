# ================================================================
# Merged Markov Strategy with SQLite Persistence - Single File
# ================================================================
'''
P(Rᵢ | X_t) = ∑_f P(Rᵢ | f₁, f₂, ..., fₙ) · P(f₁, f₂, ..., fₙ | X_t)
```

Where:
- X_t = observed market data at time t
- P(Rᵢ | f) = regime likelihood given factor configuration
- P(f | X_t) = posterior factor distribution (inferred from data)

### 3. Transition Dynamics (Impermanence)

Regimes transition according to how conditions evolve. Use a **non-stationary Hidden Markov Model**:
```
P(Rₜ | Rₜ₋₁, Fₜ) = π(Rₜ₋₁, Rₜ) · ψ(Fₜ)
```

Where:
- π(Rₜ₋₁, Rₜ) = base transition probability
- ψ(Fₜ) = factor modulation function (allowing non-stationarity)

**Bayesian update** for factor beliefs:
```
P(Fₜ | X₁:ₜ) ∝ P(Xₜ | Fₜ) · P(Fₜ | X₁:ₜ₋₁)
```

### 4. Quantum-Inspired Superposition

Allow **overlapping regimes** via amplitude formulation:
```
|Ψ(t)⟩ = α₁|R₁⟩ + α₂|R₂⟩ + α₃|R₃⟩ + α₄|R₄⟩ + α₅|R₅⟩

where ∑|αᵢ|² = 1
```

Regime probabilities: P(Rᵢ) = |αᵢ|²

**Interference term** between factors (constructive/destructive co-arising):
```
I(fᵢ, fⱼ) = 2·Re(αᵢ·αⱼ*·exp(iφᵢⱼ))
```

Where φᵢⱼ encodes phase relationship (e.g., liquidity and volatility can reinforce or cancel).

### 5. Entropy as "Emptiness"

Measure regime uncertainty using **Shannon entropy**:
```
H(R | X_t) = -∑ᵢ P(Rᵢ | X_t) · log P(Rᵢ | X_t)
```

High entropy → **Emptiness regime** (R₅): no dominant conditions, transitional phase.

---

## Model Architecture

### Bayesian Network Approach
```
Factor Layer:        [f₁] [f₂] [f₃] [f₄] [f₅]
                       ↓    ↓    ↓    ↓    ↓
                        ╲   |   /    /   ╱
                         ╲  |  /   /   ╱
Latent Regime:             [R_t]
                              ↓
Observable:                 [X_t]
```

**Conditional probability tables** (CPTs):
- P(Rₜ | f₁, f₂, ..., fₙ, Rₜ₋₁)
- P(X_t | Rₜ) = emission likelihood

### Hidden Markov Model Extension

**State space**: S = {R₁, R₂, R₃, R₄, R₅}

**Observation likelihood**: 
```
P(Xₜ | Rₜ = i) = 𝒩(μᵢ(Fₜ), Σᵢ(Fₜ))
```

Key modification: **factor-dependent parameters** μᵢ(Fₜ), Σᵢ(Fₜ) allow regime characteristics to shift based on conditions.

**Forward-backward algorithm** with factor integration:
```
α_t(i) = P(X₁:ₜ, Rₜ = i | Fₜ)
β_t(i) = P(Xₜ₊₁:T | Rₜ = i, Fₜ)
'''
import sqlite3
import numpy as np
import pandas as pd
import json
import logging
import asyncio
from datetime import datetime, timedelta
from typing import Dict, List, Tuple, Optional, Any
from pathlib import Path
import os
from dataclasses import dataclass
from enum import Enum
from collections import defaultdict
from scipy import stats
import time
logger = logging.getLogger(__name__)

from src.data.etf_flow_manager import CompositeETFFlow , ETFFlowManager

try:
    from src.strategy.indicators.enhanced_technical_indicators import EnhancedTechnicalIndicators
    ENHANCED_INDICATORS_AVAILABLE = True
except ImportError:
    EnhancedTechnicalIndicators = None
    ENHANCED_INDICATORS_AVAILABLE = False
    logger.warning("Enhanced indicators not available - using fallback mode")


# ================================================================
# ENUMS AND DATA STRUCTURES
# ================================================================

class VolatilityRegime(Enum):
    LOW = "low_vol"
    MEDIUM = "medium_vol"
    HIGH = "high_vol"

class ETFFlowRegime(Enum):
    POSITIVE = "positive_flow"
    NEUTRAL = "neutral_flow"
    NEGATIVE = "negative_flow"

class MarketRegime(Enum):
    ACCUMULATION = "accumulation"
    INSTITUTIONAL_FOMO = "inst_fomo"
    EUPHORIA = "euphoria"
    SIDEWAYS = "sideways"
    BALANCED = "balanced"
    UNCERTAIN = "uncertain"
    DISTRIBUTION = "distribution"
    PROFIT_TAKING = "profit_taking"
    PANIC = "panic"

@dataclass
class MarkovRegimeInfo:
    current_regime: MarketRegime
    regime_probability: float
    regime_duration: int
    expected_duration: float
    transition_forecast: Dict[MarketRegime, float]
    next_most_likely: MarketRegime
    transition_confidence: float
    volatility_regime: VolatilityRegime
    etf_flow_regime: ETFFlowRegime
    etf_flow_strength: float

# ================================================================
# BASE CLASSES AND TECHNICAL INDICATORS
# ================================================================

class TechnicalIndicators:
    @staticmethod
    def ema(data: pd.Series, period: int) -> pd.Series:
        return data.ewm(span=period).mean()
    
    @staticmethod
    def zscore(data: pd.Series, window: int) -> pd.Series:
        rolling_mean = data.rolling(window).mean()
        rolling_std = data.rolling(window).std()
        return (data - rolling_mean) / rolling_std
    
    @staticmethod
    def rsi(data: pd.Series, period: int = 14) -> pd.Series:
        delta = data.diff()
        gain = (delta.where(delta > 0, 0)).rolling(period).mean()
        loss = (-delta.where(delta < 0, 0)).rolling(period).mean()
        rs = gain / loss
        return 100 - (100 / (1 + rs))
    
    @staticmethod
    def bollinger_bands(data: pd.Series, period: int, std_dev: float = 2.0) -> Dict[str, pd.Series]:
        sma = data.rolling(period).mean()
        std = data.rolling(period).std()
        return {
            'upper': sma + (std * std_dev),
            'middle': sma,
            'lower': sma - (std * std_dev)
        }

class BaseStrategy:
    def __init__(self, config: dict):
        self.config = config
    
    def validate_data(self, data: pd.DataFrame) -> bool:
        required_columns = ['open', 'high', 'low', 'close', 'volume']
        return all(col in data.columns for col in required_columns)
    
    def calculate_indicators(self, data: pd.DataFrame) -> Dict[str, pd.Series]:
        raise NotImplementedError
    
    async def generate_signals(self, symbol: str, data: pd.DataFrame, indicators: Dict[str, pd.Series]) -> pd.DataFrame:
        raise NotImplementedError


class MarkovPersistenceManager:
    def __init__(self, db_path: str = "data/markov_persistence.db"):
        self.db_path = Path(db_path)
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        self.init_database()
    
    def init_database(self):
        conn = sqlite3.connect(self.db_path)
        try:
            conn.execute("""
                CREATE TABLE IF NOT EXISTS transition_matrix (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    symbol TEXT NOT NULL,
                    from_regime TEXT NOT NULL,
                    to_regime TEXT NOT NULL,
                    transition_count REAL NOT NULL,
                    probability REAL NOT NULL,
                    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                    UNIQUE(symbol, from_regime, to_regime)
                )
            """)
            
            conn.execute("""
                CREATE TABLE IF NOT EXISTS regime_duration_stats (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    symbol TEXT NOT NULL,
                    regime TEXT NOT NULL,
                    mean_duration REAL NOT NULL,
                    sample_count INTEGER NOT NULL,
                    duration_samples TEXT NOT NULL,
                    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                    UNIQUE(symbol, regime)
                )
            """)
            
            conn.execute("""
                CREATE TABLE IF NOT EXISTS regime_history (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    symbol TEXT NOT NULL,
                    regime TEXT NOT NULL,
                    regime_probability REAL NOT NULL,
                    regime_duration INTEGER NOT NULL,
                    expected_duration REAL NOT NULL,
                    volatility_regime TEXT NOT NULL,
                    etf_flow_regime TEXT NOT NULL,
                    etf_flow_strength REAL NOT NULL,
                    transition_confidence REAL NOT NULL,
                    next_most_likely TEXT NOT NULL,
                    timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP
                )
            """)
            
            conn.execute("CREATE INDEX IF NOT EXISTS idx_transition_symbol ON transition_matrix(symbol)")
            conn.execute("CREATE INDEX IF NOT EXISTS idx_duration_symbol ON regime_duration_stats(symbol)")
            conn.execute("CREATE INDEX IF NOT EXISTS idx_history_symbol_time ON regime_history(symbol, timestamp)")
            
            conn.commit()
            logger.info(f"Database initialized: {self.db_path}")
        except Exception as e:
            logger.error(f"Database initialization failed: {e}")
            conn.rollback()
        finally:
            conn.close()
    
    def save_transition_matrix(self, symbol: str, transition_matrix: np.ndarray, regime_states: List[MarketRegime]):
        conn = sqlite3.connect(self.db_path)
        try:
            conn.execute("DELETE FROM transition_matrix WHERE symbol = ?", (symbol,))
            
            for i, from_regime in enumerate(regime_states):
                for j, to_regime in enumerate(regime_states):
                    probability = float(transition_matrix[i, j])
                    if probability > 0.001:
                        conn.execute("""
                            INSERT OR REPLACE INTO transition_matrix 
                            (symbol, from_regime, to_regime, transition_count, probability, updated_at)
                            VALUES (?, ?, ?, ?, ?, ?)
                        """, (
                            symbol, from_regime.value, to_regime.value,
                            probability * 100, probability, datetime.now()
                        ))
            
            conn.commit()
            logger.info(f"Transition matrix saved for {symbol}")
        except Exception as e:
            logger.error(f"Failed to save transition matrix for {symbol}: {e}")
            conn.rollback()
        finally:
            conn.close()
    
    def load_transition_matrix(self, symbol: str, regime_states: List[MarketRegime]) -> Optional[np.ndarray]:
        conn = sqlite3.connect(self.db_path)
        try:
            cursor = conn.execute("""
                SELECT from_regime, to_regime, probability 
                FROM transition_matrix 
                WHERE symbol = ?
            """, (symbol,))
            
            rows = cursor.fetchall()
            if not rows:
                return None
            
            regime_to_index = {regime: i for i, regime in enumerate(regime_states)}
            matrix = np.zeros((len(regime_states), len(regime_states)))
            
            for from_regime_str, to_regime_str, probability in rows:
                try:
                    from_regime = MarketRegime(from_regime_str)
                    to_regime = MarketRegime(to_regime_str)
                    from_idx = regime_to_index[from_regime]
                    to_idx = regime_to_index[to_regime]
                    matrix[from_idx, to_idx] = probability
                except (ValueError, KeyError):
                    continue
            
            row_sums = matrix.sum(axis=1)
            for i, row_sum in enumerate(row_sums):
                if row_sum > 0.001:
                    matrix[i] = matrix[i] / row_sum
            
            logger.info(f"Loaded transition matrix for {symbol}")
            return matrix
        except Exception as e:
            logger.error(f"Failed to load transition matrix for {symbol}: {e}")
            return None
        finally:
            conn.close()
    def should_save_regime_history(self, symbol: str, new_regime_info: MarkovRegimeInfo) -> bool:
        """Check if regime changed enough to warrant saving"""
        try:
            conn = sqlite3.connect(self.db_path)
            cursor = conn.execute("""
                SELECT regime, regime_probability, transition_confidence 
                FROM regime_history 
                WHERE symbol = ? 
                ORDER BY timestamp DESC 
                LIMIT 1
            """, (symbol,))
            
            last_record = cursor.fetchone()
            conn.close()
            
            if not last_record:
                return True  # First record
            
            last_regime, last_prob, last_confidence = last_record
            
            # Only save if significant change
            if (last_regime != new_regime_info.current_regime.value or
                abs(last_prob - new_regime_info.regime_probability) > 0.1 or
                abs(last_confidence - new_regime_info.transition_confidence) > 0.2):
                return True
                
            return False  # Skip duplicate/similar regime
            
        except Exception as e:
            logger.error(f"Error checking regime history: {e}")
            return True  # Save on error to be safe
    def save_regime_history_entry(self, symbol: str, regime_info: MarkovRegimeInfo):
        conn = sqlite3.connect(self.db_path)
        try:
            conn.execute("""
                INSERT INTO regime_history
                (symbol, regime, regime_probability, regime_duration, expected_duration,
                 volatility_regime, etf_flow_regime, etf_flow_strength, 
                 transition_confidence, next_most_likely, timestamp)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """, (
                symbol, regime_info.current_regime.value, regime_info.regime_probability,
                regime_info.regime_duration, regime_info.expected_duration,
                regime_info.volatility_regime.value, regime_info.etf_flow_regime.value,
                regime_info.etf_flow_strength, regime_info.transition_confidence,
                regime_info.next_most_likely.value, datetime.now()
            ))
            
            conn.commit()
            
            # Cleanup old entries
            conn.execute("""
                DELETE FROM regime_history 
                WHERE symbol = ? AND id NOT IN (
                    SELECT id FROM regime_history 
                    WHERE symbol = ? 
                    ORDER BY timestamp DESC 
                    LIMIT 1000
                )
            """, (symbol, symbol))
            
            conn.commit()
        except Exception as e:
            logger.error(f"Failed to save regime history for {symbol}: {e}")
            conn.rollback()
        finally:
            conn.close()
    
    def get_database_stats(self) -> Dict[str, Any]:
        conn = sqlite3.connect(self.db_path)
        try:
            stats = {}
            tables = ['transition_matrix', 'regime_duration_stats', 'regime_history']
            
            for table in tables:
                cursor = conn.execute(f"SELECT COUNT(*) FROM {table}")
                stats[f"{table}_count"] = cursor.fetchone()[0]
            
            cursor = conn.execute("SELECT COUNT(DISTINCT symbol) FROM transition_matrix")
            stats['unique_symbols'] = cursor.fetchone()[0]
            
            stats['database_size'] = os.path.getsize(self.db_path) if self.db_path.exists() else 0
            
            return stats
        except Exception as e:
            logger.error(f"Failed to get database stats: {e}")
            return {}
        finally:
            conn.close()

# ================================================================
# MAIN MARKOV STRATEGY WITH PERSISTENCE
# ================================================================

class MarkovETFStrategyPersistent(BaseStrategy):
    def __init__(self, config: dict):
        super().__init__(config)
        self.primary_strategy_name = config.get('primary_strategy', 'markov_etf')
        # ETF Integration
        self.etf_enabled = config.get('etf_enhancement', {}).get('enabled', True)
        self.etf_manager = None
        if self.etf_enabled:
            try:
                self.etf_manager = ETFFlowManager(config.get('etf_enhancement', {}))
            except Exception as e:
                logger.error(f"ETF manager initialization failed: {e}")
                self.etf_enabled = False
        # set bridge?why?
        self.intelligence_bridge = None
        # Persistence setup
        db_path = config.get('markov_persistence', {}).get('db_path', 'data/markov_persistence.db')
        self.persistence_manager = MarkovPersistenceManager(db_path)
        self.save_frequency = config.get('markov_persistence', {}).get('save_frequency', 50)
        self.auto_load_on_init = config.get('markov_persistence', {}).get('auto_load_on_init', True)
        self._persistence_loaded = {}

        # Markov configuration
        self.markov_config = config.get('markov_model', {})
        self.lookback_period = self.markov_config.get('lookback_period', 100)
        self.min_history_for_forecast = self.markov_config.get('min_history', 30)
        
        # Regime system
        self.regime_states = list(MarketRegime)
        self.regime_to_index = {regime: i for i, regime in enumerate(self.regime_states)}
        self.regime_matrix = self._initialize_9_regime_matrix()
        
        # Markov model components
        self.transition_matrix = np.zeros((9, 9))
        self.regime_duration_stats = {}
        self.regime_history = []
        self.current_regime_info = None
        
        # Strategy weights
        self.regime_strategy_weights = self._initialize_regime_weights()
        
        # Thresholds
        self.vol_thresholds = {
            'low_medium': config.get('vol_threshold_low', 0.015),
            'medium_high': config.get('vol_threshold_high', 0.035)
        }
        self.flow_thresholds = {
            'negative_neutral': config.get('flow_threshold_low', -0.2),
            'neutral_positive': config.get('flow_threshold_high', 0.2)
        }
        
        # Persistence tracking
        self.signals_generated = 0
        self.current_symbol = None
        # Enhanced indicators
        if ENHANCED_INDICATORS_AVAILABLE and EnhancedTechnicalIndicators is not None and self.config.get('enhanced_indicators', {}).get('enabled', False):
            try:
                self.enhanced_processor = EnhancedTechnicalIndicators()
                self.use_enhanced_indicators = True
                logger.info("Enhanced indicators processor initialized successfully")
            except Exception as e:
                logger.error(f"Enhanced indicator initialization failed: {e}")
                self.enhanced_processor = None
                self.use_enhanced_indicators = False
        else:
            self.enhanced_processor = None
            self.use_enhanced_indicators = False
            logger.info("Enhanced indicators disabled or not available")
            logger.info("Enhanced indicators disabled or not available")
        logger.info(f"Markov ETF Strategy with Persistence initialized")
        logger.info(f"Database: {db_path}")
        logger.info(f"Save frequency: every {self.save_frequency} signals")

        # Add intelligence integration flag
        self.cpp_intelligence_enabled = config.get('cpp_intelligence', {}).get('enabled', False)
    
    def set_intelligence_bridge(self, bridge):
        """Set the intelligence bridge - called from main.py"""
        self.intelligence_bridge = bridge
        self.cpp_intelligence_enabled = bridge is not None
        logger.info(f"C++ intelligence bridge {'enabled' if self.cpp_intelligence_enabled else 'disabled'}")
    
    
    #use C++ as boost signal
    def _enhance_signals_with_cpp_intelligence(self, signals: pd.DataFrame, symbol: str) -> pd.DataFrame:
        """Enhance signals with C++ market intelligence - BOOST ONLY, never weaken"""
        try:
            if not self.cpp_intelligence_enabled or not self.intelligence_bridge:
                return signals
                
            intel = self.intelligence_bridge.get_intelligence(symbol)
            if not intel:
                logger.debug(f"No C++ intelligence available for {symbol}")
                return signals
            # check if signals are empty
            if signals.empty:
                logger.debug(f"No signals available for {symbol}")
                return signals
            # Create enhanced columns
            enhanced_signals = signals.copy().fillna(0.0)
            
            # Initialize boost factors - all start at 1.0 (neutral)
            liquidity_boost = 1.0
            volatility_boost = 1.0 
            imbalance_boost = 1.0
            regime_boost = 1.0
            
            # === LIQUIDITY BOOST ===
            # Only boost when liquidity is good (>0.6), no penalty for poor liquidity
            if intel.liquidity_ratio > 0.6:
                liquidity_boost = 1.0 + (intel.liquidity_ratio - 0.6) * 1.25  # Max boost: 1.5x
            enhanced_signals['cpp_liquidity_boost'] = liquidity_boost
            
            # === VOLATILITY BOOST ===
            # Boost signals in favorable volatility conditions
            if 0.01 < intel.volatility_trend < 0.025:  # Sweet spot volatility
                volatility_boost = 1.0 + 0.3  # 30% boost in optimal volatility
            elif intel.volatility_trend < 0.01:  # Low vol - good for accumulation
                volatility_boost = 1.0 + 0.2  # 20% boost
            # High volatility (>0.025): no boost, no penalty
            enhanced_signals['cpp_volatility_boost'] = volatility_boost
            
            # === ORDER BOOK IMBALANCE BOOST ===
            # Boost signals aligned with order book flow direction
            abs_imbalance = abs(intel.order_book_imbalance)
            if abs_imbalance > 0.2:  # Significant imbalance
                # Check if signal direction aligns with imbalance
                latest_signal = enhanced_signals['final_signal'].iloc[-1] if not enhanced_signals.empty else 0
                
                signal_direction = 1 if latest_signal > 0 else -1 if latest_signal < 0 else 0
                imbalance_direction = 1 if intel.order_book_imbalance > 0 else -1
                
                # Boost if signal aligns with order book flow
                if signal_direction == imbalance_direction and signal_direction != 0:
                    imbalance_boost = 1.0 + (abs_imbalance * 0.8)  # Up to 80% boost
            enhanced_signals['cpp_imbalance_boost'] = imbalance_boost
            
            # === REGIME CONFIDENCE BOOST ===
            # Boost when C++ is confident about regime classification
            if intel.regime_confidence > 0.7:
                regime_boost = 1.0 + (intel.regime_confidence - 0.7) * 0.67  # Up to 20% boost when very confident
            enhanced_signals['cpp_regime_boost'] = regime_boost
            
            # === COMBINED BOOST CALCULATION ===
            # Use geometric mean to prevent explosive multiplication
            individual_boosts = [liquidity_boost, volatility_boost, imbalance_boost, regime_boost]
            combined_boost = np.prod(individual_boosts) ** (1.0 / len(individual_boosts))
            
            # Apply maximum boost cap to prevent unrealistic enhancement
            max_total_boost = 2.0  # Maximum 2x boost
            final_boost = min(combined_boost, max_total_boost)
            
            enhanced_signals['cpp_combined_boost'] = final_boost
            
            # === APPLY BOOST TO SIGNALS ===
            # Only apply boost to existing non-zero signals
            #check if we have signal strength otherwise set it to 0.5
            if 'signal_strength' in enhanced_signals.columns:
                original_signal_strength = enhanced_signals['signal_strength'].copy()
            else:
                original_signal_strength = pd.Series(0.5, index=enhanced_signals.index)
            # Boost the continuous signals
            enhanced_signals['enhanced_final_signal'] = enhanced_signals['final_signal'] * final_boost
            enhanced_signals['enhanced_signal_strength'] = original_signal_strength * final_boost
            
            # For discrete signals, only boost if original signal exists and boost is significant
            enhanced_signals['enhanced_signal'] = enhanced_signals['final_signal'].copy()  # Start with original

            # Apply enhanced thresholds only where we have boost AND original signal
            significant_boost_mask = (final_boost > 1.1) & (enhanced_signals['final_signal'] != 0)
            
            if significant_boost_mask.any():
                signal_threshold = self.config.get('signal_threshold', 0.3)
                
                # Apply enhanced thresholding only to boosted signals
                boosted_indices = enhanced_signals[significant_boost_mask].index
                
                for idx in boosted_indices:
                    enhanced_value = enhanced_signals.loc[idx, 'enhanced_final_signal']
                    
                    if enhanced_value > signal_threshold:
                        enhanced_signals.loc[idx, 'enhanced_signal'] = 1
                    elif enhanced_value < -signal_threshold:
                        enhanced_signals.loc[idx, 'enhanced_signal'] = -1
                    # Keep original signal if enhanced doesn't meet threshold
            
            # === INTELLIGENCE METADATA ===
            # Add intelligence metadata for monitoring
            enhanced_signals['cpp_intelligence_available'] = True
            enhanced_signals['cpp_intelligence_age'] = time.time() - intel.last_update
            enhanced_signals['cpp_liquidity_ratio'] = intel.liquidity_ratio
            enhanced_signals['cpp_volatility_trend'] = intel.volatility_trend
            enhanced_signals['cpp_order_book_imbalance'] = intel.order_book_imbalance
            enhanced_signals['cpp_regime_confidence'] = intel.regime_confidence
            
            return enhanced_signals
        except Exception as e:
            logger.error(f"Error enhancing signals with C++ intelligence: {e}")
            return signals
    def _initialize_9_regime_matrix(self) -> Dict[Tuple[VolatilityRegime, ETFFlowRegime], MarketRegime]:
        return {
            (VolatilityRegime.LOW, ETFFlowRegime.POSITIVE): MarketRegime.ACCUMULATION,
            (VolatilityRegime.LOW, ETFFlowRegime.NEUTRAL): MarketRegime.SIDEWAYS,
            (VolatilityRegime.LOW, ETFFlowRegime.NEGATIVE): MarketRegime.DISTRIBUTION,
            (VolatilityRegime.MEDIUM, ETFFlowRegime.POSITIVE): MarketRegime.INSTITUTIONAL_FOMO,
            (VolatilityRegime.MEDIUM, ETFFlowRegime.NEUTRAL): MarketRegime.BALANCED,
            (VolatilityRegime.MEDIUM, ETFFlowRegime.NEGATIVE): MarketRegime.PROFIT_TAKING,
            (VolatilityRegime.HIGH, ETFFlowRegime.POSITIVE): MarketRegime.EUPHORIA,
            (VolatilityRegime.HIGH, ETFFlowRegime.NEUTRAL): MarketRegime.UNCERTAIN,
            (VolatilityRegime.HIGH, ETFFlowRegime.NEGATIVE): MarketRegime.PANIC,
        }
    
    def _initialize_regime_weights(self) -> Dict[MarketRegime, Dict[str, float]]:
        return {
            MarketRegime.ACCUMULATION: {
                'mean_reversion_weight': 0.3, 'trend_following_weight': 1.5,
                'breakout_weight': 1.2, 'momentum_weight': 1.3, 'risk_sensitivity': 0.7
            },
            MarketRegime.INSTITUTIONAL_FOMO: {
                'mean_reversion_weight': 0.5, 'trend_following_weight': 1.4,
                'breakout_weight': 1.5, 'momentum_weight': 1.2, 'risk_sensitivity': 0.8
            },
            MarketRegime.EUPHORIA: {
                'mean_reversion_weight': 0.8, 'trend_following_weight': 1.0,
                'breakout_weight': 0.7, 'momentum_weight': 0.8, 'risk_sensitivity': 1.3
            },
            MarketRegime.SIDEWAYS: {
                'mean_reversion_weight': 1.8, 'trend_following_weight': 0.2,
                'breakout_weight': 0.3, 'momentum_weight': 0.4, 'risk_sensitivity': 0.6
            },
            MarketRegime.BALANCED: {
                'mean_reversion_weight': 1.0, 'trend_following_weight': 1.0,
                'breakout_weight': 1.0, 'momentum_weight': 1.0, 'risk_sensitivity': 1.0
            },
            MarketRegime.UNCERTAIN: {
                'mean_reversion_weight': 1.2, 'trend_following_weight': 0.6,
                'breakout_weight': 0.4, 'momentum_weight': 0.5, 'risk_sensitivity': 1.5
            },
            MarketRegime.DISTRIBUTION: {
                'mean_reversion_weight': 1.6, 'trend_following_weight': 0.4,
                'breakout_weight': 0.5, 'momentum_weight': 0.6, 'risk_sensitivity': 1.1
            },
            MarketRegime.PROFIT_TAKING: {
                'mean_reversion_weight': 1.3, 'trend_following_weight': 0.7,
                'breakout_weight': 0.6, 'momentum_weight': 0.8, 'risk_sensitivity': 1.2
            },
            MarketRegime.PANIC: {
                'mean_reversion_weight': 0.8, 'trend_following_weight': 0.3,
                'breakout_weight': 0.2, 'momentum_weight': 0.3, 'risk_sensitivity': 2.0
            }
        }
     
     
    def calculate_indicators(self, data: pd.DataFrame) -> Dict[str, pd.Series]:
        """Calculate indicators with enhanced support"""
        
        # Calculate base indicators (keep existing functionality)
        indicators = {}
        close = data['close']
        
        # Basic indicators (unchanged)
        indicators['ema_10'] = TechnicalIndicators.ema(close, 10)
        indicators['ema_30'] = TechnicalIndicators.ema(close, 30)
        indicators['zscore_20'] = TechnicalIndicators.zscore(close, 20)
        indicators['rsi_14'] = TechnicalIndicators.rsi(close, 14)
        
        # Bollinger Bands
        bb = TechnicalIndicators.bollinger_bands(close, 20, 2.0)
        indicators.update(bb)
        
        # Volume and trend
        indicators['volume_sma'] = data['volume'].rolling(20).mean()
        indicators['volume_ratio'] = data['volume'] / indicators['volume_sma']
        indicators['trend_strength'] = (indicators['ema_10'] - indicators['ema_30']) / indicators['ema_30']
        
        # Volatility
        returns = close.pct_change()
        indicators['volatility'] = returns.rolling(20).std()
        
        # DIRECT Enhanced indicators integration
        if self.use_enhanced_indicators and self.enhanced_processor:
            try:
                # Call the enhanced indicators directly
                enhanced_indicators = self.enhanced_processor.calculate_comprehensive_indicators(data)
                
                # Add enhanced indicators with prefix to avoid conflicts
                for key, value in enhanced_indicators.items():
                    indicators[f'enhanced_{key}'] = value
                
                # Create simplified composite scores for strategy use
                indicators.update(self._create_simple_composite_scores(enhanced_indicators))
                
                logger.debug(f"Enhanced indicators integrated: {len(enhanced_indicators)} enhanced indicators added")
            except Exception as e:
                logger.error(f"Enhanced indicator processing failed: {e}")
                # Continue with base indicators only
        
        return indicators

    def _create_simple_composite_scores(self, enhanced_indicators: Dict[str, pd.Series]) -> Dict[str, pd.Series]:
        """Create simple composite scores from enhanced indicators"""
        
        composite = {}
        
        # Get a sample index for creating empty series
        sample_index = list(enhanced_indicators.values())[0].index
        
        # 1. Enhanced Trend Score
        trend_components = []
        if 'ema_ribbon_score' in enhanced_indicators:
            trend_components.append(enhanced_indicators['ema_ribbon_score'])
        if 'trend_strength' in enhanced_indicators:
            # Normalize trend strength to -1 to 1 range
            normalized_trend = (enhanced_indicators['trend_strength'] / 100) - 0.5
            trend_components.append(normalized_trend)
        
        if trend_components:
            composite['enhanced_trend_score'] = pd.concat(trend_components, axis=1).mean(axis=1)
        else:
            composite['enhanced_trend_score'] = pd.Series(0.0, index=sample_index)
        
        # 2. Enhanced Momentum Score
        momentum_components = []
        if 'rsi_consensus' in enhanced_indicators:
            # Normalize RSI to -1 to 1 range
            normalized_rsi = (enhanced_indicators['rsi_consensus'] - 50) / 50
            momentum_components.append(normalized_rsi)
        if 'stoch_rsi' in enhanced_indicators:
            normalized_stoch = (enhanced_indicators['stoch_rsi'] - 50) / 50
            momentum_components.append(normalized_stoch)
        
        if momentum_components:
            composite['enhanced_momentum_score'] = pd.concat(momentum_components, axis=1).mean(axis=1)
        else:
            composite['enhanced_momentum_score'] = pd.Series(0.0, index=sample_index)
        
        # 3. Enhanced Volume Score
        volume_components = []
        if 'cmf' in enhanced_indicators:
            volume_components.append(enhanced_indicators['cmf'])
        if 'obv' in enhanced_indicators:
            # Normalize OBV using percentage change
            obv_normalized = enhanced_indicators['obv'].pct_change(20).fillna(0).clip(-0.5, 0.5) * 2
            volume_components.append(obv_normalized)
        
        if volume_components:
            composite['enhanced_volume_score'] = pd.concat(volume_components, axis=1).mean(axis=1)
        else:
            composite['enhanced_volume_score'] = pd.Series(0.0, index=sample_index)
        
        # 4. Enhanced Structure Score
        structure_components = []
        if 'market_structure' in enhanced_indicators:
            structure_components.append(enhanced_indicators['market_structure'])
        if 'resistance_distance' in enhanced_indicators and 'support_distance' in enhanced_indicators:
            # Convert distances to structure signals
            resistance_signal = enhanced_indicators['resistance_distance'].clip(0, 0.1) * -10  # Near resistance = negative
            support_signal = enhanced_indicators['support_distance'].clip(0, 0.1) * 10         # Near support = positive
            structure_components.extend([resistance_signal, support_signal])
        
        if structure_components:
            composite['enhanced_structure_score'] = pd.concat(structure_components, axis=1).mean(axis=1)
        else:
            composite['enhanced_structure_score'] = pd.Series(0.0, index=sample_index)
        
        # 5. Enhanced Divergence Score
        divergence_components = []
        if 'rsi_divergence' in enhanced_indicators:
            divergence_components.append(enhanced_indicators['rsi_divergence'])
        if 'volume_divergence' in enhanced_indicators:
            divergence_components.append(enhanced_indicators['volume_divergence'])
        
        if divergence_components:
            composite['enhanced_divergence_score'] = pd.concat(divergence_components, axis=1).mean(axis=1)
        else:
            composite['enhanced_divergence_score'] = pd.Series(0.0, index=sample_index)
        
        # 6. Master Enhanced Signal (combination of all)
        weights = {'trend': 0.3, 'momentum': 0.25, 'volume': 0.2, 'structure': 0.15, 'divergence': 0.1}
        
        master_signal = (
            composite['enhanced_trend_score'] * weights['trend'] +
            composite['enhanced_momentum_score'] * weights['momentum'] +
            composite['enhanced_volume_score'] * weights['volume'] +
            composite['enhanced_structure_score'] * weights['structure'] +
            composite['enhanced_divergence_score'] * weights['divergence']
        )
        
        composite['enhanced_master_signal'] = master_signal.clip(-2.0, 2.0)
        
        # 7. Signal Confidence
        signal_components = [
            composite['enhanced_trend_score'],
            composite['enhanced_momentum_score'],
            composite['enhanced_volume_score']
        ]
        
        component_df = pd.concat(signal_components, axis=1)
        component_std = component_df.std(axis=1)
        confidence = 1 / (1 + component_std * 2)
        composite['enhanced_signal_confidence'] = confidence.fillna(0.5)
        
        return composite
    async def generate_signals(self, symbol: str, data: pd.DataFrame, indicators: Dict[str, pd.Series]) -> pd.DataFrame:
        try:
            self.current_symbol = symbol
            
            # Load persistent data if needed
            if self.auto_load_on_init and symbol not in self._persistence_loaded:
                await self._load_persistent_data(symbol)
                self._persistence_loaded[symbol] = True
            
            signals = pd.DataFrame(index=data.index)
            
            # Detect regime with Markov enhancement
            regime_info = await self._detect_regime_with_markov(data, indicators)
            
            # Get weights for current regime
            current_weights = self.regime_strategy_weights[regime_info.current_regime]
            
            # Calculate component signals
            if self.use_enhanced_indicators:
                signals['zscore_signal'] = self._calculate_zscore_signals(indicators, current_weights)
                signals['trend_signal'] = self._calculate_trend_signals(indicators, current_weights)
                signals['momentum_signal'] = self._calculate_momentum_signals(indicators, current_weights)
                
                # Add new enhanced signal components
                signals['volume_signal'] = self._calculate_volume_signals(indicators, current_weights)
                signals['structure_signal'] = self._calculate_structure_signals(indicators, current_weights)
                signals['divergence_signal'] = self._calculate_divergence_signals(indicators, current_weights)
                
                # Enhanced breakout signals
                signals['breakout_signal'] = self._calculate_breakout_signals(indicators, current_weights)
                
                # Combine signals with enhanced weighting
                base_signal = (
                    signals['zscore_signal'] * current_weights['mean_reversion_weight'] +
                    signals['trend_signal'] * current_weights['trend_following_weight'] +
                    signals['breakout_signal'] * current_weights['breakout_weight'] +
                    signals['momentum_signal'] * current_weights['momentum_weight']
                ) / 4.0
                
                # Add enhanced components
                enhanced_signal = (
                    signals['volume_signal'] * 0.15 +
                    signals['structure_signal'] * 0.10 +
                    signals['divergence_signal'] * 0.10
                )
                
                signals['base_signal'] = base_signal + enhanced_signal
                
                # Use enhanced master signal if available
                if 'enhanced_master_signal' in indicators:
                    master_signal = indicators['enhanced_master_signal']
                    # Blend with regime-adjusted signal
                    signals['base_signal'] = (signals['base_signal'] * 0.6) + (master_signal * 0.4)
                    
            else:
                # FALLBACK: Use existing calculation methods when enhanced indicators are disabled
                signals['zscore_signal'] = self._calculate_zscore_signals(indicators, current_weights)
                signals['trend_signal'] = self._calculate_trend_signals(indicators, current_weights)
                signals['breakout_signal'] = self._calculate_breakout_signals(indicators, current_weights)
                signals['momentum_signal'] = self._calculate_momentum_signals(indicators, current_weights)
                
                signals['base_signal'] = (
                    signals['zscore_signal'] * current_weights['mean_reversion_weight'] +
                    signals['trend_signal'] * current_weights['trend_following_weight'] +
                    signals['breakout_signal'] * current_weights['breakout_weight'] +
                    signals['momentum_signal'] * current_weights['momentum_weight']
                ) / 4.0

            # Apply ETF enhancement
            etf_enhancement = self._calculate_etf_enhancement(regime_info)
            signals['enhanced_signal'] = signals['base_signal'] * etf_enhancement
            
            # Apply Markov transition enhancement
            transition_enhancement = self._calculate_transition_enhancement(regime_info)
            signals['final_signal'] = signals['enhanced_signal'] * transition_enhancement
            
            # Apply confidence adjustment to final signal if enhanced indicators are available
            if self.use_enhanced_indicators and 'enhanced_signal_confidence' in indicators:
                confidence_factor = indicators['enhanced_signal_confidence']
                signals['final_signal'] = signals['final_signal'] * confidence_factor

            # FIXED LOGIC: Initialize continuous_signal properly
            # continuous_signal = signals['final_signal'].copy()
            continuous_signal = signals['final_signal'].fillna(0.0).copy()
            # logger.warning(f"Initial continuous signal for {symbol} using final_signal: {continuous_signal.iloc[-1] if not continuous_signal.empty else 'N/A'}")
            # Apply orderbook intelligence enhancement if available
            # Check for conditions that indicate C++ orderbook data is available
            # Option 1: Check if C++ intelligence is enabled
            if hasattr(self, 'cpp_intelligence_enabled') and self.cpp_intelligence_enabled:
                signals = self._enhance_signals_with_cpp_intelligence(signals, symbol) 
                # Use enhanced_final_signal if available
                if 'enhanced_final_signal' in signals.columns:
                    continuous_signal = signals['enhanced_final_signal']
                    logger.warning(f"Continuous signal for {symbol} enhanced with C++ intelligence: {continuous_signal.iloc[-1] if not continuous_signal.empty else 'N/A'}")
                else:
                    logger.warning(f"C++ intelligence enabled but enhanced_final_signal is not available for {symbol}")
            else:
                logger.warning(f"C++ intelligence not enabled for {symbol}, using final_signal as continuous signal")
            # Safety check for continuous signal
            if continuous_signal is None or continuous_signal.isnull().all():
                logger.warning(f"Continuous signal is None/null for {symbol}, using default final_signal signal")
                continuous_signal = signals['final_signal'].copy()

            # FIXED: Clamp continuous signal to reasonable bounds
            continuous_signal = continuous_signal.clip(-1.0, 1.0)
            logger.warning(f"Clamped continuous signal for {symbol}: {continuous_signal.iloc[-1] if not continuous_signal.empty else 'N/A'}")
            # Generate discrete signals from the continuous signal
            signal_threshold = self.config.get('signal_threshold', 0.3)
            signals['signal'] = 0  # Initialize discrete signals
            
            # Apply thresholding with proper boolean indexing
            buy_mask = continuous_signal > signal_threshold
            sell_mask = continuous_signal < -signal_threshold
            
            signals.loc[buy_mask, 'signal'] = 1
            signals.loc[sell_mask, 'signal'] = -1
            if continuous_signal is None or pd.isna(continuous_signal).any():
                logger.warning(f"Continuous signal contains null values for {symbol}")
                return pd.DataFrame(index=data.index if not data.empty else pd.Index([]))

            if indicators is None or any(pd.isna(v) if isinstance(v, (int, float)) else False for v in indicators.values()):
                logger.warning(f"Indicators contain null values for {symbol}")
                return pd.DataFrame(index=data.index if not data.empty else pd.Index([]))
            # FIXED: Signal strength calculation (always use absolute value)
            signals['signal_strength'] = abs(continuous_signal).clip(0.0, 1.0)
            
            # Enhanced confidence calculation
            signals['confidence'] = self._calculate_enhanced_signal_confidence(signals, indicators)

            # Add metadata
            signals['regime'] = regime_info.current_regime.value
            signals['volatility'] = indicators.get('atr', 0.05)
            signals['next_likely_regime'] = regime_info.next_most_likely.value
            signals['transition_confidence'] = regime_info.transition_confidence
            
            # Add enhanced metadata if available
            if 'enhanced_trend_score' in indicators:
                signals['enhanced_trend_component'] = indicators['enhanced_trend_score']
            if 'enhanced_momentum_score' in indicators:
                signals['enhanced_momentum_component'] = indicators['enhanced_momentum_score']
            if 'enhanced_volume_score' in indicators:
                signals['enhanced_volume_component'] = indicators['enhanced_volume_score']

            # FIXED: Add the continuous signal to output for debugging/analysis
            signals['continuous_signal'] = continuous_signal
            
            # Validate final signals
            assert signals['signal'].isin([-1, 0, 1]).all(), "Signal column contains invalid values"
            # assert not signals['signal_strength'].isnull().any(), "Signal strength contains null values"
            assert (signals['signal_strength'] >= 0).all(), "Signal strength contains negative values"
            if signals['signal_strength'].isnull().any():
                logger.error(f"Signal strength contains null values for {symbol}")
                return pd.DataFrame(index=data.index if not data.empty else pd.Index([]))
            # Persistence
            self.signals_generated += 1
            if self.signals_generated == 1 or self.signals_generated % self.save_frequency == 0:
                await self._save_persistent_data(symbol)
            
            if regime_info and self.persistence_manager.should_save_regime_history(symbol, regime_info):
                self.persistence_manager.save_regime_history_entry(symbol, regime_info)
                
            logger.info(f"Generated signal for {symbol}: {signals['signal'].iloc[-1]}")
            logger.info(f"Signal strength: {signals['signal_strength'].iloc[-1]:.3f}")
            logger.info(f"Continuous signal: {continuous_signal.iloc[-1]:.3f}")
            logger.info(f"Regime: {regime_info.current_regime.value}")
            
            return signals

        except Exception as e:
            logger.error(f"Error in signal generation for {symbol}: {e}")
            # logger.error(f"Stack trace: {traceback.format_exc()}")
            # Return empty DataFrame with proper index to prevent downstream errors
            return pd.DataFrame(index=data.index if not data.empty else pd.Index([]))
    # Also add this method to handle the else case in generate_signals:
    # async def generate_signals(self, symbol: str, data: pd.DataFrame, indicators: Dict[str, pd.Series]) -> pd.DataFrame:
    #     try:
    #         self.current_symbol = symbol
            
    #         # Load persistent data if needed
    #         if self.auto_load_on_init and symbol not in self._persistence_loaded:
    #             await self._load_persistent_data(symbol)
    #             self._persistence_loaded[symbol] = True
            
    #         signals = pd.DataFrame(index=data.index)
            
    #         # Detect regime with Markov enhancement
    #         regime_info = await self._detect_regime_with_markov(data, indicators)
            
    #         # Get weights for current regime
    #         current_weights = self.regime_strategy_weights[regime_info.current_regime]
            
    #         # Calculate component signals
    #         if self.use_enhanced_indicators:
    #             signals['zscore_signal'] = self._calculate_zscore_signals(indicators, current_weights)
    #             signals['trend_signal'] = self._calculate_trend_signals(indicators, current_weights)
    #             signals['momentum_signal'] = self._calculate_momentum_signals(indicators, current_weights)
                
    #             # Add new enhanced signal components
    #             signals['volume_signal'] = self._calculate_volume_signals(indicators, current_weights)
    #             signals['structure_signal'] = self._calculate_structure_signals(indicators, current_weights)
    #             signals['divergence_signal'] = self._calculate_divergence_signals(indicators, current_weights)
                
    #             # Enhanced breakout signals
    #             signals['breakout_signal'] = self._calculate_breakout_signals(indicators, current_weights)
                
    #             # Combine signals with enhanced weighting
    #             base_signal = (
    #                 signals['zscore_signal'] * current_weights['mean_reversion_weight'] +
    #                 signals['trend_signal'] * current_weights['trend_following_weight'] +
    #                 signals['breakout_signal'] * current_weights['breakout_weight'] +
    #                 signals['momentum_signal'] * current_weights['momentum_weight']
    #             ) / 4.0
                
    #             # Add enhanced components
    #             enhanced_signal = (
    #                 signals['volume_signal'] * 0.15 +
    #                 signals['structure_signal'] * 0.10 +
    #                 signals['divergence_signal'] * 0.10
    #             )
                
    #             signals['base_signal'] = base_signal + enhanced_signal
                
    #             # Use enhanced master signal if available
    #             if 'enhanced_master_signal' in indicators:
    #                 master_signal = indicators['enhanced_master_signal']
    #                 # Blend with regime-adjusted signal
    #                 signals['base_signal'] = (signals['base_signal'] * 0.6) + (master_signal * 0.4)
                    
    #         else:
    #             # FALLBACK: Use existing calculation methods when enhanced indicators are disabled
    #             signals['zscore_signal'] = self._calculate_zscore_signals(indicators, current_weights)
    #             signals['trend_signal'] = self._calculate_trend_signals(indicators, current_weights)
    #             signals['breakout_signal'] = self._calculate_breakout_signals(indicators, current_weights)
    #             signals['momentum_signal'] = self._calculate_momentum_signals(indicators, current_weights)
                
    #             signals['base_signal'] = (
    #                 signals['zscore_signal'] * current_weights['mean_reversion_weight'] +
    #                 signals['trend_signal'] * current_weights['trend_following_weight'] +
    #                 signals['breakout_signal'] * current_weights['breakout_weight'] +
    #                 signals['momentum_signal'] * current_weights['momentum_weight']
    #             ) / 4.0

    #         # Apply ETF enhancement
    #         etf_enhancement = self._calculate_etf_enhancement(regime_info)
    #         signals['enhanced_signal'] = signals['base_signal'] * etf_enhancement
            
    #         # Apply Markov transition enhancement
    #         transition_enhancement = self._calculate_transition_enhancement(regime_info)
    #         signals['final_signal'] = signals['enhanced_signal'] * transition_enhancement
            
    #         # Apply confidence adjustment to final signal if enhanced indicators are available
    #         if self.use_enhanced_indicators and 'enhanced_signal_confidence' in indicators:
    #             confidence_factor = indicators['enhanced_signal_confidence']
    #             signals['final_signal'] = signals['final_signal'] * confidence_factor

    #         # Apply orderbook intelligence enhancement to continuous signal
    #         if 'signal_strength' in signals.columns:
    #             signals = self._enhance_signals_with_cpp_intelligence(signals, symbol)
                
    #         # FIXED: Use enhanced continuous signal if available, otherwise use final_signal
    #         # continuous_signal = signals.get('enhanced_final_signal', signals['final_signal'])
    #         # if 'enhanced_final_signal' in signals.columns:
    #         #     continuous_signal = signals['enhanced_final_signal']
    #         # else:
    #         #     continuous_signal = signals['final_signal']
    #         if 'enhanced_final_signal' in signals.columns:
    #             continuous_signal = signals['enhanced_final_signal']
    #         else:
    #             continuous_signal = signals['final_signal']

    #         # Add safety check:
    #         if continuous_signal is None:
    #             logger.error(f"Both enhanced_final_signal and final_signal are None for {symbol}!")
    #             continuous_signal = pd.Series(0.5, index=signals.index)

    #         # Generate discrete signals from the best continuous signal
    #         signal_threshold = self.config.get('signal_threshold', 0.3)
    #         signals['signal'] = 0  # Initialize discrete signals
    #         signals.loc[continuous_signal > signal_threshold, 'signal'] = 1
    #         signals.loc[continuous_signal < -signal_threshold, 'signal'] = -1
            
    #         # Signal strength for position sizing (based on continuous signal)
    #         signals['signal_strength'] = abs(continuous_signal) if continuous_signal is not None else 0.5
            
    #         # Enhanced confidence calculation
    #         signals['confidence'] = self._calculate_enhanced_signal_confidence(signals, indicators)

    #         # Add metadata
    #         signals['regime'] = regime_info.current_regime.value
    #         signals['volatility'] = indicators.get('atr', 0.05)
    #         signals['next_likely_regime'] = regime_info.next_most_likely.value
    #         signals['transition_confidence'] = regime_info.transition_confidence
            
    #         # Add enhanced metadata if available
    #         if 'enhanced_trend_score' in indicators:
    #             signals['enhanced_trend_component'] = indicators['enhanced_trend_score']
    #         if 'enhanced_momentum_score' in indicators:
    #             signals['enhanced_momentum_component'] = indicators['enhanced_momentum_score']
    #         if 'enhanced_volume_score' in indicators:
    #             signals['enhanced_volume_component'] = indicators['enhanced_volume_score']

    #         # Validate final signals
    #         assert signals['signal'].isin([-1, 0, 1]).all(), "Signal column contains invalid values"
            
    #         # Persistence
    #         self.signals_generated += 1
    #         if self.signals_generated == 1 or self.signals_generated % self.save_frequency == 0:
    #             await self._save_persistent_data(symbol)
            
    #         if regime_info and self.persistence_manager.should_save_regime_history(symbol, regime_info):
    #             self.persistence_manager.save_regime_history_entry(symbol, regime_info)
                
    #         logger.info(f"Generated signal for {symbol}: {signals['signal'].iloc[-1]}")
    #         logger.info(f"Signal strength: {signals['signal_strength'].iloc[-1]:.3f}")
    #         logger.info(f"Regime: {regime_info.current_regime.value}")
            
    #         return signals

    #     except Exception as e:
    #         logger.error(f"SOS send help!!something is fucking wong for {symbol}: {e}")
    #         return pd.DataFrame()
    # Add method to get enhanced strategy info:
    def get_enhanced_strategy_info(self) -> Dict[str, Any]:
        """Get enhanced strategy information"""
        base_info = self.get_strategy_info()
        
        enhanced_info = {
            'enhanced_indicators': {
                'enabled': self.use_enhanced_indicators,
                'processor_available': self.enhanced_processor is not None,
                'features': {}
            }
        }
        
        if self.enhanced_processor:
            enhanced_info['enhanced_indicators']['features'] = self.enhanced_processor.get_feature_status()
        
        base_info.update(enhanced_info)
        return base_info
    def _calculate_breakout_signals_enhanced(self, indicators: Dict[str, pd.Series], weights: Dict[str, float]) -> pd.Series:
        """Enhanced breakout signals"""
        
        # Start with existing breakout logic
        upper = indicators.get('upper')
        lower = indicators.get('lower')
        if upper is None or lower is None:
            return pd.Series(0.0, index=indicators['ema_10'].index)
        
        close = indicators['ema_10']  # Use EMA as proxy for close
        signals = pd.Series(0.0, index=upper.index)
        signals[close > upper] = 1.0 * weights['breakout_weight']
        signals[close < lower] = -1.0 * weights['breakout_weight']
        
        # Enhance with Bollinger Band %B if available
        if 'bb_percent_b' in indicators:
            bb_percent_b = indicators['bb_percent_b']
            bb_signals = pd.Series(0.0, index=bb_percent_b.index)
            bb_signals[bb_percent_b > 1.0] = 1.0  # Above upper band
            bb_signals[bb_percent_b < 0.0] = -1.0  # Below lower band
            
            signals = (signals * 0.7) + (bb_signals * weights['breakout_weight'] * 0.3)
        
        return signals.fillna(0)

    # Add enhanced signal confidence calculation:
    def _calculate_enhanced_signal_confidence(self, signals: pd.DataFrame, indicators: Dict[str, pd.Series]) -> pd.Series:
        """Calculate signal confidence using enhanced indicators"""
        
        base_confidence = 0.6  # Base confidence level
        
        # Use enhanced confidence if available
        if 'enhanced_signal_confidence' in indicators:
            enhanced_confidence = indicators['enhanced_signal_confidence']
            return (enhanced_confidence * 0.7) + (base_confidence * 0.3)
        
        # Fallback: calculate confidence from signal agreement
        signal_components = []
        for col in ['zscore_signal', 'trend_signal', 'momentum_signal']:
            if col in signals.columns:
                signal_components.append(signals[col])
        
        if len(signal_components) >= 2:
            component_df = pd.concat(signal_components, axis=1)
            component_std = component_df.std(axis=1)
            confidence = 1 / (1 + component_std * 2)
            return confidence.fillna(base_confidence)
        
        return pd.Series(base_confidence, index=signals.index)
    async def _detect_regime_with_markov(self, data: pd.DataFrame, indicators: Dict[str, pd.Series]) -> MarkovRegimeInfo:
        # Get ETF flow data
        etf_flow = await self._get_etf_flow_data()
        
        # Calculate current volatility
        returns = data['close'].pct_change().dropna()
        current_volatility = returns.tail(20).std() if len(returns) >= 20 else 0.02
        
        # Classify regimes
        vol_regime = self._classify_volatility_regime(current_volatility)
        etf_regime = self._classify_etf_flow_regime(etf_flow)
        current_regime = self.regime_matrix.get((vol_regime, etf_regime), MarketRegime.BALANCED)
        
        # Calculate regime duration
        regime_duration = self._get_current_regime_duration(current_regime)
        
        # Update Markov models if sufficient history
        regime_history_regimes = [r.current_regime for r in self.regime_history[-self.lookback_period:]]
        if len(regime_history_regimes) >= self.min_history_for_forecast:
            self._update_transition_matrix(regime_history_regimes)
            self._update_regime_duration_stats(regime_history_regimes)
        
        # Forecast transitions
        if len(regime_history_regimes) >= self.min_history_for_forecast:
            forecast = self._forecast_regime_transition(current_regime, regime_duration)
        else:
            forecast = {
                'transition_forecast': {r: 1.0/9 for r in self.regime_states},
                'next_most_likely': current_regime,
                'transition_confidence': 0.0
            }
        
        # Expected duration
        expected_duration = 5.0
        if current_regime in self.regime_duration_stats:
            expected_duration = self.regime_duration_stats[current_regime]['mean_duration']
        
        # ETF flow strength
        etf_flow_strength = etf_flow.flow_strength if etf_flow else 0.0
        
        # Create regime info
        regime_info = MarkovRegimeInfo(
            current_regime=current_regime,
            regime_probability=0.8,
            regime_duration=regime_duration,
            expected_duration=expected_duration,
            transition_forecast=forecast['transition_forecast'],
            next_most_likely=forecast['next_most_likely'],
            transition_confidence=forecast['transition_confidence'],
            volatility_regime=vol_regime,
            etf_flow_regime=etf_regime,
            etf_flow_strength=etf_flow_strength
        )
        
        # Store in history
        self.regime_history.append(regime_info)
        if len(self.regime_history) > self.lookback_period:
            self.regime_history.pop(0)
        
        self.current_regime_info = regime_info
        return regime_info
    
    async def _get_etf_flow_data(self) -> Optional[CompositeETFFlow]:
        if not self.etf_enabled or not self.etf_manager:
            return None
        try:
            return await self.etf_manager.get_composite_etf_flow()
        except Exception as e:
            logger.error(f"Failed to get ETF flow: {e}")
            return None
    
    def _classify_volatility_regime(self, volatility: float) -> VolatilityRegime:
        if volatility < self.vol_thresholds['low_medium']:
            return VolatilityRegime.LOW
        elif volatility < self.vol_thresholds['medium_high']:
            return VolatilityRegime.MEDIUM
        else:
            return VolatilityRegime.HIGH
    
    def _classify_etf_flow_regime(self, etf_flow: Optional[CompositeETFFlow]) -> ETFFlowRegime:
        if etf_flow is None:
            return ETFFlowRegime.NEUTRAL
        
        flow_strength = etf_flow.flow_strength * etf_flow.confidence
        if flow_strength < self.flow_thresholds['negative_neutral']:
            return ETFFlowRegime.NEGATIVE
        elif flow_strength > self.flow_thresholds['neutral_positive']:
            return ETFFlowRegime.POSITIVE
        else:
            return ETFFlowRegime.NEUTRAL
    
    def _get_current_regime_duration(self, current_regime: MarketRegime) -> int:
        if not self.regime_history:
            return 1
        
        duration = 1
        for regime_info in reversed(self.regime_history):
            if regime_info.current_regime == current_regime:
                duration += 1
            else:
                break
        return duration
    
    def _update_transition_matrix(self, regime_history: List[MarketRegime], alpha: float = 0.5, min_transitions: int = 3, blend_weight: float = 0.7) -> None:
        """
        Update the transition matrix using both short-term (windowed) and long-term (cumulative) counts.
        - Laplace/Dirichlet smoothing (alpha)
        - Fallback to uniform for rare regimes
        - Blend short-term and long-term matrices
        """
        n_regimes = 9
        # --- Short-term (windowed) counts ---
        short_counts = np.zeros((n_regimes, n_regimes)) + alpha
        if len(regime_history) < 2:
            self.transition_matrix = np.ones((n_regimes, n_regimes)) / n_regimes
            return
        for i in range(len(regime_history) - 1):
            current_regime = regime_history[i]
            next_regime = regime_history[i + 1]
            current_idx = self.regime_to_index[current_regime]
            next_idx = self.regime_to_index[next_regime]
            short_counts[current_idx, next_idx] += 1

        # --- Long-term (cumulative) counts ---
        if not hasattr(self, 'long_term_counts') or self.long_term_counts is None:
            self.long_term_counts = np.zeros((n_regimes, n_regimes)) + alpha
        # Update long-term counts with this window
        for i in range(len(regime_history) - 1):
            current_regime = regime_history[i]
            next_regime = regime_history[i + 1]
            current_idx = self.regime_to_index[current_regime]
            next_idx = self.regime_to_index[next_regime]
            self.long_term_counts[current_idx, next_idx] += 1

        # --- Normalize to get probabilities ---
        short_tm = short_counts / short_counts.sum(axis=1, keepdims=True)
        long_tm = self.long_term_counts / self.long_term_counts.sum(axis=1, keepdims=True)

        # --- Fallback for rare regimes (row sum too small) ---
        for i in range(n_regimes):
            if short_counts[i].sum() < min_transitions:
                short_tm[i] = np.ones(n_regimes) / n_regimes
            if self.long_term_counts[i].sum() < min_transitions:
                long_tm[i] = np.ones(n_regimes) / n_regimes

        # --- Blend short and long-term matrices ---
        self.transition_matrix = blend_weight * short_tm + (1 - blend_weight) * long_tm
    def _update_regime_duration_stats(self, regime_history: List[MarketRegime]) -> None:
        if len(regime_history) < 3:
            return
            
        duration_data = defaultdict(list)
        current_regime = regime_history[0]
        duration = 1
        
        for next_regime in regime_history[1:]:
            if next_regime == current_regime:
                duration += 1
            else:
                duration_data[current_regime].append(duration)
                current_regime = next_regime
                duration = 1
        
        self.regime_duration_stats = {}
        for regime, durations in duration_data.items():
            if len(durations) > 2:
                scale = np.mean(durations)
                self.regime_duration_stats[regime] = {
                    'mean_duration': scale,
                    'distribution': stats.expon(scale=scale),
                    'samples': durations
                }
    
    def _forecast_regime_transition(self, current_regime: MarketRegime, current_duration: int) -> Dict[str, Any]:
        current_idx = self.regime_to_index[current_regime]
        base_transition_probs = self.transition_matrix[current_idx].copy()
        
        # Adjust for duration
        if current_regime in self.regime_duration_stats:
            stats_info = self.regime_duration_stats[current_regime]
            expected_duration = stats_info['mean_duration']
            duration_factor = min(3.0, current_duration / expected_duration)
            base_transition_probs[current_idx] *= (1.0 / duration_factor)
            
            # Renormalize
            if base_transition_probs.sum() > 0:
                base_transition_probs /= base_transition_probs.sum()
        
        # Convert to regime probabilities
        transition_forecast = {}
        for regime in self.regime_states:
            idx = self.regime_to_index[regime]
            transition_forecast[regime] = base_transition_probs[idx]
        
        # Find most likely next regime
        next_regime_probs = base_transition_probs.copy()
        next_regime_probs[current_idx] = 0
        
        if next_regime_probs.sum() > 0:
            most_likely_idx = np.argmax(next_regime_probs)
            next_most_likely = self.regime_states[most_likely_idx]
            transition_confidence = next_regime_probs[most_likely_idx]
        else:
            next_most_likely = current_regime
            transition_confidence = 0.0
        
        return {
            'transition_forecast': transition_forecast,
            'next_most_likely': next_most_likely,
            'transition_confidence': transition_confidence
        }
    
    # Signal calculation methods
    # def _calculate_zscore_signals(self, indicators: Dict[str, pd.Series], weights: Dict[str, float]) -> pd.Series:
    #     zscore = indicators['zscore_20']
    #     signals = pd.Series(0.0, index=zscore.index)
    #     signals[zscore < -2.0] = 1.0 * weights['mean_reversion_weight']
    #     signals[zscore > 2.0] = -1.0 * weights['mean_reversion_weight']
    #     signals[abs(zscore) < 0.5] = 0.0
    #     return signals.fillna(0)
    def _calculate_trend_signals(self, indicators: Dict[str, pd.Series], weights: Dict[str, float]) -> pd.Series:
        """Enhanced trend signals"""
        
        # Base trend signal
        trend_strength = indicators['trend_strength']
        signals = pd.Series(0.0, index=trend_strength.index)
        signals[trend_strength > 0.02] = 1.0 * weights['trend_following_weight']
        signals[trend_strength < -0.02] = -1.0 * weights['trend_following_weight']
        
        # Enhance with EMA ribbon if available
        if 'enhanced_ema_ribbon_score' in indicators:
            ema_ribbon = indicators['enhanced_ema_ribbon_score']
            ribbon_signals = ema_ribbon * weights['trend_following_weight']
            signals = (signals * 0.6) + (ribbon_signals * 0.4)
        
        # Enhance with trend composite if available
        if 'enhanced_trend_score' in indicators:
            trend_composite = indicators['enhanced_trend_score']
            composite_signals = trend_composite * weights['trend_following_weight']
            signals = (signals * 0.5) + (composite_signals * 0.5)
        
        return signals.fillna(0)
    def _calculate_momentum_signals(self, indicators: Dict[str, pd.Series], weights: Dict[str, float]) -> pd.Series:
        """Enhanced momentum signals"""
        
        # Base RSI signals
        rsi = indicators['rsi_14']
        signals = pd.Series(0.0, index=rsi.index)
        signals[(rsi < 30) & (rsi.shift(1) >= 30)] = 1.0 * weights['momentum_weight']
        signals[(rsi > 70) & (rsi.shift(1) <= 70)] = -1.0 * weights['momentum_weight']
        
        # Enhance with RSI consensus if available
        if 'enhanced_rsi_consensus' in indicators:
            rsi_consensus = indicators['enhanced_rsi_consensus']
            consensus_signals = pd.Series(0.0, index=rsi_consensus.index)
            consensus_signals[(rsi_consensus < 30) & (rsi_consensus.shift(1) >= 30)] = 1.0
            consensus_signals[(rsi_consensus > 70) & (rsi_consensus.shift(1) <= 70)] = -1.0
            
            signals = (signals * 0.6) + (consensus_signals * weights['momentum_weight'] * 0.4)
        
        # Enhance with momentum composite if available
        if 'enhanced_momentum_score' in indicators:
            momentum_composite = indicators['enhanced_momentum_score']
            composite_signals = momentum_composite * weights['momentum_weight']
            signals = (signals * 0.5) + (composite_signals * 0.5)
        
        return signals.fillna(0)

    def _calculate_zscore_signals(self, indicators: Dict[str, pd.Series], weights: Dict[str, float]) -> pd.Series:
        """Z-Score signals using both basic and enhanced indicators"""

        # Start with existing zscore logic
        zscore = indicators['zscore_20']
        signals = pd.Series(0.0, index=zscore.index)
        signals[zscore < -2.0] = 1.0 * weights['mean_reversion_weight']
        signals[zscore > 2.0] = -1.0 * weights['mean_reversion_weight']
        signals[abs(zscore) < 0.5] = 0.0
        
        # Enhance with adaptive zscore if available
        if 'enhanced_adaptive_zscore' in indicators:
            adaptive_zscore = indicators['enhanced_adaptive_zscore']
            adaptive_signals = pd.Series(0.0, index=adaptive_zscore.index)
            adaptive_signals[adaptive_zscore < -2.0] = 1.0
            adaptive_signals[adaptive_zscore > 2.0] = -1.0
            
            # Combine signals with weighting
            signals = (signals * 0.7) + (adaptive_signals * weights['mean_reversion_weight'] * 0.3)
        
        return signals.fillna(0)
    # def _calculate_trend_signals(self, indicators: Dict[str, pd.Series], weights: Dict[str, float]) -> pd.Series:
    #     trend_strength = indicators['trend_strength']
    #     signals = pd.Series(0.0, index=trend_strength.index)
    #     signals[trend_strength > 0.02] = 1.0 * weights['trend_following_weight']
    #     signals[trend_strength < -0.02] = -1.0 * weights['trend_following_weight']
    #     return signals.fillna(0)
    
    def _calculate_breakout_signals(self, indicators: Dict[str, pd.Series], weights: Dict[str, float]) -> pd.Series:
        upper = indicators.get('upper')
        lower = indicators.get('lower')
        if upper is None or lower is None:
            return pd.Series(0.0, index=indicators['ema_10'].index)
        
        # Use close from the original data
        close = upper.index.to_series().map(lambda x: indicators['ema_10'].loc[x])
        
        signals = pd.Series(0.0, index=upper.index)
        signals[close > upper] = 1.0 * weights['breakout_weight']
        signals[close < lower] = -1.0 * weights['breakout_weight']
        return signals.fillna(0)
    
    # def _calculate_momentum_signals(self, indicators: Dict[str, pd.Series], weights: Dict[str, float]) -> pd.Series:
    #     rsi = indicators['rsi_14']
    #     signals = pd.Series(0.0, index=rsi.index)
    #     signals[(rsi < 30) & (rsi.shift(1) >= 30)] = 1.0 * weights['momentum_weight']
    #     signals[(rsi > 70) & (rsi.shift(1) <= 70)] = -1.0 * weights['momentum_weight']
    #     return signals.fillna(0)
    
    def _calculate_volume_signals(self, indicators: Dict[str, pd.Series], weights: Dict[str, float]) -> pd.Series:
        """Volume-based signals using enhanced indicators"""
        
        if 'enhanced_volume_score' not in indicators:
            # Fallback to basic volume ratio
            volume_ratio = indicators.get('volume_ratio', pd.Series(0.0, index=indicators['ema_10'].index))
            signals = pd.Series(0.0, index=volume_ratio.index)
            signals[volume_ratio > 1.5] = 0.5  # High volume confirmation
            signals[volume_ratio < 0.5] = -0.2  # Low volume warning
            return signals
        
        volume_score = indicators['enhanced_volume_score']
        return volume_score * 0.5  # Scale down volume impact

    def _calculate_structure_signals(self, indicators: Dict[str, pd.Series], weights: Dict[str, float]) -> pd.Series:
        """Market structure signals"""
        
        if 'enhanced_structure_score' not in indicators:
            return pd.Series(0.0, index=indicators['ema_10'].index)
        
        structure_score = indicators['enhanced_structure_score']
        return structure_score * 0.3  # Conservative structure weighting

    def _calculate_divergence_signals(self, indicators: Dict[str, pd.Series], weights: Dict[str, float]) -> pd.Series:
        """Divergence signals"""
        
        if 'enhanced_divergence_score' not in indicators:
            return pd.Series(0.0, index=indicators['ema_10'].index)
        
        divergence_score = indicators['enhanced_divergence_score']
        return divergence_score * 0.4  # Moderate divergence impact

    def _calculate_etf_enhancement(self, regime_info: MarkovRegimeInfo) -> float:
        base_enhancement = 1.0
        flow_strength = regime_info.etf_flow_strength
        
        if regime_info.etf_flow_regime == ETFFlowRegime.POSITIVE:
            enhancement = 1.0 + (flow_strength * regime_info.regime_probability * 0.5)
        elif regime_info.etf_flow_regime == ETFFlowRegime.NEGATIVE:
            enhancement = 1.0 + (abs(flow_strength) * regime_info.regime_probability * 0.3)
        else:
            enhancement = 1.0 + (abs(flow_strength) * regime_info.regime_probability * 0.1)
        
        return np.clip(enhancement, 0.5, 2.0)
    
    def _calculate_transition_enhancement(self, regime_info: MarkovRegimeInfo) -> float:
        base_enhancement = 1.0
        
        if regime_info.transition_confidence > 0.7:
            current_regime = regime_info.current_regime
            next_regime = regime_info.next_most_likely
            
            # Specific transition enhancements
            if (current_regime == MarketRegime.SIDEWAYS and next_regime == MarketRegime.ACCUMULATION):
                enhancement = 1.3
            elif (current_regime == MarketRegime.ACCUMULATION and next_regime == MarketRegime.INSTITUTIONAL_FOMO):
                enhancement = 1.2
            elif (current_regime == MarketRegime.EUPHORIA and 
                  next_regime in [MarketRegime.PROFIT_TAKING, MarketRegime.UNCERTAIN]):
                enhancement = 0.7
            elif (current_regime in [MarketRegime.PROFIT_TAKING, MarketRegime.DISTRIBUTION] and
                  next_regime == MarketRegime.PANIC):
                enhancement = 0.5
            elif (current_regime == MarketRegime.PANIC and
                  next_regime in [MarketRegime.DISTRIBUTION, MarketRegime.SIDEWAYS]):
                enhancement = 1.1
            else:
                enhancement = 1.0 + (regime_info.transition_confidence - 0.7) * 0.2
        else:
            enhancement = base_enhancement
        
        return np.clip(enhancement, 0.3, 1.8)
    
    # Persistence methods
    async def _load_persistent_data(self, symbol: str):
        try:
            logger.info(f"Loading persistent data for {symbol}")
            
            # Load transition matrix
            loaded_matrix = self.persistence_manager.load_transition_matrix(symbol, self.regime_states)
            if loaded_matrix is not None:
                self.transition_matrix = loaded_matrix
                logger.info(f"Transition matrix loaded for {symbol}")
            
            logger.info(f"Persistence loading completed for {symbol}")
        except Exception as e:
            logger.error(f"Failed to load persistent data for {symbol}: {e}")
    
    async def _save_persistent_data(self, symbol: str):
        try:
            logger.info(f"Saving persistent data for {symbol}")
            
            if hasattr(self, 'transition_matrix') and self.transition_matrix.sum() > 0:
                self.persistence_manager.save_transition_matrix(
                    symbol, self.transition_matrix, self.regime_states
                )
            
            logger.info(f"Persistent data saved for {symbol}")
        except Exception as e:
            logger.error(f"Failed to save persistent data for {symbol}: {e}")
    
    def force_save_all_data(self, symbol: str = None):
        if symbol is None:
            symbol = self.current_symbol
        if symbol:
            asyncio.create_task(self._save_persistent_data(symbol))
            logger.info(f"Force save initiated for {symbol}")
    
    def get_persistence_stats(self) -> Dict[str, Any]:
        stats = self.persistence_manager.get_database_stats()
        stats.update({
            'current_symbol': self.current_symbol,
            'signals_generated': self.signals_generated,
            'next_save_in': self.save_frequency - (self.signals_generated % self.save_frequency),
            'transition_matrix_non_zero': int(np.count_nonzero(self.transition_matrix)),
            'regime_history_length': len(self.regime_history),
            'duration_stats_count': len(self.regime_duration_stats)
        })
        return stats
    
    def get_strategy_info(self) -> Dict[str, Any]:
        info = {
            'strategy_name': 'full indicator Markov-Enhanced ETF Strategy with Persistence',
            'model_type': 'Markov Chain Regime Switching',
            'regime_system': '9-regime (3x3 volatility x ETF flow)',
            'markov_states': len(self.regime_states),
            'etf_enabled': self.etf_enabled,
            'persistence_enabled': True,
            'current_regime': None,
            'markov_forecast': None
        }
        
        if self.current_regime_info:
            info['current_regime'] = {
                'regime': self.current_regime_info.current_regime.value,
                'duration': self.current_regime_info.regime_duration,
                'expected_duration': self.current_regime_info.expected_duration,
                'probability': self.current_regime_info.regime_probability,
                'volatility': self.current_regime_info.volatility_regime.value,
                'etf_flow': self.current_regime_info.etf_flow_regime.value,
                'etf_strength': self.current_regime_info.etf_flow_strength
            }
            
            info['markov_forecast'] = {
                'next_most_likely': self.current_regime_info.next_most_likely.value,
                'transition_confidence': self.current_regime_info.transition_confidence
            }
        info['persistence'] = self.get_persistence_stats()
        return info
    def get_system_capabilities(self) -> Dict:
        """Get system capabilities - main.py compatibility"""
        
        capabilities = {
            'strategies': {
                'total': 1,
                'primary': self.primary_strategy_name,
                'enabled': [self.primary_strategy_name],
                'types': {
                    'markov_etf': 1 if 'markov' in self.primary_strategy_name else 0,
                    'traditional': 0 if 'markov' in self.primary_strategy_name else 1,
                    'fallback': 1 if 'fallback' in self.primary_strategy_name else 0
                }
            },
            'etf_integration': {
                'available': True,
                'enabled': self.etf_enabled,
                'manager_ready': self.etf_enabled,
                'strategy_type': 'markov_etf' if 'markov' in self.primary_strategy_name else 'traditional'
            },
            'regime_tracking': {
                'current_regime': str(getattr(self.current_regime_info, 'current_regime', 'none')) if self.current_regime_info else None,
                'regime_history_length': len(getattr(self.current_regime_info, 'regime_history', [])),
                'markov_enabled': 'markov' in self.primary_strategy_name
            },
            'features': self._get_strategy_features()
        }
        
        return capabilities
    def _get_strategy_features(self) -> Dict[str, bool]:
        """Get strategy features - main.py compatibility"""

        features = {
            'etf_support': self.etf_enabled,
            'markov_support': 'markov' in self.primary_strategy_name,
            'persistence_support': True
        }

        return features
# ================================================================
# USAGE EXAMPLE AND TESTING
# ================================================================

async def test_merged_strategy():
    """Test the merged strategy"""
    
    config = {
        'etf_enhancement': {
            'enabled': True,
            'alphavantage_api_key': 'demo'
        },
        'markov_persistence': {
            'db_path': 'data/test_markov_merged.db',
            'save_frequency': 3,
            'auto_load_on_init': True
        },
        'markov_model': {
            'lookback_period': 100,
            'min_history': 30
        },
        'signal_threshold': 0.3
    }
    
    print("Testing Merged Markov Strategy with Persistence")
    print("=" * 50)
    
    # Create strategy
    strategy = MarkovETFStrategyPersistent(config)
    
    # Create test data
    dates = pd.date_range('2024-01-01', periods=60, freq='D')
    np.random.seed(42)
    
    price_data = []
    base_price = 50000
    
    for i in range(60):
        if i < 20:
            daily_return = np.random.normal(0, 0.01)  # Sideways
        elif i < 40:
            daily_return = np.random.normal(0.002, 0.015)  # Accumulation
        else:
            daily_return = np.random.normal(0, 0.03)  # Volatile
            
        if i == 0:
            price_data.append(base_price)
        else:
            price_data.append(price_data[-1] * (1 + daily_return))
    
    test_data = pd.DataFrame({
        'open': np.array(price_data) * 0.999,
        'high': np.array(price_data) * 1.005,
        'low': np.array(price_data) * 0.995,
        'close': price_data,
        'volume': np.random.lognormal(15, 0.5, 60)
    }, index=dates)
    
    test_symbol = 'BTCUSDT'
    
    print(f"Running strategy for {test_symbol}")
    print(f"Data shape: {test_data.shape}")
    
    # Test multiple runs
    for run in range(3):
        print(f"\nRun {run + 1}/3")
        
        # Calculate indicators
        indicators = strategy.calculate_indicators(test_data)
        print(f"Calculated {len(indicators)} indicators")
        
        # Generate signals
        signals = await strategy.generate_signals(test_symbol, test_data, indicators)
        print(f"Generated {len(signals)} signal rows")
        print(f"Final signal: {signals['signal'].iloc[-1]}")
        print(f"Regime: {signals['regime'].iloc[-1]}")
        
        # Get stats
        stats = strategy.get_persistence_stats()
        print(f"Persistence: {stats['signals_generated']} signals generated")
        print(f"Transition matrix: {stats['transition_matrix_non_zero']} non-zero entries")
    
    # Force save and show final stats
    strategy.force_save_all_data(test_symbol)
    
    final_stats = strategy.get_persistence_stats()
    print(f"\nFinal Statistics:")
    print(f"Database size: {final_stats['database_size']} bytes")
    print(f"Unique symbols: {final_stats['unique_symbols']}")
    print(f"History entries: {final_stats['regime_history_count']}")
    
    print(f"\nStrategy info:")
    info = strategy.get_strategy_info()
    print(f"Strategy: {info['strategy_name']}")
    print(f"States: {info['markov_states']}")
    print(f"ETF enabled: {info['etf_enabled']}")
    
    if info['current_regime']:
        print(f"Current regime: {info['current_regime']['regime']}")
        print(f"Duration: {info['current_regime']['duration']}/{info['current_regime']['expected_duration']:.1f}")
    
    print(f"\nTest completed successfully!")

if __name__ == "__main__":
    asyncio.run(test_merged_strategy())