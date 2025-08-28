# src/risk/metrics.py
"""
Risk Metrics Calculations
Uses NumPy and SciPy for statistical calculations
"""

import numpy as np
from scipy import stats
from typing import Dict, List, Tuple, Optional
import pandas as pd
from datetime import datetime, timedelta
import logging

logger = logging.getLogger(__name__)

class RiskMetricsCalculator:
    """Calculate various risk metrics using established statistical libraries"""
    
    def __init__(self):
        pass
    
    def calculate_value_at_risk(self, returns: np.ndarray, 
                              confidence_level: float = 0.05,
                              method: str = 'parametric') -> float:
        """Calculate Value at Risk using different methods"""
        
        if len(returns) == 0:
            return 0.0
            
        if method == 'parametric':
            # Parametric VaR using normal distribution
            mean = np.mean(returns)
            std = np.std(returns)
            var = stats.norm.ppf(confidence_level, mean, std)
            
        elif method == 'historical':
            # Historical VaR
            var = np.percentile(returns, confidence_level * 100)
            
        elif method == 'monte_carlo':
            # Monte Carlo VaR (simplified)
            mean = np.mean(returns)
            std = np.std(returns)
            simulations = np.random.normal(mean, std, 10000)
            var = np.percentile(simulations, confidence_level * 100)
            
        else:
            raise ValueError(f"Unknown VaR method: {method}")
            
        return abs(var)  # Return positive value
    
    def calculate_expected_shortfall(self, returns: np.ndarray,
                                   confidence_level: float = 0.05) -> float:
        """Calculate Expected Shortfall (Conditional VaR)"""
        
        if len(returns) == 0:
            return 0.0
            
        # Sort returns and find the tail
        sorted_returns = np.sort(returns)
        cutoff_index = int(confidence_level * len(sorted_returns))
        
        if cutoff_index == 0:
            return abs(sorted_returns[0])
            
        # Average of the worst returns in the tail
        tail_returns = sorted_returns[:cutoff_index]
        expected_shortfall = np.mean(tail_returns)
        
        return abs(expected_shortfall)
    
    def calculate_sharpe_ratio(self, returns: np.ndarray, 
                             risk_free_rate: float = 0.02) -> float:
        """Calculate Sharpe ratio"""
        
        if len(returns) == 0:
            return 0.0
            
        mean_return = np.mean(returns)
        std_return = np.std(returns)
        
        if std_return == 0:
            return 0.0
            
        # Annualized Sharpe ratio (assuming daily returns)
        excess_return = mean_return * 252 - risk_free_rate
        annualized_volatility = std_return * np.sqrt(252)
        
        return excess_return / annualized_volatility
    
    def calculate_sortino_ratio(self, returns: np.ndarray,
                              risk_free_rate: float = 0.02) -> float:
        """Calculate Sortino ratio (downside deviation)"""
        
        if len(returns) == 0:
            return 0.0
            
        mean_return = np.mean(returns)
        
        # Calculate downside deviation
        negative_returns = returns[returns < 0]
        if len(negative_returns) == 0:
            return float('inf')  # No downside risk
            
        downside_deviation = np.std(negative_returns)
        
        if downside_deviation == 0:
            return 0.0
            
        # Annualized Sortino ratio
        excess_return = mean_return * 252 - risk_free_rate
        annualized_downside_dev = downside_deviation * np.sqrt(252)
        
        return excess_return / annualized_downside_dev
    
    def calculate_maximum_drawdown(self, equity_curve: np.ndarray) -> Dict[str, float]:
        """Calculate maximum drawdown and related metrics"""
        
        if len(equity_curve) == 0:
            return {'max_drawdown': 0.0, 'drawdown_duration': 0, 'recovery_time': 0}
            
        # Calculate running maximum (peak)
        peak = np.maximum.accumulate(equity_curve)
        
        # Calculate drawdown
        drawdown = (equity_curve - peak) / peak
        
        # Find maximum drawdown
        max_drawdown = np.min(drawdown)
        max_dd_index = np.argmin(drawdown)
        
        # Find peak before max drawdown
        peak_before_dd = np.argmax(peak[:max_dd_index + 1])
        
        # Calculate drawdown duration
        drawdown_duration = max_dd_index - peak_before_dd
        
        # Find recovery point (if any)
        recovery_point = None
        for i in range(max_dd_index, len(equity_curve)):
            if equity_curve[i] >= peak[max_dd_index]:
                recovery_point = i
                break
                
        recovery_time = (recovery_point - max_dd_index) if recovery_point else len(equity_curve) - max_dd_index
        
        return {
            'max_drawdown': abs(max_drawdown),
            'drawdown_duration': drawdown_duration,
            'recovery_time': recovery_time,
            'current_drawdown': abs(drawdown[-1])
        }
    
    def calculate_beta(self, asset_returns: np.ndarray, 
                      market_returns: np.ndarray) -> float:
        """Calculate beta relative to market"""
        
        if len(asset_returns) != len(market_returns) or len(asset_returns) == 0:
            return 1.0
            
        # Use numpy for linear regression
        covariance = np.cov(asset_returns, market_returns)[0, 1]
        market_variance = np.var(market_returns)
        
        if market_variance == 0:
            return 1.0
            
        beta = covariance / market_variance
        return beta
    
    def calculate_alpha(self, asset_returns: np.ndarray,
                       market_returns: np.ndarray,
                       risk_free_rate: float = 0.02) -> float:
        """Calculate alpha (excess return over CAPM)"""
        
        if len(asset_returns) != len(market_returns) or len(asset_returns) == 0:
            return 0.0
            
        beta = self.calculate_beta(asset_returns, market_returns)
        
        asset_mean = np.mean(asset_returns) * 252  # Annualized
        market_mean = np.mean(market_returns) * 252  # Annualized
        
        expected_return = risk_free_rate + beta * (market_mean - risk_free_rate)
        alpha = asset_mean - expected_return
        
        return alpha
    
    def calculate_information_ratio(self, asset_returns: np.ndarray,
                                  benchmark_returns: np.ndarray) -> float:
        """Calculate Information Ratio"""
        
        if len(asset_returns) != len(benchmark_returns) or len(asset_returns) == 0:
            return 0.0
            
        # Active return
        active_returns = asset_returns - benchmark_returns
        
        mean_active_return = np.mean(active_returns)
        tracking_error = np.std(active_returns)
        
        if tracking_error == 0:
            return 0.0
            
        # Annualized Information Ratio
        information_ratio = (mean_active_return * 252) / (tracking_error * np.sqrt(252))
        
        return information_ratio
    
    def calculate_calmar_ratio(self, returns: np.ndarray) -> float:
        """Calculate Calmar Ratio (annual return / max drawdown)"""
        
        if len(returns) == 0:
            return 0.0
            
        # Calculate annualized return
        cumulative_return = np.prod(1 + returns)
        years = len(returns) / 252  # Assuming daily returns
        annualized_return = (cumulative_return ** (1/years)) - 1
        
        # Calculate maximum drawdown
        equity_curve = np.cumprod(1 + returns)
        dd_metrics = self.calculate_maximum_drawdown(equity_curve)
        max_drawdown = dd_metrics['max_drawdown']
        
        if max_drawdown == 0:
            return float('inf')
            
        return annualized_return / max_drawdown
    
    def calculate_volatility_metrics(self, returns: np.ndarray) -> Dict[str, float]:
        """Calculate various volatility metrics"""
        
        if len(returns) == 0:
            return {}
            
        return {
            'daily_volatility': np.std(returns),
            'annualized_volatility': np.std(returns) * np.sqrt(252),
            'rolling_vol_20d': np.std(returns[-20:]) if len(returns) >= 20 else np.std(returns),
            'rolling_vol_50d': np.std(returns[-50:]) if len(returns) >= 50 else np.std(returns),
            'upside_volatility': np.std(returns[returns > 0]) if len(returns[returns > 0]) > 0 else 0,
            'downside_volatility': np.std(returns[returns < 0]) if len(returns[returns < 0]) > 0 else 0
        }
    
    def calculate_correlation_matrix(self, returns_data: Dict[str, np.ndarray]) -> Tuple[List[str], np.ndarray]:
        """Calculate correlation matrix for multiple assets"""
        
        symbols = list(returns_data.keys())
        
        if len(symbols) < 2:
            return symbols, np.array([[1.0]])
            
        # Find common length
        min_length = min(len(returns) for returns in returns_data.values())
        
        # Create return matrix
        return_matrix = np.array([returns_data[symbol][-min_length:] for symbol in symbols])
        
        # Calculate correlation matrix using numpy
        correlation_matrix = np.corrcoef(return_matrix)
        
        return symbols, correlation_matrix
    
    def calculate_portfolio_risk(self, weights: np.ndarray, 
                               correlation_matrix: np.ndarray,
                               volatilities: np.ndarray) -> float:
        """Calculate portfolio risk using modern portfolio theory"""
        
        if len(weights) != len(volatilities) or len(weights) != correlation_matrix.shape[0]:
            raise ValueError("Dimension mismatch in portfolio risk calculation")
            
        # Create covariance matrix
        covariance_matrix = correlation_matrix * np.outer(volatilities, volatilities)
        
        # Portfolio variance: w^T * Σ * w
        portfolio_variance = np.dot(weights, np.dot(covariance_matrix, weights))
        
        return np.sqrt(portfolio_variance)