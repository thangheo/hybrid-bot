# src/risk/manager.py
"""
Risk Management Module - Central Risk Controller
Uses established libraries for statistical calculations and portfolio monitoring
"""

from typing import Dict, List, Optional, Tuple
import numpy as np
from dataclasses import dataclass
from enum import Enum
import logging
 # Initialize components using standard libraries

logger = logging.getLogger(__name__)

class RiskLevel(Enum):
    LOW = "low"
    MEDIUM = "medium"
    HIGH = "high"
    CRITICAL = "critical"

@dataclass
class RiskMetrics:
    portfolio_var: float
    max_position_size: float
    current_exposure: float
    drawdown_level: float
    risk_level: RiskLevel

class RiskManager:
    """Central risk management controller"""
    
    def __init__(self, config: dict):
        self.config = config
        
        from .position_sizer import PositionSizer
        from .portfolio_monitor import PortfolioRiskMonitor
        from .stop_loss_manager import StopLossManager
        # Initialize components       
        
        self.position_sizer = PositionSizer(config.get('position_sizing', {}))
        self.portfolio_monitor = PortfolioRiskMonitor(config.get('portfolio_risk', {}))
        self.stop_loss_manager = StopLossManager(config.get('stop_loss', {}))
        
        # Risk limits from config
        self.max_portfolio_risk = config.get('max_portfolio_risk', 0.1)
        self.max_position_risk = config.get('max_position_risk', 0.08)
        self.max_correlation_exposure = config.get('max_correlation_exposure', 0.5)
        self.max_drawdown_limit = config.get('max_drawdown_limit', 0.15)
        
    def evaluate_trade_risk(self, symbol: str, signal: dict, 
                          current_portfolio: dict) -> Tuple[bool, float, str]:
        """Evaluate if trade meets risk criteria"""
        
        # Calculate position size
        position_size = self.position_sizer.calculate_position_size(
            symbol, signal, current_portfolio
        )
        
        # Check portfolio risk using NumPy/SciPy calculations
        portfolio_risk = self.portfolio_monitor.calculate_portfolio_risk(
            current_portfolio, symbol, position_size
        )
        
        # Risk checks
        risk_checks = {
            'position_size': position_size <= self.max_position_risk,
            'portfolio_risk': portfolio_risk <= self.max_portfolio_risk,
            'correlation': self._check_correlation_risk(symbol, current_portfolio),
            'drawdown': self._check_drawdown_risk(current_portfolio)
        }
        
        # Decision logic
        approved = all(risk_checks.values())
        final_size = position_size if approved else 0
        reason = "Approved" if approved else f"Rejected: {[k for k, v in risk_checks.items() if not v]}"
        
        return approved, final_size, reason
    
    def get_risk_metrics(self, portfolio: dict) -> RiskMetrics:
        """Get current portfolio risk metrics"""
        return self.portfolio_monitor.get_risk_metrics(portfolio)
    
    def _check_correlation_risk(self, symbol: str, portfolio: dict) -> bool:
        """Check correlation exposure limits"""
        # Use NumPy for correlation calculations
        return self.portfolio_monitor.check_correlation_exposure(symbol, portfolio)
    
    def _check_drawdown_risk(self, portfolio: dict) -> bool:
        """Check drawdown limits"""
        current_drawdown = self.portfolio_monitor.calculate_current_drawdown(portfolio)
        return current_drawdown < self.max_drawdown_limit