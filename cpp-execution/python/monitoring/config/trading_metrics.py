# monitoring/config/trading_metrics.py

from .settings import settings

class TradingMetricsConfig:
    """Configuration for trading metrics components."""
    
    @property
    def calculation_interval(self):
        """Interval (in seconds) for calculating trading metrics."""
        return settings.get("trading.calculation_interval", 300)
        
    @property
    def pnl_window_sizes(self):
        """Time windows for PnL calculations (in seconds)."""
        return settings.get("trading.pnl_window_sizes", [
            300,    # 5 minutes
            3600,   # 1 hour
            86400,  # 1 day
            604800  # 1 week
        ])
        
    @property
    def execution_quality_thresholds(self):
        """Thresholds for execution quality metrics."""
        return {
            "slippage": {
                "critical": settings.get("trading.execution_quality.slippage.critical", 0.5),
                "warning": settings.get("trading.execution_quality.slippage.warning", 0.2),
            },
            "fill_rate": {
                "critical": settings.get("trading.execution_quality.fill_rate.critical", 0.7),
                "warning": settings.get("trading.execution_quality.fill_rate.warning", 0.9),
            }
        }
        
    @property
    def strategy_metrics(self):
        """Metrics to track for strategy performance."""
        return settings.get("trading.strategy_metrics", [
            "sharpe_ratio",
            "max_drawdown",
            "win_rate",
            "profit_factor"
        ])

# Create singleton instance
trading_metrics_config = TradingMetricsConfig()