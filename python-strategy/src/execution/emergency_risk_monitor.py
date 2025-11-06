"""
Emergency Risk Monitor - Blocks trades when risk limits exceeded
"""
import json
import logging
from datetime import datetime, timedelta
from typing import Dict, Optional
from dataclasses import dataclass

@dataclass
class RiskLimits:
    max_loss_per_symbol: float = -50.0  # -$50 per symbol
    max_total_daily_loss: float = -200.0  # -$200 total per day
    max_concentration_pct: float = 0.15  # 15% max per symbol
    max_trades_per_hour: int = 20  # Max 20 trades/hour
    
class EmergencyRiskMonitor:
    def __init__(self, config_path: str = '/home/e8/works/hybrid-bot/config'):
        self.limits = RiskLimits()
        self.trade_history = []
        self.daily_pnl = {}
        self.emergency_stop = False
        self.blocked_symbols = set()
        
        # Load current PnL if available
        self.load_current_state()
        
    def load_current_state(self):
        """Load current PnL state"""
        try:
            pnl_file = '/home/e8/works/hybrid-bot/trade_analysis.json'
            with open(pnl_file, 'r') as f:
                data = json.load(f)
                
            # Load current positions
            for symbol, pair_data in data.get('pairs', {}).items():
                current_pnl = pair_data.get('total_pnl', 0.0)
                self.daily_pnl[symbol] = current_pnl
                
                # Block symbols already at loss limit
                if current_pnl < self.limits.max_loss_per_symbol:
                    self.blocked_symbols.add(symbol)
                    logging.critical(f"🚫 BLOCKING {symbol}: Current PnL {current_pnl:.2f}")
                    
        except Exception as e:
            logging.warning(f"Could not load current state: {e}")
            
    def should_block_trade(self, symbol: str, side: str, quantity: float, price: float) -> bool:
        """Check if trade should be blocked"""
        
        # 1. Emergency stop check
        if self.emergency_stop:
            logging.critical(f"🚫 EMERGENCY STOP ACTIVE - Blocking all trades")
            return True
            
        # 2. Symbol-specific stop loss
        if symbol in self.blocked_symbols:
            logging.warning(f"🚫 BLOCKING {symbol}: Symbol is blocked due to losses")
            return True
            
        current_pnl = self.daily_pnl.get(symbol, 0.0)
        if current_pnl < self.limits.max_loss_per_symbol:
            self.blocked_symbols.add(symbol)
            logging.critical(f"🚫 BLOCKING {symbol}: PnL {current_pnl:.2f} below limit {self.limits.max_loss_per_symbol}")
            return True
            
        # 3. Total portfolio loss check  
        total_loss = sum(pnl for pnl in self.daily_pnl.values() if pnl < 0)
        if total_loss < self.limits.max_total_daily_loss:
            self.emergency_stop = True
            logging.critical(f"🚫 EMERGENCY STOP: Total loss {total_loss:.2f} exceeds limit {self.limits.max_total_daily_loss}")
            return True
            
        # 4. Trading frequency check
        if self.is_trading_too_frequently():
            logging.warning(f"🚫 BLOCKING {symbol}: Trading frequency too high")
            return True
            
        return False
        
    def is_trading_too_frequently(self) -> bool:
        """Check if trading frequency is too high"""
        now = datetime.now()
        hour_ago = now - timedelta(hours=1)
        
        recent_trades = [t for t in self.trade_history if t > hour_ago]
        return len(recent_trades) >= self.limits.max_trades_per_hour
        
    def record_trade(self, symbol: str, pnl_change: float):
        """Record a completed trade"""
        self.trade_history.append(datetime.now())
        
        # Update PnL
        if symbol not in self.daily_pnl:
            self.daily_pnl[symbol] = 0.0
        self.daily_pnl[symbol] += pnl_change
        
        # Check if we need to block this symbol
        if self.daily_pnl[symbol] < self.limits.max_loss_per_symbol:
            self.blocked_symbols.add(symbol)
            logging.critical(f"🚫 BLOCKING {symbol} after trade: PnL now {self.daily_pnl[symbol]:.2f}")
            
    def get_status(self) -> Dict:
        """Get current risk status"""
        total_pnl = sum(self.daily_pnl.values())
        
        return {
            'emergency_stop': self.emergency_stop,
            'blocked_symbols': list(self.blocked_symbols),
            'total_pnl': total_pnl,
            'daily_pnl': self.daily_pnl,
            'trades_last_hour': len([t for t in self.trade_history if t > datetime.now() - timedelta(hours=1)]),
            'risk_limits': {
                'max_loss_per_symbol': self.limits.max_loss_per_symbol,
                'max_total_daily_loss': self.limits.max_total_daily_loss,
                'max_trades_per_hour': self.limits.max_trades_per_hour
            }
        }

# Global instance
risk_monitor = EmergencyRiskMonitor()

def check_trade_allowed(symbol: str, side: str, quantity: float, price: float) -> bool:
    """Global function to check if trade is allowed"""
    return not risk_monitor.should_block_trade(symbol, side, quantity, price)
