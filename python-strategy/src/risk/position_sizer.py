# src/risk/position_sizer.py
"""
Position Sizing Algorithms
Uses NumPy for statistical calculations and SciPy for advanced math
"""

import numpy as np
from typing import Dict, Optional
import logging

logger = logging.getLogger(__name__)

class PositionSizer:
    """Advanced position sizing using established statistical libraries"""
    
    def __init__(self, config: dict):
        self.config = config
        self.method = config.get('method', 'fixed_percent')
        self.base_risk_per_trade = config.get('base_risk_per_trade', 0.05)
        self.max_position_size = config.get('max_position_size', 0.1)
        self.volatility_lookback = config.get('volatility_lookback', 20)
        
    def calculate_position_size(self, symbol: str, signal: dict, 
                              portfolio: dict) -> float:
        """Calculate optimal position size using various algorithms"""
        
        if self.method == 'kelly_criterion':
            return self._kelly_criterion_sizing(symbol, signal, portfolio)
        elif self.method == 'volatility_adjusted':
            return self._volatility_adjusted_sizing(symbol, signal, portfolio)
        elif self.method == 'fixed_percent':
            return self._fixed_percent_sizing(signal)
        else:
            return self._risk_parity_sizing(symbol, signal, portfolio)
    
    def _kelly_criterion_sizing(self, symbol: str, signal: dict, 
                               portfolio: dict) -> float:
        """Kelly Criterion position sizing using NumPy"""
        
        # Get historical performance data
        win_rate = signal.get('win_rate', 0.5)
        avg_win = signal.get('avg_win', 0.02)
        avg_loss = signal.get('avg_loss', -0.01)
        
        if avg_loss == 0:
            return 0
            
        # Kelly formula: f = (bp - q) / b
        # where b = odds received (avg_win/abs(avg_loss))
        # p = probability of win, q = probability of loss
        b = avg_win / abs(avg_loss)
        p = win_rate
        q = 1 - win_rate
        
        kelly_fraction = (b * p - q) / b
        
        # Apply Kelly fraction with safety margin (use 25% of Kelly)
        safe_kelly = max(0, kelly_fraction * 0.25)
        
        return min(safe_kelly, self.max_position_size)
    
    def _volatility_adjusted_sizing(self, symbol: str, signal: dict, 
                                   portfolio: dict) -> float:
        """Volatility-adjusted position sizing using NumPy statistical functions"""
        
        # Get recent volatility from signal
        volatility = signal.get('volatility', 0.02)
        
        # Base position size adjusted by volatility
        target_volatility = 0.05  # 2% target volatility
        vol_adjustment = target_volatility / max(volatility, 0.005)
        
        adjusted_size = self.base_risk_per_trade * vol_adjustment
        
        return min(adjusted_size, self.max_position_size)
    
    def _fixed_percent_sizing(self, signal: dict) -> float:
        """Fixed percentage position sizing"""
        
        signal_strength = signal.get('signal_strength', 1.0)
        base_size = self.base_risk_per_trade * signal_strength
        
        return min(base_size, self.max_position_size)
    
    def _risk_parity_sizing(self, symbol: str, signal: dict, 
                           portfolio: dict) -> float:
        """Risk parity based position sizing using NumPy"""
        
        # Calculate individual asset risk contribution
        asset_volatility = signal.get('volatility', 0.02)
        portfolio_volatility = self._calculate_portfolio_volatility(portfolio)
        
        # Inverse volatility weighting using NumPy
        if portfolio_volatility > 0:
            risk_weight = (1 / asset_volatility) / portfolio_volatility
        else:
            risk_weight = self.base_risk_per_trade
            
        return min(risk_weight, self.max_position_size)
    
    def _calculate_portfolio_volatility_old(self, portfolio: dict) -> float:
        """Calculate portfolio volatility using NumPy"""
        
        # Simple implementation - in production use proper portfolio variance calculation
        volatilities = []
        weights = []
        
        total_value = sum(portfolio.values()) if portfolio else 1
        
        for symbol, position in portfolio.items():
            # Estimate volatility (in production, get from market data)
            estimated_vol = 0.02  # 2% default
            volatilities.append(estimated_vol)
            weights.append(position / total_value)
        
        if not volatilities:
            return 0.02
            
        # Calculate weighted portfolio volatility
        volatilities = np.array(volatilities)
        weights = np.array(weights)
        
        # Simplified calculation (in production use correlation matrix)
        portfolio_vol = np.sqrt(np.sum((weights * volatilities) ** 2))
        
        return portfolio_vol
    def _calculate_portfolio_volatility(self, portfolio: dict) -> float:
        """Calculate portfolio volatility - FIXED for enhanced portfolio"""
        try:
            # 🔧 FIX: Handle enhanced portfolio structure
            if 'total_value_usdt' in portfolio:
                # Enhanced portfolio format from main.py
                total_value = portfolio.get('total_value_usdt', 1.0)
                positions = portfolio.get('positions', {})
            else:
                # Legacy portfolio format
                total_value = sum(v for v in portfolio.values() if isinstance(v, (int, float)))
                positions = {k: v for k, v in portfolio.items() if isinstance(v, dict)}
            
            if total_value <= 0 or not positions:
                return 0.02  # Default 2% volatility
            
            # Calculate weighted volatility
            weighted_vol = 0
            for asset, position_data in positions.items():
                if isinstance(position_data, dict):
                    value = position_data.get('value_usdt', 0)
                    weight = value / total_value if total_value > 0 else 0
                    
                    # Estimate asset volatility (you could enhance this with real data)
                    asset_vol = self._estimate_asset_volatility(asset)
                    weighted_vol += weight * asset_vol
            
            return max(weighted_vol, 0.01)  # Minimum 1% volatility
            
        except Exception as e:
            logger.warning(f"Portfolio volatility calculation failed: {e}")
            return 0.02  # Default volatility
    
    def _estimate_asset_volatility(self, asset: str) -> float:
        """Estimate asset volatility (placeholder - enhance with real data)"""
        # Basic volatility estimates by asset type
        volatility_map = {
            'BTC': 0.04,    # 4%
            'ETH': 0.05,    # 5%
            'SOL': 0.08,    # 8%
            'ARB': 0.10,    # 10%
            'USDT': 0.001,  # 0.1%
        }
        
        return volatility_map.get(asset, 0.06)  # Default 6% for unknown assets
    
    def _risk_parity_sizing(self, symbol: str, signal: dict, portfolio: dict) -> float:
        """Risk parity position sizing - FIXED for enhanced portfolio"""
        try:
            # 🔧 FIX: Handle enhanced portfolio structure  
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
            
            # Target risk per position (1-2% of portfolio)
            target_risk = self.max_position_size * 0.5  # Conservative approach
            
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
            return 0.0