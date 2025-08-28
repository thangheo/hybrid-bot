# Enhanced Exit Strategy với Signal Decay & Half-Life Concept
# src/strategy/exit/signal_decay_manager.py

import numpy as np
import pandas as pd
from typing import Dict, List, Optional, Tuple, Any
from dataclasses import dataclass
from datetime import datetime, timedelta
import math

@dataclass
class SignalDecayConfig:
    """Configuration for signal decay exit strategy"""
    decay_method: str = "exponential"  # exponential, linear, adaptive
    half_life_hours: float = 24.0      # Signal half-life in hours
    min_signal_threshold: float = 0.1   # Below this, start exiting
    exit_speed_factor: float = 1.0      # How fast to exit (1.0 = normal speed)
    momentum_protection: bool = True    # Protect against sudden reversals
    confidence_weighting: bool = True   # Weight decay by original confidence

@dataclass
class PositionWithDecay:
    """Position tracking with signal decay"""
    symbol: str
    entry_time: datetime
    entry_price: float
    current_position: float
    original_signal_strength: float
    original_confidence: float
    current_signal_strength: float
    decay_rate: float
    exit_schedule: List[Tuple[datetime, float]]  # (time, exit_amount)

class SignalDecayExitManager:
    """
    Bio-inspired exit strategy based on signal decay
    
    Concept: Like biological processes, trading signals naturally decay over time.
    Instead of binary exit triggers, we gradually reduce positions as signal weakens.
    """
    
    def __init__(self, config: SignalDecayConfig):
        self.config = config
        self.active_positions: Dict[str, PositionWithDecay] = {}
        self.exit_history: List[Dict] = []
        
    def add_position(self, symbol: str, entry_time: datetime, 
                    entry_price: float, position_size: float,
                    signal_strength: float, confidence: float):
        """Add new position with decay tracking"""
        
        # Calculate decay rate based on half-life
        decay_rate = math.log(2) / self.config.half_life_hours
        
        # Create exit schedule
        exit_schedule = self._create_exit_schedule(
            entry_time, position_size, signal_strength, confidence
        )
        
        position = PositionWithDecay(
            symbol=symbol,
            entry_time=entry_time,
            entry_price=entry_price,
            current_position=position_size,
            original_signal_strength=signal_strength,
            original_confidence=confidence,
            current_signal_strength=signal_strength,
            decay_rate=decay_rate,
            exit_schedule=exit_schedule
        )
        
        self.active_positions[symbol] = position

    def get_decay_status(self, symbol: str) -> Optional[Dict[str, Any]]:
        """Get the decay status of a position"""
        position = self.active_positions.get(symbol)
        if not position:
            return None

        return {
            'decay_level': position.current_signal_strength / position.original_signal_strength,
            'time_to_full_decay': self.config.half_life_hours * 3
        }

    def update_signal_strength(self, symbol: str, new_signal_strength: float,
                              current_time: datetime) -> Dict[str, float]:
        """
        Update signal strength và return recommended exit actions
        
        Returns:
            Dict with 'exit_amount' and 'urgency_level'
        """
        if symbol not in self.active_positions:
            return {'exit_amount': 0.0, 'urgency_level': 0.0}
            
        position = self.active_positions[symbol]
        
        # Calculate time-based decay
        hours_elapsed = (current_time - position.entry_time).total_seconds() / 3600
        time_decay = math.exp(-position.decay_rate * hours_elapsed)
        
        # Calculate signal-based decay
        signal_decay = max(0, new_signal_strength / position.original_signal_strength)
        
        # Combined decay (weighted by confidence)
        if self.config.confidence_weighting:
            combined_decay = (
                time_decay * position.original_confidence + 
                signal_decay * (1 - position.original_confidence)
            )
        else:
            combined_decay = (time_decay + signal_decay) / 2
            
        # Update position
        position.current_signal_strength = new_signal_strength
        
        # Determine exit action
        exit_recommendation = self._calculate_exit_amount(
            position, combined_decay, current_time
        )
        
        return exit_recommendation
    
    def _create_exit_schedule(self, entry_time: datetime, position_size: float,
                             signal_strength: float, confidence: float) -> List[Tuple[datetime, float]]:
        """Create scheduled exit plan based on expected signal decay"""
        
        schedule = []
        remaining_position = position_size
        
        # Create exit points over 3x half-life period
        total_hours = self.config.half_life_hours * 3
        num_exits = 10  # Number of planned exit points
        
        for i in range(1, num_exits + 1):
            exit_time = entry_time + timedelta(hours=(total_hours * i / num_exits))
            
            # Calculate expected signal strength at this time
            hours = total_hours * i / num_exits
            expected_strength = signal_strength * math.exp(-math.log(2) * hours / self.config.half_life_hours)
            
            # Exit percentage based on signal decay
            if expected_strength < self.config.min_signal_threshold:
                exit_pct = 0.3  # Exit 30% when signal gets weak
            else:
                exit_pct = 0.1  # Small exits to lock in profits
                
            exit_amount = remaining_position * exit_pct
            remaining_position -= exit_amount
            
            schedule.append((exit_time, exit_amount))
            
        return schedule
    
    def _calculate_exit_amount(self, position: PositionWithDecay, 
                              combined_decay: float, current_time: datetime) -> Dict[str, Any]:
        """Calculate how much to exit based on current decay level"""
        
        # Base exit amount calculation
        if combined_decay < self.config.min_signal_threshold:
            # Signal very weak - accelerated exit
            base_exit_pct = 0.4 * self.config.exit_speed_factor
            urgency = 0.8
            
        elif combined_decay < 0.3:
            # Signal weakening - start exiting
            base_exit_pct = 0.2 * self.config.exit_speed_factor
            urgency = 0.6
            
        elif combined_decay < 0.5:
            # Signal moderate - small exits
            base_exit_pct = 0.1 * self.config.exit_speed_factor
            urgency = 0.3
            
        else:
            # Signal still strong - minimal exit
            base_exit_pct = 0.05 * self.config.exit_speed_factor
            urgency = 0.1
            
        # Apply momentum protection
        if self.config.momentum_protection:
            momentum_factor = self._calculate_momentum_factor(position, current_time)
            base_exit_pct *= momentum_factor
            
        exit_amount = position.current_position * base_exit_pct
        
        return {
            'exit_amount': exit_amount,
            'urgency_level': urgency,
            'decay_level': combined_decay,
            'recommended_action': self._get_action_recommendation(urgency)
        }
    
    def _calculate_momentum_factor(self, position: PositionWithDecay, 
                                  current_time: datetime) -> float:
        """
        Calculate momentum protection factor
        Reduce exits if signal is strengthening unexpectedly
        """
        
        # Get recent signal strength changes
        current_strength = position.current_signal_strength
        original_strength = position.original_signal_strength
        
        # If signal is stronger than expected, reduce exit pressure
        strength_ratio = current_strength / original_strength
        
        if strength_ratio > 1.2:  # Signal 20% stronger than entry
            return 0.3  # Reduce exit amount by 70%
        elif strength_ratio > 1.0:  # Signal stronger than entry
            return 0.7  # Reduce exit amount by 30%
        else:
            return 1.0  # Normal exit
    
    def _get_action_recommendation(self, urgency_level: float) -> str:
        """Convert urgency level to action recommendation"""
        if urgency_level > 0.7:
            return "AGGRESSIVE_EXIT"
        elif urgency_level > 0.4:
            return "GRADUAL_EXIT"
        elif urgency_level > 0.2:
            return "PARTIAL_EXIT"
        else:
            return "HOLD_MONITOR"
    
    def get_scheduled_exits(self, current_time: datetime) -> Dict[str, float]:
        """Get any scheduled exits that should execute now"""
        
        scheduled_exits = {}
        
        for symbol, position in self.active_positions.items():
            for exit_time, exit_amount in position.exit_schedule:
                # Check if this exit time has passed
                if current_time >= exit_time and exit_amount > 0:
                    if symbol not in scheduled_exits:
                        scheduled_exits[symbol] = 0
                    scheduled_exits[symbol] += exit_amount
                    
                    # Mark this exit as completed
                    position.exit_schedule = [
                        (t, a) for t, a in position.exit_schedule 
                        if not (t == exit_time and a == exit_amount)
                    ]
                    
        return scheduled_exits
    
    def execute_exit(self, symbol: str, exit_amount: float, 
                    exit_price: float, current_time: datetime):
        """Record exit execution and update position"""
        
        if symbol in self.active_positions:
            position = self.active_positions[symbol]
            position.current_position -= exit_amount
            
            # Record exit in history
            self.exit_history.append({
                'symbol': symbol,
                'exit_time': current_time,
                'exit_amount': exit_amount,
                'exit_price': exit_price,
                'entry_price': position.entry_price,
                'profit_loss': (exit_price - position.entry_price) * exit_amount,
                'signal_decay_level': position.current_signal_strength / position.original_signal_strength
            })
            
            # Remove position if fully exited
            if position.current_position <= 0.001:  # Small threshold for floating point
                del self.active_positions[symbol]
    
    def get_exit_performance_metrics(self) -> Dict[str, float]:
        """Analyze performance of exit strategy"""
        
        if not self.exit_history:
            return {}
            
        exits_df = pd.DataFrame(self.exit_history)
        
        total_pnl = exits_df['profit_loss'].sum()
        win_rate = (exits_df['profit_loss'] > 0).mean()
        avg_profit = exits_df[exits_df['profit_loss'] > 0]['profit_loss'].mean()
        avg_loss = exits_df[exits_df['profit_loss'] < 0]['profit_loss'].mean()
        
        # Analyze exit timing effectiveness
        avg_decay_at_exit = exits_df['signal_decay_level'].mean()
        early_exits = (exits_df['signal_decay_level'] > 0.5).mean()  # % exits while signal still strong
        
        return {
            'total_pnl': total_pnl,
            'win_rate': win_rate,
            'avg_profit': avg_profit if not pd.isna(avg_profit) else 0,
            'avg_loss': avg_loss if not pd.isna(avg_loss) else 0,
            'avg_decay_at_exit': avg_decay_at_exit,
            'early_exit_rate': early_exits,
            'total_exits': len(exits_df)
        }


# Integration with existing strategy system
class EnhancedStrategyWithDecayExit:
    """
    Enhanced strategy that integrates signal decay exit management
    """
    
    def __init__(self, base_strategy, decay_config: SignalDecayConfig):
        self.base_strategy = base_strategy
        self.exit_manager = SignalDecayExitManager(decay_config)
        
    def generate_signals(self, data: pd.DataFrame) -> pd.DataFrame:
        """Generate entry signals using base strategy"""
        return self.base_strategy.generate_signals(data)
    
    def process_signal_update(self, symbol: str, current_data: pd.DataFrame) -> Dict:
        """Process signal updates and generate exit recommendations"""
        
        # Get current signal strength from base strategy
        indicators = self.base_strategy.calculate_indicators(current_data)
        current_signals = self.base_strategy.generate_signals(current_data, indicators)
        
        if len(current_signals) == 0:
            current_signal_strength = 0
        else:
            current_signal_strength = abs(current_signals['signal'].iloc[-1])
            
        # Update exit manager
        current_time = datetime.now()
        exit_recommendation = self.exit_manager.update_signal_strength(
            symbol, current_signal_strength, current_time
        )
        
        # Check for scheduled exits
        scheduled_exits = self.exit_manager.get_scheduled_exits(current_time)
        
        return {
            'current_signal_strength': current_signal_strength,
            'exit_recommendation': exit_recommendation,
            'scheduled_exits': scheduled_exits.get(symbol, 0),
            'total_exit_amount': exit_recommendation['exit_amount'] + scheduled_exits.get(symbol, 0)
        }


# Configuration for different exit strategies
CONSERVATIVE_EXIT_CONFIG = SignalDecayConfig(
    decay_method="exponential",
    half_life_hours=48.0,      # Longer hold periods
    min_signal_threshold=0.2,   # Higher threshold before exiting
    exit_speed_factor=0.7,      # Slower exits
    momentum_protection=True,
    confidence_weighting=True
)

AGGRESSIVE_EXIT_CONFIG = SignalDecayConfig(
    decay_method="exponential",
    half_life_hours=12.0,      # Shorter hold periods
    min_signal_threshold=0.3,   # Exit earlier when signal weakens
    exit_speed_factor=1.5,      # Faster exits
    momentum_protection=True,
    confidence_weighting=True
)

ADAPTIVE_EXIT_CONFIG = SignalDecayConfig(
    decay_method="adaptive",
    half_life_hours=24.0,      # Standard hold period
    min_signal_threshold=0.15,  # Adaptive threshold
    exit_speed_factor=1.0,      # Standard speed
    momentum_protection=True,
    confidence_weighting=True
)


# Usage example
if __name__ == "__main__":
    # Example usage
    from strategy.strategies.statistical_mean_reversion import StatisticalMeanReversionStrategy
    
    # Create base strategy
    base_config = {
        'zscore_window': 20,
        'zscore_entry': 2.0,
        'zscore_exit': 0.5
    }
    base_strategy = StatisticalMeanReversionStrategy(base_config)
    
    # Create enhanced strategy with decay exit
    enhanced_strategy = EnhancedStrategyWithDecayExit(
        base_strategy, 
        ADAPTIVE_EXIT_CONFIG
    )
    
    # Simulate signal decay exit
    print("Signal Decay Exit Strategy initialized!")
    print("Features:")
    print("- Half-life based signal decay")
    print("- Scheduled partial exits")
    print("- Momentum protection")
    print("- Confidence weighting")
    print("- Performance tracking")