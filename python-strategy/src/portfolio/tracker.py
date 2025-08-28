
# src/portfolio/tracker.py
"""
Position Tracker - Simple position management
Uses basic data structures and pandas for calculations
"""
from typing import Tuple
# from .models import PortfolioManager, PositionSummary, PositionSide
from .manager import PortfolioManager
class PositionTracker:
    """
    Simple position tracker for integration with execution module
    No complex inheritance - just working functions
    """
    
    def __init__(self, portfolio_manager: PortfolioManager):
        self.portfolio_manager = portfolio_manager
        
    def get_position(self, symbol: str) -> float:
        """Get current position quantity for symbol"""
        if symbol in self.portfolio_manager.positions:
            return self.portfolio_manager.positions[symbol].quantity
        return 0.0
    
    def get_position_value(self, symbol: str) -> float:
        """Get current position market value"""
        if symbol in self.portfolio_manager.positions:
            return self.portfolio_manager.positions[symbol].market_value
        return 0.0
    
    def can_trade(self, symbol: str, side: str, quantity: float, price: float) -> Tuple[bool, str]:
        """
        Check if trade is possible given current positions and cash
        Simple validation logic
        """
        current_position = self.get_position(symbol)
        trade_value = quantity * price
        
        if side == 'BUY':
            # Check if we have enough cash
            if trade_value > self.portfolio_manager.cash_balance:
                return False, f"Insufficient cash: need {trade_value}, have {self.portfolio_manager.cash_balance}"
            return True, "OK"
            
        elif side == 'SELL':
            # Check if we have enough position to sell
            if quantity > current_position:
                return False, f"Insufficient position: trying to sell {quantity}, have {current_position}"
            return True, "OK"
        
        return False, "Invalid side"
    
    def get_available_cash(self) -> float:
        """Get available cash for trading"""
        return self.portfolio_manager.cash_balance
    
    def get_total_exposure(self) -> float:
        """Get total position exposure"""
        return sum(abs(pos.market_value) for pos in self.portfolio_manager.positions.values())
