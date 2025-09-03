# src/risk/manager.py
"""
Risk Management Module - Central Risk Controller
Enhanced with regime-aware risk parameter adjustment
"""

from typing import Dict, List, Optional, Tuple
import numpy as np
from dataclasses import dataclass
from enum import Enum
import logging

logger = logging.getLogger(__name__)

class RiskLevel(Enum):
    LOW = "low"
    MEDIUM = "medium"
    HIGH = "high"
    CRITICAL = "critical"

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
class RiskMetrics:
    portfolio_var: float
    max_position_size: float
    current_exposure: float
    drawdown_level: float
    risk_level: RiskLevel
    current_regime: Optional[str] = None
    regime_multiplier: Optional[float] = None

@dataclass
class RegimeAdjustedLimits:
    """Current regime-adjusted risk limits"""
    max_portfolio_risk: float
    max_position_risk: float
    max_drawdown_limit: float
    regime: str
    confidence: float
    multiplier: float

class RiskManager:
    """Central risk management controller with regime awareness"""
    
    def __init__(self, config: dict):
        self.config = config
        
        from .position_sizer import PositionSizer
        from .portfolio_monitor import PortfolioRiskMonitor
        from .stop_loss_manager import StopLossManager
        # Initialize components       
        
        self.position_sizer = PositionSizer(config.get('position_sizing', {}))
        self.portfolio_monitor = PortfolioRiskMonitor(config.get('portfolio_risk', {}))
        self.stop_loss_manager = StopLossManager(config.get('stop_loss', {}))
        
        # Base risk limits from config (these remain unchanged)
        self.base_max_portfolio_risk = config.get('max_portfolio_risk', 0.05)
        self.base_max_position_risk = config.get('max_position_risk', 0.02)
        self.base_max_correlation_exposure = config.get('max_correlation_exposure', 0.5)
        self.base_max_drawdown_limit = config.get('max_drawdown_limit', 0.25)
        
        # Current active limits (will be adjusted by regime)
        self.max_portfolio_risk = self.base_max_portfolio_risk
        self.max_position_risk = self.base_max_position_risk
        self.max_correlation_exposure = self.base_max_correlation_exposure
        self.max_drawdown_limit = self.base_max_drawdown_limit
        
        # Regime adjustment setup
        self.regime_multipliers = config.get('regime_risk_multipliers', {
            'accumulation': 1.3,
            'institutional_fomo': 1.4,
            'euphoria': 0.8,
            'sideways': 1.0,
            'balanced': 1.0,
            'uncertain': 0.7,
            'distribution': 0.6,
            'profit_taking': 0.7,
            'panic': 0.4
        })
        
        # Current regime state
        self.current_regime_limits: Optional[RegimeAdjustedLimits] = None
        
    def update_regime_risk_parameters(self, 
                                    regime: str,
                                    confidence: float,
                                    regime_strength: float,
                                    current_volatility: Optional[float] = None):
        """Update risk parameters based on current market regime"""
        
        try:
            regime_enum = MarketRegime(regime.lower())
        except ValueError:
            logger.warning(f"Unknown regime: {regime}, using BALANCED")
            regime = 'balanced'
        
        # Get base multiplier
        base_multiplier = self.regime_multipliers.get(regime.lower(), 1.0)
        
        # Adjust multiplier based on confidence and strength
        confidence_factor = self._calculate_confidence_factor(confidence, regime_strength)
        adjusted_multiplier = self._adjust_multiplier_by_confidence(base_multiplier, confidence_factor)
        
        # Apply volatility adjustment if provided
        if current_volatility is not None:
            vol_adjustment = self._get_volatility_adjustment(current_volatility)
            adjusted_multiplier *= vol_adjustment
        
        # Calculate new limits
        new_limits = RegimeAdjustedLimits(
            max_portfolio_risk=np.clip(
                self.base_max_portfolio_risk * adjusted_multiplier, 0.01, 0.15
            ),
            max_position_risk=np.clip(
                self.base_max_position_risk * adjusted_multiplier, 0.005, 0.08
            ),
            max_drawdown_limit=np.clip(
                self.base_max_drawdown_limit * self._get_drawdown_multiplier(regime, adjusted_multiplier), 
                0.10, 0.40
            ),
            regime=regime,
            confidence=confidence,
            multiplier=adjusted_multiplier
        )
        
        # Update current active limits
        self.max_portfolio_risk = new_limits.max_portfolio_risk
        self.max_position_risk = new_limits.max_position_risk
        self.max_drawdown_limit = new_limits.max_drawdown_limit
        
        self.current_regime_limits = new_limits
        
        # Log the changes
        logger.info(f"Risk parameters updated for regime: {regime}")
        logger.info(f"Confidence: {confidence:.2f}, Multiplier: {adjusted_multiplier:.2f}")
        logger.info(f"Max portfolio risk: {self.base_max_portfolio_risk:.3f} -> {self.max_portfolio_risk:.3f}")
        logger.info(f"Max position risk: {self.base_max_position_risk:.3f} -> {self.max_position_risk:.3f}")
        
        # Update position sizer with new parameters
        self.position_sizer.update_regime_parameters(regime, adjusted_multiplier)
        
        # Update stop loss manager with regime-adjusted parameters  
        self.stop_loss_manager.update_regime_parameters(regime, adjusted_multiplier, current_volatility)

    def evaluate_trade_risk(self, symbol: str, signal: dict, 
                          current_portfolio: dict) -> Tuple[bool, float, str]:
        """Evaluate if trade meets risk criteria (now regime-aware)"""
        
        # Calculate position size (now uses regime-adjusted parameters)
        position_size = self.position_sizer.calculate_position_size(
            symbol, signal, current_portfolio
        )
        
        # Check portfolio risk using current (possibly adjusted) limits
        portfolio_risk = self.portfolio_monitor.calculate_portfolio_risk(
            current_portfolio, symbol, position_size
        )
        
        # Risk checks with current limits
        risk_checks = {
            'position_size': position_size <= self.max_position_risk,
            'portfolio_risk': portfolio_risk <= self.max_portfolio_risk,
            'correlation': self._check_correlation_risk(symbol, current_portfolio),
            'drawdown': self._check_drawdown_risk(current_portfolio)
        }
        
        # Decision logic
        approved = all(risk_checks.values())
        final_size = position_size if approved else 0
        
        # Enhanced reason with regime info
        if approved:
            reason = f"Approved (regime: {self.current_regime_limits.regime if self.current_regime_limits else 'default'})"
        else:
            failed_checks = [k for k, v in risk_checks.items() if not v]
            reason = f"Rejected: {failed_checks}"
            if self.current_regime_limits:
                reason += f" (regime: {self.current_regime_limits.regime})"
        
        return approved, final_size, reason
    
    def get_risk_metrics(self, portfolio: dict) -> RiskMetrics:
        """Get current portfolio risk metrics (enhanced with regime info)"""
        base_metrics = self.portfolio_monitor.get_risk_metrics(portfolio)
        
        # Add regime information
        # base_metrics.current_regime = self.current_regime_limits.regime if self.current_regime_limits else None
        # base_metrics.regime_multiplier = self.current_regime_limits.multiplier if self.current_regime_limits else None
        
        return base_metrics
    
    def get_regime_risk_guidance(self) -> Optional[Dict]:
        """Get risk management guidance for current regime"""
        if not self.current_regime_limits:
            return None
            
        guidance = {
            'accumulation': {
                'strategy': 'Increase position sizes, patient entries',
                'focus': 'Scale into positions, wider stops',
                'risk_appetite': 'Higher - market building base'
            },
            'institutional_fomo': {
                'strategy': 'Aggressive positioning, momentum following', 
                'focus': 'Quick entries, tight risk management',
                'risk_appetite': 'Highest - capture institutional flow'
            },
            'euphoria': {
                'strategy': 'Moderate positions, prepare for reversal',
                'focus': 'Tighter stops, quicker profits',
                'risk_appetite': 'Moderate - market overextended'
            },
            'panic': {
                'strategy': 'Contrarian small positions, catch knife carefully',
                'focus': 'Small sizes, wide stops, big rewards',
                'risk_appetite': 'Controlled - high risk/reward'
            },
            'distribution': {
                'strategy': 'Very selective, small positions',
                'focus': 'Short-term trades, quick exits',
                'risk_appetite': 'Low - smart money distributing'
            }
        }
        
        regime = self.current_regime_limits.regime.lower()
        return guidance.get(regime, {
            'strategy': 'Standard risk management',
            'focus': 'Normal position sizing',
            'risk_appetite': 'Baseline - balanced conditions'
        })
    
    def _calculate_confidence_factor(self, confidence: float, strength: float) -> float:
        """Calculate confidence adjustment factor"""
        # Combine confidence and strength
        combined_score = (confidence * 0.7 + strength * 0.3)
        
        # Convert to adjustment factor (0.5 to 1.5 range)
        confidence_factor = 0.5 + combined_score
        
        return np.clip(confidence_factor, 0.3, 1.8)
    
    def _adjust_multiplier_by_confidence(self, base_multiplier: float, confidence_factor: float) -> float:
        """Adjust regime multiplier based on confidence"""
        if base_multiplier > 1.0:  # Aggressive regimes
            adjusted = 1.0 + (base_multiplier - 1.0) * confidence_factor
        else:  # Conservative regimes
            adjusted = base_multiplier + (1.0 - base_multiplier) * (2.0 - confidence_factor)
        
        return np.clip(adjusted, 0.2, 2.0)
    
    def _get_volatility_adjustment(self, current_volatility: float) -> float:
        """Get volatility-based adjustment factor"""
        # Normalize around 2% volatility for crypto
        normal_vol = 0.02
        vol_ratio = current_volatility / normal_vol
        
        # Higher volatility = slightly more conservative
        # But not too conservative (crypto is naturally volatile)
        adjustment = np.clip(1.0 / np.sqrt(vol_ratio), 0.7, 1.3)
        return adjustment
    
    def _get_drawdown_multiplier(self, regime: str, base_multiplier: float) -> float:
        """Get drawdown limit multiplier (inverse for risky regimes)"""
        risky_regimes = ['panic', 'distribution', 'euphoria']
        
        if regime.lower() in risky_regimes:
            # In risky regimes, keep drawdown limits tighter despite higher position sizes
            return min(base_multiplier, 1.0)
        else:
            return base_multiplier
    
    def _check_correlation_risk(self, symbol: str, portfolio: dict) -> bool:
        """Check correlation exposure limits"""
        return self.portfolio_monitor.check_correlation_exposure(symbol, portfolio)
    
    def _check_drawdown_risk(self, portfolio: dict) -> bool:
        """Check drawdown limits (now uses regime-adjusted limit)"""
        current_drawdown = self.portfolio_monitor.calculate_current_drawdown(portfolio)
        return current_drawdown < self.max_drawdown_limit
    
    def reset_to_base_parameters(self):
        """Reset to base risk parameters (useful for testing)"""
        self.max_portfolio_risk = self.base_max_portfolio_risk
        self.max_position_risk = self.base_max_position_risk
        self.max_drawdown_limit = self.base_max_drawdown_limit
        self.current_regime_limits = None
        
        logger.info("Risk parameters reset to base values")
    
    def get_current_limits(self) -> Dict:
        """Get current active risk limits"""
        return {
            'max_portfolio_risk': self.max_portfolio_risk,
            'max_position_risk': self.max_position_risk,
            'max_drawdown_limit': self.max_drawdown_limit,
            'regime_info': {
                'regime': self.current_regime_limits.regime if self.current_regime_limits else None,
                'multiplier': self.current_regime_limits.multiplier if self.current_regime_limits else None,
                'confidence': self.current_regime_limits.confidence if self.current_regime_limits else None
            }
        }