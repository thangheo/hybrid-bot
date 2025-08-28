#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Latency Analyzer module for analyzing system latency metrics.
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
from .analyzer_base import AnalyzerBase

class LatencyAnalyzer(AnalyzerBase):
    """
    Analyzer for system latency metrics.
    
    This analyzer processes latency data from various system components
    to identify bottlenecks, anomalies, and performance trends.
    """
    
    def __init__(self, config: Dict[str, Any], 
                 timeseries_db: TimeSeriesDB,
                 operational_db: Optional[OperationalDB] = None):
        """
        Initialize the latency analyzer.
        
        Args:
            config: Configuration dictionary
            timeseries_db: Time series database connector
            operational_db: Optional operational database connector
        """
        super().__init__(config, timeseries_db)
        self.operational_db = operational_db
        self.latency_metric_prefix = self.config.get('latency_metric_prefix', 'system.latency')
        self.slo_thresholds = self.config.get('slo_thresholds', {
            'critical_path': 50,  # milliseconds
            'execution': 100,     # milliseconds
            'data_ingestion': 30, # milliseconds
            'strategy': 200,      # milliseconds
            'risk': 20,           # milliseconds
            'default': 100        # milliseconds
        })
        self.percentiles = self.config.get('percentiles', [50, 95, 99, 99.9])
        self.baseline_window_days = self.config.get('baseline_window_days', 7)
        self.component_paths = self.config.get('component_paths', {
            'data_ingestion': [
                'websocket.connection', 
                'orderbook.reconstruction', 
                'data.normalization'
            ],
            'strategy': [
                'orderbook.analysis', 
                'imbalance.detection', 
                'signal.generation'
            ],
            'execution': [
                'order.validation', 
                'order.routing', 
                'order.placement', 
                'order.tracking'
            ],
            'risk': [
                'pretrade.validation', 
                'position.tracking', 
                'limit.checking'
            ],
            'monitoring': [
                'metrics.collection',
                'alert.processing'
            ]
        })
        
    async def analyze(self, start_time: datetime, end_time: datetime) -> Dict[str, Any]:
        """
        Analyze latency metrics within the specified time window.
        
        Args:
            start_time: Start of analysis window
            end_time: End of analysis window
            
        Returns:
            Dictionary containing analysis results
        """
        self.logger.info(f"Analyzing latency metrics from {start_time} to {end_time}")
        
        # Fetch components and their latency metrics
        components = await self._fetch_components()
        
        if not components:
            self.logger.warning("No components found for latency analysis")
            return {"error": "No components found", "status": "error"}
        
        # Analyze each component
        component_analyses = {}
        critical_path_components = []
        
        for component, paths in components.items():
            component_analysis = await self._analyze_component_latency(
                component, paths, start_time, end_time
            )
            
            component_analyses[component] = component_analysis
            
            # Check if component is in critical path
            if component_analysis.get('is_critical_path', False):
                critical_path_components.append({
                    "component": component,
                    "avg_latency": component_analysis.get("overall_metrics", {}).get("avg_latency_ms", 0)
                })
        
        # Calculate end-to-end latency
        end_to_end_analysis = await self._analyze_end_to_end_latency(start_time, end_time)
        
        # Analyze critical path
        critical_path_analysis = self._analyze_critical_path(critical_path_components)
        
        # Generate insights
        insights = self._generate_latency_insights(
            component_analyses, end_to_end_analysis, critical_path_analysis
        )
        
        # Generate recommendations
        recommendations = self._generate_recommendations(
            component_analyses, end_to_end_analysis, critical_path_analysis
        )
        
        self.last_analysis_time = datetime.now()
        
        return {
            "status": "success",
            "analysis_period": {
                "start": start_time,
                "end": end_time,
                "duration_hours": (end_time - start_time).total_seconds() / 3600
            },
            "components_analyzed": len(component_analyses),
            "component_analyses": component_analyses,
            "end_to_end_latency": end_to_end_analysis,
            "critical_path": critical_path_analysis,
            "insights": insights,
            "recommendations": recommendations
        }
    
    async def _fetch_components(self) -> Dict[str, List[str]]:
        """
        Fetch available components and their latency paths.
        
        Returns:
            Dictionary mapping component names to their latency paths
        """
        return self.component_paths
    
    async def _analyze_component_latency(self, component: str, paths: List[str], 
                                       start_time: datetime, end_time: datetime) -> Dict[str, Any]:
        """
        Analyze latency metrics for a specific component.
        
        Args:
            component: Component name
            paths: List of latency paths within the component
            start_time: Start of analysis window
            end_time: End of analysis window
            
        Returns:
            Dictionary with component latency analysis
        """
        try:
            # Define metric names for each path
            path_metrics = {}
            
            for path in paths:
                metric_name = f"{self.latency_metric_prefix}.{component}.{path}"
                
                # Fetch latency data
                latency_data = await self.fetch_data(metric_name, start_time, end_time)
                
                if not latency_data.empty and 'value' in latency_data.columns:
                    path_metrics[path] = latency_data
            
            if not path_metrics:
                return {"status": "no_data"}
            
            # Calculate overall component metrics
            all_latencies = pd.Series(dtype=float)
            
            for path, data in path_metrics.items():
                if not data.empty and 'value' in data.columns:
                    all_latencies = pd.concat([all_latencies, data['value']])
            
            if all_latencies.empty:
                return {"status": "no_data"}
            
            # Calculate percentiles
            percentile_values = {}
            for p in self.percentiles:
                percentile_values[f"p{p}"] = float(np.percentile(all_latencies, p))
            
            # Calculate basic statistics
            avg_latency = float(all_latencies.mean())
            median_latency = float(all_latencies.median())
            min_latency = float(all_latencies.min())
            max_latency = float(all_latencies.max())
            std_dev = float(all_latencies.std())
            
            # Calculate SLO compliance
            slo_threshold = self.slo_thresholds.get(component, self.slo_thresholds['default'])
            slo_violations = (all_latencies > slo_threshold).sum()
            slo_compliance = 1 - (slo_violations / len(all_latencies)) if len(all_latencies) > 0 else 1
            
            # Analyze individual paths
            path_analyses = {}
            
            for path, data in path_metrics.items():
                if not data.empty and 'value' in data.columns:
                    path_analyses[path] = self._analyze_path_latency(path, data, slo_threshold)
            
            # Calculate latency trend
            trend_analysis = self._analyze_latency_trend(all_latencies)
            
            # Detect anomalies
            anomalies = self.detect_anomalies(
                pd.DataFrame({'value': all_latencies}), 
                f"{component}_latency"
            )
            
            # Determine critical path
            is_critical_path = component in ['data_ingestion', 'execution', 'strategy']
            
            # Determine if latency is concerning
            is_concerning = (
                slo_compliance < 0.95 or  # Less than 95% SLO compliance
                trend_analysis.get('direction', '') == 'increasing' or  # Increasing trend
                percentile_values.get('p99', 0) > slo_threshold * 2  # P99 latency > 2x threshold
            )
            
            # Compare with baseline
            baseline_comparison = await self._compare_with_baseline(component, all_latencies, start_time)
            
            return {
                "status": "success",
                "overall_metrics": {
                    "avg_latency_ms": avg_latency,
                    "median_latency_ms": median_latency,
                    "min_latency_ms": min_latency,
                    "max_latency_ms": max_latency,
                    "std_dev_ms": std_dev,
                    "percentiles": percentile_values,
                    "slo_threshold_ms": slo_threshold,
                    "slo_compliance": float(slo_compliance),
                    "slo_violations": int(slo_violations),
                    "sample_count": len(all_latencies)
                },
                "path_analyses": path_analyses,
                "trend": trend_analysis,
                "anomalies": self._extract_anomalies(anomalies),
                "baseline_comparison": baseline_comparison,
                "is_critical_path": is_critical_path,
                "is_concerning": is_concerning
            }
        
        except Exception as e:
            self.logger.error(f"Error analyzing component latency for {component}: {str(e)}")
            return {"status": "error", "message": str(e)}
    
    def _analyze_path_latency(self, path: str, data: pd.DataFrame, slo_threshold: float) -> Dict[str, Any]:
        """
        Analyze latency metrics for a specific path.
        
        Args:
            path: Path name
            data: DataFrame containing latency data
            slo_threshold: SLO threshold in milliseconds
            
        Returns:
            Dictionary with path latency analysis
        """
        if data.empty or 'value' not in data.columns:
            return {"status": "no_data"}
        
        try:
            # Calculate percentiles
            percentile_values = {}
            for p in self.percentiles:
                percentile_values[f"p{p}"] = float(np.percentile(data['value'], p))
            
            # Calculate basic statistics
            avg_latency = float(data['value'].mean())
            median_latency = float(data['value'].median())
            min_latency = float(data['value'].min())
            max_latency = float(data['value'].max())
            std_dev = float(data['value'].std())
            
            # Calculate SLO compliance
            slo_violations = (data['value'] > slo_threshold).sum()
            slo_compliance = 1 - (slo_violations / len(data)) if len(data) > 0 else 1
            
            # Detect anomalies
            anomalies = self.detect_anomalies(data, f"{path}_latency")
            
            return {
                "avg_latency_ms": avg_latency,
                "median_latency_ms": median_latency,
                "min_latency_ms": min_latency,
                "max_latency_ms": max_latency,
                "std_dev_ms": std_dev,
                "percentiles": percentile_values,
                "slo_compliance": float(slo_compliance),
                "slo_violations": int(slo_violations),
                "sample_count": len(data),
                "anomalies": self._extract_anomalies(anomalies)
            }
        
        except Exception as e:
            self.logger.error(f"Error analyzing path latency for {path}: {str(e)}")
            return {"status": "error", "message": str(e)}
    
    def _analyze_latency_trend(self, latency_series: pd.Series) -> Dict[str, Any]:
        """
        Analyze trend in latency data.
        
        Args:
            latency_series: Series of latency values
            
        Returns:
            Dictionary with trend analysis
        """
        if latency_series.empty or len(latency_series) < 5:
            return {"status": "insufficient_data"}
        
        try:
            # Convert to DataFrame with indices for regression
            df = pd.DataFrame({
                'index': np.arange(len(latency_series)),
                'latency': latency_series.values
            })
            
            # Perform linear regression
            slope, intercept, r_value, p_value, std_err = stats.linregress(
                df['index'], df['latency']
            )
            
            # Determine trend direction
            if abs(slope) < 0.001 or p_value > 0.05:
                direction = "stable"
            elif slope > 0:
                direction = "increasing"
            else:
                direction = "decreasing"
            
            # Calculate projected values
            current_value = intercept + slope * (len(latency_series) - 1)
            projected_value_10 = intercept + slope * (len(latency_series) + 9)
            percent_change_10 = ((projected_value_10 / current_value) - 1) * 100 if current_value != 0 else 0
            
            return {
                "slope": float(slope),
                "r_squared": float(r_value ** 2),
                "p_value": float(p_value),
                "direction": direction,
                "significance": "significant" if p_value <= 0.05 else "not_significant",
                "projected_change_10_samples": float(percent_change_10)
            }
        
        except Exception as e:
            self.logger.error(f"Error analyzing latency trend: {str(e)}")
            return {"status": "error", "message": str(e)}
    
    async def _compare_with_baseline(self, component: str, current_latencies: pd.Series, 
                                   end_time: datetime) -> Dict[str, Any]:
        """
        Compare current latency with historical baseline.
        
        Args:
            component: Component name
            current_latencies: Series of current latency values
            end_time: End of the current analysis window
            
        Returns:
            Dictionary with baseline comparison results
        """
        try:
            # Calculate baseline period
            baseline_start = end_time - timedelta(days=self.baseline_window_days)
            baseline_end = end_time - timedelta(days=1)
            
            # Fetch baseline data for all paths in the component
            baseline_data = pd.Series(dtype=float)
            
            for path in self.component_paths.get(component, []):
                metric_name = f"{self.latency_metric_prefix}.{component}.{path}"
                
                path_data = await self.fetch_data(metric_name, baseline_start, baseline_end)
                
                if not path_data.empty and 'value' in path_data.columns:
                    baseline_data = pd.concat([baseline_data, path_data['value']])
            
            if baseline_data.empty:
                return {"status": "no_baseline_data"}
            
            # Calculate baseline statistics
            baseline_avg = float(baseline_data.mean())
            baseline_median = float(baseline_data.median())
            baseline_p95 = float(np.percentile(baseline_data, 95))
            baseline_p99 = float(np.percentile(baseline_data, 99))
            
            # Calculate current statistics
            current_avg = float(current_latencies.mean())
            current_median = float(current_latencies.median())
            current_p95 = float(np.percentile(current_latencies, 95))
            current_p99 = float(np.percentile(current_latencies, 99))
            
            # Calculate percent changes
            avg_change = ((current_avg / baseline_avg) - 1) * 100 if baseline_avg != 0 else 0
            median_change = ((current_median / baseline_median) - 1) * 100 if baseline_median != 0 else 0
            p95_change = ((current_p95 / baseline_p95) - 1) * 100 if baseline_p95 != 0 else 0
            p99_change = ((current_p99 / baseline_p99) - 1) * 100 if baseline_p99 != 0 else 0
            
            # Determine overall change
            if abs(avg_change) < 5 and abs(p95_change) < 10:
                change_assessment = "stable"
            elif avg_change > 10 or p95_change > 20:
                change_assessment = "significantly_worse"
            elif avg_change > 5 or p95_change > 10:
                change_assessment = "worse"
            elif avg_change < -10 or p95_change < -20:
                change_assessment = "significantly_better"
            elif avg_change < -5 or p95_change < -10:
                change_assessment = "better"
            else:
                change_assessment = "mixed"
            
            return {
                "baseline_period": {
                    "start": baseline_start,
                    "end": baseline_end,
                    "days": self.baseline_window_days
                },
                "baseline_metrics": {
                    "avg_latency_ms": baseline_avg,
                    "median_latency_ms": baseline_median,
                    "p95_latency_ms": baseline_p95,
                    "p99_latency_ms": baseline_p99,
                    "sample_count": len(baseline_data)
                },
                "current_metrics": {
                    "avg_latency_ms": current_avg,
                    "median_latency_ms": current_median,
                    "p95_latency_ms": current_p95,
                    "p99_latency_ms": current_p99,
                    "sample_count": len(current_latencies)
                },
                "changes": {
                    "avg_change_percent": float(avg_change),
                    "median_change_percent": float(median_change),
                    "p95_change_percent": float(p95_change),
                    "p99_change_percent": float(p99_change)
                },
                "assessment": change_assessment
            }
        
        except Exception as e:
            self.logger.error(f"Error comparing with baseline for {component}: {str(e)}")
            return {"status": "error", "message": str(e)}
    
    async def _analyze_end_to_end_latency(self, start_time: datetime, end_time: datetime) -> Dict[str, Any]:
        """
        Analyze end-to-end system latency.
        
        Args:
            start_time: Start of analysis window
            end_time: End of analysis window
            
        Returns:
            Dictionary with end-to-end latency analysis
        """
        try:
            # Fetch end-to-end latency metric
            metric_name = f"{self.latency_metric_prefix}.end_to_end"
            
            latency_data = await self.fetch_data(metric_name, start_time, end_time)
            
            if latency_data.empty or 'value' not in latency_data.columns:
                return {"status": "no_data"}
            
            # Calculate percentiles
            percentile_values = {}
            for p in self.percentiles:
                percentile_values[f"p{p}"] = float(np.percentile(latency_data['value'], p))
            
            # Calculate basic statistics
            avg_latency = float(latency_data['value'].mean())
            median_latency = float(latency_data['value'].median())
            min_latency = float(latency_data['value'].min())
            max_latency = float(latency_data['value'].max())
            std_dev = float(latency_data['value'].std())
            
            # Calculate SLO compliance
            slo_threshold = self.slo_thresholds.get('end_to_end', 500)  # Higher threshold for end-to-end
            slo_violations = (latency_data['value'] > slo_threshold).sum()
            slo_compliance = 1 - (slo_violations / len(latency_data)) if len(latency_data) > 0 else 1
            
            # Analyze trend
            trend_analysis = self._analyze_latency_trend(latency_data['value'])
            
            # Detect anomalies
            anomalies = self.detect_anomalies(latency_data, "end_to_end_latency")
            
            # Compare with baseline
            baseline_comparison = await self._compare_end_to_end_with_baseline(
                latency_data['value'], end_time
            )
            
            # Distribution by time of day (if timestamp available)
            time_distribution = {}
            if 'timestamp' in latency_data.columns:
                # Convert to datetime if needed
                if not pd.api.types.is_datetime64_any_dtype(latency_data['timestamp']):
                    latency_data['timestamp'] = pd.to_datetime(latency_data['timestamp'])
                
                # Group by hour
                latency_data['hour'] = latency_data['timestamp'].dt.hour
                hourly_latency = latency_data.groupby('hour')['value'].mean()
                
                time_distribution = {
                    "hourly_avg_latency": {
                        str(hour): float(avg_latency) for hour, avg_latency in hourly_latency.items()
                    },
                    "peak_hour": int(hourly_latency.idxmax()) if not hourly_latency.empty else None,
                    "min_hour": int(hourly_latency.idxmin()) if not hourly_latency.empty else None
                }
            
            return {
                "status": "success",
                "overall_metrics": {
                    "avg_latency_ms": avg_latency,
                    "median_latency_ms": median_latency,
                    "min_latency_ms": min_latency,
                    "max_latency_ms": max_latency,
                    "std_dev_ms": std_dev,
                    "percentiles": percentile_values,
                    "slo_threshold_ms": slo_threshold,
                    "slo_compliance": float(slo_compliance),
                    "slo_violations": int(slo_violations),
                    "sample_count": len(latency_data)
                },
                "trend": trend_analysis,
                "anomalies": self._extract_anomalies(anomalies),
                "baseline_comparison": baseline_comparison,
                "time_distribution": time_distribution,
                "is_concerning": (
                    slo_compliance < 0.95 or  # Less than 95% SLO compliance
                    trend_analysis.get('direction', '') == 'increasing' or  # Increasing trend
                    percentile_values.get('p99', 0) > slo_threshold * 2  # P99 latency > 2x threshold
                )
            }
        
        except Exception as e:
            self.logger.error(f"Error analyzing end-to-end latency: {str(e)}")
            return {"status": "error", "message": str(e)}
    
    async def _compare_end_to_end_with_baseline(self, current_latencies: pd.Series, 
                                             end_time: datetime) -> Dict[str, Any]:
        """
        Compare current end-to-end latency with historical baseline.
        
        Args:
            current_latencies: Series of current latency values
            end_time: End of the current analysis window
            
        Returns:
            Dictionary with baseline comparison results
        """
        try:
            # Calculate baseline period
            baseline_start = end_time - timedelta(days=self.baseline_window_days)
            baseline_end = end_time - timedelta(days=1)
            
            # Fetch baseline data
            metric_name = f"{self.latency_metric_prefix}.end_to_end"
            
            baseline_data = await self.fetch_data(metric_name, baseline_start, baseline_end)
            
            if baseline_data.empty or 'value' not in baseline_data.columns:
                return {"status": "no_baseline_data"}
            
            # Calculate baseline statistics
            baseline_avg = float(baseline_data['value'].mean())
            baseline_median = float(baseline_data['value'].median())
            baseline_p95 = float(np.percentile(baseline_data['value'], 95))
            baseline_p99 = float(np.percentile(baseline_data['value'], 99))
            
            # Calculate current statistics
            current_avg = float(current_latencies.mean())
            current_median = float(current_latencies.median())
            current_p95 = float(np.percentile(current_latencies, 95))
            current_p99 = float(np.percentile(current_latencies, 99))
            
            # Calculate percent changes
            avg_change = ((current_avg / baseline_avg) - 1) * 100 if baseline_avg != 0 else 0
            median_change = ((current_median / baseline_median) - 1) * 100 if baseline_median != 0 else 0
            p95_change = ((current_p95 / baseline_p95) - 1) * 100 if baseline_p95 != 0 else 0
            p99_change = ((current_p99 / baseline_p99) - 1) * 100 if baseline_p99 != 0 else 0
            
            # Determine overall change
            if abs(avg_change) < 5 and abs(p95_change) < 10:
                change_assessment = "stable"
            elif avg_change > 10 or p95_change > 20:
                change_assessment = "significantly_worse"
            elif avg_change > 5 or p95_change > 10:
                change_assessment = "worse"
            elif avg_change < -10 or p95_change < -20:
                change_assessment = "significantly_better"
            elif avg_change < -5 or p95_change < -10:
                change_assessment = "better"
            else:
                change_assessment = "mixed"
            
            return {
                "baseline_period": {
                    "start": baseline_start,
                    "end": baseline_end,
                    "days": self.baseline_window_days
                },
                "baseline_metrics": {
                    "avg_latency_ms": baseline_avg,
                    "median_latency_ms": baseline_median,
                    "p95_latency_ms": baseline_p95,
                    "p99_latency_ms": baseline_p99,
                    "sample_count": len(baseline_data)
                },
                "current_metrics": {
                    "avg_latency_ms": current_avg,
                    "median_latency_ms": current_median,
                    "p95_latency_ms": current_p95,
                    "p99_latency_ms": current_p99,
                    "sample_count": len(current_latencies)
                },
                "changes": {
                    "avg_change_percent": float(avg_change),
                    "median_change_percent": float(median_change),
                    "p95_change_percent": float(p95_change),
                    "p99_change_percent": float(p99_change)
                },
                "assessment": change_assessment
            }
        
        except Exception as e:
            self.logger.error(f"Error comparing end-to-end with baseline: {str(e)}")
            return {"status": "error", "message": str(e)}
    
    def _analyze_critical_path(self, critical_path_components: List[Dict[str, Any]]) -> Dict[str, Any]:
        """
        Analyze the critical path components.
        
        Args:
            critical_path_components: List of components in the critical path
            
        Returns:
            Dictionary with critical path analysis
        """
        if not critical_path_components:
            return {"status": "no_data"}
        
        try:
            # Sort by average latency
            sorted_components = sorted(
                critical_path_components, 
                key=lambda x: x.get("avg_latency", 0),
                reverse=True
            )
            
            # Calculate total critical path latency
            total_latency = sum(c.get("avg_latency", 0) for c in critical_path_components)
            
            # Calculate contribution percentages
            components_with_contribution = []
            
            for component in sorted_components:
                latency = component.get("avg_latency", 0)
                contribution = (latency / total_latency) * 100 if total_latency > 0 else 0
                
                components_with_contribution.append({
                    "component": component.get("component", ""),
                    "avg_latency_ms": latency,
                    "contribution_percent": float(contribution)
                })
            
            # Identify bottleneck (component with highest contribution)
            bottleneck = components_with_contribution[0] if components_with_contribution else None
            
            return {
                "status": "success",
                "total_critical_path_latency_ms": float(total_latency),
                "components": components_with_contribution,
                "bottleneck": bottleneck
            }
        
        except Exception as e:
            self.logger.error(f"Error analyzing critical path: {str(e)}")
            return {"status": "error", "message": str(e)}
    
    def _extract_anomalies(self, anomalies_df: pd.DataFrame) -> List[Dict[str, Any]]:
        """
        Extract anomalies from the anomaly detection results.
        
        Args:
            anomalies_df: DataFrame with anomaly detection results
            
        Returns:
            List of anomalies with details
        """
        if anomalies_df.empty or 'anomaly' not in anomalies_df.columns:
            return []
        
        try:
            # Filter to only anomalous points
            anomalies = anomalies_df[anomalies_df['anomaly']]
            
            if anomalies.empty:
                return []
            
            # Format results
            result = []
            
            for _, row in anomalies.iterrows():
                anomaly = {
                    "value": float(row['value']),
                    "anomaly_score": float(row['anomaly_score']) if 'anomaly_score' in row else None
                }
                
                # Add timestamp if available
                if 'timestamp' in row:
                    anomaly["timestamp"] = row['timestamp']
                
                result.append(anomaly)
            
            # Limit to top 10 anomalies by score
            if 'anomaly_score' in anomalies.columns:
                result.sort(key=lambda x: x.get('anomaly_score', 0), reverse=True)
                result = result[:10]
            
            return result
        
        except Exception as e:
            self.logger.error(f"Error extracting anomalies: {str(e)}")
            return []
    
    def _generate_latency_insights(self, component_analyses: Dict[str, Dict[str, Any]],
                                 end_to_end_analysis: Dict[str, Any],
                                 critical_path_analysis: Dict[str, Any]) -> List[Dict[str, Any]]:
        """
        Generate insights from latency analysis.
        
        Args:
            component_analyses: Dictionary with analysis results for each component
            end_to_end_analysis: Dictionary with end-to-end latency analysis
            critical_path_analysis: Dictionary with critical path analysis
            
        Returns:
            List of insights
        """
        insights = []
        
        try:
            # Check if we have valid analyses
            if end_to_end_analysis.get("status") != "success":
                return insights
            
            # Insight 1: Overall system latency health
            end_to_end_metrics = end_to_end_analysis.get("overall_metrics", {})
            slo_compliance = end_to_end_metrics.get("slo_compliance", 1.0)
            
            if slo_compliance >= 0.99:
                insights.append({
                    "type": "system_health",
                    "importance": "high",
                    "insight": "System latency is excellent, with over 99% of requests meeting SLO targets.",
                    "metrics": {
                        "slo_compliance": slo_compliance,
                        "avg_latency_ms": end_to_end_metrics.get("avg_latency_ms", 0)
                    }
                })
            elif slo_compliance >= 0.95:
                insights.append({
                    "type": "system_health",
                    "importance": "medium",
                    "insight": "System latency is good, with over 95% of requests meeting SLO targets.",
                    "metrics": {
                        "slo_compliance": slo_compliance,
                        "avg_latency_ms": end_to_end_metrics.get("avg_latency_ms", 0)
                    }
                })
            else:
                insights.append({
                    "type": "system_health",
                    "importance": "high",
                    "insight": "System latency requires attention, with less than 95% of requests meeting SLO targets.",
                    "metrics": {
                        "slo_compliance": slo_compliance,
                        "avg_latency_ms": end_to_end_metrics.get("avg_latency_ms", 0),
                        "slo_violations": end_to_end_metrics.get("slo_violations", 0)
                    }
                })
            
            # Insight 2: Critical path bottleneck
            if critical_path_analysis.get("status") == "success":
                bottleneck = critical_path_analysis.get("bottleneck", {})
                
                if bottleneck:
                    component = bottleneck.get("component", "")
                    contribution = bottleneck.get("contribution_percent", 0)
                    
                    if contribution > 50:
                        insights.append({
                            "type": "bottleneck",
                            "importance": "high",
                            "insight": f"The {component} component is a major bottleneck, contributing over 50% of critical path latency.",
                            "metrics": {
                                "component": component,
                                "contribution_percent": contribution,
                                "avg_latency_ms": bottleneck.get("avg_latency_ms", 0)
                            }
                        })
                    elif contribution > 30:
                        insights.append({
                            "type": "bottleneck",
                            "importance": "medium",
                            "insight": f"The {component} component is a significant bottleneck, contributing over 30% of critical path latency.",
                            "metrics": {
                                "component": component,
                                "contribution_percent": contribution,
                                "avg_latency_ms": bottleneck.get("avg_latency_ms", 0)
                            }
                        })
            
            # Insight 3: Concerning latency trends
            end_to_end_trend = end_to_end_analysis.get("trend", {})
            
            if end_to_end_trend.get("direction") == "increasing" and end_to_end_trend.get("significance") == "significant":
                projected_change = end_to_end_trend.get("projected_change_10_samples", 0)
                
                if projected_change > 20:
                    insights.append({
                        "type": "trend",
                        "importance": "high",
                        "insight": "System latency is increasing significantly, projecting over 20% increase in the near term.",
                        "metrics": {
                            "projected_change_percent": projected_change,
                            "trend_direction": "increasing",
                            "r_squared": end_to_end_trend.get("r_squared", 0)
                        }
                    })
                elif projected_change > 10:
                    insights.append({
                        "type": "trend",
                        "importance": "medium",
                        "insight": "System latency is showing an upward trend, projecting over 10% increase in the near term.",
                        "metrics": {
                            "projected_change_percent": projected_change,
                            "trend_direction": "increasing",
                            "r_squared": end_to_end_trend.get("r_squared", 0)
                        }
                    })
            elif end_to_end_trend.get("direction") == "decreasing" and end_to_end_trend.get("significance") == "significant":
                projected_change = end_to_end_trend.get("projected_change_10_samples", 0)
                
                if projected_change < -10:
                    insights.append({
                        "type": "trend",
                        "importance": "medium",
                        "insight": "System latency is showing a positive decreasing trend, projecting over 10% improvement in the near term.",
                        "metrics": {
                            "projected_change_percent": projected_change,
                            "trend_direction": "decreasing",
                            "r_squared": end_to_end_trend.get("r_squared", 0)
                        }
                    })
            
            # Insight 4: Concerning component latency
            concerning_components = []
            
            for component, analysis in component_analyses.items():
                if analysis.get("is_concerning", False):
                    concerning_components.append({
                        "component": component,
                        "slo_compliance": analysis.get("overall_metrics", {}).get("slo_compliance", 1.0),
                        "trend": analysis.get("trend", {}).get("direction", "stable")
                    })
            
            if concerning_components:
                if len(concerning_components) > 2:
                    insights.append({
                        "type": "component_health",
                        "importance": "high",
                        "insight": f"Multiple components ({len(concerning_components)}) are showing concerning latency patterns.",
                        "metrics": {
                            "concerning_components": [c["component"] for c in concerning_components],
                            "component_count": len(concerning_components)
                        }
                    })
                else:
                    for component in concerning_components:
                        insights.append({
                            "type": "component_health",
                            "importance": "medium",
                            "insight": f"The {component['component']} component is showing concerning latency patterns.",
                            "metrics": {
                                "component": component["component"],
                                "slo_compliance": component["slo_compliance"],
                                "trend": component["trend"]
                            }
                        })
            
            # Insight 5: Baseline comparison
            baseline_comparison = end_to_end_analysis.get("baseline_comparison", {})
            
            if baseline_comparison.get("assessment") == "significantly_worse":
                avg_change = baseline_comparison.get("changes", {}).get("avg_change_percent", 0)
                p95_change = baseline_comparison.get("changes", {}).get("p95_change_percent", 0)
                
                insights.append({
                    "type": "baseline_comparison",
                    "importance": "high",
                    "insight": "System latency has significantly degraded compared to the baseline period.",
                    "metrics": {
                        "avg_change_percent": avg_change,
                        "p95_change_percent": p95_change,
                        "assessment": "significantly_worse"
                    }
                })
            elif baseline_comparison.get("assessment") == "worse":
                avg_change = baseline_comparison.get("changes", {}).get("avg_change_percent", 0)
                
                insights.append({
                    "type": "baseline_comparison",
                    "importance": "medium",
                    "insight": "System latency has degraded compared to the baseline period.",
                    "metrics": {
                        "avg_change_percent": avg_change,
                        "assessment": "worse"
                    }
                })
            elif baseline_comparison.get("assessment") == "significantly_better":
                avg_change = baseline_comparison.get("changes", {}).get("avg_change_percent", 0)
                
                insights.append({
                    "type": "baseline_comparison",
                    "importance": "medium",
                    "insight": "System latency has significantly improved compared to the baseline period.",
                    "metrics": {
                        "avg_change_percent": avg_change,
                        "assessment": "significantly_better"
                    }
                })
            
            # Insight 6: Time distribution insights
            time_distribution = end_to_end_analysis.get("time_distribution", {})
            
            if "peak_hour" in time_distribution and "min_hour" in time_distribution:
                peak_hour = time_distribution.get("peak_hour")
                min_hour = time_distribution.get("min_hour")
                
                if peak_hour is not None and min_hour is not None:
                    hourly_latency = time_distribution.get("hourly_avg_latency", {})
                    
                    if str(peak_hour) in hourly_latency and str(min_hour) in hourly_latency:
                        peak_latency = hourly_latency[str(peak_hour)]
                        min_latency = hourly_latency[str(min_hour)]
                        
                        if peak_latency > min_latency * 1.5:
                            insights.append({
                                "type": "time_pattern",
                                "importance": "medium",
                                "insight": f"System latency shows significant time-of-day pattern, with peak latency at {peak_hour}:00 hours.",
                                "metrics": {
                                    "peak_hour": peak_hour,
                                    "peak_latency_ms": peak_latency,
                                    "min_hour": min_hour,
                                    "min_latency_ms": min_latency,
                                    "difference_percent": ((peak_latency / min_latency) - 1) * 100 if min_latency > 0 else 0
                                }
                            })
            
            # Sort insights by importance
            importance_order = {"high": 0, "medium": 1, "low": 2}
            insights.sort(key=lambda x: importance_order.get(x["importance"], 3))
            
            return insights
        
        except Exception as e:
            self.logger.error(f"Error generating latency insights: {str(e)}")
            return []
    
    def _generate_recommendations(self, component_analyses: Dict[str, Dict[str, Any]],
                                end_to_end_analysis: Dict[str, Any],
                                critical_path_analysis: Dict[str, Any]) -> List[Dict[str, Any]]:
        """
        Generate recommendations based on latency analysis.
        
        Args:
            component_analyses: Dictionary with analysis results for each component
            end_to_end_analysis: Dictionary with end-to-end latency analysis
            critical_path_analysis: Dictionary with critical path analysis
            
        Returns:
            List of recommendations
        """
        recommendations = []
        
        try:
            # Check if we have valid analyses
            if end_to_end_analysis.get("status") != "success":
                return recommendations
            
            # Recommendation 1: Address critical path bottleneck
            if critical_path_analysis.get("status") == "success":
                bottleneck = critical_path_analysis.get("bottleneck", {})
                
                if bottleneck:
                    component = bottleneck.get("component", "")
                    contribution = bottleneck.get("contribution_percent", 0)
                    
                    if contribution > 40:
                        # Find specific path with highest latency
                        component_analysis = component_analyses.get(component, {})
                        path_analyses = component_analysis.get("path_analyses", {})
                        
                        if path_analyses:
                            # Find path with highest average latency
                            slowest_path = max(
                                path_analyses.items(),
                                key=lambda x: x[1].get("avg_latency_ms", 0)
                            )
                            
                            path_name = slowest_path[0]
                            path_latency = slowest_path[1].get("avg_latency_ms", 0)
                            
                            recommendations.append({
                                "type": "bottleneck",
                                "priority": "high",
                                "recommendation": f"Optimize the {path_name} path in the {component} component to reduce critical path latency.",
                                "details": {
                                    "component": component,
                                    "path": path_name,
                                    "path_latency_ms": path_latency,
                                    "contribution_percent": contribution,
                                    "estimated_impact": "high"
                                }
                            })
                        else:
                            recommendations.append({
                                "type": "bottleneck",
                                "priority": "high",
                                "recommendation": f"Optimize the {component} component to reduce critical path latency.",
                                "details": {
                                    "component": component,
                                    "contribution_percent": contribution,
                                    "estimated_impact": "high"
                                }
                            })
            
            # Recommendation 2: Address SLO violations
            end_to_end_metrics = end_to_end_analysis.get("overall_metrics", {})
            slo_compliance = end_to_end_metrics.get("slo_compliance", 1.0)
            
            if slo_compliance < 0.95:
                # Find components with low SLO compliance
                problematic_components = []
                
                for component, analysis in component_analyses.items():
                    comp_slo = analysis.get("overall_metrics", {}).get("slo_compliance", 1.0)
                    
                    if comp_slo < 0.9:
                        problematic_components.append({
                            "component": component,
                            "slo_compliance": comp_slo
                        })
                
                if problematic_components:
                    # Sort by SLO compliance (lowest first)
                    problematic_components.sort(key=lambda x: x["slo_compliance"])
                    
                    worst_component = problematic_components[0]["component"]
                    worst_compliance = problematic_components[0]["slo_compliance"]
                    
                    recommendations.append({
                        "type": "slo",
                        "priority": "high",
                        "recommendation": f"Address SLO violations in the {worst_component} component to improve overall system latency compliance.",
                        "details": {
                            "component": worst_component,
                            "slo_compliance": worst_compliance,
                            "system_slo_compliance": slo_compliance,
                            "estimated_impact": "high"
                        }
                    })
                else:
                    recommendations.append({
                        "type": "slo",
                        "priority": "high",
                        "recommendation": "Identify and address the root cause of system-wide SLO violations.",
                        "details": {
                            "system_slo_compliance": slo_compliance,
                            "slo_violations": end_to_end_metrics.get("slo_violations", 0),
                            "estimated_impact": "high"
                        }
                    })
            
            # Recommendation 3: Address concerning trends
            for component, analysis in component_analyses.items():
                trend = analysis.get("trend", {})
                
                if (trend.get("direction") == "increasing" and 
                    trend.get("significance") == "significant" and 
                    trend.get("projected_change_10_samples", 0) > 15):
                    
                    recommendations.append({
                        "type": "trend",
                        "priority": "medium",
                        "recommendation": f"Investigate and address the increasing latency trend in the {component} component.",
                        "details": {
                            "component": component,
                            "trend_direction": "increasing",
                            "projected_change_percent": trend.get("projected_change_10_samples", 0),
                            "estimated_impact": "medium"
                        }
                    })
            
            # Recommendation 4: Optimization opportunities for high-latency paths
            for component, analysis in component_analyses.items():
                path_analyses = analysis.get("path_analyses", {})
                
                high_latency_paths = []
                
                for path, path_analysis in path_analyses.items():
                    slo_threshold = self.slo_thresholds.get(component, self.slo_thresholds['default'])
                    path_latency = path_analysis.get("avg_latency_ms", 0)
                    
                    if path_latency > slo_threshold * 0.7:  # Path consuming 70%+ of component SLO
                        high_latency_paths.append({
                            "path": path,
                            "latency_ms": path_latency,
                            "slo_percent": (path_latency / slo_threshold) * 100 if slo_threshold > 0 else 0
                        })
                
                if high_latency_paths:
                    # Sort by latency (highest first)
                    high_latency_paths.sort(key=lambda x: x["latency_ms"], reverse=True)
                    
                    # Recommend optimizing the highest latency path
                    worst_path = high_latency_paths[0]
                    
                    recommendations.append({
                        "type": "optimization",
                        "priority": "medium",
                        "recommendation": f"Optimize the {worst_path['path']} path in the {component} component to improve performance.",
                        "details": {
                            "component": component,
                            "path": worst_path["path"],
                            "latency_ms": worst_path["latency_ms"],
                            "slo_percent": worst_path["slo_percent"],
                            "estimated_impact": "medium"
                        }
                    })
            
            # Recommendation 5: Baseline degradation
            baseline_comparison = end_to_end_analysis.get("baseline_comparison", {})
            
            if baseline_comparison.get("assessment") == "significantly_worse":
                avg_change = baseline_comparison.get("changes", {}).get("avg_change_percent", 0)
                
                recommendations.append({
                    "type": "baseline",
                    "priority": "high",
                    "recommendation": "Investigate recent changes that may have caused the significant latency degradation compared to baseline.",
                    "details": {
                        "avg_change_percent": avg_change,
                        "baseline_period": baseline_comparison.get("baseline_period", {}),
                        "estimated_impact": "high"
                    }
                })
            
            # Recommendation 6: Time-of-day optimization
            time_distribution = end_to_end_analysis.get("time_distribution", {})
            
            if "peak_hour" in time_distribution:
                peak_hour = time_distribution.get("peak_hour")
                
                if peak_hour is not None:
                    recommendations.append({
                        "type": "time_optimization",
                        "priority": "low",
                        "recommendation": f"Consider optimizing system resources during peak latency hours around {peak_hour}:00.",
                        "details": {
                            "peak_hour": peak_hour,
                            "hourly_pattern": time_distribution.get("hourly_avg_latency", {}).get(str(peak_hour), 0),
                            "estimated_impact": "medium"
                        }
                    })
            
            # Sort recommendations by priority
            priority_order = {"high": 0, "medium": 1, "low": 2}
            recommendations.sort(key=lambda x: priority_order.get(x["priority"], 3))
            
            return recommendations
        
        except Exception as e:
            self.logger.error(f"Error generating recommendations: {str(e)}")
            return []