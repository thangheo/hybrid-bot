#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PnL Analyzer module for analyzing trading profit and loss metrics.
"""

import asyncio
from datetime import datetime, timedelta
import pandas as pd
import numpy as np
from typing import Dict, List, Optional, Any, Tuple, Set

from ..database.timeseries_db import TimeSeriesDB
from ..database.operational_db import OperationalDB
from ..utils.time_utils import timestamp_to_datetime, datetime_to_timestamp
from ..utils.math_utils import calculate_sharpe_ratio, calculate_sortino_ratio, calculate_drawdown
from .analyzer_base import AnalyzerBase

class PnLAnalyzer(AnalyzerBase):
    """
    Analyzer for trading Profit and Loss (PnL) metrics.
    
    This analyzer processes historical PnL data to calculate performance metrics,
    identify trends, and detect anomalies in trading performance.
    """
    
    def __init__(self, config: Dict[str, Any], 
                 timeseries_db: TimeSeriesDB,
                 operational_db: OperationalDB):
        """
        Initialize the PnL analyzer with database connections.
        
        Args:
            config: Configuration dictionary
            timeseries_db: Time series database connector for metrics
            operational_db: Operational database connector for trading data
        """
        super().__init__(config, timeseries_db)
        self.operational_db = operational_db
        self.risk_free_rate = self.config.get('risk_free_rate', 0.02)  # Annual risk-free rate
        self.pnl_metric_name = self.config.get('pnl_metric_name', 'trading.pnl')
        self.position_metric_name = self.config.get('position_metric_name', 'trading.position')
        self.strategy_metrics = self.config.get('strategy_metrics', {
            'sharpe_ratio': True,
            'sortino_ratio': True,
            'max_drawdown': True,
            'win_rate': True,
            'profit_factor': True,
            'avg_trade_duration': True
        })
        self.analysis_timeframes = self.config.get('analysis_timeframes', [
            'hourly', 'daily', 'weekly', 'monthly'
        ])
        self.min_trades_for_analysis = self.config.get('min_trades_for_analysis', 10)
    
    async def analyze(self, start_time: datetime, end_time: datetime) -> Dict[str, Any]:
        """
        Analyze PnL data within the specified time window.
        
        Args:
            start_time: Start of analysis window
            end_time: End of analysis window
            
        Returns:
            Dictionary containing analysis results
        """
        self.logger.info(f"Analyzing PnL from {start_time} to {end_time}")
        
        # Fetch raw PnL data
        pnl_data = await self._fetch_pnl_data(start_time, end_time)
        
        if pnl_data.empty:
            self.logger.warning("No PnL data found in the specified time window")
            return {"error": "No PnL data found", "status": "error"}
        
        # Fetch trade data
        trade_data = await self._fetch_trade_data(start_time, end_time)
        
        # Get active strategies
        strategies = self._get_strategies(pnl_data)
        
        # Get active trading pairs
        trading_pairs = self._get_trading_pairs(pnl_data)
        
        # Overall performance metrics
        overall_metrics = await self._calculate_overall_metrics(pnl_data, trade_data)
        
        # Strategy-specific metrics
        strategy_metrics = await self._calculate_strategy_metrics(pnl_data, trade_data, strategies)
        
        # Trading pair metrics
        pair_metrics = await self._calculate_pair_metrics(pnl_data, trade_data, trading_pairs)
        
        # Time-based analysis
        time_analysis = self._analyze_time_patterns(pnl_data)
        
        # Market condition analysis
        market_analysis = await self._analyze_market_conditions(pnl_data, start_time, end_time)
        
        # Anomaly detection
        anomalies = self._detect_pnl_anomalies(pnl_data)
        
        # Strategy correlation analysis
        strategy_correlation = self._calculate_strategy_correlation(pnl_data, strategies)
        
        # Generate recommendations
        recommendations = self._generate_recommendations(
            overall_metrics, strategy_metrics, pair_metrics, anomalies, market_analysis
        )
        
        self.last_analysis_time = datetime.now()
        
        return {
            "status": "success",
            "analysis_period": {
                "start": start_time,
                "end": end_time,
                "duration_days": (end_time - start_time).days
            },
            "overall_metrics": overall_metrics,
            "strategy_metrics": strategy_metrics,
            "trading_pair_metrics": pair_metrics,
            "time_analysis": time_analysis,
            "market_condition_analysis": market_analysis,
            "anomalies": anomalies,
            "strategy_correlation": strategy_correlation,
            "recommendations": recommendations
        }
    
    async def _fetch_pnl_data(self, start_time: datetime, end_time: datetime) -> pd.DataFrame:
        """
        Fetch PnL data from time series database.
        
        Args:
            start_time: Start of analysis window
            end_time: End of analysis window
            
        Returns:
            DataFrame containing PnL data
        """
        try:
            # Fetch raw PnL data
            pnl_data = await self.fetch_data(
                self.pnl_metric_name,
                start_time,
                end_time
            )
            
            if pnl_data.empty:
                return pd.DataFrame()
            
            # Ensure we have the necessary columns
            required_columns = ['timestamp', 'value', 'strategy', 'trading_pair', 'exchange']
            for col in required_columns:
                if col not in pnl_data.columns:
                    if col == 'value':
                        self.logger.error(f"Required column '{col}' not found in PnL data")
                        return pd.DataFrame()
                    else:
                        pnl_data[col] = 'unknown'
            
            # Convert timestamp to datetime if it's not already
            if not pd.api.types.is_datetime64_any_dtype(pnl_data['timestamp']):
                pnl_data['timestamp'] = pd.to_datetime(pnl_data['timestamp'])
            
            # Sort by timestamp
            pnl_data = pnl_data.sort_values('timestamp')
            
            return pnl_data
        
        except Exception as e:
            self.logger.error(f"Error fetching PnL data: {str(e)}")
            return pd.DataFrame()
    
    async def _fetch_trade_data(self, start_time: datetime, end_time: datetime) -> pd.DataFrame:
        """
        Fetch trade data from operational database.
        
        Args:
            start_time: Start of analysis window
            end_time: End of analysis window
            
        Returns:
            DataFrame containing trade data
        """
        try:
            query = """
                SELECT 
                    id, order_id, strategy_id, trading_pair, exchange_id,
                    side, quantity, price, fee, fee_currency,
                    created_at, completed_at, status, pnl
                FROM trades
                WHERE created_at BETWEEN %s AND %s
                ORDER BY created_at
            """
            params = (datetime_to_timestamp(start_time), datetime_to_timestamp(end_time))
            
            result = await self.operational_db.execute_query(query, params)
            
            if not result:
                return pd.DataFrame()
            
            # Convert to DataFrame
            df = pd.DataFrame(result)
            
            # Convert timestamps to datetime
            time_columns = ['created_at', 'completed_at']
            for col in time_columns:
                if col in df.columns:
                    df[col] = df[col].apply(timestamp_to_datetime)
            
            return df
        
        except Exception as e:
            self.logger.error(f"Error fetching trade data: {str(e)}")
            return pd.DataFrame()
    
    def _get_strategies(self, pnl_data: pd.DataFrame) -> List[str]:
        """
        Extract unique strategy names from PnL data.
        
        Args:
            pnl_data: DataFrame containing PnL data
            
        Returns:
            List of strategy names
        """
        if pnl_data.empty or 'strategy' not in pnl_data.columns:
            return []
        
        return sorted(pnl_data['strategy'].unique().tolist())
    
    def _get_trading_pairs(self, pnl_data: pd.DataFrame) -> List[str]:
        """
        Extract unique trading pairs from PnL data.
        
        Args:
            pnl_data: DataFrame containing PnL data
            
        Returns:
            List of trading pairs
        """
        if pnl_data.empty or 'trading_pair' not in pnl_data.columns:
            return []
        
        return sorted(pnl_data['trading_pair'].unique().tolist())
    
    async def _calculate_overall_metrics(self, pnl_data: pd.DataFrame, 
                                        trade_data: pd.DataFrame) -> Dict[str, Any]:
        """
        Calculate overall performance metrics.
        
        Args:
            pnl_data: DataFrame containing PnL data
            trade_data: DataFrame containing trade data
            
        Returns:
            Dictionary with overall performance metrics
        """
        metrics = {}
        
        try:
            # Basic PnL metrics
            if not pnl_data.empty and 'value' in pnl_data.columns:
                total_pnl = pnl_data['value'].sum()
                avg_daily_pnl = self._calculate_average_daily_pnl(pnl_data)
                
                metrics.update({
                    "total_pnl": float(total_pnl),
                    "average_daily_pnl": float(avg_daily_pnl),
                    "profitable_days_percent": self._calculate_profitable_days_percent(pnl_data),
                    "best_day": self._find_best_day(pnl_data),
                    "worst_day": self._find_worst_day(pnl_data)
                })
            
            # Trade metrics
            if not trade_data.empty:
                completed_trades = trade_data[trade_data['status'] == 'completed']
                
                if not completed_trades.empty:
                    profitable_trades = completed_trades[completed_trades['pnl'] > 0]
                    
                    metrics.update({
                        "total_trades": len(completed_trades),
                        "profitable_trades": len(profitable_trades),
                        "win_rate": len(profitable_trades) / len(completed_trades) if len(completed_trades) > 0 else 0,
                        "average_trade_pnl": float(completed_trades['pnl'].mean() if 'pnl' in completed_trades.columns else 0),
                        "best_trade": self._find_best_trade(completed_trades),
                        "worst_trade": self._find_worst_trade(completed_trades)
                    })
            
            # Risk metrics
            if not pnl_data.empty and 'value' in pnl_data.columns:
                daily_returns = self._calculate_daily_returns(pnl_data)
                
                if not daily_returns.empty:
                    metrics.update({
                        "sharpe_ratio": calculate_sharpe_ratio(daily_returns, self.risk_free_rate),
                        "sortino_ratio": calculate_sortino_ratio(daily_returns, self.risk_free_rate),
                        "volatility": float(daily_returns.std()),
                        "max_drawdown": calculate_drawdown(daily_returns)
                    })
            
            # Additional metrics
            if not pnl_data.empty and not trade_data.empty:
                metrics.update({
                    "profit_factor": self._calculate_profit_factor(completed_trades) if 'completed_trades' in locals() else 0,
                    "average_holding_time": self._calculate_average_holding_time(completed_trades) if 'completed_trades' in locals() else 0,
                    "inactive_days": self._count_inactive_days(pnl_data),
                    "pnl_consistency": self._calculate_pnl_consistency(pnl_data)
                })
        
        except Exception as e:
            self.logger.error(f"Error calculating overall metrics: {str(e)}")
        
        return metrics
    
    async def _calculate_strategy_metrics(self, pnl_data: pd.DataFrame, 
                                         trade_data: pd.DataFrame,
                                         strategies: List[str]) -> Dict[str, Dict[str, Any]]:
        """
        Calculate performance metrics for each strategy.
        
        Args:
            pnl_data: DataFrame containing PnL data
            trade_data: DataFrame containing trade data
            strategies: List of strategy names
            
        Returns:
            Dictionary with metrics for each strategy
        """
        strategy_metrics = {}
        
        for strategy in strategies:
            try:
                # Filter data for this strategy
                strategy_pnl = pnl_data[pnl_data['strategy'] == strategy]
                
                strategy_trades = trade_data
                if not trade_data.empty and 'strategy_id' in trade_data.columns:
                    strategy_trades = trade_data[trade_data['strategy_id'] == strategy]
                
                # Skip if not enough data
                if strategy_pnl.empty:
                    continue
                
                # Calculate metrics
                total_pnl = strategy_pnl['value'].sum()
                
                metrics = {
                    "total_pnl": float(total_pnl),
                    "contribution_percent": 0,  # Will calculate after processing all strategies
                    "average_daily_pnl": float(self._calculate_average_daily_pnl(strategy_pnl))
                }
                
                # Calculate trade metrics if we have trade data
                if not strategy_trades.empty:
                    completed_trades = strategy_trades[strategy_trades['status'] == 'completed']
                    
                    if not completed_trades.empty and len(completed_trades) >= self.min_trades_for_analysis:
                        profitable_trades = completed_trades[completed_trades['pnl'] > 0]
                        
                        metrics.update({
                            "total_trades": len(completed_trades),
                            "win_rate": len(profitable_trades) / len(completed_trades) if len(completed_trades) > 0 else 0,
                            "profit_factor": self._calculate_profit_factor(completed_trades),
                            "average_trade_pnl": float(completed_trades['pnl'].mean()) if 'pnl' in completed_trades.columns else 0
                        })
                
                # Calculate risk metrics
                daily_returns = self._calculate_daily_returns(strategy_pnl)
                
                if not daily_returns.empty:
                    metrics.update({
                        "sharpe_ratio": calculate_sharpe_ratio(daily_returns, self.risk_free_rate),
                        "sortino_ratio": calculate_sortino_ratio(daily_returns, self.risk_free_rate),
                        "volatility": float(daily_returns.std()),
                        "max_drawdown": calculate_drawdown(daily_returns)
                    })
                
                # Track active trading pairs
                if 'trading_pair' in strategy_pnl.columns:
                    metrics["active_pairs"] = strategy_pnl['trading_pair'].unique().tolist()
                
                # Add to results
                strategy_metrics[strategy] = metrics
            
            except Exception as e:
                self.logger.error(f"Error calculating metrics for strategy {strategy}: {str(e)}")
        
        # Calculate contribution percentages
        total_system_pnl = sum(metrics["total_pnl"] for metrics in strategy_metrics.values())
        if total_system_pnl != 0:
            for strategy, metrics in strategy_metrics.items():
                metrics["contribution_percent"] = (metrics["total_pnl"] / total_system_pnl) * 100
        
        return strategy_metrics
    
    async def _calculate_pair_metrics(self, pnl_data: pd.DataFrame, 
                                     trade_data: pd.DataFrame,
                                     trading_pairs: List[str]) -> Dict[str, Dict[str, Any]]:
        """
        Calculate performance metrics for each trading pair.
        
        Args:
            pnl_data: DataFrame containing PnL data
            trade_data: DataFrame containing trade data
            trading_pairs: List of trading pairs
            
        Returns:
            Dictionary with metrics for each trading pair
        """
        pair_metrics = {}
        
        for pair in trading_pairs:
            try:
                # Filter data for this trading pair
                pair_pnl = pnl_data[pnl_data['trading_pair'] == pair]
                
                pair_trades = trade_data
                if not trade_data.empty and 'trading_pair' in trade_data.columns:
                    pair_trades = trade_data[trade_data['trading_pair'] == pair]
                
                # Skip if not enough data
                if pair_pnl.empty:
                    continue
                
                # Calculate metrics
                total_pnl = pair_pnl['value'].sum()
                
                metrics = {
                    "total_pnl": float(total_pnl),
                    "average_daily_pnl": float(self._calculate_average_daily_pnl(pair_pnl))
                }
                
                # Calculate trade metrics if we have trade data
                if not pair_trades.empty:
                    completed_trades = pair_trades[pair_trades['status'] == 'completed']
                    
                    if not completed_trades.empty and len(completed_trades) >= self.min_trades_for_analysis:
                        profitable_trades = completed_trades[completed_trades['pnl'] > 0]
                        
                        metrics.update({
                            "total_trades": len(completed_trades),
                            "win_rate": len(profitable_trades) / len(completed_trades) if len(completed_trades) > 0 else 0,
                            "profit_factor": self._calculate_profit_factor(completed_trades),
                            "average_trade_pnl": float(completed_trades['pnl'].mean()) if 'pnl' in completed_trades.columns else 0
                        })
                
                # Calculate risk metrics
                daily_returns = self._calculate_daily_returns(pair_pnl)
                
                if not daily_returns.empty:
                    metrics.update({
                        "sharpe_ratio": calculate_sharpe_ratio(daily_returns, self.risk_free_rate),
                        "volatility": float(daily_returns.std()),
                        "max_drawdown": calculate_drawdown(daily_returns)
                    })
                
                # Track active strategies for this pair
                if 'strategy' in pair_pnl.columns:
                    metrics["active_strategies"] = pair_pnl['strategy'].unique().tolist()
                
                # Add to results
                pair_metrics[pair] = metrics
            
            except Exception as e:
                self.logger.error(f"Error calculating metrics for trading pair {pair}: {str(e)}")
        
        return pair_metrics
    
    def _analyze_time_patterns(self, pnl_data: pd.DataFrame) -> Dict[str, Any]:
        """
        Analyze time-based patterns in PnL data.
        
        Args:
            pnl_data: DataFrame containing PnL data
            
        Returns:
            Dictionary with time pattern analysis
        """
        if pnl_data.empty or 'timestamp' not in pnl_data.columns or 'value' not in pnl_data.columns:
            return {}
        
        try:
            # Ensure timestamp column is datetime type
            if not pd.api.types.is_datetime64_any_dtype(pnl_data['timestamp']):
                pnl_data['timestamp'] = pd.to_datetime(pnl_data['timestamp'])
            
            # Create time-based features
            pnl_data['hour'] = pnl_data['timestamp'].dt.hour
            pnl_data['day'] = pnl_data['timestamp'].dt.day_name()
            pnl_data['month'] = pnl_data['timestamp'].dt.month_name()
            pnl_data['year'] = pnl_data['timestamp'].dt.year
            pnl_data['week'] = pnl_data['timestamp'].dt.isocalendar().week
            
            # Hourly analysis
            hourly_pnl = pnl_data.groupby('hour')['value'].sum()
            best_hour = hourly_pnl.idxmax() if not hourly_pnl.empty else None
            worst_hour = hourly_pnl.idxmin() if not hourly_pnl.empty else None
            
            # Daily analysis
            day_order = ['Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday', 'Sunday']
            daily_pnl = pnl_data.groupby('day')['value'].sum()
            daily_pnl = daily_pnl.reindex(day_order)
            best_day = daily_pnl.idxmax() if not daily_pnl.empty else None
            worst_day = daily_pnl.idxmin() if not daily_pnl.empty else None
            
            # Monthly analysis
            month_order = ['January', 'February', 'March', 'April', 'May', 'June', 
                          'July', 'August', 'September', 'October', 'November', 'December']
            monthly_pnl = pnl_data.groupby('month')['value'].sum()
            monthly_pnl = monthly_pnl.reindex(month_order)
            best_month = monthly_pnl.idxmax() if not monthly_pnl.empty else None
            worst_month = monthly_pnl.idxmin() if not monthly_pnl.empty else None
            
            # Weekly analysis (performance per week number)
            weekly_pnl = pnl_data.groupby(['year', 'week'])['value'].sum()
            
            # Calculate weekly volatility and stability
            weekly_volatility = weekly_pnl.std() if len(weekly_pnl) > 1 else 0
            weekly_stability = 1 - (weekly_volatility / abs(weekly_pnl.mean())) if abs(weekly_pnl.mean()) > 0 else 0
            
            # Return results
            return {
                "hourly_analysis": {
                    "best_hour": best_hour,
                    "worst_hour": worst_hour,
                    "hourly_pnl": {str(hour): float(pnl) for hour, pnl in hourly_pnl.items()}
                },
                "daily_analysis": {
                    "best_day": best_day,
                    "worst_day": worst_day,
                    "daily_pnl": {day: float(pnl) for day, pnl in daily_pnl.items()}
                },
                "monthly_analysis": {
                    "best_month": best_month,
                    "worst_month": worst_month,
                    "monthly_pnl": {month: float(pnl) for month, pnl in monthly_pnl.items()}
                },
                "weekly_analysis": {
                    "volatility": float(weekly_volatility),
                    "stability": float(weekly_stability),
                    "weeks_analyzed": len(weekly_pnl)
                }
            }
        except Exception as e:
            self.logger.error(f"Error analyzing time patterns: {str(e)}")
            return {}
    
    async def _analyze_market_conditions(self, pnl_data: pd.DataFrame, 
                                        start_time: datetime, end_time: datetime) -> Dict[str, Any]:
        """
        Analyze how market conditions affect PnL.
        
        Args:
            pnl_data: DataFrame containing PnL data
            start_time: Start of analysis window
            end_time: End of analysis window
            
        Returns:
            Dictionary with market condition analysis
        """
        if pnl_data.empty:
            return {}
        
        try:
            # Fetch market data (price, volume, volatility)
            market_metrics = {
                'price': 'market.price',
                'volume': 'market.volume',
                'volatility': 'market.volatility'
            }
            
            market_data = {}
            for name, metric in market_metrics.items():
                data = await self.fetch_data(metric, start_time, end_time)
                if not data.empty and 'value' in data.columns:
                    market_data[name] = data
            
            if not market_data:
                return {}
            
            # Align PnL data with market data
            aligned_data = pd.DataFrame()
            aligned_data['timestamp'] = pnl_data['timestamp']
            aligned_data['pnl'] = pnl_data['value']
            
            for name, data in market_data.items():
                # Resample market data to match PnL timestamps
                if not data.empty and 'timestamp' in data.columns and 'value' in data.columns:
                    data = data.set_index('timestamp')
                    resampled = data.resample('D').mean()
                    aligned_data[name] = aligned_data['timestamp'].map(
                        lambda ts: resampled.asof(ts)['value'] if ts in resampled.index else None
                    )
            
            # Calculate correlations
            correlations = {}
            for name in market_data.keys():
                if name in aligned_data.columns:
                    # Remove rows with missing data
                    valid_data = aligned_data.dropna(subset=['pnl', name])
                    if len(valid_data) > 1:
                        corr = valid_data['pnl'].corr(valid_data[name])
                        correlations[name] = float(corr)
            
            # Analyze market regimes
            regimes = {}
            for name in market_data.keys():
                if name in aligned_data.columns:
                    # Divide data into high/low regimes based on median
                    valid_data = aligned_data.dropna(subset=['pnl', name])
                    if len(valid_data) > 1:
                        median = valid_data[name].median()
                        high_regime = valid_data[valid_data[name] > median]
                        low_regime = valid_data[valid_data[name] <= median]
                        
                        regimes[name] = {
                            "high_regime": {
                                "avg_pnl": float(high_regime['pnl'].mean()),
                                "total_pnl": float(high_regime['pnl'].sum()),
                                "samples": len(high_regime)
                            },
                            "low_regime": {
                                "avg_pnl": float(low_regime['pnl'].mean()),
                                "total_pnl": float(low_regime['pnl'].sum()),
                                "samples": len(low_regime)
                            }
                        }
            
            return {
                "correlations": correlations,
                "market_regimes": regimes
            }
        
        except Exception as e:
            self.logger.error(f"Error analyzing market conditions: {str(e)}")
            return {}
    
    def _detect_pnl_anomalies(self, pnl_data: pd.DataFrame) -> Dict[str, Any]:
        """
        Detect anomalies in PnL data.
        
        Args:
            pnl_data: DataFrame containing PnL data
            
        Returns:
            Dictionary with detected anomalies
        """
        if pnl_data.empty or 'value' not in pnl_data.columns or 'timestamp' not in pnl_data.columns:
            return {}
        
        try:
            # Ensure timestamp column is datetime type
            if not pd.api.types.is_datetime64_any_dtype(pnl_data['timestamp']):
                pnl_data['timestamp'] = pd.to_datetime(pnl_data['timestamp'])
            
            # Group by day for daily PnL
            pnl_data['date'] = pnl_data['timestamp'].dt.date
            daily_pnl = pnl_data.groupby('date')['value'].sum().reset_index()
            
            # Apply anomaly detection
            anomalous_days = self.detect_anomalies(daily_pnl, 'daily_pnl', method='zscore')
            
            # Get anomalies by strategy
            strategy_anomalies = {}
            if 'strategy' in pnl_data.columns:
                for strategy in pnl_data['strategy'].unique():
                    strategy_data = pnl_data[pnl_data['strategy'] == strategy]
                    if not strategy_data.empty:
                        strategy_daily = strategy_data.groupby('date')['value'].sum().reset_index()
                        if len(strategy_daily) > 10:  # Need enough data points
                            strategy_anomalies[strategy] = self.detect_anomalies(
                                strategy_daily, f'strategy_{strategy}', method='zscore'
                            )
            
            # Get anomalies by trading pair
            pair_anomalies = {}
            if 'trading_pair' in pnl_data.columns:
                for pair in pnl_data['trading_pair'].unique():
                    pair_data = pnl_data[pnl_data['trading_pair'] == pair]
                    if not pair_data.empty:
                        pair_daily = pair_data.groupby('date')['value'].sum().reset_index()
                        if len(pair_daily) > 10:  # Need enough data points
                            pair_anomalies[pair] = self.detect_anomalies(
                                pair_daily, f'pair_{pair}', method='zscore'
                            )
            
            # Extract anomalies
            overall_anomalies = []
            if 'anomaly' in anomalous_days.columns:
                anomaly_days = anomalous_days[anomalous_days['anomaly']]
                for _, row in anomaly_days.iterrows():
                    overall_anomalies.append({
                        "date": row['date'],
                        "pnl": float(row['value']),
                        "anomaly_score": float(row['anomaly_score']) if 'anomaly_score' in row else 0
                    })
            
            # Format results
            result = {
                "anomalous_days": overall_anomalies,
                "anomalous_days_count": len(overall_anomalies),
                "strategy_anomalies": {},
                "pair_anomalies": {}
            }
            
            # Add strategy anomalies to result
            for strategy, anomalies in strategy_anomalies.items():
                if 'anomaly' in anomalies.columns:
                    strategy_anomaly_days = anomalies[anomalies['anomaly']]
                    result["strategy_anomalies"][strategy] = []
                    for _, row in strategy_anomaly_days.iterrows():
                        result["strategy_anomalies"][strategy].append({
                            "date": row['date'],
                            "pnl": float(row['value']),
                            "anomaly_score": float(row['anomaly_score']) if 'anomaly_score' in row else 0
                        })
            
            # Add pair anomalies to result
            for pair, anomalies in pair_anomalies.items():
                if 'anomaly' in anomalies.columns:
                    pair_anomaly_days = anomalies[anomalies['anomaly']]
                    result["pair_anomalies"][pair] = []
                    for _, row in pair_anomaly_days.iterrows():
                        result["pair_anomalies"][pair].append({
                            "date": row['date'],
                            "pnl": float(row['value']),
                            "anomaly_score": float(row['anomaly_score']) if 'anomaly_score' in row else 0
                        })
            
            return result
        
        except Exception as e:
            self.logger.error(f"Error detecting PnL anomalies: {str(e)}")
            return {}
    
    def _calculate_strategy_correlation(self, pnl_data: pd.DataFrame, 
                                      strategies: List[str]) -> Dict[str, Any]:
        """
        Calculate correlation between different strategies' PnL.
        
        Args:
            pnl_data: DataFrame containing PnL data
            strategies: List of strategy names
            
        Returns:
            Dictionary with strategy correlation analysis
        """
        if pnl_data.empty or 'strategy' not in pnl_data.columns or 'value' not in pnl_data.columns:
            return {}
        
        try:
            # Group by date and strategy for daily PnL by strategy
            pnl_data['date'] = pnl_data['timestamp'].dt.date
            daily_strategy_pnl = pnl_data.groupby(['date', 'strategy'])['value'].sum().reset_index()
            
            # Pivot to get strategies in columns
            strategy_matrix = daily_strategy_pnl.pivot(index='date', columns='strategy', values='value')
            
            # Calculate correlation matrix
            corr_matrix = strategy_matrix.corr()
            
            # Find highly correlated and anti-correlated pairs
            high_corr_pairs = []
            anti_corr_pairs = []
            
            for i, strategy1 in enumerate(corr_matrix.index):
                for j, strategy2 in enumerate(corr_matrix.columns):
                    if i < j:  # Only check each pair once
                        corr = corr_matrix.loc[strategy1, strategy2]
                        if corr >= 0.7:
                            high_corr_pairs.append({
                                "strategy1": strategy1,
                                "strategy2": strategy2,
                                "correlation": float(corr)
                            })
                        elif corr <= -0.5:
                            anti_corr_pairs.append({
                                "strategy1": strategy1,
                                "strategy2": strategy2,
                                "correlation": float(corr)
                            })
            
            # Calculate overall portfolio diversification score
            # Based on average absolute correlation between strategies
            if len(corr_matrix) > 1:
                # Get upper triangle of correlation matrix (excluding diagonal)
                upper_triangle = np.triu(corr_matrix.values, k=1)
                mean_abs_corr = np.mean(np.abs(upper_triangle[upper_triangle != 0]))
                diversification_score = 1 - mean_abs_corr
            else:
                diversification_score = 0
            
            return {
                "correlation_matrix": corr_matrix.to_dict(),
                "highly_correlated_pairs": high_corr_pairs,
                "anti_correlated_pairs": anti_corr_pairs,
                "diversification_score": float(diversification_score)
            }
        
        except Exception as e:
            self.logger.error(f"Error calculating strategy correlation: {str(e)}")
            return {}
    
    def _generate_recommendations(self, overall_metrics: Dict[str, Any],
                                 strategy_metrics: Dict[str, Dict[str, Any]],
                                 pair_metrics: Dict[str, Dict[str, Any]],
                                 anomalies: Dict[str, Any],
                                 market_analysis: Dict[str, Any]) -> List[Dict[str, Any]]:
        """
        Generate actionable recommendations based on analysis.
        
        Args:
            overall_metrics: Dictionary with overall performance metrics
            strategy_metrics: Dictionary with metrics for each strategy
            pair_metrics: Dictionary with metrics for each trading pair
            anomalies: Dictionary with detected anomalies
            market_analysis: Dictionary with market condition analysis
            
        Returns:
            List of recommendations
        """
        recommendations = []
        
        try:
            # Check system-wide issues
            if overall_metrics.get("sharpe_ratio", 0) < 0.5:
                recommendations.append({
                    "level": "system",
                    "type": "risk_management",
                    "priority": "high",
                    "recommendation": "Improve overall risk-adjusted returns. System Sharpe ratio is below 0.5.",
                    "details": {
                        "current_sharpe": overall_metrics.get("sharpe_ratio", 0),
                        "target_sharpe": 1.0
                    }
                })
            
            if overall_metrics.get("max_drawdown", 0) > 0.15:
                recommendations.append({
                    "level": "system",
                    "type": "risk_management",
                    "priority": "high",
                    "recommendation": "Reduce system maximum drawdown which is currently above 15%.",
                    "details": {
                        "current_drawdown": overall_metrics.get("max_drawdown", 0),
                        "target_drawdown": 0.1
                    }
                })
            
            # Strategy-specific recommendations
            for strategy, metrics in strategy_metrics.items():
                # Poor performing strategies
                if metrics.get("total_pnl", 0) < 0:
                    recommendations.append({
                        "level": "strategy",
                        "type": "performance",
                        "priority": "high",
                        "strategy": strategy,
                        "recommendation": f"Consider deactivating strategy '{strategy}' which is losing money.",
                        "details": {
                            "total_pnl": metrics.get("total_pnl", 0),
                            "sharpe_ratio": metrics.get("sharpe_ratio", 0)
                        }
                    })
                
                # Low Sharpe ratio strategies
                elif metrics.get("sharpe_ratio", 0) < 0.3 and metrics.get("total_trades", 0) > self.min_trades_for_analysis:
                    recommendations.append({
                        "level": "strategy",
                        "type": "risk_management",
                        "priority": "medium",
                        "strategy": strategy,
                        "recommendation": f"Optimize risk-adjusted returns for strategy '{strategy}'.",
                        "details": {
                            "current_sharpe": metrics.get("sharpe_ratio", 0),
                            "target_sharpe": 0.8
                        }
                    })
                
                # High volatility strategies
                if metrics.get("volatility", 0) > 0.1 and metrics.get("total_trades", 0) > self.min_trades_for_analysis:
                    recommendations.append({
                        "level": "strategy",
                        "type": "risk_management",
                        "priority": "medium",
                        "strategy": strategy,
                        "recommendation": f"Reduce volatility for strategy '{strategy}'.",
                        "details": {
                            "current_volatility": metrics.get("volatility", 0),
                            "target_volatility": 0.05
                        }
                    })
            
            # Trading pair recommendations
            for pair, metrics in pair_metrics.items():
                # Poor performing pairs
                if metrics.get("total_pnl", 0) < 0 and metrics.get("total_trades", 0) > self.min_trades_for_analysis:
                    recommendations.append({
                        "level": "trading_pair",
                        "type": "performance",
                        "priority": "medium",
                        "trading_pair": pair,
                        "recommendation": f"Review strategies trading '{pair}' which is showing negative returns.",
                        "details": {
                            "total_pnl": metrics.get("total_pnl", 0),
                            "total_trades": metrics.get("total_trades", 0),
                            "active_strategies": metrics.get("active_strategies", [])
                        }
                    })
            
            # Market condition recommendations
            if market_analysis:
                correlations = market_analysis.get("correlations", {})
                regimes = market_analysis.get("market_regimes", {})
                
                # Check for high correlation with market volatility
                if "volatility" in correlations and abs(correlations["volatility"]) > 0.6:
                    if correlations["volatility"] > 0:
                        recommendations.append({
                            "level": "system",
                            "type": "market_conditions",
                            "priority": "medium",
                            "recommendation": "System performs better in volatile markets. Consider increasing allocation during high volatility periods.",
                            "details": {
                                "volatility_correlation": correlations["volatility"]
                            }
                        })
                    else:
                        recommendations.append({
                            "level": "system",
                            "type": "market_conditions",
                            "priority": "medium",
                            "recommendation": "System performs worse in volatile markets. Consider reducing exposure during high volatility periods.",
                            "details": {
                                "volatility_correlation": correlations["volatility"]
                            }
                        })
            
            # Recommendations based on anomalies
            if anomalies and anomalies.get("anomalous_days_count", 0) > 0:
                anomaly_count = anomalies.get("anomalous_days_count", 0)
                recommendations.append({
                    "level": "system",
                    "type": "anomalies",
                    "priority": "medium",
                    "recommendation": f"Investigate {anomaly_count} anomalous PnL days to understand outlier performance.",
                    "details": {
                        "anomalous_days_count": anomaly_count,
                        "dates": [a.get("date") for a in anomalies.get("anomalous_days", [])][:5]
                    }
                })
            
            # Prioritize recommendations
            recommendations.sort(key=lambda x: 0 if x["priority"] == "high" else (1 if x["priority"] == "medium" else 2))
            
            return recommendations[:10]  # Limit to top 10 recommendations
        
        except Exception as e:
            self.logger.error(f"Error generating recommendations: {str(e)}")
            return []
    
    def _calculate_average_daily_pnl(self, pnl_data: pd.DataFrame) -> float:
        """
        Calculate average PnL per day.
        
        Args:
            pnl_data: DataFrame containing PnL data
            
        Returns:
            Average daily PnL value
        """
        if pnl_data.empty or 'value' not in pnl_data.columns or 'timestamp' not in pnl_data.columns:
            return 0.0
        
        # Group by day
        pnl_data['date'] = pnl_data['timestamp'].dt.date
        daily_pnl = pnl_data.groupby('date')['value'].sum()
        
        # Calculate average
        return daily_pnl.mean()
    
    def _calculate_profitable_days_percent(self, pnl_data: pd.DataFrame) -> float:
        """
        Calculate percentage of days with positive PnL.
        
        Args:
            pnl_data: DataFrame containing PnL data
            
        Returns:
            Percentage of profitable days
        """
        if pnl_data.empty or 'value' not in pnl_data.columns or 'timestamp' not in pnl_data.columns:
            return 0.0
        
        # Group by day
        pnl_data['date'] = pnl_data['timestamp'].dt.date
        daily_pnl = pnl_data.groupby('date')['value'].sum()
        
        # Calculate percentage of profitable days
        profitable_days = (daily_pnl > 0).sum()
        total_days = len(daily_pnl)
        
        return (profitable_days / total_days) * 100 if total_days > 0 else 0.0
    
    def _find_best_day(self, pnl_data: pd.DataFrame) -> Dict[str, Any]:
        """
        Find the day with highest PnL.
        
        Args:
            pnl_data: DataFrame containing PnL data
            
        Returns:
            Dictionary with best day details
        """
        if pnl_data.empty or 'value' not in pnl_data.columns or 'timestamp' not in pnl_data.columns:
            return {}
        
        # Group by day
        pnl_data['date'] = pnl_data['timestamp'].dt.date
        daily_pnl = pnl_data.groupby('date')['value'].sum().reset_index()
        
        if daily_pnl.empty:
            return {}
        
        # Find best day
        best_idx = daily_pnl['value'].idxmax()
        best_day = daily_pnl.iloc[best_idx]
        
        return {
            "date": best_day['date'],
            "pnl": float(best_day['value'])
        }
    
    def _find_worst_day(self, pnl_data: pd.DataFrame) -> Dict[str, Any]:
        """
        Find the day with lowest PnL.
        
        Args:
            pnl_data: DataFrame containing PnL data
            
        Returns:
            Dictionary with worst day details
        """
        if pnl_data.empty or 'value' not in pnl_data.columns or 'timestamp' not in pnl_data.columns:
            return {}
        
        # Group by day
        pnl_data['date'] = pnl_data['timestamp'].dt.date
        daily_pnl = pnl_data.groupby('date')['value'].sum().reset_index()
        
        if daily_pnl.empty:
            return {}
        
        # Find worst day
        worst_idx = daily_pnl['value'].idxmin()
        worst_day = daily_pnl.iloc[worst_idx]
        
        return {
            "date": worst_day['date'],
            "pnl": float(worst_day['value'])
        }
    
    def _find_best_trade(self, trade_data: pd.DataFrame) -> Dict[str, Any]:
        """
        Find the most profitable trade.
        
        Args:
            trade_data: DataFrame containing trade data
            
        Returns:
            Dictionary with best trade details
        """
        if trade_data.empty or 'pnl' not in trade_data.columns:
            return {}
        
        # Find best trade
        best_idx = trade_data['pnl'].idxmax()
        best_trade = trade_data.iloc[best_idx]
        
        return {
            "id": best_trade.get('id', ''),
            "strategy": best_trade.get('strategy_id', ''),
            "trading_pair": best_trade.get('trading_pair', ''),
            "pnl": float(best_trade['pnl']),
            "timestamp": best_trade.get('created_at', None)
        }
    
    def _find_worst_trade(self, trade_data: pd.DataFrame) -> Dict[str, Any]:
        """
        Find the least profitable trade.
        
        Args:
            trade_data: DataFrame containing trade data
            
        Returns:
            Dictionary with worst trade details
        """
        if trade_data.empty or 'pnl' not in trade_data.columns:
            return {}
        
        # Find worst trade
        worst_idx = trade_data['pnl'].idxmin()
        worst_trade = trade_data.iloc[worst_idx]
        
        return {
            "id": worst_trade.get('id', ''),
            "strategy": worst_trade.get('strategy_id', ''),
            "trading_pair": worst_trade.get('trading_pair', ''),
            "pnl": float(worst_trade['pnl']),
            "timestamp": worst_trade.get('created_at', None)
        }
    
    def _calculate_daily_returns(self, pnl_data: pd.DataFrame) -> pd.Series:
        """
        Calculate daily returns from PnL data.
        
        Args:
            pnl_data: DataFrame containing PnL data
            
        Returns:
            Series of daily returns
        """
        if pnl_data.empty or 'value' not in pnl_data.columns or 'timestamp' not in pnl_data.columns:
            return pd.Series()
        
        # Group by day
        pnl_data['date'] = pnl_data['timestamp'].dt.date
        daily_pnl = pnl_data.groupby('date')['value'].sum()
        
        return daily_pnl
    
    def _calculate_profit_factor(self, trade_data: pd.DataFrame) -> float:
        """
        Calculate profit factor (gross profit / gross loss).
        
        Args:
            trade_data: DataFrame containing trade data
            
        Returns:
            Profit factor value
        """
        if trade_data.empty or 'pnl' not in trade_data.columns:
            return 0.0
        
        # Calculate gross profit and loss
        winning_trades = trade_data[trade_data['pnl'] > 0]
        losing_trades = trade_data[trade_data['pnl'] < 0]
        
        gross_profit = winning_trades['pnl'].sum() if not winning_trades.empty else 0
        gross_loss = abs(losing_trades['pnl'].sum()) if not losing_trades.empty else 0
        
        # Calculate profit factor
        return gross_profit / gross_loss if gross_loss > 0 else (float('inf') if gross_profit > 0 else 0)
    
    def _calculate_average_holding_time(self, trade_data: pd.DataFrame) -> float:
        """
        Calculate average holding time for trades in seconds.
        
        Args:
            trade_data: DataFrame containing trade data
            
        Returns:
            Average holding time in seconds
        """
        if trade_data.empty or 'created_at' not in trade_data.columns or 'completed_at' not in trade_data.columns:
            return 0.0
        
        # Remove trades with missing timestamps
        valid_trades = trade_data.dropna(subset=['created_at', 'completed_at'])
        
        if valid_trades.empty:
            return 0.0
        
        # Calculate duration for each trade
        holding_times = (valid_trades['completed_at'] - valid_trades['created_at']).dt.total_seconds()
        
        # Calculate average
        return holding_times.mean()
    
    def _count_inactive_days(self, pnl_data: pd.DataFrame) -> int:
        """
        Count days with no trading activity.
        
        Args:
            pnl_data: DataFrame containing PnL data
            
        Returns:
            Number of inactive days
        """
        if pnl_data.empty or 'timestamp' not in pnl_data.columns:
            return 0
        
        # Get date range
        min_date = pnl_data['timestamp'].min().date()
        max_date = pnl_data['timestamp'].max().date()
        
        # Create a complete date range
        date_range = pd.date_range(start=min_date, end=max_date, freq='D')
        
        # Get active days
        pnl_data['date'] = pnl_data['timestamp'].dt.date
        active_days = set(pnl_data['date'].unique())
        
        # Count inactive days
        inactive_days = sum(1 for date in date_range if date.date() not in active_days)
        
        return inactive_days
    
    def _calculate_pnl_consistency(self, pnl_data: pd.DataFrame) -> float:
        """
        Calculate the consistency of PnL returns.
        
        Args:
            pnl_data: DataFrame containing PnL data
            
        Returns:
            Consistency score (0-1)
        """
        if pnl_data.empty or 'value' not in pnl_data.columns or 'timestamp' not in pnl_data.columns:
            return 0.0
        
        # Group by day
        pnl_data['date'] = pnl_data['timestamp'].dt.date
        daily_pnl = pnl_data.groupby('date')['value'].sum()
        
        if len(daily_pnl) < 5:
            return 0.0
        
        # Calculate coefficient of variation (lower is more consistent)
        mean_pnl = daily_pnl.mean()
        std_pnl = daily_pnl.std()
        
        if mean_pnl == 0:
            return 0.0
        
        cv = std_pnl / abs(mean_pnl)
        
        # Convert to consistency score (1 - normalized CV)
        consistency = 1 / (1 + cv)
        
        return consistency