#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Base Analyzer class that provides common functionality for all analyzers.
"""

import abc
import logging
import asyncio
from datetime import datetime, timedelta
import numpy as np
import pandas as pd
from typing import Dict, List, Optional, Union, Any, Tuple

class AnalyzerBase(abc.ABC):
    """
    Base class for all analyzer components. Defines common interface and
    shared functionality for data analysis.
    """
    
    def __init__(self, config: Dict[str, Any], db_connector=None):
        """
        Initialize the analyzer with configuration and database connection.
        
        Args:
            config: Dictionary containing analyzer configuration
            db_connector: Database connector for accessing time series data
        """
        self.logger = logging.getLogger(self.__class__.__name__)
        self.config = config
        self.db_connector = db_connector
        self.last_analysis_time = datetime.now()
        self.anomaly_thresholds = self.config.get('anomaly_thresholds', {})
        
    @abc.abstractmethod
    async def analyze(self, start_time: datetime, end_time: datetime) -> Dict[str, Any]:
        """
        Main analysis method to be implemented by all analyzer subclasses.
        
        Args:
            start_time: Beginning of analysis time window
            end_time: End of analysis time window
            
        Returns:
            Dictionary containing analysis results
        """
        pass
    
    async def fetch_data(self, metric_name: str, start_time: datetime, 
                        end_time: datetime, filters: Optional[Dict[str, str]] = None) -> pd.DataFrame:
        """
        Fetch time series data for a specific metric.
        
        Args:
            metric_name: Name of the metric to fetch
            start_time: Start time for the data window
            end_time: End time for the data window
            filters: Optional filters to apply to the query
            
        Returns:
            DataFrame containing the requested time series data
        """
        if self.db_connector is None:
            self.logger.error("Database connector not initialized")
            return pd.DataFrame()
            
        try:
            query_result = await self.db_connector.query_timeseries(
                metric_name=metric_name,
                start_time=start_time,
                end_time=end_time,
                filters=filters
            )
            return pd.DataFrame(query_result)
        except Exception as e:
            self.logger.error(f"Error fetching data: {str(e)}")
            return pd.DataFrame()
    
    def detect_anomalies(self, data: pd.DataFrame, metric_name: str, 
                         method: str = 'zscore', window: int = 20) -> pd.DataFrame:
        """
        Detect anomalies in time series data using various statistical methods.
        
        Args:
            data: DataFrame containing time series data
            metric_name: Name of the metric being analyzed
            method: Anomaly detection method ('zscore', 'iqr', 'ewma')
            window: Window size for rolling calculations
            
        Returns:
            DataFrame with original data and anomaly flags
        """
        if data.empty:
            return pd.DataFrame()
            
        # Ensure we have the value column
        if 'value' not in data.columns:
            self.logger.warning(f"Expected 'value' column not found in data for {metric_name}")
            return data
            
        # Copy data to avoid modifying the original
        result = data.copy()
        result['anomaly'] = False
        
        try:
            threshold = self.anomaly_thresholds.get(metric_name, 3.0)
            
            if method == 'zscore':
                # Z-score method
                rolling_mean = data['value'].rolling(window=window).mean()
                rolling_std = data['value'].rolling(window=window).std()
                z_scores = np.abs((data['value'] - rolling_mean) / rolling_std)
                result['anomaly'] = z_scores > threshold
                result['anomaly_score'] = z_scores
                
            elif method == 'iqr':
                # Interquartile Range method
                q1 = data['value'].rolling(window=window).quantile(0.25)
                q3 = data['value'].rolling(window=window).quantile(0.75)
                iqr = q3 - q1
                lower_bound = q1 - threshold * iqr
                upper_bound = q3 + threshold * iqr
                result['anomaly'] = (data['value'] < lower_bound) | (data['value'] > upper_bound)
                result['anomaly_score'] = np.abs((data['value'] - (q1 + q3) / 2) / iqr)
                
            elif method == 'ewma':
                # Exponentially Weighted Moving Average
                alpha = 0.2  # Smoothing factor
                ewma = data['value'].ewm(alpha=alpha).mean()
                residuals = np.abs(data['value'] - ewma)
                mean_residual = residuals.ewm(alpha=alpha).mean()
                result['anomaly'] = residuals > threshold * mean_residual
                result['anomaly_score'] = residuals / mean_residual
                
            else:
                self.logger.warning(f"Unknown anomaly detection method: {method}")
                
        except Exception as e:
            self.logger.error(f"Error in anomaly detection: {str(e)}")
            
        return result
    
    def calculate_baseline(self, data: pd.DataFrame, metric_name: str, 
                          window_days: int = 7) -> Dict[str, float]:
        """
        Calculate baseline statistics for a metric.
        
        Args:
            data: DataFrame containing historical data
            metric_name: Name of the metric being analyzed
            window_days: Number of days to include in baseline
            
        Returns:
            Dictionary with baseline statistics
        """
        if data.empty or 'value' not in data.columns:
            return {}
            
        try:
            stats = {
                'mean': float(data['value'].mean()),
                'median': float(data['value'].median()),
                'std_dev': float(data['value'].std()),
                'min': float(data['value'].min()),
                'max': float(data['value'].max()),
                'p95': float(data['value'].quantile(0.95)),
                'p99': float(data['value'].quantile(0.99))
            }
            return stats
        except Exception as e:
            self.logger.error(f"Error calculating baseline: {str(e)}")
            return {}
    
    async def get_correlation_matrix(self, metrics: List[str], 
                                    start_time: datetime, end_time: datetime) -> pd.DataFrame:
        """
        Calculate correlation matrix between multiple metrics.
        
        Args:
            metrics: List of metric names to correlate
            start_time: Start time for analysis window
            end_time: End time for analysis window
            
        Returns:
            DataFrame containing correlation matrix
        """
        all_data = {}
        
        for metric in metrics:
            df = await self.fetch_data(metric, start_time, end_time)
            if not df.empty and 'value' in df.columns:
                all_data[metric] = df['value']
                
        if not all_data:
            return pd.DataFrame()
            
        # Create a DataFrame with all metrics and calculate correlation
        combined_df = pd.DataFrame(all_data)
        return combined_df.corr()