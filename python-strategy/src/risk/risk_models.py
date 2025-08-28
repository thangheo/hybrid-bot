# src/risk/risk_models.py
"""
Risk Management Data Models
Simple dataclasses for risk-related data structures
"""

from dataclasses import dataclass, field
from typing import Dict, List, Optional
from datetime import datetime
from enum import Enum

class RiskLevel(Enum):
    LOW = "low"
    MEDIUM = "medium"
    HIGH = "high"
    CRITICAL = "critical"

class PositionSide(Enum):
    LONG = "LONG"
    SHORT = "SHORT"

class RiskEventType(Enum):
    STOP_LOSS = "STOP_LOSS"
    TAKE_PROFIT = "TAKE_PROFIT"
    EMERGENCY_STOP = "EMERGENCY_STOP"
    RISK_LIMIT_BREACH = "RISK_LIMIT_BREACH"
    POSITION_SIZE_LIMIT = "POSITION_SIZE_LIMIT"

@dataclass
class RiskMetrics:
    """Risk metrics data structure"""
    portfolio_var: float
    max_position_size: float
    current_exposure: float
    drawdown_level: float
    risk_level: RiskLevel

@dataclass
class PositionRisk:
    """Risk metrics for individual position"""
    symbol: str
    position_size: float
    entry_price: float
    current_price: float
    stop_loss: float
    take_profit: float
    position_side: PositionSide
    risk_amount: float
    unrealized_pnl: float
    position_heat: float  # Percentage of risk used
    created_at: datetime = field(default_factory=datetime.now)

@dataclass
class PortfolioRisk:
    """Portfolio-level risk metrics"""
    total_value: float
    var_95: float  # Value at Risk 95%
    max_drawdown: float
    current_drawdown: float
    total_exposure: float
    cash_percentage: float
    position_count: int
    largest_position_pct: float
    sector_concentrations: Dict[str, float] = field(default_factory=dict)
    correlation_risk: float = 0.0
    timestamp: datetime = field(default_factory=datetime.now)

@dataclass
class RiskEvent:
    """Risk management event record"""
    event_id: str
    event_type: RiskEventType
    symbol: Optional[str]
    description: str
    severity: str  # LOW, MEDIUM, HIGH, CRITICAL
    action_taken: str
    portfolio_impact: float
    metadata: Dict = field(default_factory=dict)
    timestamp: datetime = field(default_factory=datetime.now)

@dataclass
class RiskLimits:
    """Risk limit configuration"""
    max_portfolio_risk: float = 0.02  # 2% max portfolio VaR
    max_position_risk: float = 0.01   # 1% max per position
    max_position_size: float = 0.1    # 10% max position size
    max_drawdown_limit: float = 0.15  # 15% max drawdown
    max_correlation_exposure: float = 0.5  # 50% max correlated exposure
    max_daily_loss: float = 0.05      # 5% max daily loss
    min_cash_percentage: float = 0.05  # 5% minimum cash
    max_leverage: float = 1.0         # No leverage by default

@dataclass
class StopLossLevels:
    """Stop loss and take profit levels"""
    symbol: str
    entry_price: float
    stop_loss: float
    take_profit: float
    trailing_stop: Optional[float] = None
    position_side: PositionSide = PositionSide.LONG
    volatility_adjusted: bool = True
    created_at: datetime = field(default_factory=datetime.now)
    updated_at: datetime = field(default_factory=datetime.now)

@dataclass
class RiskAdjustment:
    """Record of risk parameter adjustments"""
    parameter_name: str
    old_value: float
    new_value: float
    reason: str
    adjusted_by: str
    market_conditions: Dict = field(default_factory=dict)
    timestamp: datetime = field(default_factory=datetime.now)

@dataclass
class CorrelationMatrix:
    """Correlation data between assets"""
    symbols: List[str]
    correlation_data: List[List[float]]
    calculation_period: int  # Days
    updated_at: datetime = field(default_factory=datetime.now)
    
    def get_correlation(self, symbol1: str, symbol2: str) -> Optional[float]:
        """Get correlation between two symbols"""
        try:
            idx1 = self.symbols.index(symbol1)
            idx2 = self.symbols.index(symbol2)
            return self.correlation_data[idx1][idx2]
        except (ValueError, IndexError):
            return None

@dataclass
class VolatilityData:
    """Volatility metrics for assets"""
    symbol: str
    daily_volatility: float
    weekly_volatility: float
    monthly_volatility: float
    rolling_volatility_20d: float
    rolling_volatility_50d: float
    volatility_percentile: float  # Current vol vs historical
    calculation_date: datetime = field(default_factory=datetime.now)

@dataclass
class RiskReport:
    """Comprehensive risk report"""
    report_id: str
    portfolio_risk: PortfolioRisk
    position_risks: List[PositionRisk]
    risk_events: List[RiskEvent]
    risk_limits: RiskLimits
    compliance_status: Dict[str, bool]  # Which limits are being respected
    recommendations: List[str]
    generated_at: datetime = field(default_factory=datetime.now)