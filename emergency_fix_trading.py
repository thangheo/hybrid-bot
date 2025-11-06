#!/usr/bin/env python3
"""
Emergency Risk Management & Frequency Reducer
Thực hiện ngay lập tức để ngăn chặn lỗ tiếp theo
"""

import json
import os
import logging
from datetime import datetime, timedelta
from pathlib import Path

# Setup logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s',
    handlers=[
        logging.FileHandler('/home/e8/works/hybrid-bot/logs/emergency_fixes.log'),
        logging.StreamHandler()
    ]
)

class EmergencyTradingFix:
    def __init__(self):
        self.base_path = Path('/home/e8/works/hybrid-bot')
        self.config_path = self.base_path / 'config'
        
    def apply_emergency_fixes(self):
        """Apply all emergency fixes"""
        logging.info("🚨 STARTING EMERGENCY TRADING FIXES")
        
        # 1. Update signal deduplication config
        self.update_signal_dedup_config()
        
        # 2. Update orderbook analyzer thresholds  
        self.update_orderbook_thresholds()
        
        # 3. Create emergency risk monitor
        self.create_emergency_risk_monitor()
        
        # 4. Update risk config with tighter limits
        self.update_risk_config()
        
        # 5. Create trading frequency limiter
        self.create_frequency_limiter()
        
        logging.info("✅ ALL EMERGENCY FIXES APPLIED")
        
    def update_signal_dedup_config(self):
        """Tăng debounce window để giảm tần suất signal"""
        config_file = self.config_path / 'signal_dedup_config.json'
        
        config = {
            "description": "Emergency signal deduplication config to reduce trading frequency",
            "debounceWindowMs": 15000,  # 15s instead of 500ms
            "relativeStrengthThreshold": 0.25,  # 25% instead of 10%
            "absoluteStrengthThreshold": 0.1,   # 10% instead of 5%
            "flipStrengthThreshold": 0.8,       # 80% instead of 70%
            "aggressiveDelta": 0.3,             # 30% instead of 15%
            "includeExchangeInKey": True,
            "enabled": True,
            "emergencyMode": True,
            "lastUpdated": datetime.now().isoformat()
        }
        
        with open(config_file, 'w') as f:
            json.dump(config, f, indent=2)
            
        logging.info(f"✅ Updated signal dedup config: {config_file}")
        
    def update_orderbook_thresholds(self):
        """Tăng threshold để chỉ trade signal mạnh"""
        config_file = self.config_path / 'orderbook_analyzer_config.json'
        
        config = {
            "description": "Emergency orderbook analyzer config with higher thresholds",
            "depth": 20,
            "historySize": 50,  # Reduced from 1000
            "priceWeightFactor": 0.5,
            "volumeWeightFactor": 0.5,
            "imbalanceThreshold": 0.8,  # Increased from 0.6
            "liquidityPressureThreshold": 0.9,  # Increased from 0.7  
            "orderFlowThreshold": 0.8,  # Increased from 0.5
            "priceGroupingFactor": 0.0001,
            "minSamplesForStability": 10,  # Increased from 5
            "emaAlpha": 0.1,  # Reduced from 0.2 for slower response
            "emergencyMode": True,
            "minTradeGapSeconds": 30,  # New: minimum 30s between trades
            "lastUpdated": datetime.now().isoformat()
        }
        
        with open(config_file, 'w') as f:
            json.dump(config, f, indent=2)
            
        logging.info(f"✅ Updated orderbook analyzer config: {config_file}")
        
    def create_emergency_risk_monitor(self):
        """Tạo emergency risk monitor"""
        monitor_file = self.base_path / 'python-strategy' / 'src' / 'execution' / 'emergency_risk_monitor.py'
        monitor_file.parent.mkdir(parents=True, exist_ok=True)
        
        monitor_code = '''"""
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
'''
        
        with open(monitor_file, 'w') as f:
            f.write(monitor_code)
            
        logging.info(f"✅ Created emergency risk monitor: {monitor_file}")
        
    def update_risk_config(self):
        """Update risk config with tighter limits"""
        risk_config_file = self.config_path / 'risk_config.json'
        
        try:
            with open(risk_config_file, 'r') as f:
                config = json.load(f)
        except:
            config = {}
            
        # Update stop loss section
        config['stopLoss'] = {
            "maxLossPercentage": 0.03,  # 3% instead of 5%
            "trailingStopDistance": 0.015,  # 1.5% instead of 2%
            "enableTrailingStop": True,
            "forceLiquidation": True,
            "liquidationDelaySeconds": 2,  # Faster liquidation
            "perSymbolLimit": -50.0,  # New: per symbol limit
            "emergencyMode": True
        }
        
        # Update position limits
        config['positionLimits'] = {
            "maxPositionSizeUSD": 1000.0,  # Max $1000 per position
            "maxConcentrationPct": 0.15,   # Max 15% portfolio per symbol
            "maxDailyLossUSD": 200.0,      # Max $200 loss per day
            "maxTradesPerHour": 20         # Max 20 trades per hour
        }
        
        config['lastUpdated'] = datetime.now().isoformat()
        config['emergencyMode'] = True
        
        with open(risk_config_file, 'w') as f:
            json.dump(config, f, indent=2)
            
        logging.info(f"✅ Updated risk config: {risk_config_file}")
        
    def create_frequency_limiter(self):
        """Create trading frequency limiter"""
        limiter_file = self.base_path / 'python-strategy' / 'src' / 'execution' / 'frequency_limiter.py'
        
        limiter_code = '''"""
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
'''

        with open(limiter_file, 'w') as f:
            f.write(limiter_code)
            
        logging.info(f"✅ Created frequency limiter: {limiter_file}")
        
    def create_integration_patch(self):
        """Create patch to integrate risk monitoring into main trading loop"""
        patch_file = self.base_path / 'emergency_integration_patch.py'
        
        patch_code = '''"""
Emergency Integration Patch
Add this to your main trading loop BEFORE executing any trades
"""

import sys
import os
sys.path.append('/home/e8/works/hybrid-bot/python-strategy/src')

from execution.emergency_risk_monitor import risk_monitor, check_trade_allowed
from execution.frequency_limiter import frequency_limiter

def emergency_trade_check(symbol: str, side: str, quantity: float, price: float) -> tuple[bool, str]:
    """
    EMERGENCY CHECK - Call this BEFORE executing ANY trade
    Returns: (allowed: bool, reason: str)
    """
    
    # 1. Risk monitor check
    if not check_trade_allowed(symbol, side, quantity, price):
        return False, "BLOCKED by risk monitor"
        
    # 2. Frequency limiter check  
    can_trade, reason = frequency_limiter.can_trade(symbol)
    if not can_trade:
        return False, f"BLOCKED by frequency limiter: {reason}"
        
    return True, "APPROVED"

def record_executed_trade(symbol: str, pnl_change: float):
    """
    Call this AFTER a trade is executed
    """
    risk_monitor.record_trade(symbol, pnl_change)
    frequency_limiter.record_trade(symbol)

# EXAMPLE USAGE:
# before_trade_allowed, reason = emergency_trade_check("BTCUSDT", "BUY", 0.001, 50000)
# if before_trade_allowed:
#     # Execute trade
#     execute_trade(...)
#     # Record it
#     record_executed_trade("BTCUSDT", realized_pnl_change)
# else:
#     logger.warning(f"Trade blocked: {reason}")
'''

        with open(patch_file, 'w') as f:
            f.write(patch_code)
            
        logging.info(f"✅ Created integration patch: {patch_file}")

def main():
    """Main execution"""
    print("🚨 EMERGENCY TRADING FREQUENCY & RISK FIX")
    print("==========================================")
    
    fixer = EmergencyTradingFix()
    fixer.apply_emergency_fixes()
    fixer.create_integration_patch()
    
    print("\n✅ EMERGENCY FIXES COMPLETED!")
    print("\n📋 NEXT STEPS:")
    print("1. Restart your trading bot to load new configs")
    print("2. Import emergency_integration_patch.py into your main trading loop")  
    print("3. Call emergency_trade_check() BEFORE each trade")
    print("4. Monitor logs/emergency_fixes.log for status")
    print("\n🎯 EXPECTED RESULTS:")
    print("- Trading frequency: 2676 → ~200-400 trades/day")
    print("- Max loss per symbol: -$50")
    print("- Max total daily loss: -$200")
    print("- Minimum 30s between trades per symbol")

if __name__ == "__main__":
    main()