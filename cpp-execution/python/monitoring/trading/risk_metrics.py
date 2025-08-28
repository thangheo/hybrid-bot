"""
Risk Metrics Module

This module calculates risk metrics for trading strategies, including VaR, expected shortfall,
and correlation analysis.

Classes:
    RiskMetricsCalculator: Main class for risk metrics calculation
"""

import numpy as np
import pandas as pd
from scipy import stats
from collections import defaultdict


class RiskMetricsCalculator:
    """
    Calculates risk metrics for trading strategies.
    
    Attributes:
        strategy_returns (dict): Daily returns by strategy
        risk_metrics (dict): Risk metrics by strategy
        confidence_level (float): Confidence level for VaR calculations
        correlation_matrix (pd.DataFrame): Correlation matrix between strategies
    """
    
    def __init__(self, confidence_level=0.95):
        """
        Initialize the risk metrics calculator.
        
        Args:
            confidence_level (float): Confidence level for VaR calculations
        """
        self.strategy_returns = {}
        self.risk_metrics = {}
        self.confidence_level = confidence_level
        self.correlation_matrix = None
    
    def add_returns(self, strategy_id, returns):
        """
        Add returns for a strategy.
        
        Args:
            strategy_id (str): Strategy identifier
            returns (pd.Series): Daily returns series
        """
        self.strategy_returns[strategy_id] = returns
        
        # Recalculate correlation matrix when new data is added
        self._update_correlation_matrix()
    
    def _update_correlation_matrix(self):
        """Update the correlation matrix between strategies."""
        if len(self.strategy_returns) < 2:
            self.correlation_matrix = None
            return
        
        # Combine returns into a DataFrame
        returns_df = pd.DataFrame(self.strategy_returns)
        
        # Calculate correlation matrix
        self.correlation_matrix = returns_df.corr()
    
    def calculate_var(self, strategy_id, confidence_level=None, lookback_days=None):
        """
        Calculate Value at Risk (VaR) for a strategy.
        
        Args:
            strategy_id (str): Strategy identifier
            confidence_level (float): Optional confidence level (overrides default)
            lookback_days (int): Optional lookback period in days
        
        Returns:
            float: VaR value
        """
        if strategy_id not in self.strategy_returns:
            return 0
        
        returns = self.strategy_returns[strategy_id]
        
        if lookback_days:
            returns = returns.iloc[-lookback_days:]
        
        if len(returns) == 0:
            return 0
        
        conf_level = confidence_level or self.confidence_level
        
        # Calculate VaR using historical method
        var = returns.quantile(1 - conf_level)
        
        return abs(var)
    
    def calculate_expected_shortfall(self, strategy_id, confidence_level=None, lookback_days=None):
        """
        Calculate Expected Shortfall (Conditional VaR) for a strategy.
        
        Args:
            strategy_id (str): Strategy identifier
            confidence_level (float): Optional confidence level (overrides default)
            lookback_days (int): Optional lookback period in days
        
        Returns:
            float: Expected Shortfall value
        """
        if strategy_id not in self.strategy_returns:
            return 0
        
        returns = self.strategy_returns[strategy_id]
        
        if lookback_days:
            returns = returns.iloc[-lookback_days:]
        
        if len(returns) == 0:
            return 0
        
        conf_level = confidence_level or self.confidence_level
        
        # Calculate VaR
        var = self.calculate_var(strategy_id, conf_level, lookback_days)
        
        # Calculate Expected Shortfall (mean of returns beyond VaR)
        es_returns = returns[returns < -var]
        
        if len(es_returns) == 0:
            return var  # Fall back to VaR if no returns beyond VaR
        
        expected_shortfall = abs(es_returns.mean())
        
        return expected_shortfall
    
    def calculate_all_risk_metrics(self, strategy_id=None, lookback_days=None):
        """
        Calculate comprehensive risk metrics for strategies.
        
        Args:
            strategy_id (str): Optional strategy filter
            lookback_days (int): Optional lookback period in days
        
        Returns:
            dict: Risk metrics
        """
        if strategy_id:
            return self._calculate_strategy_risk_metrics(strategy_id, lookback_days)
        
        # Calculate for all strategies
        results = {}
        
        for sid in self.strategy_returns:
            results[sid] = self._calculate_strategy_risk_metrics(sid, lookback_days)
        
        return results
    
    def _calculate_strategy_risk_metrics(self, strategy_id, lookback_days=None):
        """
        Calculate risk metrics for a specific strategy.
        
        Args:
            strategy_id (str): Strategy identifier
            lookback_days (int): Optional lookback period in days
        
        Returns:
            dict: Risk metrics
        """
        if strategy_id not in self.strategy_returns:
            return {
                'strategy_id': strategy_id,
                'var': 0,
                'expected_shortfall': 0,
                'volatility': 0,
                'downside_deviation': 0,
                'max_drawdown': 0,
                'correlation_with_market': 0
            }
        
        returns = self.strategy_returns[strategy_id]
        
        if lookback_days:
            returns = returns.iloc[-lookback_days:]
        
        if len(returns) == 0:
            return {
                'strategy_id': strategy_id,
                'var': 0,
                'expected_shortfall': 0,
                'volatility': 0,
                'downside_deviation': 0,
                'max_drawdown': 0,
                'correlation_with_market': 0
            }
        
        # Calculate VaR and Expected Shortfall
        var = self.calculate_var(strategy_id, lookback_days=lookback_days)
        es = self.calculate_expected_shortfall(strategy_id, lookback_days=lookback_days)
        
        # Calculate volatility (standard deviation of returns)
        volatility = returns.std()
        
        # Calculate downside deviation (std. dev. of negative returns)
        downside_returns = returns[returns < 0]
        downside_deviation = downside_returns.std() if len(downside_returns) > 0 else 0
        
        # Calculate maximum drawdown
        equity = returns.cumsum()
        rolling_max = equity.cummax()
        drawdown = (rolling_max - equity) / rolling_max
        max_drawdown = drawdown.max() if len(drawdown) > 0 else 0
        
        # Calculate correlation with "market" (if available)
        # Placeholder for actual market correlation calculation
        correlation_with_market = 0
        
        # Save and return metrics
        metrics = {
            'strategy_id': strategy_id,
            'var': var,
            'expected_shortfall': es,
            'volatility': volatility,
            'downside_deviation': downside_deviation,
            'max_drawdown': max_drawdown,
            'correlation_with_market': correlation_with_market
        }
        
        self.risk_metrics[strategy_id] = metrics
        
        return metrics
    
    def run_stress_test(self, strategy_id, scenario):
        """
        Run a stress test for a strategy.
        
        Args:
            strategy_id (str): Strategy identifier
            scenario (dict): Stress test scenario parameters
        
        Returns:
            dict: Stress test results
        """
        # TODO: Implement stress test scenarios
        return {}
    
    def get_correlation_matrix(self):
        """
        Get the correlation matrix between strategies.
        
        Returns:
            pd.DataFrame: Correlation matrix
        """
        return self.correlation_matrix
    
    def calculate_correlation_with_factor(self, strategy_id, factor_returns):
        """
        Calculate correlation between a strategy and a factor.
        
        Args:
            strategy_id (str): Strategy identifier
            factor_returns (pd.Series): Factor returns series
        
        Returns:
            float: Correlation coefficient
        """
        if strategy_id not in self.strategy_returns:
            return 0
        
        returns = self.strategy_returns[strategy_id]
        
        # Align dates
        aligned_data = pd.concat([returns, factor_returns], axis=1).dropna()
        
        if len(aligned_data) < 2:
            return 0
        
        correlation = aligned_data.corr().iloc[0, 1]
        
        return correlation
