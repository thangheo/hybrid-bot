from fastapi import APIRouter, Depends, HTTPException, Query, Path
from typing import List, Dict, Any, Optional
from pydantic import BaseModel
from datetime import datetime, timedelta
from enum import Enum

from python.monitoring.trading.pnl_calculator import PnLCalculator
from python.monitoring.trading.execution_analyzer import ExecutionAnalyzer
from python.monitoring.trading.strategy_performance import StrategyPerformance
from python.monitoring.trading.risk_metrics import RiskMetricsCalculator
from python.monitoring.utils.security import get_current_user, User

router = APIRouter()
pnl_calculator = PnLCalculator()
execution_analyzer = ExecutionAnalyzer()
strategy_performance = StrategyPerformance()
risk_metrics_calculator = RiskMetricsCalculator()

class TimeFrame(str, Enum):
    day = "day"
    week = "week"
    month = "month"
    year = "year"
    all = "all"

class PnLData(BaseModel):
    timestamp: datetime
    realized_pnl: float
    unrealized_pnl: float
    total_pnl: float
    cumulative_pnl: float
    details: Dict[str, Any] = {}

class StrategyMetrics(BaseModel):
    strategy_id: str
    name: str
    pnl: float
    roi: float
    sharpe_ratio: float
    max_drawdown: float
    win_rate: float
    avg_trade_pnl: float
    trade_count: int
    details: Dict[str, Any] = {}

class ExecutionMetrics(BaseModel):
    timestamp: datetime
    exchange: str
    trading_pair: str
    fill_rate: float
    slippage_bps: float
    execution_time_ms: float
    cost_bps: float
    details: Dict[str, Any] = {}

class RiskMetric(BaseModel):
    name: str
    value: float
    timestamp: datetime
    description: Optional[str] = None
    details: Dict[str, Any] = {}

@router.get("/pnl", response_model=List[PnLData])
async def get_pnl_data(
    timeframe: TimeFrame = Query(TimeFrame.day, description="Timeframe for PnL data"),
    start_date: Optional[datetime] = Query(None, description="Start date for PnL calculation"),
    end_date: Optional[datetime] = Query(None, description="End date for PnL calculation"),
    strategy_id: Optional[str] = Query(None, description="Filter by strategy ID"),
    trading_pair: Optional[str] = Query(None, description="Filter by trading pair"),
    exchange: Optional[str] = Query(None, description="Filter by exchange"),
    interval: Optional[str] = Query("1h", description="Data interval (e.g., 1m, 5m, 1h, 1d)"),
    current_user: User = Depends(get_current_user)
):
    """
    Get P&L data over a specified timeframe with optional filtering.
    """
    # Set default time range based on timeframe if not provided
    if start_date is None:
        if timeframe == TimeFrame.day:
            start_date = datetime.now() - timedelta(days=1)
        elif timeframe == TimeFrame.week:
            start_date = datetime.now() - timedelta(weeks=1)
        elif timeframe == TimeFrame.month:
            start_date = datetime.now() - timedelta(days=30)
        elif timeframe == TimeFrame.year:
            start_date = datetime.now() - timedelta(days=365)
        else:  # All time
            start_date = datetime(2020, 1, 1)  # Arbitrary starting point
    
    if end_date is None:
        end_date = datetime.now()
    
    pnl_data = await pnl_calculator.get_pnl_data(
        start_date=start_date,
        end_date=end_date,
        strategy_id=strategy_id,
        trading_pair=trading_pair,
        exchange=exchange,
        interval=interval
    )
    
    return pnl_data

@router.get("/strategy-performance", response_model=List[StrategyMetrics])
async def get_strategy_performance(
    timeframe: TimeFrame = Query(TimeFrame.day, description="Timeframe for performance calculation"),
    start_date: Optional[datetime] = Query(None, description="Start date for performance calculation"),
    end_date: Optional[datetime] = Query(None, description="End date for performance calculation"),
    strategy_id: Optional[str] = Query(None, description="Filter by strategy ID"),
    current_user: User = Depends(get_current_user)
):
    """
    Get performance metrics for trading strategies.
    """
    # Set default time range based on timeframe if not provided
    if start_date is None:
        if timeframe == TimeFrame.day:
            start_date = datetime.now() - timedelta(days=1)
        elif timeframe == TimeFrame.week:
            start_date = datetime.now() - timedelta(weeks=1)
        elif timeframe == TimeFrame.month:
            start_date = datetime.now() - timedelta(days=30)
        elif timeframe == TimeFrame.year:
            start_date = datetime.now() - timedelta(days=365)
        else:  # All time
            start_date = datetime(2020, 1, 1)  # Arbitrary starting point
    
    if end_date is None:
        end_date = datetime.now()
        
    performance_data = await strategy_performance.get_strategy_metrics(
        start_date=start_date,
        end_date=end_date,
        strategy_id=strategy_id
    )
    
    return performance_data

@router.get("/execution-quality", response_model=List[ExecutionMetrics])
async def get_execution_quality(
    timeframe: TimeFrame = Query(TimeFrame.day, description="Timeframe for execution quality data"),
    start_date: Optional[datetime] = Query(None, description="Start date"),
    end_date: Optional[datetime] = Query(None, description="End date"),
    exchange: Optional[str] = Query(None, description="Filter by exchange"),
    trading_pair: Optional[str] = Query(None, description="Filter by trading pair"),
    interval: Optional[str] = Query("1h", description="Data interval (e.g., 1m, 5m, 1h, 1d)"),
    current_user: User = Depends(get_current_user)
):
    """
    Get execution quality metrics.
    """
    # Set default time range based on timeframe if not provided
    if start_date is None:
        if timeframe == TimeFrame.day:
            start_date = datetime.now() - timedelta(days=1)
        elif timeframe == TimeFrame.week:
            start_date = datetime.now() - timedelta(weeks=1)
        elif timeframe == TimeFrame.month:
            start_date = datetime.now() - timedelta(days=30)
        elif timeframe == TimeFrame.year:
            start_date = datetime.now() - timedelta(days=365)
        else:  # All time
            start_date = datetime(2020, 1, 1)  # Arbitrary starting point
    
    if end_date is None:
        end_date = datetime.now()
        
    execution_data = await execution_analyzer.get_execution_metrics(
        start_date=start_date,
        end_date=end_date,
        exchange=exchange,
        trading_pair=trading_pair,
        interval=interval
    )
    
    return execution_data

@router.get("/risk-metrics", response_model=List[RiskMetric])
async def get_risk_metrics(
    metrics: List[str] = Query(["VaR", "expected_shortfall", "sharpe_ratio"], 
                              description="Risk metrics to calculate"),
    confidence_level: float = Query(0.95, ge=0.5, le=0.99, 
                                  description="Confidence level for VaR and ES calculations"),
    timeframe: TimeFrame = Query(TimeFrame.day, description="Timeframe for risk calculations"),
    current_user: User = Depends(get_current_user)
):
    """
    Get risk metrics for the current portfolio.
    """
    risk_data = await risk_metrics_calculator.calculate_risk_metrics(
        metrics=metrics,
        confidence_level=confidence_level,
        timeframe=timeframe
    )
    
    return risk_data

@router.get("/active-positions", response_model=Dict[str, Any])
async def get_active_positions(
    current_user: User = Depends(get_current_user)
):
    """
    Get current active positions across all exchanges and trading pairs.
    """
    positions = await risk_metrics_calculator.get_active_positions()
    return positions