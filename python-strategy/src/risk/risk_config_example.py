# Example risk management configuration and usage
import numpy as np
from manager import RiskMetrics, RiskLevel
import logging
import yaml
from manager import RiskManager

# # Configuration (would typically be in YAML)
# RISK_CONFIG = {
#     # Portfolio level limits
#     'max_portfolio_risk': 0.02,  # 2% max portfolio VaR
#     'max_position_risk': 0.01,   # 1% max per position  
#     'max_drawdown_limit': 0.15,  # 15% max drawdown before stopping
#     'max_correlation_exposure': 0.5,  # 50% max correlated exposure
    
#     # Position sizing
#     'position_sizing': {
#         'method': 'volatility_adjusted',  # kelly_criterion, volatility_adjusted, fixed_percent, risk_parity
#         'base_risk_per_trade': 0.01,     # 1% base risk per trade
#         'max_position_size': 0.1,        # 10% max position size
#         'volatility_lookback': 20,       # Days for volatility calculation
#         'kelly_safety_factor': 0.25      # Use 25% of Kelly fraction
#     },
    
#     # Stop loss management  
#     'stop_loss': {
#         'initial_stop_pct': 0.05,        # 5% initial stop loss
#         'trailing_stop_pct': 0.03,       # 3% trailing stop
#         'take_profit_ratio': 2.0,        # 2:1 profit/loss ratio
#         'breakeven_threshold': 0.02      # 2% gain before trailing activates
#     },
    
#     # Portfolio monitoring
#     'portfolio_risk': {
#         'var_confidence': 0.05,          # 95% VaR confidence
#         'correlation_threshold': 0.7      # High correlation threshold
#     },
    
#     # Emergency controls
#     'max_daily_loss': 0.05,             # 5% max daily loss
#     'flash_crash_threshold': 0.15,      # 15% flash crash threshold
#     'correlation_spike': 0.9             # 90% correlation spike threshold
# }

# Usage Example 

def example_usage():
    """Example of how to use the risk management system"""
    #read configuration from YAML file
    with open('config/risk.yaml', 'r') as file:
        RISK_CONFIG = yaml.safe_load(file)
    # Initialize risk manager
    risk_manager = RiskManager(RISK_CONFIG)
    
    # Example portfolio
    current_portfolio = {
        'BTCUSDT': 50000,   # $50k in BTC
        'ETHUSDT': 30000,   # $30k in ETH  
        'ADAUSDT': 10000,   # $10k in ADA
        'USDT': 10000       # $10k cash
    }
    
    # Example trading signal
    signal = {
        'symbol': 'SOLUSDT',
        'signal_strength': 0.8,   # 80% strength
        'volatility': 0.06,       # 6% daily volatility
        'win_rate': 0.55,         # 55% historical win rate
        'avg_win': 0.03,          # 3% average win
        'avg_loss': -0.015        # 1.5% average loss
    }
    
    # Evaluate if we should take this trade
    approved, position_size, reason = risk_manager.evaluate_trade_risk(
        symbol='SOLUSDT',
        signal=signal,
        current_portfolio=current_portfolio
    )
    
    print(f"Trade approved: {approved}")
    print(f"Position size: {position_size:.1%}")
    print(f"Reason: {reason}")
    
    # Get current risk metrics
    risk_metrics = risk_manager.get_risk_metrics(current_portfolio)
    print(f"Portfolio VaR: {risk_metrics.portfolio_var:.2%}")
    print(f"Risk Level: {risk_metrics.risk_level.value}")
    
    # Calculate stop losses for approved trade
    if approved:
        stop_levels = risk_manager.stop_loss_manager.calculate_stop_levels(
            symbol='SOLUSDT',
            entry_price=100.0,  # $100 entry price
            position_side='LONG',
            volatility=signal['volatility']
        )
        
        print(f"Stop Loss: ${stop_levels['stop_loss']:.2f}")
        print(f"Take Profit: ${stop_levels['take_profit']:.2f}")

if __name__ == "__main__":
    example_usage()