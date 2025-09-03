# src/risk/stop_loss_manager.py
"""
Dynamic Stop Loss Management - Enhanced with Regime Awareness
Uses NumPy for calculations and standard libraries for state management
"""

import numpy as np
from typing import Dict, Tuple, Optional
import logging

logger = logging.getLogger(__name__)

class StopLossManager:
    """Dynamic stop-loss and take-profit management with regime awareness"""
    
    def __init__(self, config: dict):
        self.config = config
        
        # Base parameters (these remain unchanged)
        self.base_initial_stop_pct = config.get('initial_stop_pct', 0.08)
        self.base_trailing_stop_pct = config.get('trailing_stop_pct', 0.05)
        self.base_take_profit_ratio = config.get('take_profit_ratio', 3.0)
        self.base_breakeven_threshold = config.get('breakeven_threshold', 0.03)
        
        # Current active parameters (adjusted by regime)
        self.initial_stop_pct = self.base_initial_stop_pct
        self.trailing_stop_pct = self.base_trailing_stop_pct
        self.take_profit_ratio = self.base_take_profit_ratio
        self.breakeven_threshold = self.base_breakeven_threshold
        
        # Regime-specific settings
        self.current_regime = None
        self.regime_multiplier = 1.0
        self.current_volatility = None
        
        # Regime-specific stop management strategies
        self.regime_stop_strategies = {
            'panic': {
                'stop_multiplier': 2.0,      # Wide stops to avoid shakeouts
                'tp_multiplier': 2.5,        # Bigger profit targets
                'trailing_aggressive': False, # Don't trail too tight
                'breakeven_fast': False      # Don't move to breakeven too fast
            },
            'euphoria': {
                'stop_multiplier': 1.2,      # Slightly wider stops
                'tp_multiplier': 0.8,        # Take profits faster
                'trailing_aggressive': True,  # Trail tight to lock profits
                'breakeven_fast': True       # Move to breakeven quickly
            },
            'institutional_fomo': {
                'stop_multiplier': 1.3,      # Allow for volatility
                'tp_multiplier': 1.5,        # Good profit targets
                'trailing_aggressive': False, # Let it run
                'breakeven_fast': False      # Give it room
            },
            'accumulation': {
                'stop_multiplier': 0.8,      # Tighter stops (low vol)
                'tp_multiplier': 2.0,        # Bigger targets
                'trailing_aggressive': False, # Patient approach
                'breakeven_fast': False      # Let accumulation work
            },
            'distribution': {
                'stop_multiplier': 1.5,      # Wider stops (choppy)
                'tp_multiplier': 0.7,        # Quick profits
                'trailing_aggressive': True,  # Lock in gains fast
                'breakeven_fast': True       # Protect quickly
            },
            'sideways': {
                'stop_multiplier': 0.9,      # Normal stops
                'tp_multiplier': 0.8,        # Range-bound profits
                'trailing_aggressive': False, # Standard trailing
                'breakeven_fast': False      # Normal breakeven
            }
        }
        
    def update_regime_parameters(self, regime: str, regime_multiplier: float, 
                               current_volatility: Optional[float] = None):
        """Update stop loss parameters based on market regime and volatility"""
        
        self.current_regime = regime.lower()
        self.regime_multiplier = regime_multiplier
        self.current_volatility = current_volatility
        
        # Get regime-specific strategy
        strategy = self.regime_stop_strategies.get(self.current_regime, {
            'stop_multiplier': 1.0,
            'tp_multiplier': 1.0,
            'trailing_aggressive': False,
            'breakeven_fast': False
        })
        
        # Calculate new parameters
        base_stop_multiplier = strategy['stop_multiplier']
        base_tp_multiplier = strategy['tp_multiplier']
        
        # Apply volatility adjustment if available
        if current_volatility is not None:
            vol_adjustment = self._get_volatility_stop_adjustment(current_volatility)
            base_stop_multiplier *= vol_adjustment
            base_tp_multiplier *= np.sqrt(vol_adjustment)  # Less aggressive scaling for TP
        
        # Update current parameters
        self.initial_stop_pct = np.clip(
            self.base_initial_stop_pct * base_stop_multiplier, 0.02, 0.25
        )
        self.trailing_stop_pct = np.clip(
            self.base_trailing_stop_pct * base_stop_multiplier, 0.01, 0.20
        )
        self.take_profit_ratio = np.clip(
            self.base_take_profit_ratio * base_tp_multiplier, 1.0, 8.0
        )
        self.breakeven_threshold = np.clip(
            self.base_breakeven_threshold * base_stop_multiplier, 0.005, 0.15
        )
        
        # Log the updates
        logger.info(f"Stop loss manager updated for regime: {regime}")
        logger.info(f"Initial stop: {self.base_initial_stop_pct:.3f} -> {self.initial_stop_pct:.3f}")
        logger.info(f"Take profit ratio: {self.base_take_profit_ratio:.1f} -> {self.take_profit_ratio:.1f}")
        logger.info(f"Breakeven threshold: {self.base_breakeven_threshold:.3f} -> {self.breakeven_threshold:.3f}")
        
    def calculate_stop_levels(self, symbol: str, entry_price: float,
                            position_side: str, volatility: float,
                            signal_strength: Optional[float] = None) -> Dict[str, float]:
        """Calculate stop-loss and take-profit levels with regime awareness"""
        
        # Use current volatility or provided volatility
        effective_volatility = self.current_volatility if self.current_volatility else volatility
        
        # Base volatility adjustment
        vol_adjustment = self._get_volatility_stop_adjustment(effective_volatility)
        adjusted_stop_pct = self.initial_stop_pct * vol_adjustment
        
        # Signal strength adjustment
        if signal_strength is not None:
            # Stronger signals can have tighter stops (more confidence)
            signal_adjustment = 1.0 - (abs(signal_strength) - 0.5) * 0.2
            signal_adjustment = np.clip(signal_adjustment, 0.7, 1.3)
            adjusted_stop_pct *= signal_adjustment
        
        # Regime-specific adjustments
        if self.current_regime:
            adjusted_stop_pct = self._apply_regime_stop_adjustments(adjusted_stop_pct, entry_price)
        
        # Calculate levels
        if position_side.upper() == 'LONG':
            stop_loss = entry_price * (1 - adjusted_stop_pct)
            take_profit = entry_price * (1 + adjusted_stop_pct * self.take_profit_ratio)
        else:  # SHORT
            stop_loss = entry_price * (1 + adjusted_stop_pct)
            take_profit = entry_price * (1 - adjusted_stop_pct * self.take_profit_ratio)
            
        return {
            'stop_loss': stop_loss,
            'take_profit': take_profit,
            'trailing_stop': None,  # Will be set after initial move
            'initial_stop_pct': adjusted_stop_pct,
            'regime': self.current_regime,
            'strategy_info': self.regime_stop_strategies.get(self.current_regime, {})
        }
    
    def _apply_regime_stop_adjustments(self, base_stop_pct: float, entry_price: float) -> float:
        """Apply regime-specific stop adjustments"""
        
        strategy = self.regime_stop_strategies.get(self.current_regime, {})
        
        if self.current_regime == 'panic':
            # In panic: wider stops to avoid getting shaken out of good positions
            # But don't go crazy - crypto can still move fast
            panic_adjustment = min(2.0, strategy.get('stop_multiplier', 2.0))
            return base_stop_pct * panic_adjustment
            
        elif self.current_regime == 'euphoria':
            # In euphoria: slightly wider stops but prepare for quick reversal
            euphoria_adjustment = strategy.get('stop_multiplier', 1.2)
            return base_stop_pct * euphoria_adjustment
            
        elif self.current_regime == 'accumulation':
            # In accumulation: can use tighter stops due to lower volatility
            accumulation_adjustment = strategy.get('stop_multiplier', 0.8)
            return base_stop_pct * accumulation_adjustment
            
        elif self.current_regime == 'distribution':
            # In distribution: wider stops due to choppiness, but take profits fast
            distribution_adjustment = strategy.get('stop_multiplier', 1.5)
            return base_stop_pct * distribution_adjustment
            
        else:
            # Default: use strategy multiplier
            default_adjustment = strategy.get('stop_multiplier', 1.0)
            return base_stop_pct * default_adjustment
    
    def update_trailing_stop(self, symbol: str, current_price: float,
                           entry_price: float, position_side: str,
                           current_stop: float, stop_levels: Optional[Dict] = None) -> float:
        """Update trailing stop based on price movement and regime"""
        
        strategy = self.regime_stop_strategies.get(self.current_regime, {})
        is_aggressive_trailing = strategy.get('trailing_aggressive', False)
        is_fast_breakeven = strategy.get('breakeven_fast', False)
        
        # Determine breakeven threshold (regime-aware)
        breakeven_threshold = self.breakeven_threshold
        if is_fast_breakeven:
            breakeven_threshold *= 0.7  # Move to breakeven faster
        
        # Determine trailing percentage (regime-aware)
        trailing_pct = self.trailing_stop_pct
        if is_aggressive_trailing:
            trailing_pct *= 0.8  # Trail tighter to lock profits
        
        if position_side.upper() == 'LONG':
            # Check if price moved favorably enough to activate trailing
            profit_ratio = (current_price - entry_price) / entry_price
            
            if profit_ratio >= breakeven_threshold:
                if is_fast_breakeven and profit_ratio < breakeven_threshold * 2:
                    # Fast breakeven mode - move stop to breakeven quickly
                    potential_stop = entry_price * 1.001  # Just above breakeven
                else:
                    # Normal trailing stop calculation
                    potential_stop = current_price * (1 - trailing_pct)
                
                return max(current_stop, potential_stop)
                
        else:  # SHORT
            profit_ratio = (entry_price - current_price) / entry_price
            
            if profit_ratio >= breakeven_threshold:
                if is_fast_breakeven and profit_ratio < breakeven_threshold * 2:
                    # Fast breakeven mode
                    potential_stop = entry_price * 0.999  # Just below breakeven
                else:
                    # Normal trailing stop calculation
                    potential_stop = current_price * (1 + trailing_pct)
                
                return min(current_stop, potential_stop)
                
        return current_stop
    
    def should_exit_position(self, symbol: str, current_price: float,
                           stop_levels: Dict[str, float],
                           position_side: str) -> Tuple[bool, str]:
        """Check if position should be exited (regime-aware)"""
        
        strategy = self.regime_stop_strategies.get(self.current_regime, {})
        
        if position_side.upper() == 'LONG':
            if current_price <= stop_levels['stop_loss']:
                return True, f"Stop Loss Hit (regime: {self.current_regime})"
            elif current_price >= stop_levels['take_profit']:
                # In some regimes, consider partial profits
                if self.current_regime == 'euphoria':
                    return True, f"Take Profit Hit - Euphoria Exit (regime: {self.current_regime})"
                else:
                    return True, f"Take Profit Hit (regime: {self.current_regime})"
        else:  # SHORT
            if current_price >= stop_levels['stop_loss']:
                return True, f"Stop Loss Hit (regime: {self.current_regime})"
            elif current_price <= stop_levels['take_profit']:
                return True, f"Take Profit Hit (regime: {self.current_regime})"
                
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
                            lookback_volatility: float = 0.05) -> float:
        """Adjust stop percentage based on volatility using NumPy"""
        
        # Use NumPy for volatility scaling (crypto-appropriate baseline)
        vol_ratio = np.clip(current_volatility / lookback_volatility, 0.3, 4.0)
        adjusted_stop = base_stop_pct * vol_ratio
        
        return adjusted_stop
    
    def _get_volatility_stop_adjustment(self, current_volatility: float) -> float:
        """Get volatility-based stop adjustment factor"""
        # Crypto baseline volatility
        baseline_vol = 0.05  # 5% daily volatility for crypto
        vol_ratio = current_volatility / baseline_vol
        
        # Scale stops with volatility but not linearly (use square root)
        # Higher vol = wider stops, but not proportionally
        adjustment = np.sqrt(np.clip(vol_ratio, 0.3, 3.0))
        
        return adjustment
    
    def calculate_position_heat(self, entry_price: float, 
                              current_price: float,
                              stop_loss: float,
                              position_side: str) -> float:
        """Calculate position heat (percentage of risk used)"""
        
        if position_side.upper() == 'LONG':
            total_risk = abs(entry_price - stop_loss)
            current_risk = abs(entry_price - current_price) if current_price < entry_price else 0
        else:  # SHORT
            total_risk = abs(stop_loss - entry_price)
            current_risk = abs(current_price - entry_price) if current_price > entry_price else 0
            
        if total_risk == 0:
            return 0
            
        heat = current_risk / total_risk
        return np.clip(heat, 0, 1)  # Cap at 100%
    
    def get_regime_stop_info(self) -> Dict:
        """Get current regime stop loss information"""
        return {
            'current_regime': self.current_regime,
            'regime_multiplier': self.regime_multiplier,
            'current_volatility': self.current_volatility,
            'current_parameters': {
                'initial_stop_pct': self.initial_stop_pct,
                'trailing_stop_pct': self.trailing_stop_pct,
                'take_profit_ratio': self.take_profit_ratio,
                'breakeven_threshold': self.breakeven_threshold
            },
            'base_parameters': {
                'initial_stop_pct': self.base_initial_stop_pct,
                'trailing_stop_pct': self.base_trailing_stop_pct,
                'take_profit_ratio': self.base_take_profit_ratio,
                'breakeven_threshold': self.base_breakeven_threshold
            },
            'regime_strategy': self.regime_stop_strategies.get(self.current_regime, {})
        }
    
    def reset_to_base_parameters(self):
        """Reset to base parameters (for testing)"""
        self.initial_stop_pct = self.base_initial_stop_pct
        self.trailing_stop_pct = self.base_trailing_stop_pct
        self.take_profit_ratio = self.base_take_profit_ratio
        self.breakeven_threshold = self.base_breakeven_threshold
        self.current_regime = None
        self.regime_multiplier = 1.0
        self.current_volatility = None
        
        logger.info("Stop loss manager parameters reset to base values")
    
    def suggest_position_management(self, symbol: str, current_pnl_pct: float) -> Dict:
        """Suggest position management actions based on regime and performance"""
        
        if not self.current_regime:
            return {'action': 'hold', 'reason': 'No regime information'}
        
        strategy = self.regime_stop_strategies.get(self.current_regime, {})
        
        suggestions = []
        
        # Regime-specific management
        if self.current_regime == 'euphoria' and current_pnl_pct > 0.1:  # 10% profit
            suggestions.append({
                'action': 'consider_partial_profit',
                'reason': 'Euphoria regime - consider taking some profits',
                'percentage': 0.5
            })
        
        elif self.current_regime == 'panic' and current_pnl_pct < -0.05:  # 5% loss
            suggestions.append({
                'action': 'hold_or_add',
                'reason': 'Panic regime - consider holding or adding (if conviction high)',
                'caution': 'Only if fundamental thesis intact'
            })
        
        elif self.current_regime == 'distribution' and current_pnl_pct > 0.05:  # 5% profit
            suggestions.append({
                'action': 'tighten_stops',
                'reason': 'Distribution regime - tighten trailing stops',
                'new_trailing_pct': self.trailing_stop_pct * 0.8
            })
        
        return {
            'regime': self.current_regime,
            'current_pnl_pct': current_pnl_pct,
            'suggestions': suggestions,
            'strategy_info': strategy
        }