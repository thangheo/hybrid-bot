"""
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
