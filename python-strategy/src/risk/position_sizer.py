# src/risk/position_sizer.py
"""
Position Sizing Algorithms - Enhanced with Regime Awareness
Uses NumPy for statistical calculations and SciPy for advanced math
"""

import numpy as np
from typing import Dict, Optional
import logging

logger = logging.getLogger(__name__)

class PositionSizer:
    """Advanced position sizing with regime awareness using established statistical libraries"""
    
    def __init__(self, config: dict):
        self.config = config
        self.method = config.get('method', 'kelly_criterion')
        
        # Base parameters (these remain unchanged)
        self.base_risk_per_trade = config.get('base_risk_per_trade', 0.02)
        self.base_max_position_size = config.get('max_position_size', 0.15)
        self.volatility_lookback = config.get('volatility_lookback', 14)
        
        # Current active parameters (adjusted by regime)
        self.current_risk_per_trade = self.base_risk_per_trade
        self.max_position_size = self.base_max_position_size
        
        # Regime-specific adjustments
        self.current_regime = None
        self.regime_multiplier = 1.0
        
        # Regime-specific sizing strategies
        self.regime_sizing_strategies = {
            'accumulation': 'volatility_adjusted',      # Patient, scale in
            'institutional_fomo': 'kelly_criterion',    # Aggressive, momentum
            'euphoria': 'risk_parity',                  # Moderate, diversified
            'panic': 'contrarian_sizing',               # Small, wide stops
            'distribution': 'defensive_sizing',         # Small, quick exits
            'sideways': 'range_bound_sizing',           # Normal, range trading
            'balanced': 'kelly_criterion',              # Standard approach
            'uncertain': 'defensive_sizing',            # Conservative
            'profit_taking': 'profit_protection_sizing' # Lock in gains
        }
        
    def update_regime_parameters(self, regime: str, regime_multiplier: float):
        """Update position sizing parameters based on market regime"""
        
        self.current_regime = regime.lower()
        self.regime_multiplier = regime_multiplier
        
        # Update current parameters
        self.current_risk_per_trade = np.clip(
            self.base_risk_per_trade * regime_multiplier, 0.005, 0.05
        )
        self.max_position_size = np.clip(
            self.base_max_position_size * regime_multiplier, 0.05, 0.30
        )
        
        # Log the updates
        logger.info(f"Position sizer updated for regime: {regime}")
        logger.info(f"Risk per trade: {self.base_risk_per_trade:.3f} -> {self.current_risk_per_trade:.3f}")
        logger.info(f"Max position size: {self.base_max_position_size:.3f} -> {self.max_position_size:.3f}")
        
    def calculate_position_size(self, symbol: str, signal: dict, portfolio: dict) -> float:
        """Calculate optimal position size using regime-aware algorithms"""
        
        # Get regime-specific method if available
        if self.current_regime:
            method = self.regime_sizing_strategies.get(self.current_regime, self.method)
        else:
            method = self.method
        
        # Calculate base position size
        if method == 'kelly_criterion':
            base_size = self._kelly_criterion_sizing(symbol, signal, portfolio)
        elif method == 'volatility_adjusted':
            base_size = self._volatility_adjusted_sizing(symbol, signal, portfolio)
        elif method == 'risk_parity':
            base_size = self._risk_parity_sizing(symbol, signal, portfolio)
        elif method == 'contrarian_sizing':
            base_size = self._contrarian_sizing(symbol, signal, portfolio)
        elif method == 'defensive_sizing':
            base_size = self._defensive_sizing(symbol, signal, portfolio)
        elif method == 'range_bound_sizing':
            base_size = self._range_bound_sizing(symbol, signal, portfolio)
        elif method == 'profit_protection_sizing':
            base_size = self._profit_protection_sizing(symbol, signal, portfolio)
        else:
            base_size = self._fixed_percent_sizing(signal)
        
        # Apply regime-specific adjustments
        adjusted_size = self._apply_regime_adjustments(base_size, signal)
        
        return min(adjusted_size, self.max_position_size)
    
    def _apply_regime_adjustments(self, base_size: float, signal: dict) -> float:
        """Apply regime-specific position size adjustments"""
        
        if not self.current_regime:
            return base_size
            
        signal_strength = abs(signal.get('signal_strength', 0.5))
        confidence = signal.get('confidence', 0.5)
        
        # Regime-specific adjustments
        if self.current_regime == 'panic':
            # In panic: Small sizes but allow for big rewards
            # Scale down base size but increase with signal strength
            adjusted_size = base_size * 0.5 * (1 + signal_strength)
            
        elif self.current_regime == 'institutional_fomo':
            # In FOMO: Aggressive sizing for strong signals
            confidence_boost = 1 + (confidence - 0.5) * 0.8
            adjusted_size = base_size * confidence_boost
            
        elif self.current_regime == 'euphoria':
            # In euphoria: Moderate sizing, prepare for reversal
            # Reduce size as signal gets stronger (contrarian)
            euphoria_discount = 1 - (signal_strength - 0.5) * 0.3
            adjusted_size = base_size * max(0.5, euphoria_discount)
            
        elif self.current_regime == 'accumulation':
            # In accumulation: Scale into positions
            # Larger sizes for strong signals in accumulation
            accumulation_boost = 1 + signal_strength * 0.5
            adjusted_size = base_size * accumulation_boost
            
        elif self.current_regime in ['distribution', 'profit_taking']:
            # In distribution/profit taking: Defensive sizing
            defensive_factor = 0.7 + confidence * 0.3
            adjusted_size = base_size * defensive_factor
            
        else:
            # Default: apply regime multiplier with signal strength
            strength_factor = 0.8 + signal_strength * 0.4
            adjusted_size = base_size * strength_factor
        
        return max(adjusted_size, 0.0)
    
    def _kelly_criterion_sizing(self, symbol: str, signal: dict, portfolio: dict) -> float:
        """Kelly Criterion position sizing using NumPy"""
        
        # Get historical performance data
        win_rate = signal.get('win_rate', 0.55)  # Slightly optimistic for crypto
        avg_win = signal.get('avg_win', 0.03)    # 3% average win
        avg_loss = signal.get('avg_loss', -0.015) # 1.5% average loss
        
        if avg_loss >= 0:  # Protect against invalid data
            return self.current_risk_per_trade
            
        # Kelly formula: f = (bp - q) / b
        # where b = odds received (avg_win/abs(avg_loss))
        # p = probability of win, q = probability of loss
        b = avg_win / abs(avg_loss)
        p = win_rate
        q = 1 - win_rate
        
        kelly_fraction = (b * p - q) / b
        
        # Apply Kelly fraction with safety margin (use 25% of Kelly for crypto)
        safe_kelly = max(0, kelly_fraction * 0.25)
        
        return min(safe_kelly, self.current_risk_per_trade * 2)  # Cap at 2x base risk
    
    def _volatility_adjusted_sizing(self, symbol: str, signal: dict, portfolio: dict) -> float:
        """Volatility-adjusted position sizing - good for accumulation phase"""
        
        # Get recent volatility from signal
        volatility = signal.get('volatility', 0.05)  # 5% default for crypto
        
        # Base position size adjusted by volatility
        target_volatility = 0.03  # 3% target volatility for crypto
        vol_adjustment = target_volatility / max(volatility, 0.01)
        
        adjusted_size = self.current_risk_per_trade * vol_adjustment
        
        return min(adjusted_size, self.current_risk_per_trade * 1.5)
    
    def _contrarian_sizing(self, symbol: str, signal: dict, portfolio: dict) -> float:
        """Contrarian position sizing for panic conditions"""
        
        # In panic, use smaller base sizes but allow for asymmetric upside
        panic_base = self.current_risk_per_trade * 0.5
        
        # Scale with signal strength - stronger panic signals get slightly larger size
        signal_strength = abs(signal.get('signal_strength', 0.5))
        strength_multiplier = 0.8 + signal_strength * 0.6
        
        # Volatility adjustment - wider stops need smaller positions
        volatility = signal.get('volatility', 0.08)  # Higher default in panic
        vol_factor = min(1.5, 0.05 / max(volatility, 0.02))
        
        final_size = panic_base * strength_multiplier * vol_factor
        
        return max(final_size, self.current_risk_per_trade * 0.3)  # Minimum size
    
    def _defensive_sizing(self, symbol: str, signal: dict, portfolio: dict) -> float:
        """Defensive sizing for uncertain/distribution regimes"""
        
        # Conservative base
        defensive_base = self.current_risk_per_trade * 0.7
        
        # Only increase size for very high confidence signals
        confidence = signal.get('confidence', 0.5)
        if confidence > 0.8:
            confidence_boost = 1 + (confidence - 0.8) * 0.5
            defensive_base *= confidence_boost
        
        return defensive_base
    
    def _range_bound_sizing(self, symbol: str, signal: dict, portfolio: dict) -> float:
        """Range-bound sizing for sideways markets"""
        
        # Standard sizing but with tight risk management
        base_size = self.current_risk_per_trade
        
        # Adjust for expected range (tighter ranges = smaller positions)
        expected_move = signal.get('expected_move', 0.02)  # 2% default
        range_factor = min(1.2, expected_move / 0.015)  # Scale around 1.5% moves
        
        return base_size * range_factor
    
    def _profit_protection_sizing(self, symbol: str, signal: dict, portfolio: dict) -> float:
        """Profit protection sizing when taking profits"""
        
        # Very conservative - focus on protecting existing gains
        protection_base = self.current_risk_per_trade * 0.6
        
        # Only trade high-confidence signals
        confidence = signal.get('confidence', 0.5)
        if confidence < 0.7:
            return protection_base * 0.5  # Very small size for low confidence
        
        return protection_base
    
    def _fixed_percent_sizing(self, signal: dict) -> float:
        """Fixed percentage position sizing"""
        
        signal_strength = abs(signal.get('signal_strength', 1.0))
        base_size = self.current_risk_per_trade * signal_strength
        
        return base_size
    
    def _risk_parity_sizing(self, symbol: str, signal: dict, portfolio: dict) -> float:
        """Risk parity position sizing - FIXED for enhanced portfolio"""
        try:
            # Handle enhanced portfolio structure  
            if 'total_value_usdt' in portfolio:
                total_value = portfolio.get('total_value_usdt', 1.0)
                cash_usdt = portfolio.get('cash_usdt', 0.0)
                positions = portfolio.get('positions', {})
            else:
                # Legacy format
                total_value = sum(v for v in portfolio.values() if isinstance(v, (int, float)))
                cash_usdt = portfolio.get('USDT', 0)
                positions = {k: v for k, v in portfolio.items() if isinstance(v, dict)}
            
            if total_value <= 0:
                logger.warning("Total portfolio value is zero or negative")
                return 0.0
            
            # Target risk per position adjusted by regime
            target_risk = self.current_risk_per_trade
            
            # Get signal strength and volatility
            signal_strength = abs(signal.get('signal_strength', 0.5))
            signal_volatility = signal.get('volatility', 0.05)
            
            # Calculate portfolio volatility
            portfolio_volatility = self._calculate_portfolio_volatility(portfolio)
            
            # Risk-adjusted position size
            base_size = target_risk / max(signal_volatility, 0.01)
            
            # Adjust for signal strength
            adjusted_size = base_size * signal_strength
            
            # Ensure we don't exceed maximum position size
            final_size = min(adjusted_size, self.max_position_size)
            
            # Ensure we have enough cash (use cash_usdt from enhanced portfolio)
            available_ratio = cash_usdt / total_value if total_value > 0 else 0
            final_size = min(final_size, available_ratio * 0.9)  # 90% of available cash
            
            return max(final_size, 0.0)
            
        except Exception as e:
            logger.error(f"Risk parity sizing failed: {e}")
            return self.current_risk_per_trade * 0.5  # Fallback to conservative size
    
    def _calculate_portfolio_volatility(self, portfolio: dict) -> float:
        """Calculate portfolio volatility - FIXED for enhanced portfolio"""
        try:
            # Handle enhanced portfolio structure
            if 'total_value_usdt' in portfolio:
                # Enhanced portfolio format from main.py
                total_value = portfolio.get('total_value_usdt', 1.0)
                positions = portfolio.get('positions', {})
            else:
                # Legacy portfolio format
                total_value = sum(v for v in portfolio.values() if isinstance(v, (int, float)))
                positions = {k: v for k, v in portfolio.items() if isinstance(v, dict)}
            
            if total_value <= 0 or not positions:
                return 0.05  # Default 5% volatility for crypto
            
            # Calculate weighted volatility
            weighted_vol = 0
            for asset, position_data in positions.items():
                if isinstance(position_data, dict):
                    value = position_data.get('value_usdt', 0)
                    weight = value / total_value if total_value > 0 else 0
                    
                    # Estimate asset volatility
                    asset_vol = self._estimate_asset_volatility(asset)
                    weighted_vol += weight * asset_vol
            
            return max(weighted_vol, 0.02)  # Minimum 2% volatility
            
        except Exception as e:
            logger.warning(f"Portfolio volatility calculation failed: {e}")
            return 0.05  # Default volatility
    
    def _estimate_asset_volatility(self, asset: str) -> float:
        """Estimate asset volatility with regime adjustment"""
        # Base volatility estimates by asset type
        base_volatility_map = {
            'BTC': 0.04,    # 4%
            'ETH': 0.05,    # 5%
            'SOL': 0.08,    # 8%
            'ARB': 0.10,    # 10%
            'SUI': 0.12,    # 12%
            'USDT': 0.001,  # 0.1%
        }
        
        # Extract base symbol from trading pair
        base_symbol = asset.replace('USDT', '').replace('USD', '')
        base_vol = base_volatility_map.get(base_symbol, 0.06)  # Default 6%
        
        # Adjust for current regime
        if self.current_regime:
            regime_vol_multipliers = {
                'panic': 2.0,
                'euphoria': 1.5,
                'institutional_fomo': 1.3,
                'distribution': 1.2,
                'accumulation': 0.8,
                'sideways': 0.9,
                'balanced': 1.0
            }
            vol_multiplier = regime_vol_multipliers.get(self.current_regime, 1.0)
            base_vol *= vol_multiplier
        
        return base_vol
    
    def get_regime_sizing_info(self) -> Dict:
        """Get current regime sizing information"""
        return {
            'current_regime': self.current_regime,
            'regime_multiplier': self.regime_multiplier,
            'current_risk_per_trade': self.current_risk_per_trade,
            'max_position_size': self.max_position_size,
            'base_risk_per_trade': self.base_risk_per_trade,
            'base_max_position_size': self.base_max_position_size,
            'sizing_method': self.regime_sizing_strategies.get(self.current_regime, self.method) if self.current_regime else self.method
        }
    
    def reset_to_base_parameters(self):
        """Reset to base parameters (for testing)"""
        self.current_risk_per_trade = self.base_risk_per_trade
        self.max_position_size = self.base_max_position_size
        self.current_regime = None
        self.regime_multiplier = 1.0
        
        logger.info("Position sizer parameters reset to base values")