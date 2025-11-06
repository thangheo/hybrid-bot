"""
Trading Frequency Limiter - Ensures minimum gaps between trades
"""
from datetime import datetime, timedelta
from collections import defaultdict
import logging

class TradingFrequencyLimiter:
    def __init__(self):
        self.last_trade_time = {}
        self.min_gap_seconds = 30  # Minimum 30 seconds between trades
        self.hourly_trade_count = defaultdict(int)
        self.max_trades_per_hour = 20
        
    def can_trade(self, symbol: str) -> tuple[bool, str]:
        """Check if trading is allowed for this symbol"""
        now = datetime.now()
        
        # 1. Check minimum gap
        if symbol in self.last_trade_time:
            elapsed = (now - self.last_trade_time[symbol]).total_seconds()
            if elapsed < self.min_gap_seconds:
                return False, f"Too soon: {elapsed:.1f}s < {self.min_gap_seconds}s gap required"
                
        # 2. Check hourly limit
        current_hour = now.replace(minute=0, second=0, microsecond=0)
        if self.hourly_trade_count[current_hour] >= self.max_trades_per_hour:
            return False, f"Hourly limit reached: {self.hourly_trade_count[current_hour]}/{self.max_trades_per_hour}"
            
        return True, "OK"
        
    def record_trade(self, symbol: str):
        """Record that a trade was executed"""
        now = datetime.now()
        self.last_trade_time[symbol] = now
        
        current_hour = now.replace(minute=0, second=0, microsecond=0)
        self.hourly_trade_count[current_hour] += 1
        
        logging.info(f"Trade recorded for {symbol}. Hourly count: {self.hourly_trade_count[current_hour]}")
        
    def cleanup_old_data(self):
        """Cleanup old hourly data"""
        cutoff = datetime.now() - timedelta(hours=24)
        
        keys_to_remove = [k for k in self.hourly_trade_count.keys() if k < cutoff]
        for key in keys_to_remove:
            del self.hourly_trade_count[key]

# Global instance            
frequency_limiter = TradingFrequencyLimiter()
