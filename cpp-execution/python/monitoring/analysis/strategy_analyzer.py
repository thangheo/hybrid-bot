#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Strategy Performance Analyzer for evaluating trading strategy metrics.
"""

import asyncio
from datetime import datetime, timedelta
import pandas as pd
import numpy as np
from typing import Dict, List, Optional, Any, Tuple, Set
import scipy.stats as stats

from ..database.timeseries_db import TimeSeriesDB
from ..database.operational_db import OperationalDB
from ..utils.time_utils import timestamp_to_datetime, datetime_to_timestamp
from ..utils.math_utils import calculate_sharpe_ratio, calculate_sortino_ratio, calculate_drawdown
from .analyzer_base import AnalyzerBase

class StrategyAnalyzer(AnalyzerBase):
    """
    Analyzer for trading strategy performance metrics.
    
    This analyzer evaluates the performance of trading strategies, 
    identifying strengths, weaknesses, and opportunities for optimization.
    """
    
    def __init__(self, config: Dict[str, Any], 
                 timeseries_db: TimeSeriesDB,
                 operational_db: OperationalDB):
        """
        Initialize the strategy analyzer with database connections.
        
        Args:
            config: Configuration dictionary
            timeseries_db: Time series database connector
            operational_db: Operational database connector
        """
        super().__init__(config, timeseries_db)
        self.operational_db = operational_db
        self.signal_metric_name = self.config.get('signal_metric_name', 'strategy.signal')
        self.trade_metric_name = self.config.get('trade_metric_name', 'strategy.trade')
        self.pnl_metric_name = self.config.get('pnl_metric_name', 'trading.pnl')
        self.min_trades_for_analysis = self.config.get('min_trades_for_analysis', 20)
        self.min_signals_for_analysis = self.config.get('min_signals_for_analysis', 50)
        self.lookback_windows = self.config.get('lookback_windows', {
            'short_term': 7,   # days
            'medium_term': 30, # days
            'long_term': 90    # days
        })
        
    async def analyze(self, start_time: datetime, end_time: datetime) -> Dict[str, Any]:
        """
        Analyze strategy performance within the specified time window.
        
        Args:
            start_time: Start of analysis window
            end_time: End of analysis window
            
        Returns:
            Dictionary containing analysis results
        """
        self.logger.info(f"Analyzing strategy performance from {start_time} to {end_time}")
        
        # Fetch strategy data
        strategies = await self._fetch_strategies()
        
        if not strategies:
            self.logger.warning("No strategies found")
            return {"error": "No strategies found", "status": "error"}
        
        # Fetch signals data
        signals_data = await self._fetch_signals_data(start_time, end_time)
        
        # Fetch trades data
        trades_data = await self._fetch_trades_data(start_time, end_time)
        
        # Fetch PnL data
        pnl_data = await self._fetch_pnl_data(start_time, end_time)
        
        # Analyze each strategy
        strategy_analyses = {}
        
        for strategy_id, strategy_info in strategies.items():
            strategy_analysis = await self._analyze_strategy(
                strategy_id, 
                strategy_info,
                signals_data.get(strategy_id, pd.DataFrame()),
                trades_data.get(strategy_id, pd.DataFrame()),
                pnl_data.get(strategy_id, pd.DataFrame()),
                start_time,
                end_time
            )
            
            strategy_analyses[strategy_id] = strategy_analysis
        
        # Calculate comparative metrics
        comparative_analysis = self._analyze_strategy_comparison(strategy_analyses)
        
        # Generate recommendations
        recommendations = self._generate_recommendations(strategy_analyses, comparative_analysis)
        
        self.last_analysis_time = datetime.now()
        
        return {
            "status": "success",
            "analysis_period": {
                "start": start_time,
                "end": end_time,
                "duration_days": (end_time - start_time).days
            },
            "strategies_analyzed": len(strategy_analyses),
            "strategy_analyses": strategy_analyses,
            "comparative_analysis": comparative_analysis,
            "recommendations": recommendations
        }
    
    async def _fetch_strategies(self) -> Dict[str, Dict[str, Any]]:
        """
        Fetch all active trading strategies from the database.
        
        Returns:
            Dictionary mapping strategy IDs to strategy information
        """
        try:
            query = """
                SELECT 
                    id, name, description, type, parameters, 
                    created_at, updated_at, status
                FROM strategies
                WHERE status = 'active'
            """
            
            result = await self.operational_db.execute_query(query)
            
            strategies = {}
            for row in result:
                strategy_id = row.get('id')
                strategies[strategy_id] = {
                    "name": row.get('name', ''),
                    "description": row.get('description', ''),
                    "type": row.get('type', ''),
                    "parameters": row.get('parameters', {}),
                    "created_at": timestamp_to_datetime(row.get('created_at')),
                    "updated_at": timestamp_to_datetime(row.get('updated_at')),
                    "status": row.get('status', '')
                }
            
            return strategies
        
        except Exception as e:
            self.logger.error(f"Error fetching strategies: {str(e)}")
            return {}
    
    async def _fetch_signals_data(self, start_time: datetime, end_time: datetime) -> Dict[str, pd.DataFrame]:
        """
        Fetch signal data for all strategies within the specified time window.
        
        Args:
            start_time: Start of analysis window
            end_time: End of analysis window
            
        Returns:
            Dictionary mapping strategy IDs to signal data DataFrames
        """
        try:
            # Fetch signal metrics from time series database
            signals_data = await self.fetch_data(
                self.signal_metric_name,
                start_time,
                end_time
            )
            
            if signals_data.empty:
                return {}
            
            # Group by strategy
            if 'strategy' not in signals_data.columns:
                self.logger.warning("Strategy column missing from signals data")
                return {}
            
            strategy_signals = {}
            for strategy, group in signals_data.groupby('strategy'):
                strategy_signals[strategy] = group.reset_index(drop=True)
            
            return strategy_signals
        
        except Exception as e:
            self.logger.error(f"Error fetching signals data: {str(e)}")
            return {}
    
    async def _fetch_trades_data(self, start_time: datetime, end_time: datetime) -> Dict[str, pd.DataFrame]:
        """
        Fetch trade data for all strategies within the specified time window.
        
        Args:
            start_time: Start of analysis window
            end_time: End of analysis window
            
        Returns:
            Dictionary mapping strategy IDs to trade data DataFrames
        """
        try:
            query = """
                SELECT 
                    id, order_id, strategy_id, signal_id, trading_pair, exchange_id,
                    side, quantity, price, fee, fee_currency,
                    created_at, completed_at, status, pnl
                FROM trades
                WHERE created_at BETWEEN %s AND %s
                ORDER BY created_at
            """
            params = (datetime_to_timestamp(start_time), datetime_to_timestamp(end_time))
            
            result = await self.operational_db.execute_query(query, params)
            
            if not result:
                return {}
            
            # Convert to DataFrame
            df = pd.DataFrame(result)
            
            # Convert timestamps to datetime
            time_columns = ['created_at', 'completed_at']
            for col in time_columns:
                if col in df.columns:
                    df[col] = df[col].apply(timestamp_to_datetime)
            
            # Group by strategy
            if 'strategy_id' not in df.columns:
                self.logger.warning("Strategy ID column missing from trades data")
                return {}
            
            strategy_trades = {}
            for strategy, group in df.groupby('strategy_id'):
                strategy_trades[strategy] = group.reset_index(drop=True)
            
            return strategy_trades
        
        except Exception as e:
            self.logger.error(f"Error fetching trades data: {str(e)}")
            return {}
    
    async def _fetch_pnl_data(self, start_time: datetime, end_time: datetime) -> Dict[str, pd.DataFrame]:
        """
        Fetch PnL data for all strategies within the specified time window.
        
        Args:
            start_time: Start of analysis window
            end_time: End of analysis window
            
        Returns:
            Dictionary mapping strategy IDs to PnL data DataFrames
        """
        try:
            # Fetch PnL metrics from time series database
            pnl_data = await self.fetch_data(
                self.pnl_metric_name,
                start_time,
                end_time
            )
            
            if pnl_data.empty:
                return {}
            
            # Group by strategy
            if 'strategy' not in pnl_data.columns:
                self.logger.warning("Strategy column missing from PnL data")
                return {}
            
            strategy_pnl = {}
            for strategy, group in pnl_data.groupby('strategy'):
                strategy_pnl[strategy] = group.reset_index(drop=True)
            
            return strategy_pnl
        
        except Exception as e:
            self.logger.error(f"Error fetching PnL data: {str(e)}")
            return {}
    
    async def _analyze_strategy(self, strategy_id: str, strategy_info: Dict[str, Any],
                               signals_data: pd.DataFrame, trades_data: pd.DataFrame, 
                               pnl_data: pd.DataFrame, start_time: datetime, 
                               end_time: datetime) -> Dict[str, Any]:
        """
        Analyze performance of a single strategy.
        
        Args:
            strategy_id: Strategy identifier
            strategy_info: Basic information about the strategy
            signals_data: DataFrame with signal metrics
            trades_data: DataFrame with trade data
            pnl_data: DataFrame with PnL data
            start_time: Start of analysis window
            end_time: End of analysis window
            
        Returns:
            Dictionary with analysis results for the strategy
        """
        # Basic strategy info
        result = {
            "strategy_id": strategy_id,
            "name": strategy_info.get("name", ""),
            "type": strategy_info.get("type", ""),
            "status": strategy_info.get("status", ""),
        }
        
        # Check if we have enough data
        if signals_data.empty and trades_data.empty and pnl_data.empty:
            result["status"] = "insufficient_data"
            result["message"] = "Not enough data for analysis"
            return result
        
        # Analyze signals
        if not signals_data.empty:
            result["signal_metrics"] = self._analyze_signals(signals_data)
        else:
            result["signal_metrics"] = {}
        
        # Analyze trades
        if not trades_data.empty:
            result["trade_metrics"] = self._analyze_trades(trades_data)
        else:
            result["trade_metrics"] = {}
        
        # Analyze PnL
        if not pnl_data.empty:
            result["pnl_metrics"] = self._analyze_pnl(pnl_data)
        else:
            result["pnl_metrics"] = {}
        
        # Analyze signal-to-trade efficiency
        if not signals_data.empty and not trades_data.empty:
            result["signal_trade_efficiency"] = self._analyze_signal_trade_efficiency(
                signals_data, trades_data
            )
        else:
            result["signal_trade_efficiency"] = {}
        
        # Analyze performance trends
        result["performance_trends"] = self._analyze_performance_trends(
            signals_data, trades_data, pnl_data, start_time, end_time
        )
        
        # Analyze performance by market conditions
        result["market_condition_performance"] = await self._analyze_market_condition_performance(
            strategy_id, pnl_data, start_time, end_time
        )
        
        # Analyze performance by trading pair
        result["trading_pair_performance"] = self._analyze_trading_pair_performance(
            signals_data, trades_data, pnl_data
        )
        
        # Calculate strategy health score
        result["health_score"] = self._calculate_strategy_health_score(result)
        
        # Strategy strengths and weaknesses
        strength_weakness = self._identify_strengths_weaknesses(result)
        result["strengths"] = strength_weakness["strengths"]
        result["weaknesses"] = strength_weakness["weaknesses"]
        
        return result
    
    def _analyze_signals(self, signals_data: pd.DataFrame) -> Dict[str, Any]:
        """
        Analyze strategy signal metrics.
        
        Args:
            signals_data: DataFrame containing signal data
            
        Returns:
            Dictionary with signal analysis metrics
        """
        if signals_data.empty:
            return {}
        
        try:
            # Basic signal metrics
            total_signals = len(signals_data)
            
            # Signal directions
            signal_directions = {}
            if 'direction' in signals_data.columns:
                direction_counts = signals_data['direction'].value_counts()
                signal_directions = {
                    direction: int(count) 
                    for direction, count in direction_counts.items()
                }
            
            # Signal strength distribution
            strength_metrics = {}
            if 'strength' in signals_data.columns:
                strength_metrics = {
                    "mean": float(signals_data['strength'].mean()),
                    "median": float(signals_data['strength'].median()),
                    "std_dev": float(signals_data['strength'].std()),
                    "min": float(signals_data['strength'].min()),
                    "max": float(signals_data['strength'].max()),
                    "histogram": self._calculate_histogram(signals_data['strength'], 10)
                }
            
            # Signal frequency
            if 'timestamp' in signals_data.columns:
                # Convert to datetime if needed
                if not pd.api.types.is_datetime64_any_dtype(signals_data['timestamp']):
                    signals_data['timestamp'] = pd.to_datetime(signals_data['timestamp'])
                
                # Sort by timestamp
                sorted_signals = signals_data.sort_values('timestamp')
                
                # Calculate time between signals
                if len(sorted_signals) > 1:
                    time_diffs = sorted_signals['timestamp'].diff().dropna()
                    
                    # Convert to seconds
                    time_diffs_seconds = time_diffs.dt.total_seconds()
                    
                    avg_signal_interval = float(time_diffs_seconds.mean())
                    median_signal_interval = float(time_diffs_seconds.median())
                    
                    # Calculate signals per day
                    total_days = (sorted_signals['timestamp'].max() - 
                                 sorted_signals['timestamp'].min()).total_seconds() / 86400
                    signals_per_day = total_signals / total_days if total_days > 0 else 0
                else:
                    avg_signal_interval = 0
                    median_signal_interval = 0
                    signals_per_day = 0
            else:
                avg_signal_interval = 0
                median_signal_interval = 0
                signals_per_day = 0
            
            # Signal timing analysis
            timing_analysis = {}
            if 'timestamp' in signals_data.columns:
                signals_data['hour'] = signals_data['timestamp'].dt.hour
                hourly_distribution = signals_data.groupby('hour').size()
                
                timing_analysis = {
                    "hourly_distribution": hourly_distribution.to_dict(),
                    "peak_hour": int(hourly_distribution.idxmax()) if not hourly_distribution.empty else None,
                    "min_hour": int(hourly_distribution.idxmin()) if not hourly_distribution.empty else None
                }
            
            # Trading pair distribution
            pair_distribution = {}
            if 'trading_pair' in signals_data.columns:
                pair_counts = signals_data['trading_pair'].value_counts()
                total_pairs = len(pair_counts)
                top_pairs = pair_counts.head(5).to_dict()
                
                pair_distribution = {
                    "total_pairs": total_pairs,
                    "top_pairs": {str(pair): int(count) for pair, count in top_pairs.items()},
                    "pair_concentration": float((pair_counts.max() / total_signals) if total_signals > 0 else 0)
                }
            
            return {
                "total_signals": total_signals,
                "signal_directions": signal_directions,
                "strength_metrics": strength_metrics,
                "avg_signal_interval_seconds": avg_signal_interval,
                "median_signal_interval_seconds": median_signal_interval,
                "signals_per_day": float(signals_per_day),
                "timing_analysis": timing_analysis,
                "pair_distribution": pair_distribution
            }
        
        except Exception as e:
            self.logger.error(f"Error analyzing signals: {str(e)}")
            return {"error": str(e)}
    
    def _analyze_trades(self, trades_data: pd.DataFrame) -> Dict[str, Any]:
        """
        Analyze strategy trade metrics.
        
        Args:
            trades_data: DataFrame containing trade data
            
        Returns:
            Dictionary with trade analysis metrics
        """
        if trades_data.empty:
            return {}
        
        try:
            # Basic trade metrics
            total_trades = len(trades_data)
            
            # Filter completed trades
            completed_trades = trades_data[trades_data['status'] == 'completed']
            completed_count = len(completed_trades)
            
            # PnL metrics
            pnl_metrics = {}
            if 'pnl' in completed_trades.columns and not completed_trades.empty:
                profitable_trades = completed_trades[completed_trades['pnl'] > 0]
                profitable_count = len(profitable_trades)
                
                win_rate = profitable_count / completed_count if completed_count > 0 else 0
                
                total_pnl = completed_trades['pnl'].sum()
                avg_pnl = completed_trades['pnl'].mean()
                
                # Calculate profit factor
                gross_profit = profitable_trades['pnl'].sum() if not profitable_trades.empty else 0
                losing_trades = completed_trades[completed_trades['pnl'] <= 0]
                gross_loss = abs(losing_trades['pnl'].sum()) if not losing_trades.empty else 0
                
                profit_factor = gross_profit / gross_loss if gross_loss > 0 else float('inf')
                
                # Best and worst trades
                best_pnl = completed_trades['pnl'].max() if not completed_trades.empty else 0
                worst_pnl = completed_trades['pnl'].min() if not completed_trades.empty else 0
                
                pnl_metrics = {
                    "total_pnl": float(total_pnl),
                    "avg_pnl": float(avg_pnl),
                    "win_rate": float(win_rate),
                    "profit_factor": float(profit_factor) if profit_factor != float('inf') else None,
                    "best_trade_pnl": float(best_pnl),
                    "worst_trade_pnl": float(worst_pnl),
                    "pnl_std_dev": float(completed_trades['pnl'].std())
                }
            
            # Trade duration metrics
            duration_metrics = {}
            if 'created_at' in trades_data.columns and 'completed_at' in trades_data.columns:
                # Calculate duration for completed trades
                valid_trades = completed_trades.dropna(subset=['created_at', 'completed_at'])
                
                if not valid_trades.empty:
                    durations = (valid_trades['completed_at'] - valid_trades['created_at']).dt.total_seconds()
                    
                    duration_metrics = {
                        "avg_duration_seconds": float(durations.mean()),
                        "median_duration_seconds": float(durations.median()),
                        "min_duration_seconds": float(durations.min()),
                        "max_duration_seconds": float(durations.max()),
                        "std_dev_seconds": float(durations.std())
                    }
            
            # Trade side distribution
            side_distribution = {}
            if 'side' in trades_data.columns:
                side_counts = trades_data['side'].value_counts()
                side_distribution = {
                    str(side): int(count) for side, count in side_counts.items()
                }
            
            # Trading pair distribution
            pair_distribution = {}
            if 'trading_pair' in trades_data.columns:
                pair_counts = trades_data['trading_pair'].value_counts()
                total_pairs = len(pair_counts)
                top_pairs = pair_counts.head(5).to_dict()
                
                pair_distribution = {
                    "total_pairs": total_pairs,
                    "top_pairs": {str(pair): int(count) for pair, count in top_pairs.items()},
                    "pair_concentration": float((pair_counts.max() / total_trades) if total_trades > 0 else 0)
                }
            
            # Exchange distribution
            exchange_distribution = {}
            if 'exchange_id' in trades_data.columns:
                exchange_counts = trades_data['exchange_id'].value_counts()
                exchange_distribution = {
                    str(exchange): int(count) for exchange, count in exchange_counts.items()
                }
            
            # Trade frequency
            frequency_metrics = {}
            if 'created_at' in trades_data.columns:
                # Sort by timestamp
                sorted_trades = trades_data.sort_values('created_at')
                
                # Calculate time between trades
                if len(sorted_trades) > 1:
                    time_diffs = sorted_trades['created_at'].diff().dropna()
                    
                    # Convert to seconds
                    time_diffs_seconds = time_diffs.dt.total_seconds()
                    
                    avg_trade_interval = float(time_diffs_seconds.mean())
                    median_trade_interval = float(time_diffs_seconds.median())
                    
                    # Calculate trades per day
                    total_days = (sorted_trades['created_at'].max() - 
                                 sorted_trades['created_at'].min()).total_seconds() / 86400
                    trades_per_day = total_trades / total_days if total_days > 0 else 0
                else:
                    avg_trade_interval = 0
                    median_trade_interval = 0
                    trades_per_day = 0
                
                frequency_metrics = {
                    "avg_interval_seconds": avg_trade_interval,
                    "median_interval_seconds": median_trade_interval,
                    "trades_per_day": float(trades_per_day)
                }
            
            return {
                "total_trades": total_trades,
                "completed_trades": completed_count,
                "completion_rate": float(completed_count / total_trades if total_trades > 0 else 0),
                "pnl_metrics": pnl_metrics,
                "duration_metrics": duration_metrics,
                "side_distribution": side_distribution,
                "pair_distribution": pair_distribution,
                "exchange_distribution": exchange_distribution,
                "frequency_metrics": frequency_metrics
            }
        
        except Exception as e:
            self.logger.error(f"Error analyzing trades: {str(e)}")
            return {"error": str(e)}
    
    def _analyze_pnl(self, pnl_data: pd.DataFrame) -> Dict[str, Any]:
        """
        Analyze strategy PnL metrics.
        
        Args:
            pnl_data: DataFrame containing PnL data
            
        Returns:
            Dictionary with PnL analysis metrics
        """
        if pnl_data.empty or 'value' not in pnl_data.columns:
            return {}
        
        try:
            # Basic PnL metrics
            total_pnl = pnl_data['value'].sum()
            
            # Group by day for daily metrics
            if 'timestamp' in pnl_data.columns:
                # Convert to datetime if needed
                if not pd.api.types.is_datetime64_any_dtype(pnl_data['timestamp']):
                    pnl_data['timestamp'] = pd.to_datetime(pnl_data['timestamp'])
                
                pnl_data['date'] = pnl_data['timestamp'].dt.date
                daily_pnl = pnl_data.groupby('date')['value'].sum().reset_index()
                
                # Calculate daily metrics
                positive_days = (daily_pnl['value'] > 0).sum()
                total_days = len(daily_pnl)
                
                positive_days_percent = (positive_days / total_days) * 100 if total_days > 0 else 0
                
                # Calculate drawdown
                if len(daily_pnl) > 1:
                    daily_returns = daily_pnl['value']
                    max_drawdown = calculate_drawdown(daily_returns)
                else:
                    max_drawdown = 0
                
                # Calculate Sharpe ratio
                if len(daily_pnl) > 1:
                    sharpe = calculate_sharpe_ratio(daily_pnl['value'])
                else:
                    sharpe = 0
                
                # Best and worst days
                if not daily_pnl.empty:
                    best_day_idx = daily_pnl['value'].idxmax()
                    worst_day_idx = daily_pnl['value'].idxmin()
                    
                    best_day = {
                        "date": daily_pnl.loc[best_day_idx, 'date'],
                        "pnl": float(daily_pnl.loc[best_day_idx, 'value'])
                    }
                    
                    worst_day = {
                        "date": daily_pnl.loc[worst_day_idx, 'date'],
                        "pnl": float(daily_pnl.loc[worst_day_idx, 'value'])
                    }
                else:
                    best_day = {}
                    worst_day = {}
                
                daily_metrics = {
                    "positive_days": int(positive_days),
                    "positive_days_percent": float(positive_days_percent),
                    "total_days": int(total_days),
                    "avg_daily_pnl": float(daily_pnl['value'].mean()) if not daily_pnl.empty else 0,
                    "median_daily_pnl": float(daily_pnl['value'].median()) if not daily_pnl.empty else 0,
                    "pnl_std_dev": float(daily_pnl['value'].std()) if not daily_pnl.empty else 0,
                    "max_drawdown": float(max_drawdown),
                    "sharpe_ratio": float(sharpe),
                    "best_day": best_day,
                    "worst_day": worst_day
                }
            else:
                daily_metrics = {}
            
            # Trading pair analysis
            pair_metrics = {}
            if 'trading_pair' in pnl_data.columns:
                pair_pnl = pnl_data.groupby('trading_pair')['value'].sum().reset_index()
                
                # Sort by PnL
                pair_pnl = pair_pnl.sort_values('value', ascending=False)
                
                # Get top and bottom pairs
                top_pairs = pair_pnl.head(5)
                bottom_pairs = pair_pnl.tail(5)
                
                pair_metrics = {
                    "top_pairs": {
                        row['trading_pair']: float(row['value']) 
                        for _, row in top_pairs.iterrows()
                    },
                    "bottom_pairs": {
                        row['trading_pair']: float(row['value']) 
                        for _, row in bottom_pairs.iterrows()
                    },
                    "total_pairs": len(pair_pnl),
                    "profitable_pairs": int((pair_pnl['value'] > 0).sum()),
                    "unprofitable_pairs": int((pair_pnl['value'] <= 0).sum())
                }
            
            return {
                "total_pnl": float(total_pnl),
                "daily_metrics": daily_metrics,
                "pair_metrics": pair_metrics
            }
        
        except Exception as e:
            self.logger.error(f"Error analyzing PnL: {str(e)}")
            return {"error": str(e)}
    
    def _analyze_signal_trade_efficiency(self, signals_data: pd.DataFrame, 
                                       trades_data: pd.DataFrame) -> Dict[str, Any]:
        """
        Analyze the efficiency of converting signals to trades.
        
        Args:
            signals_data: DataFrame containing signal data
            trades_data: DataFrame containing trade data
            
        Returns:
            Dictionary with signal-to-trade efficiency metrics
        """
        if signals_data.empty or trades_data.empty:
            return {}
        
        try:
            # Basic counts
            total_signals = len(signals_data)
            total_trades = len(trades_data)
            
            # Calculate conversion rate
            conversion_rate = total_trades / total_signals if total_signals > 0 else 0
            
            # If we have signal IDs in trades, we can do more detailed analysis
            signal_trade_mapping = {}
            if 'signal_id' in trades_data.columns and 'id' in signals_data.columns:
                # Count signals that resulted in trades
                executed_signal_ids = set(trades_data['signal_id'].dropna())
                signal_ids = set(signals_data['id'])
                
                executed_signals = len(executed_signal_ids)
                unexecuted_signals = total_signals - executed_signals
                
                # Calculate execution latency if we have timestamps
                latency_metrics = {}
                if ('timestamp' in signals_data.columns and 
                    'created_at' in trades_data.columns and 
                    'signal_id' in trades_data.columns):
                    
                    # Create a mapping of signal ID to timestamp
                    signal_timestamps = dict(zip(signals_data['id'], signals_data['timestamp']))
                    
                    # Calculate latency for each trade with a known signal
                    latencies = []
                    for _, trade in trades_data.dropna(subset=['signal_id']).iterrows():
                        signal_id = trade['signal_id']
                        if signal_id in signal_timestamps:
                            signal_time = signal_timestamps[signal_id]
                            trade_time = trade['created_at']
                            
                            # Calculate difference in seconds
                            latency = (trade_time - signal_time).total_seconds()
                            latencies.append(latency)
                    
                    if latencies:
                        latency_metrics = {
                            "avg_latency_seconds": float(np.mean(latencies)),
                            "median_latency_seconds": float(np.median(latencies)),
                            "min_latency_seconds": float(min(latencies)),
                            "max_latency_seconds": float(max(latencies)),
                            "std_dev_seconds": float(np.std(latencies))
                        }
                
                signal_trade_mapping = {
                    "executed_signals": executed_signals,
                    "unexecuted_signals": unexecuted_signals,
                    "execution_rate": float(executed_signals / total_signals) if total_signals > 0 else 0,
                    "latency_metrics": latency_metrics
                }
            
            # Analysis by signal strength (if available)
            strength_analysis = {}
            if ('strength' in signals_data.columns and 
                'signal_id' in trades_data.columns and 
                'id' in signals_data.columns):
                
                # Create a mapping of signal ID to strength
                signal_strengths = dict(zip(signals_data['id'], signals_data['strength']))
                
                # Get strengths for executed and unexecuted signals
                executed_strengths = [
                    signal_strengths[sid] for sid in executed_signal_ids 
                    if sid in signal_strengths
                ]
                
                unexecuted_strengths = [
                    signal_strengths[sid] for sid in (signal_ids - executed_signal_ids)
                    if sid in signal_strengths
                ]
                
                if executed_strengths and unexecuted_strengths:
                    strength_analysis = {
                        "avg_executed_strength": float(np.mean(executed_strengths)),
                        "avg_unexecuted_strength": float(np.mean(unexecuted_strengths)),
                        "strength_execution_correlation": float(
                            np.corrcoef(
                                [signal_strengths.get(sid, 0) for sid in signal_ids],
                                [1 if sid in executed_signal_ids else 0 for sid in signal_ids]
                            )[0, 1]
                        ) if len(signal_ids) > 1 else 0
                    }
            
            return {
                "total_signals": total_signals,
                "total_trades": total_trades,
                "conversion_rate": float(conversion_rate),
                "signal_trade_mapping": signal_trade_mapping,
                "strength_analysis": strength_analysis
            }
        
        except Exception as e:
            self.logger.error(f"Error analyzing signal-trade efficiency: {str(e)}")
            return {"error": str(e)}
    
    def _analyze_performance_trends(self, signals_data: pd.DataFrame, 
                                  trades_data: pd.DataFrame, 
                                  pnl_data: pd.DataFrame,
                                  start_time: datetime,
                                  end_time: datetime) -> Dict[str, Any]:
        """
        Analyze performance trends over time.
        
        Args:
            signals_data: DataFrame containing signal data
            trades_data: DataFrame containing trade data
            pnl_data: DataFrame containing PnL data
            start_time: Start of analysis window
            end_time: End of analysis window
            
        Returns:
            Dictionary with performance trend analysis
        """
        try:
            # Initialize result with empty values
            result = {
                "short_term": {},
                "medium_term": {},
                "long_term": {}
            }
            
            # Define time windows
            windows = {
                "short_term": timedelta(days=self.lookback_windows.get("short_term", 7)),
                "medium_term": timedelta(days=self.lookback_windows.get("medium_term", 30)),
                "long_term": timedelta(days=self.lookback_windows.get("long_term", 90))
            }
            
            # Check if we have PnL data with timestamps
            if not pnl_data.empty and 'timestamp' in pnl_data.columns and 'value' in pnl_data.columns:
                # Ensure timestamp is datetime
                if not pd.api.types.is_datetime64_any_dtype(pnl_data['timestamp']):
                    pnl_data['timestamp'] = pd.to_datetime(pnl_data['timestamp'])
                
                # Group by day
                pnl_data['date'] = pnl_data['timestamp'].dt.date
                daily_pnl = pnl_data.groupby('date')['value'].sum().reset_index()
                daily_pnl['timestamp'] = pd.to_datetime(daily_pnl['date'])
                
                # Sort by date
                daily_pnl = daily_pnl.sort_values('timestamp')
                
                # Calculate trends for each window
                for window_name, window_size in windows.items():
                    window_start = end_time - window_size
                    window_data = daily_pnl[daily_pnl['timestamp'] >= window_start]
                    
                    if len(window_data) >= 3:  # Need at least 3 points for trend
                        # Calculate linear trend
                        x = np.arange(len(window_data))
                        y = window_data['value'].values
                        
                        slope, intercept, r_value, p_value, std_err = stats.linregress(x, y)
                        
                        # Calculate total and average PnL
                        total_pnl = window_data['value'].sum()
                        avg_daily_pnl = window_data['value'].mean()
                        
                        # Calculate Sharpe ratio
                        sharpe = calculate_sharpe_ratio(window_data['value'])
                        
                        # Calculate drawdown
                        max_drawdown = calculate_drawdown(window_data['value'])
                        
                        # Store results
                        result[window_name] = {
                            "total_pnl": float(total_pnl),
                            "avg_daily_pnl": float(avg_daily_pnl),
                            "trend_slope": float(slope),
                            "trend_r_squared": float(r_value ** 2),
                            "trend_p_value": float(p_value),
                            "trend_direction": "upward" if slope > 0 else "downward",
                            "sharpe_ratio": float(sharpe),
                            "max_drawdown": float(max_drawdown),
                            "days_analyzed": len(window_data)
                        }
            
            # Check if we have trade data with timestamps
            if not trades_data.empty and 'created_at' in trades_data.columns:
                # Ensure created_at is datetime
                if not pd.api.types.is_datetime64_any_dtype(trades_data['created_at']):
                    trades_data['created_at'] = pd.to_datetime(trades_data['created_at'])
                
                # Group by day
                trades_data['date'] = trades_data['created_at'].dt.date
                daily_trades = trades_data.groupby('date').size().reset_index(name='count')
                daily_trades['timestamp'] = pd.to_datetime(daily_trades['date'])
                
                # Calculate win rate by day if we have PnL data
                if 'pnl' in trades_data.columns:
                    win_rates = []
                    for date, group in trades_data.groupby('date'):
                        completed = group[group['status'] == 'completed']
                        if len(completed) > 0:
                            win_rate = (completed['pnl'] > 0).sum() / len(completed)
                            win_rates.append({'date': date, 'win_rate': win_rate})
                    
                    if win_rates:
                        win_rate_df = pd.DataFrame(win_rates)
                        win_rate_df['timestamp'] = pd.to_datetime(win_rate_df['date'])
                        
                        # Calculate win rate trends for each window
                        for window_name, window_size in windows.items():
                            window_start = end_time - window_size
                            window_data = win_rate_df[win_rate_df['timestamp'] >= window_start]
                            
                            if len(window_data) >= 3:  # Need at least 3 points for trend
                                # Calculate linear trend
                                x = np.arange(len(window_data))
                                y = window_data['win_rate'].values
                                
                                slope, intercept, r_value, p_value, std_err = stats.linregress(x, y)
                                
                                # Store results
                                if window_name in result:
                                    result[window_name]["win_rate_trend"] = {
                                        "slope": float(slope),
                                        "r_squared": float(r_value ** 2),
                                        "direction": "improving" if slope > 0 else "deteriorating",
                                        "avg_win_rate": float(window_data['win_rate'].mean())
                                    }
                
                # Calculate trade frequency trends for each window
                for window_name, window_size in windows.items():
                    window_start = end_time - window_size
                    window_data = daily_trades[daily_trades['timestamp'] >= window_start]
                    
                    if len(window_data) >= 3:  # Need at least 3 points for trend
                        # Calculate linear trend
                        x = np.arange(len(window_data))
                        y = window_data['count'].values
                        
                        slope, intercept, r_value, p_value, std_err = stats.linregress(x, y)
                        
                        # Store results
                        if window_name in result:
                            result[window_name]["trade_frequency_trend"] = {
                                "slope": float(slope),
                                "r_squared": float(r_value ** 2),
                                "direction": "increasing" if slope > 0 else "decreasing",
                                "avg_trades_per_day": float(window_data['count'].mean())
                            }
            
            # Add summary of trends
            result["summary"] = self._summarize_trends(result)
            
            return result
        
        except Exception as e:
            self.logger.error(f"Error analyzing performance trends: {str(e)}")
            return {"error": str(e)}
    
    def _summarize_trends(self, trend_data: Dict[str, Dict[str, Any]]) -> Dict[str, Any]:
        """
        Summarize performance trends across different time windows.
        
        Args:
            trend_data: Dictionary with trend data for different time windows
            
        Returns:
            Dictionary with trend summary
        """
        summary = {
            "pnl_trend": "stable",
            "win_rate_trend": "stable",
            "trade_frequency_trend": "stable",
            "overall_trend": "stable"
        }
        
        # Check if we have enough data
        if not trend_data or not trend_data.get("short_term"):
            return summary
        
        # Analyze PnL trends
        pnl_trends = []
        for window in ["short_term", "medium_term", "long_term"]:
            if window in trend_data and "trend_slope" in trend_data[window]:
                slope = trend_data[window]["trend_slope"]
                r_squared = trend_data[window]["trend_r_squared"]
                
                # Only consider significant trends
                if r_squared > 0.3:
                    if slope > 0:
                        pnl_trends.append(1)  # Positive trend
                    elif slope < 0:
                        pnl_trends.append(-1)  # Negative trend
                    else:
                        pnl_trends.append(0)  # Stable
                else:
                    pnl_trends.append(0)  # Not significant, consider stable
        
        # Analyze win rate trends
        win_rate_trends = []
        for window in ["short_term", "medium_term", "long_term"]:
            if (window in trend_data and 
                "win_rate_trend" in trend_data[window] and 
                "slope" in trend_data[window]["win_rate_trend"]):
                
                slope = trend_data[window]["win_rate_trend"]["slope"]
                r_squared = trend_data[window]["win_rate_trend"]["r_squared"]
                
                # Only consider significant trends
                if r_squared > 0.3:
                    if slope > 0:
                        win_rate_trends.append(1)  # Improving
                    elif slope < 0:
                        win_rate_trends.append(-1)  # Deteriorating
                    else:
                        win_rate_trends.append(0)  # Stable
                else:
                    win_rate_trends.append(0)  # Not significant, consider stable
        
        # Analyze trade frequency trends
        frequency_trends = []
        for window in ["short_term", "medium_term", "long_term"]:
            if (window in trend_data and 
                "trade_frequency_trend" in trend_data[window] and 
                "slope" in trend_data[window]["trade_frequency_trend"]):
                
                slope = trend_data[window]["trade_frequency_trend"]["slope"]
                r_squared = trend_data[window]["trade_frequency_trend"]["r_squared"]
                
                # Only consider significant trends
                if r_squared > 0.3:
                    if slope > 0:
                        frequency_trends.append(1)  # Increasing
                    elif slope < 0:
                        frequency_trends.append(-1)  # Decreasing
                    else:
                        frequency_trends.append(0)  # Stable
                else:
                    frequency_trends.append(0)  # Not significant, consider stable
        
        # Determine overall PnL trend
        if pnl_trends:
            # Give more weight to short-term trends
            weighted_sum = sum(t * w for t, w in zip(pnl_trends, [3, 2, 1][:len(pnl_trends)]))
            weighted_count = sum([3, 2, 1][:len(pnl_trends)])
            
            if weighted_sum / weighted_count > 0.3:
                summary["pnl_trend"] = "improving"
            elif weighted_sum / weighted_count < -0.3:
                summary["pnl_trend"] = "deteriorating"
        
        # Determine overall win rate trend
        if win_rate_trends:
            # Give more weight to short-term trends
            weighted_sum = sum(t * w for t, w in zip(win_rate_trends, [3, 2, 1][:len(win_rate_trends)]))
            weighted_count = sum([3, 2, 1][:len(win_rate_trends)])
            
            if weighted_sum / weighted_count > 0.3:
                summary["win_rate_trend"] = "improving"
            elif weighted_sum / weighted_count < -0.3:
                summary["win_rate_trend"] = "deteriorating"
        
        # Determine overall trade frequency trend
        if frequency_trends:
            # Give more weight to short-term trends
            weighted_sum = sum(t * w for t, w in zip(frequency_trends, [3, 2, 1][:len(frequency_trends)]))
            weighted_count = sum([3, 2, 1][:len(frequency_trends)])
            
            if weighted_sum / weighted_count > 0.3:
                summary["trade_frequency_trend"] = "increasing"
            elif weighted_sum / weighted_count < -0.3:
                summary["trade_frequency_trend"] = "decreasing"
        
        # Determine overall strategy trend
        # Prioritize PnL trend, then win rate trend
        if summary["pnl_trend"] == "improving":
            summary["overall_trend"] = "improving"
        elif summary["pnl_trend"] == "deteriorating":
            summary["overall_trend"] = "deteriorating"
        elif summary["win_rate_trend"] == "improving":
            summary["overall_trend"] = "slightly_improving"
        elif summary["win_rate_trend"] == "deteriorating":
            summary["overall_trend"] = "slightly_deteriorating"
        
        return summary
    
    async def _analyze_market_condition_performance(self, strategy_id: str, 
                                                 pnl_data: pd.DataFrame,
                                                 start_time: datetime,
                                                 end_time: datetime) -> Dict[str, Any]:
        """
        Analyze how strategy performs under different market conditions.
        
        Args:
            strategy_id: Strategy identifier
            pnl_data: DataFrame containing PnL data
            start_time: Start of analysis window
            end_time: End of analysis window
            
        Returns:
            Dictionary with market condition performance analysis
        """
        if pnl_data.empty or 'value' not in pnl_data.columns or 'timestamp' not in pnl_data.columns:
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
                if not data.empty and 'value' in data.columns and 'timestamp' in data.columns:
                    market_data[name] = data
            
            if not market_data:
                return {}
            
            # Align PnL data with market data
            # Group PnL by day first
            pnl_data['date'] = pnl_data['timestamp'].dt.date
            daily_pnl = pnl_data.groupby('date')['value'].sum().reset_index()
            daily_pnl['timestamp'] = pd.to_datetime(daily_pnl['date'])
            
            aligned_data = pd.DataFrame()
            aligned_data['timestamp'] = daily_pnl['timestamp']
            aligned_data['pnl'] = daily_pnl['value']
            
            for name, data in market_data.items():
                # Resample market data to daily
                data['date'] = data['timestamp'].dt.date
                daily_market = data.groupby('date')['value'].mean().reset_index()
                daily_market['timestamp'] = pd.to_datetime(daily_market['date'])
                
                # Merge with PnL data
                merged = pd.merge(
                    aligned_data, 
                    daily_market[['timestamp', 'value']], 
                    on='timestamp', 
                    how='left'
                )
                
                aligned_data[name] = merged['value_y']
            
            # Remove rows with missing data
            aligned_data = aligned_data.dropna()
            
            if aligned_data.empty:
                return {}
            
            # Calculate correlations
            correlations = {}
            for name in market_data.keys():
                if name in aligned_data.columns:
                    corr = aligned_data['pnl'].corr(aligned_data[name])
                    correlations[name] = float(corr)
            
            # Analyze market regimes
            regimes = {}
            for name in market_data.keys():
                if name in aligned_data.columns:
                    # Divide data into high/low regimes based on median
                    median = aligned_data[name].median()
                    high_regime = aligned_data[aligned_data[name] > median]
                    low_regime = aligned_data[aligned_data[name] <= median]
                    
                    # Calculate metrics for each regime
                    high_regime_metrics = {
                        "avg_pnl": float(high_regime['pnl'].mean()),
                        "total_pnl": float(high_regime['pnl'].sum()),
                        "win_rate": float((high_regime['pnl'] > 0).mean()),
                        "days": len(high_regime)
                    }
                    
                    low_regime_metrics = {
                        "avg_pnl": float(low_regime['pnl'].mean()),
                        "total_pnl": float(low_regime['pnl'].sum()),
                        "win_rate": float((low_regime['pnl'] > 0).mean()),
                        "days": len(low_regime)
                    }
                    
                    # Calculate performance difference
                    pnl_difference = high_regime_metrics["avg_pnl"] - low_regime_metrics["avg_pnl"]
                    win_rate_difference = high_regime_metrics["win_rate"] - low_regime_metrics["win_rate"]
                    
                    # Determine optimal regime
                    if pnl_difference > 0:
                        optimal_regime = "high"
                        advantage = pnl_difference / abs(low_regime_metrics["avg_pnl"]) if low_regime_metrics["avg_pnl"] != 0 else float('inf')
                    else:
                        optimal_regime = "low"
                        advantage = -pnl_difference / abs(high_regime_metrics["avg_pnl"]) if high_regime_metrics["avg_pnl"] != 0 else float('inf')
                    
                    regimes[name] = {
                        "high_regime": high_regime_metrics,
                        "low_regime": low_regime_metrics,
                        "pnl_difference": float(pnl_difference),
                        "win_rate_difference": float(win_rate_difference),
                        "optimal_regime": optimal_regime,
                        "performance_advantage": float(advantage) if advantage != float('inf') else None
                    }
            
            # Determine overall market condition preference
            preferences = {}
            for name, regime_data in regimes.items():
                if abs(regime_data["pnl_difference"]) > 0 and regime_data["performance_advantage"] > 0.2:
                    preferences[name] = {
                        "preferred_condition": f"{regime_data['optimal_regime']} {name}",
                        "advantage": regime_data["performance_advantage"],
                        "confidence": "high" if regime_data["performance_advantage"] > 0.5 else "medium"
                    }
            
            return {
                "correlations": correlations,
                "market_regimes": regimes,
                "market_preferences": preferences
            }
        
        except Exception as e:
            self.logger.error(f"Error analyzing market condition performance: {str(e)}")
            return {"error": str(e)}
    
    def _analyze_trading_pair_performance(self, signals_data: pd.DataFrame,
                                        trades_data: pd.DataFrame, 
                                        pnl_data: pd.DataFrame) -> Dict[str, Any]:
        """
        Analyze strategy performance by trading pair.
        
        Args:
            signals_data: DataFrame containing signal data
            trades_data: DataFrame containing trade data
            pnl_data: DataFrame containing PnL data
            
        Returns:
            Dictionary with trading pair performance analysis
        """
        if pnl_data.empty or 'trading_pair' not in pnl_data.columns:
            return {}
        
        try:
            # Group PnL by trading pair
            pair_pnl = pnl_data.groupby('trading_pair')['value'].sum().reset_index()
            
            # Get top and bottom performers
            pair_pnl = pair_pnl.sort_values('value', ascending=False)
            
            # Calculate additional metrics if we have trades data
            pair_metrics = {}
            
            if not trades_data.empty and 'trading_pair' in trades_data.columns:
                for pair in pnl_data['trading_pair'].unique():
                    # Filter trades for this pair
                    pair_trades = trades_data[trades_data['trading_pair'] == pair]
                    
                    # Calculate metrics if we have trades
                    if not pair_trades.empty:
                        completed_trades = pair_trades[pair_trades['status'] == 'completed']
                        
                        metrics = {
                            "total_trades": len(pair_trades),
                            "completed_trades": len(completed_trades)
                        }
                        
                        # Calculate win rate if we have PnL data
                        if not completed_trades.empty and 'pnl' in completed_trades.columns:
                            profitable_trades = completed_trades[completed_trades['pnl'] > 0]
                            
                            metrics.update({
                                "win_rate": float(len(profitable_trades) / len(completed_trades)) if len(completed_trades) > 0 else 0,
                                "avg_pnl_per_trade": float(completed_trades['pnl'].mean()),
                                "best_trade_pnl": float(completed_trades['pnl'].max()),
                                "worst_trade_pnl": float(completed_trades['pnl'].min())
                            })
                        
                        pair_metrics[pair] = metrics
            
            # Create results
            top_pairs = []
            bottom_pairs = []
            
            for _, row in pair_pnl.head(5).iterrows():
                pair = row['trading_pair']
                pair_data = {
                    "trading_pair": pair,
                    "total_pnl": float(row['value'])
                }
                
                # Add metrics if available
                if pair in pair_metrics:
                    pair_data.update(pair_metrics[pair])
                
                top_pairs.append(pair_data)
            
            for _, row in pair_pnl.tail(5).iterrows():
                pair = row['trading_pair']
                pair_data = {
                    "trading_pair": pair,
                    "total_pnl": float(row['value'])
                }
                
                # Add metrics if available
                if pair in pair_metrics:
                    pair_data.update(pair_metrics[pair])
                
                bottom_pairs.append(pair_data)
            
            # Summary statistics
            profitable_pairs = (pair_pnl['value'] > 0).sum()
            unprofitable_pairs = (pair_pnl['value'] <= 0).sum()
            
            return {
                "top_pairs": top_pairs,
                "bottom_pairs": bottom_pairs,
                "profitable_pairs_count": int(profitable_pairs),
                "unprofitable_pairs_count": int(unprofitable_pairs),
                "total_pairs": len(pair_pnl),
                "pair_concentration": float(pair_pnl['value'].max() / pair_pnl['value'].sum()) if pair_pnl['value'].sum() != 0 else 0
            }
        
        except Exception as e:
            self.logger.error(f"Error analyzing trading pair performance: {str(e)}")
            return {"error": str(e)}
    
    def _calculate_strategy_health_score(self, strategy_analysis: Dict[str, Any]) -> float:
        """
        Calculate an overall health score for the strategy.
        
        Args:
            strategy_analysis: Dictionary with strategy analysis results
            
        Returns:
            Health score (0-100)
        """
        # Default score components
        pnl_score = 50
        consistency_score = 50
        technical_score = 50
        trend_score = 50
        
        try:
            # Calculate PnL score (0-100)
            if "pnl_metrics" in strategy_analysis and "daily_metrics" in strategy_analysis["pnl_metrics"]:
                daily_metrics = strategy_analysis["pnl_metrics"]["daily_metrics"]
                
                # Positive total PnL is good
                total_pnl = strategy_analysis["pnl_metrics"].get("total_pnl", 0)
                
                # High percentage of profitable days is good
                profitable_days_pct = daily_metrics.get("positive_days_percent", 0)
                
                # Low drawdown is good
                max_drawdown = daily_metrics.get("max_drawdown", 1.0)
                
                # Good Sharpe ratio is good
                sharpe_ratio = daily_metrics.get("sharpe_ratio", 0)
                
                # Calculate PnL score
                if total_pnl > 0:
                    pnl_base = 60
                    pnl_base += min(20, profitable_days_pct / 5)  # Up to 20 points for profitable days
                    pnl_base += min(10, (1 - max_drawdown) * 10)  # Up to 10 points for low drawdown
                    pnl_base += min(10, sharpe_ratio * 5)        # Up to 10 points for good Sharpe
                    pnl_score = pnl_base
                else:
                    pnl_base = 40
                    pnl_base += min(5, profitable_days_pct / 10)   # Up to 5 points for profitable days
                    pnl_base -= min(20, max_drawdown * 20)         # Lose up to 20 points for high drawdown
                    pnl_base += min(5, sharpe_ratio * 2.5)         # Up to 5 points for good Sharpe
                    pnl_score = max(0, pnl_base)
            
            # Calculate consistency score (0-100)
            if "pnl_metrics" in strategy_analysis and "daily_metrics" in strategy_analysis["pnl_metrics"]:
                daily_metrics = strategy_analysis["pnl_metrics"]["daily_metrics"]
                
                # Low PnL standard deviation relative to mean is good
                pnl_std_dev = daily_metrics.get("pnl_std_dev", 0)
                avg_daily_pnl = daily_metrics.get("avg_daily_pnl", 0)
                
                if avg_daily_pnl != 0:
                    coefficient_of_variation = abs(pnl_std_dev / avg_daily_pnl)
                    consistency_base = 50
                    
                    if coefficient_of_variation < 1.0:
                        consistency_base += min(30, (1 - coefficient_of_variation) * 30)
                    else:
                        consistency_base -= min(30, (coefficient_of_variation - 1) * 15)
                    
                    # Add points for high profitable days percentage
                    profitable_days_pct = daily_metrics.get("positive_days_percent", 0)
                    consistency_base += min(20, profitable_days_pct / 5)
                    
                    consistency_score = max(0, min(100, consistency_base))
            
            # Calculate technical score (0-100)
            if "signal_metrics" in strategy_analysis and "trade_metrics" in strategy_analysis:
                signal_metrics = strategy_analysis["signal_metrics"]
                trade_metrics = strategy_analysis["trade_metrics"]
                
                technical_base = 50
                
                # Signal quality
                if signal_metrics:
                    signals_per_day = signal_metrics.get("signals_per_day", 0)
                    if 0.5 <= signals_per_day <= 20:
                        technical_base += 10  # Reasonable signal frequency
                    
                    if "strength_metrics" in signal_metrics:
                        strength_mean = signal_metrics["strength_metrics"].get("mean", 0)
                        if 0.5 <= strength_mean <= 0.9:
                            technical_base += 5  # Good average signal strength
                
                # Trade execution
                if trade_metrics:
                    completion_rate = trade_metrics.get("completion_rate", 0)
                    technical_base += min(15, completion_rate * 15)  # Up to 15 points for high completion rate
                    
                    if "pnl_metrics" in trade_metrics:
                        win_rate = trade_metrics["pnl_metrics"].get("win_rate", 0)
                        technical_base += min(20, win_rate * 20)  # Up to 20 points for high win rate
                
                technical_score = max(0, min(100, technical_base))
            
            # Calculate trend score (0-100)
            if "performance_trends" in strategy_analysis and "summary" in strategy_analysis["performance_trends"]:
                trend_summary = strategy_analysis["performance_trends"]["summary"]
                
                trend_base = 50
                
                # Overall trend
                overall_trend = trend_summary.get("overall_trend", "stable")
                if overall_trend == "improving":
                    trend_base += 25
                elif overall_trend == "slightly_improving":
                    trend_base += 15
                elif overall_trend == "slightly_deteriorating":
                    trend_base -= 15
                elif overall_trend == "deteriorating":
                    trend_base -= 25
                
                # PnL trend
                pnl_trend = trend_summary.get("pnl_trend", "stable")
                if pnl_trend == "improving":
                    trend_base += 15
                elif pnl_trend == "deteriorating":
                    trend_base -= 15
                
                # Win rate trend
                win_rate_trend = trend_summary.get("win_rate_trend", "stable")
                if win_rate_trend == "improving":
                    trend_base += 10
                elif win_rate_trend == "deteriorating":
                    trend_base -= 10
                
                trend_score = max(0, min(100, trend_base))
            
            # Calculate overall health score
            # Weights: PnL (40%), Consistency (25%), Technical (20%), Trend (15%)
            overall_score = (
                pnl_score * 0.4 +
                consistency_score * 0.25 +
                technical_score * 0.2 +
                trend_score * 0.15
            )
            
            return float(overall_score)
        
        except Exception as e:
            self.logger.error(f"Error calculating strategy health score: {str(e)}")
            return 50.0  # Default score
    
    def _identify_strengths_weaknesses(self, strategy_analysis: Dict[str, Any]) -> Dict[str, List[str]]:
        """
        Identify strengths and weaknesses of the strategy.
        
        Args:
            strategy_analysis: Dictionary with strategy analysis results
            
        Returns:
            Dictionary with lists of strengths and weaknesses
        """
        strengths = []
        weaknesses = []
        
        try:
            # Check PnL metrics
            if "pnl_metrics" in strategy_analysis:
                pnl_metrics = strategy_analysis["pnl_metrics"]
                
                # Check total PnL
                total_pnl = pnl_metrics.get("total_pnl", 0)
                if total_pnl > 0:
                    strengths.append("Positive overall PnL")
                else:
                    weaknesses.append("Negative overall PnL")
                
                # Check daily metrics
                if "daily_metrics" in pnl_metrics:
                    daily_metrics = pnl_metrics["daily_metrics"]
                    
                    # Check profitable days percentage
                    profitable_days_pct = daily_metrics.get("positive_days_percent", 0)
                    if profitable_days_pct >= 60:
                        strengths.append(f"High percentage of profitable days ({profitable_days_pct:.1f}%)")
                    elif profitable_days_pct < 40:
                        weaknesses.append(f"Low percentage of profitable days ({profitable_days_pct:.1f}%)")
                    
                    # Check Sharpe ratio
                    sharpe_ratio = daily_metrics.get("sharpe_ratio", 0)
                    if sharpe_ratio >= 1.0:
                        strengths.append(f"Good risk-adjusted returns (Sharpe ratio: {sharpe_ratio:.2f})")
                    elif sharpe_ratio < 0:
                        weaknesses.append(f"Poor risk-adjusted returns (Sharpe ratio: {sharpe_ratio:.2f})")
                    
                    # Check max drawdown
                    max_drawdown = daily_metrics.get("max_drawdown", 0)
                    if max_drawdown <= 0.1:
                        strengths.append(f"Low maximum drawdown ({max_drawdown:.1%})")
                    elif max_drawdown >= 0.3:
                        weaknesses.append(f"High maximum drawdown ({max_drawdown:.1%})")
            
            # Check trade metrics
            if "trade_metrics" in strategy_analysis:
                trade_metrics = strategy_analysis["trade_metrics"]
                
                # Check completion rate
                completion_rate = trade_metrics.get("completion_rate", 0)
                if completion_rate >= 0.9:
                    strengths.append(f"High trade completion rate ({completion_rate:.1%})")
                elif completion_rate < 0.7:
                    weaknesses.append(f"Low trade completion rate ({completion_rate:.1%})")
                
                # Check PnL metrics
                if "pnl_metrics" in trade_metrics:
                    pnl_metrics = trade_metrics["pnl_metrics"]
                    
                    # Check win rate
                    win_rate = pnl_metrics.get("win_rate", 0)
                    if win_rate >= 0.55:
                        strengths.append(f"High win rate ({win_rate:.1%})")
                    elif win_rate < 0.45:
                        weaknesses.append(f"Low win rate ({win_rate:.1%})")
                    
                    # Check profit factor
                    profit_factor = pnl_metrics.get("profit_factor", 0)
                    if profit_factor >= 1.5:
                        strengths.append(f"Strong profit factor ({profit_factor:.2f})")
                    elif profit_factor < 1.0:
                        weaknesses.append(f"Poor profit factor ({profit_factor:.2f})")
            
            # Check signal-trade efficiency
            if "signal_trade_efficiency" in strategy_analysis:
                efficiency = strategy_analysis["signal_trade_efficiency"]
                
                conversion_rate = efficiency.get("conversion_rate", 0)
                if conversion_rate >= 0.7:
                    strengths.append(f"High signal-to-trade conversion ({conversion_rate:.1%})")
                elif conversion_rate < 0.3:
                    weaknesses.append(f"Low signal-to-trade conversion ({conversion_rate:.1%})")
            
            # Check performance trends
            if "performance_trends" in strategy_analysis and "summary" in strategy_analysis["performance_trends"]:
                trends = strategy_analysis["performance_trends"]["summary"]
                
                # Check overall trend
                overall_trend = trends.get("overall_trend", "stable")
                if overall_trend in ["improving", "slightly_improving"]:
                    strengths.append(f"Performance trend is {overall_trend}")
                elif overall_trend in ["deteriorating", "slightly_deteriorating"]:
                    weaknesses.append(f"Performance trend is {overall_trend}")
                
                # Check PnL trend
                pnl_trend = trends.get("pnl_trend", "stable")
                if pnl_trend == "improving":
                    strengths.append("PnL trend is improving")
                elif pnl_trend == "deteriorating":
                    weaknesses.append("PnL trend is deteriorating")
            
            # Check market condition performance
            if "market_condition_performance" in strategy_analysis and "market_preferences" in strategy_analysis["market_condition_performance"]:
                preferences = strategy_analysis["market_condition_performance"]["market_preferences"]
                
                if preferences:
                    for condition, data in preferences.items():
                        preferred = data.get("preferred_condition", "")
                        advantage = data.get("advantage", 0)
                        
                        if advantage >= 0.5:
                            strengths.append(f"Strong performance in {preferred} conditions (advantage: {advantage:.1f}x)")
                        elif advantage >= 0.2:
                            strengths.append(f"Good performance in {preferred} conditions")
            
            # Check trading pair performance
            if "trading_pair_performance" in strategy_analysis:
                pair_perf = strategy_analysis["trading_pair_performance"]
                
                profitable_pairs = pair_perf.get("profitable_pairs_count", 0)
                total_pairs = pair_perf.get("total_pairs", 0)
                
                if total_pairs > 0:
                    profitable_pairs_pct = (profitable_pairs / total_pairs) * 100
                    
                    if profitable_pairs_pct >= 70:
                        strengths.append(f"High percentage of profitable trading pairs ({profitable_pairs_pct:.1f}%)")
                    elif profitable_pairs_pct < 40:
                        weaknesses.append(f"Low percentage of profitable trading pairs ({profitable_pairs_pct:.1f}%)")
                
                # Check concentration
                concentration = pair_perf.get("pair_concentration", 0)
                if concentration > 0.5:
                    weaknesses.append(f"High concentration of PnL in few trading pairs ({concentration:.1%})")
            
            return {
                "strengths": strengths[:5],  # Limit to top 5 strengths
                "weaknesses": weaknesses[:5]  # Limit to top 5 weaknesses
            }
        
        except Exception as e:
            self.logger.error(f"Error identifying strengths and weaknesses: {str(e)}")
            return {"strengths": [], "weaknesses": []}
    
    def _analyze_strategy_comparison(self, strategy_analyses: Dict[str, Dict[str, Any]]) -> Dict[str, Any]:
        """
        Compare multiple strategies and identify the best performers.
        
        Args:
            strategy_analyses: Dictionary with analysis results for each strategy
            
        Returns:
            Dictionary with comparison results
        """
        if not strategy_analyses:
            return {}
        
        try:
            # Extract key metrics from each strategy
            comparison_metrics = {}
            
            for strategy_id, analysis in strategy_analyses.items():
                metrics = {}
                
                # Get PnL metrics
                if "pnl_metrics" in analysis:
                    pnl_metrics = analysis["pnl_metrics"]
                    metrics["total_pnl"] = pnl_metrics.get("total_pnl", 0)
                    
                    if "daily_metrics" in pnl_metrics:
                        daily = pnl_metrics["daily_metrics"]
                        metrics["sharpe_ratio"] = daily.get("sharpe_ratio", 0)
                        metrics["max_drawdown"] = daily.get("max_drawdown", 1.0)
                        metrics["profitable_days_pct"] = daily.get("positive_days_percent", 0)
                
                # Get trade metrics
                if "trade_metrics" in analysis and "pnl_metrics" in analysis["trade_metrics"]:
                    trade_pnl = analysis["trade_metrics"]["pnl_metrics"]
                    metrics["win_rate"] = trade_pnl.get("win_rate", 0)
                    metrics["profit_factor"] = trade_pnl.get("profit_factor", 0)
                
                # Get health score
                metrics["health_score"] = analysis.get("health_score", 0)
                
                # Get trend info
                if "performance_trends" in analysis and "summary" in analysis["performance_trends"]:
                    trends = analysis["performance_trends"]["summary"]
                    metrics["overall_trend"] = trends.get("overall_trend", "stable")
                
                comparison_metrics[strategy_id] = metrics
            
            # Identify best performers in different categories
            best_performers = {}
            
            # Best by total PnL
            if all("total_pnl" in metrics for metrics in comparison_metrics.values()):
                best_pnl = max(comparison_metrics.items(), key=lambda x: x[1]["total_pnl"])
                best_performers["best_pnl"] = {
                    "strategy_id": best_pnl[0],
                    "total_pnl": best_pnl[1]["total_pnl"]
                }
            
            # Best by Sharpe ratio
            if all("sharpe_ratio" in metrics for metrics in comparison_metrics.values()):
                best_sharpe = max(comparison_metrics.items(), key=lambda x: x[1]["sharpe_ratio"])
                best_performers["best_risk_adjusted"] = {
                    "strategy_id": best_sharpe[0],
                    "sharpe_ratio": best_sharpe[1]["sharpe_ratio"]
                }
            
            # Best by win rate
            if all("win_rate" in metrics for metrics in comparison_metrics.values()):
                best_win_rate = max(comparison_metrics.items(), key=lambda x: x[1]["win_rate"])
                best_performers["best_win_rate"] = {
                    "strategy_id": best_win_rate[0],
                    "win_rate": best_win_rate[1]["win_rate"]
                }
            
            # Best by health score
            if all("health_score" in metrics for metrics in comparison_metrics.values()):
                best_health = max(comparison_metrics.items(), key=lambda x: x[1]["health_score"])
                best_performers["best_overall"] = {
                    "strategy_id": best_health[0],
                    "health_score": best_health[1]["health_score"]
                }
            
            # Strategies with improving trend
            improving_strategies = [
                strategy_id for strategy_id, metrics in comparison_metrics.items()
                if "overall_trend" in metrics and metrics["overall_trend"] in ["improving", "slightly_improving"]
            ]
            
            # Strategies with deteriorating trend
            deteriorating_strategies = [
                strategy_id for strategy_id, metrics in comparison_metrics.items()
                if "overall_trend" in metrics and metrics["overall_trend"] in ["deteriorating", "slightly_deteriorating"]
            ]
            
            # Calculate correlation between strategies
            correlation_matrix = self._calculate_strategy_correlation(strategy_analyses)
            
            # Calculate diversification score
            diversification_score = self._calculate_diversification_score(correlation_matrix)
            
            return {
                "best_performers": best_performers,
                "improving_strategies": improving_strategies,
                "deteriorating_strategies": deteriorating_strategies,
                "strategy_correlation": correlation_matrix,
                "diversification_score": diversification_score,
                "total_strategies": len(strategy_analyses)
            }
        
        except Exception as e:
            self.logger.error(f"Error analyzing strategy comparison: {str(e)}")
            return {"error": str(e)}
    
    def _calculate_strategy_correlation(self, strategy_analyses: Dict[str, Dict[str, Any]]) -> Dict[str, Dict[str, float]]:
        """
        Calculate correlation between strategies based on daily PnL.
        
        Args:
            strategy_analyses: Dictionary with analysis results for each strategy
            
        Returns:
            Dictionary with correlation matrix
        """
        # Extract daily PnL for each strategy
        daily_pnl_by_strategy = {}
        
        for strategy_id, analysis in strategy_analyses.items():
            if "pnl_metrics" in analysis and "daily_metrics" in analysis["pnl_metrics"]:
                # We need the raw PnL data to calculate correlation
                if "pnl_data" in analysis and not analysis["pnl_data"].empty:
                    pnl_data = analysis["pnl_data"]
                    
                    if "timestamp" in pnl_data.columns and "value" in pnl_data.columns:
                        # Group by day
                        pnl_data['date'] = pnl_data['timestamp'].dt.date
                        daily_pnl = pnl_data.groupby('date')['value'].sum()
                        
                        daily_pnl_by_strategy[strategy_id] = daily_pnl
        
        # If we have at least 2 strategies with daily PnL data
        if len(daily_pnl_by_strategy) >= 2:
            # Align dates across strategies
            all_dates = set()
            for daily_pnl in daily_pnl_by_strategy.values():
                all_dates.update(daily_pnl.index)
            
            # Create a DataFrame with aligned data
            aligned_data = pd.DataFrame(index=sorted(all_dates))
            
            for strategy_id, daily_pnl in daily_pnl_by_strategy.items():
                aligned_data[strategy_id] = daily_pnl
            
            # Fill missing values with 0
            aligned_data.fillna(0, inplace=True)
            
            # Calculate correlation matrix
            corr_matrix = aligned_data.corr().to_dict()
            
            # Format result
            result = {}
            for strategy1, correlations in corr_matrix.items():
                result[strategy1] = {
                    strategy2: float(corr) for strategy2, corr in correlations.items()
                }
            
            return result
        
        return {}
    
    def _calculate_diversification_score(self, correlation_matrix: Dict[str, Dict[str, float]]) -> float:
        """
        Calculate a diversification score based on strategy correlations.
        
        Args:
            correlation_matrix: Dictionary with correlation matrix
            
        Returns:
            Diversification score (0-100)
        """
        if not correlation_matrix:
            return 0.0
        
        try:
            # Extract all correlation values (excluding self-correlations)
            correlations = []
            
            for strategy1, values in correlation_matrix.items():
                for strategy2, corr in values.items():
                    if strategy1 != strategy2:
                        correlations.append(corr)
            
            if not correlations:
                return 0.0
            
            # Calculate average absolute correlation
            avg_abs_corr = sum(abs(c) for c in correlations) / len(correlations)
            
            # Convert to diversification score (0-100)
            # Lower correlation means higher diversification
            diversification_score = (1 - avg_abs_corr) * 100
            
            return float(diversification_score)
        
        except Exception as e:
            self.logger.error(f"Error calculating diversification score: {str(e)}")
            return 0.0
    
    def _generate_recommendations(self, strategy_analyses: Dict[str, Dict[str, Any]],
                                comparative_analysis: Dict[str, Any]) -> List[Dict[str, Any]]:
        """
        Generate actionable recommendations for strategy optimization.
        
        Args:
            strategy_analyses: Dictionary with analysis results for each strategy
            comparative_analysis: Dictionary with comparative analysis results
            
        Returns:
            List of recommendations
        """
        recommendations = []
        
        try:
            # System-level recommendations
            
            # Check diversification
            diversification_score = comparative_analysis.get("diversification_score", 0)
            if diversification_score < 50:
                recommendations.append({
                    "level": "system",
                    "type": "diversification",
                    "priority": "high" if diversification_score < 30 else "medium",
                    "recommendation": "Improve strategy diversification to reduce correlation between strategies.",
                    "details": {
                        "current_diversification": diversification_score,
                        "target_diversification": 70,
                        "high_correlation_pairs": self._get_high_correlation_pairs(
                            comparative_analysis.get("strategy_correlation", {})
                        )
                    }
                })
            
            # Strategy-specific recommendations
            for strategy_id, analysis in strategy_analyses.items():
                # Check for negative PnL
                if "pnl_metrics" in analysis and analysis["pnl_metrics"].get("total_pnl", 0) < 0:
                    recommendations.append({
                        "level": "strategy",
                        "type": "performance",
                        "priority": "high",
                        "strategy_id": strategy_id,
                        "recommendation": f"Consider pausing or recalibrating strategy '{strategy_id}' which is currently unprofitable.",
                        "details": {
                            "total_pnl": analysis["pnl_metrics"].get("total_pnl", 0),
                            "health_score": analysis.get("health_score", 0)
                        }
                    })
                
                # Check for deteriorating performance
                if ("performance_trends" in analysis and 
                    "summary" in analysis["performance_trends"] and
                    analysis["performance_trends"]["summary"].get("overall_trend", "") == "deteriorating"):
                    
                    recommendations.append({
                        "level": "strategy",
                        "type": "trend",
                        "priority": "high",
                        "strategy_id": strategy_id,
                        "recommendation": f"Investigate and address deteriorating performance of strategy '{strategy_id}'.",
                        "details": {
                            "trend_data": analysis["performance_trends"].get("short_term", {}),
                            "weaknesses": analysis.get("weaknesses", [])
                        }
                    })
                
                # Check for market condition preferences
                if ("market_condition_performance" in analysis and 
                    "market_preferences" in analysis["market_condition_performance"] and
                    analysis["market_condition_performance"]["market_preferences"]):
                    
                    preferences = analysis["market_condition_performance"]["market_preferences"]
                    best_advantage = 0
                    best_condition = ""
                    
                    for condition, data in preferences.items():
                        if data.get("advantage", 0) > best_advantage:
                            best_advantage = data.get("advantage", 0)
                            best_condition = data.get("preferred_condition", "")
                    
                    if best_advantage >= 0.5:
                        recommendations.append({
                            "level": "strategy",
                            "type": "optimization",
                            "priority": "medium",
                            "strategy_id": strategy_id,
                            "recommendation": f"Optimize strategy '{strategy_id}' to focus on {best_condition} market conditions.",
                            "details": {
                                "preferred_condition": best_condition,
                                "performance_advantage": best_advantage
                            }
                        })
                
                # Trading pair recommendations
                if "trading_pair_performance" in analysis:
                    pair_perf = analysis["trading_pair_performance"]
                    
                    # Check for pairs to remove
                    if "bottom_pairs" in pair_perf and pair_perf["bottom_pairs"]:
                        worst_pairs = []
                        
                        for pair_data in pair_perf["bottom_pairs"]:
                            if (pair_data.get("total_pnl", 0) < 0 and 
                                pair_data.get("total_trades", 0) >= 5):
                                worst_pairs.append(pair_data)
                        
                        if worst_pairs:
                            recommendations.append({
                                "level": "strategy",
                                "type": "trading_pairs",
                                "priority": "medium",
                                "strategy_id": strategy_id,
                                "recommendation": f"Consider removing consistently unprofitable trading pairs from strategy '{strategy_id}'.",
                                "details": {
                                    "pairs_to_remove": [
                                        {"pair": p["trading_pair"], "pnl": p["total_pnl"]}
                                        for p in worst_pairs
                                    ]
                                }
                            })
                
                # Check signal-trade efficiency
                if ("signal_trade_efficiency" in analysis and 
                    analysis["signal_trade_efficiency"].get("conversion_rate", 1.0) < 0.5):
                    
                    recommendations.append({
                        "level": "strategy",
                        "type": "execution",
                        "priority": "medium",
                        "strategy_id": strategy_id,
                        "recommendation": f"Improve signal-to-trade conversion rate for strategy '{strategy_id}'.",
                        "details": {
                            "current_rate": analysis["signal_trade_efficiency"].get("conversion_rate", 0),
                            "target_rate": 0.8,
                            "unexecuted_signals": analysis["signal_trade_efficiency"].get("signal_trade_mapping", {}).get("unexecuted_signals", 0)
                        }
                    })
                
                # Check win rate
                if ("trade_metrics" in analysis and 
                    "pnl_metrics" in analysis["trade_metrics"] and
                    analysis["trade_metrics"]["pnl_metrics"].get("win_rate", 1.0) < 0.4):
                    
                    recommendations.append({
                        "level": "strategy",
                        "type": "optimization",
                        "priority": "high",
                        "strategy_id": strategy_id,
                        "recommendation": f"Improve win rate for strategy '{strategy_id}'.",
                        "details": {
                            "current_win_rate": analysis["trade_metrics"]["pnl_metrics"].get("win_rate", 0),
                            "target_win_rate": 0.5,
                            "total_trades": analysis["trade_metrics"].get("completed_trades", 0)
                        }
                    })
            
            # Sort recommendations by priority
            priority_order = {"high": 0, "medium": 1, "low": 2}
            recommendations.sort(key=lambda x: priority_order.get(x["priority"], 3))
            
            return recommendations[:10]  # Limit to top 10 recommendations
        
        except Exception as e:
            self.logger.error(f"Error generating recommendations: {str(e)}")
            return []
    
    def _get_high_correlation_pairs(self, correlation_matrix: Dict[str, Dict[str, float]]) -> List[Dict[str, Any]]:
        """
        Find pairs of strategies with high correlation.
        
        Args:
            correlation_matrix: Dictionary with correlation matrix
            
        Returns:
            List of highly correlated strategy pairs
        """
        high_corr_pairs = []
        
        for strategy1, correlations in correlation_matrix.items():
            for strategy2, corr in correlations.items():
                if strategy1 != strategy2 and corr > 0.7:
                    # Check if the reverse pair is already in the list
                    if not any(p["strategy1"] == strategy2 and p["strategy2"] == strategy1 
                             for p in high_corr_pairs):
                        high_corr_pairs.append({
                            "strategy1": strategy1,
                            "strategy2": strategy2,
                            "correlation": corr
                        })
        
        # Sort by correlation (highest first)
        high_corr_pairs.sort(key=lambda x: x["correlation"], reverse=True)
        
        return high_corr_pairs
    
    def _calculate_histogram(self, data: pd.Series, bins: int) -> Dict[str, int]:
        """
        Calculate a histogram for a series of values.
        
        Args:
            data: Series of values
            bins: Number of bins for the histogram
            
        Returns:
            Dictionary mapping bin ranges to counts
        """
        if data.empty:
            return {}
        
        hist, bin_edges = np.histogram(data, bins=bins)
        
        result = {}
        for i in range(len(hist)):
            bin_range = f"{bin_edges[i]:.2f}-{bin_edges[i+1]:.2f}"
            result[bin_range] = int(hist[i])
        
        return result