# src/risk/stop_loss_manager.py
"""
Dynamic Stop Loss Management
Uses NumPy for calculations and standard libraries for state management
"""

import numpy as np
from typing import Dict, Tuple
import logging

logger = logging.getLogger(__name__)

class StopLossManager:
    """Dynamic stop-loss and take-profit management"""
    
    def __init__(self, config: dict):
        self.config = config
        self.initial_stop_pct = config.get('initial_stop_pct', 0.05)
        self.trailing_stop_pct = config.get('trailing_stop_pct', 0.03)
        self.take_profit_ratio = config.get('take_profit_ratio', 2.0)  # 2:1 profit/loss ratio
        self.breakeven_threshold = config.get('breakeven_threshold', 0.02)
        
    def calculate_stop_levels(self, symbol: str, entry_price: float,
                            position_side: str, volatility: float) -> Dict[str, float]:
        """Calculate stop-loss and take-profit levels using NumPy"""
        
        # Volatility-adjusted stops using NumPy functions
        vol_adjustment = np.clip(volatility / 0.02, 0.5, 2.0)  # Scale by 2% base volatility
        adjusted_stop_pct = self.initial_stop_pct * vol_adjustment
        
        if position_side.upper() == 'LONG':
            stop_loss = entry_price * (1 - adjusted_stop_pct)
            take_profit = entry_price * (1 + adjusted_stop_pct * self.take_profit_ratio)
        else:  # SHORT
            stop_loss = entry_price * (1 + adjusted_stop_pct)
            take_profit = entry_price * (1 - adjusted_stop_pct * self.take_profit_ratio)
            
        return {
            'stop_loss': stop_loss,
            'take_profit': take_profit,
            'trailing_stop': None  # Will be set after initial move
        }
    
    def update_trailing_stop(self, symbol: str, current_price: float,
                           entry_price: float, position_side: str,
                           current_stop: float) -> float:
        """Update trailing stop based on price movement"""
        
        if position_side.upper() == 'LONG':
            # Check if price moved favorably enough to activate trailing
            if current_price >= entry_price * (1 + self.breakeven_threshold):
                # Calculate trailing stop using NumPy
                potential_stop = current_price * (1 - self.trailing_stop_pct)
                return max(current_stop, potential_stop)
        else:  # SHORT
            if current_price <= entry_price * (1 - self.breakeven_threshold):
                potential_stop = current_price * (1 + self.trailing_stop_pct)
                return min(current_stop, potential_stop)
                
        return current_stop
    
    def should_exit_position(self, symbol: str, current_price: float,
                           stop_levels: Dict[str, float],
                           position_side: str) -> Tuple[bool, str]:
        """Check if position should be exited"""
        
        if position_side.upper() == 'LONG':
            if current_price <= stop_levels['stop_loss']:
                return True, "Stop Loss Hit"
            elif current_price >= stop_levels['take_profit']:
                return True, "Take Profit Hit"
        else:  # SHORT
            if current_price >= stop_levels['stop_loss']:
                return True, "Stop Loss Hit"
            elif current_price <= stop_levels['take_profit']:
                return True, "Take Profit Hit"
                
        return False, ""
    
    def calculate_risk_reward_ratio(self, entry_price: float, 
                                  stop_loss: float, 
                                  take_profit: float,
                                  position_side: str) -> float:
        """Calculate risk/reward ratio using NumPy"""
        
        if position_side.upper() == 'LONG':
            risk = abs(entry_price - stop_loss)
            reward = abs(take_profit - entry_price)
        else:  # SHORT
            risk = abs(stop_loss - entry_price)
            reward = abs(entry_price - take_profit)
            
        if risk == 0:
            return 0
            
        return reward / risk
    
    def adjust_for_volatility(self, base_stop_pct: float, 
                            current_volatility: float,
                            lookback_volatility: float = 0.02) -> float:
        """Adjust stop percentage based on volatility using NumPy"""
        
        # Use NumPy for volatility scaling
        vol_ratio = np.clip(current_volatility / lookback_volatility, 0.5, 3.0)
        adjusted_stop = base_stop_pct * vol_ratio
        
        return adjusted_stop
    
    def calculate_position_heat(self, entry_price: float, 
                              current_price: float,
                              stop_loss: float,
                              position_side: str) -> float:
        """Calculate position heat (percentage of risk used)"""
        
        if position_side.upper() == 'LONG':
            total_risk = abs(entry_price - stop_loss)
            current_risk = abs(entry_price - current_price)
        else:  # SHORT
            total_risk = abs(stop_loss - entry_price)
            current_risk = abs(current_price - entry_price)
            
        if total_risk == 0:
            return 0
            
        heat = current_risk / total_risk
        return np.clip(heat, 0, 1)  # Cap at 100%