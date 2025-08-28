#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Log Analyzer module for processing and analyzing system logs.
"""

import re
import asyncio
from datetime import datetime, timedelta
import pandas as pd
import numpy as np
from collections import Counter
from typing import Dict, List, Optional, Any, Set, Tuple

from ..database.operational_db import OperationalDB
from ..database.timeseries_db import TimeSeriesDB
from ..utils.time_utils import timestamp_to_datetime, datetime_to_timestamp
from .analyzer_base import AnalyzerBase

class LogAnalyzer(AnalyzerBase):
    """
    Analyzer for system logs to detect patterns, errors, and potential issues.
    """
    
    def __init__(self, config: Dict[str, Any], 
                 operational_db: OperationalDB,
                 timeseries_db: TimeSeriesDB):
        """
        Initialize the log analyzer with database connections.
        
        Args:
            config: Configuration dictionary
            operational_db: Operational database connector
            timeseries_db: Time series database connector
        """
        super().__init__(config, timeseries_db)
        self.operational_db = operational_db
        self.error_patterns = self.config.get('error_patterns', [
            r'error',
            r'exception',
            r'failed',
            r'timeout',
            r'unavailable',
            r'refused'
        ])
        self.log_components = self.config.get('log_components', [
            'data_ingestion',
            'strategy',
            'execution',
            'risk',
            'monitoring'
        ])
        self.ignore_patterns = self.config.get('ignore_patterns', [])
        self.max_log_entries = self.config.get('max_log_entries', 10000)
    
    async def analyze(self, start_time: datetime, end_time: datetime) -> Dict[str, Any]:
        """
        Analyze logs within the specified time window.
        
        Args:
            start_time: Start of analysis window
            end_time: End of analysis window
            
        Returns:
            Dictionary containing analysis results
        """
        self.logger.info(f"Analyzing logs from {start_time} to {end_time}")
        
        # Fetch logs from database
        logs = await self._fetch_logs(start_time, end_time)
        
        if not logs:
            self.logger.warning("No logs found in the specified time window")
            return {"error": "No logs found", "count": 0}
        
        # Process and analyze logs
        result = {
            "total_count": len(logs),
            "logs_by_level": self._count_by_level(logs),
            "logs_by_component": self._count_by_component(logs),
            "error_analysis": await self._analyze_errors(logs),
            "pattern_detection": self._detect_patterns(logs),
            "time_distribution": self._analyze_time_distribution(logs),
            "correlated_events": await self._find_correlated_events(logs, start_time, end_time),
            "anomalies": self._detect_log_anomalies(logs),
            "summary": self._generate_summary(logs)
        }
        
        self.last_analysis_time = datetime.now()
        return result
    
    async def _fetch_logs(self, start_time: datetime, end_time: datetime) -> List[Dict[str, Any]]:
        """
        Fetch logs from the database within specified time range.
        
        Args:
            start_time: Start of log time range
            end_time: End of log time range
            
        Returns:
            List of log entries
        """
        try:
            query = """
                SELECT id, timestamp, level, component, message, context
                FROM logs
                WHERE timestamp BETWEEN %s AND %s
                ORDER BY timestamp DESC
                LIMIT %s
            """
            params = (datetime_to_timestamp(start_time), 
                     datetime_to_timestamp(end_time),
                     self.max_log_entries)
            
            result = await self.operational_db.execute_query(query, params)
            
            # Convert timestamps to datetime objects
            for log in result:
                log['timestamp'] = timestamp_to_datetime(log['timestamp'])
                
            return result
        except Exception as e:
            self.logger.error(f"Error fetching logs: {str(e)}")
            return []
    
    def _count_by_level(self, logs: List[Dict[str, Any]]) -> Dict[str, int]:
        """
        Count logs by severity level.
        
        Args:
            logs: List of log entries
            
        Returns:
            Dictionary with counts by log level
        """
        level_counts = Counter()
        for log in logs:
            level = log.get('level', 'unknown').lower()
            level_counts[level] += 1
        return dict(level_counts)
    
    def _count_by_component(self, logs: List[Dict[str, Any]]) -> Dict[str, int]:
        """
        Count logs by system component.
        
        Args:
            logs: List of log entries
            
        Returns:
            Dictionary with counts by component
        """
        component_counts = Counter()
        for log in logs:
            component = log.get('component', 'unknown')
            component_counts[component] += 1
        return dict(component_counts)
    
    async def _analyze_errors(self, logs: List[Dict[str, Any]]) -> Dict[str, Any]:
        """
        Analyze error logs to identify common issues and patterns.
        
        Args:
            logs: List of log entries
            
        Returns:
            Dictionary with error analysis results
        """
        # Filter error and warning logs
        error_logs = [log for log in logs if log.get('level', '').lower() in ('error', 'critical', 'warning')]
        
        if not error_logs:
            return {"count": 0, "common_errors": []}
        
        # Extract error messages and find common patterns
        error_messages = [log.get('message', '') for log in error_logs]
        message_counts = Counter(error_messages)
        common_errors = message_counts.most_common(10)
        
        # Group similar errors
        grouped_errors = await self._group_similar_errors(error_logs)
        
        return {
            "count": len(error_logs),
            "error_rate": len(error_logs) / len(logs) if logs else 0,
            "common_errors": [{"message": msg, "count": count} for msg, count in common_errors],
            "grouped_errors": grouped_errors,
            "components_with_errors": self._components_with_errors(error_logs)
        }
    
    async def _group_similar_errors(self, error_logs: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
        """
        Group similar error messages together using pattern matching.
        
        Args:
            error_logs: List of error log entries
            
        Returns:
            List of error groups with counts
        """
        # Replace specific values (IDs, timestamps, etc.) with placeholders
        patterns = {}
        
        for log in error_logs:
            message = log.get('message', '')
            
            # Replace numbers, UUIDs, timestamps, etc. with placeholders
            generalized = re.sub(r'\b[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\b', '<uuid>', message)
            generalized = re.sub(r'\b\d+\.\d+\.\d+\.\d+\b', '<ip>', generalized)
            generalized = re.sub(r'\b\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d+)?(Z|[+-]\d{2}:\d{2})?\b', '<timestamp>', generalized)
            generalized = re.sub(r'\b\d+\b', '<number>', generalized)
            
            if generalized not in patterns:
                patterns[generalized] = []
            
            patterns[generalized].append(log)
        
        # Sort by frequency
        grouped_errors = [
            {
                "pattern": pattern,
                "count": len(logs),
                "sample": logs[0].get('message', ''),
                "component": logs[0].get('component', 'unknown'),
                "first_seen": min(log.get('timestamp', datetime.now()) for log in logs),
                "last_seen": max(log.get('timestamp', datetime.now()) for log in logs)
            }
            for pattern, logs in sorted(patterns.items(), key=lambda x: len(x[1]), reverse=True)
        ]
        
        return grouped_errors[:20]  # Return top 20 patterns
    
    def _components_with_errors(self, error_logs: List[Dict[str, Any]]) -> Dict[str, int]:
        """
        Count errors by component.
        
        Args:
            error_logs: List of error log entries
            
        Returns:
            Dictionary with error counts by component
        """
        component_errors = Counter()
        for log in error_logs:
            component = log.get('component', 'unknown')
            component_errors[component] += 1
        return dict(component_errors)
    
    def _detect_patterns(self, logs: List[Dict[str, Any]]) -> Dict[str, Any]:
        """
        Detect patterns and sequences in log messages.
        
        Args:
            logs: List of log entries
            
        Returns:
            Dictionary with detected patterns
        """
        # Look for sequences that might indicate issues
        sequences = []
        
        # Sort logs by timestamp
        sorted_logs = sorted(logs, key=lambda x: x.get('timestamp', datetime.now()))
        
        # Find restart sequences
        restart_sequences = self._find_restart_sequences(sorted_logs)
        
        # Find request-response pairs
        request_response_pairs = self._find_request_response_pairs(sorted_logs)
        
        # Find connection issues
        connection_issues = self._find_connection_issues(sorted_logs)
        
        return {
            "restart_sequences": restart_sequences,
            "request_response_pairs": len(request_response_pairs),
            "connection_issues": connection_issues,
            "recurring_patterns": self._find_recurring_patterns(sorted_logs)
        }
    
    def _find_restart_sequences(self, sorted_logs: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
        """
        Find sequences indicating service restarts.
        
        Args:
            sorted_logs: Chronologically sorted log entries
            
        Returns:
            List of detected restart sequences
        """
        restart_sequences = []
        components_seen = set()
        
        for i, log in enumerate(sorted_logs):
            message = log.get('message', '').lower()
            component = log.get('component', '')
            
            # Look for shutdown/startup messages
            if ('starting' in message or 'initializing' in message) and component:
                # Check if this component was seen shutting down recently
                shutdown_idx = -1
                for j in range(i-1, max(0, i-20), -1):
                    prev_log = sorted_logs[j]
                    prev_message = prev_log.get('message', '').lower()
                    prev_component = prev_log.get('component', '')
                    
                    if (prev_component == component and 
                        ('shutdown' in prev_message or 'stopping' in prev_message or 'terminated' in prev_message)):
                        shutdown_idx = j
                        break
                
                if shutdown_idx >= 0:
                    # Found a restart sequence
                    restart_time = (log.get('timestamp', datetime.now()) - 
                                   sorted_logs[shutdown_idx].get('timestamp', datetime.now()))
                    
                    restart_sequences.append({
                        "component": component,
                        "shutdown_time": sorted_logs[shutdown_idx].get('timestamp'),
                        "startup_time": log.get('timestamp'),
                        "downtime_seconds": restart_time.total_seconds(),
                        "shutdown_message": sorted_logs[shutdown_idx].get('message'),
                        "startup_message": log.get('message')
                    })
        
        return restart_sequences
    
    def _find_request_response_pairs(self, sorted_logs: List[Dict[str, Any]]) -> List[Tuple]:
        """
        Find request-response pairs in logs.
        
        Args:
            sorted_logs: Chronologically sorted log entries
            
        Returns:
            List of request-response pairs
        """
        # This implementation would be highly dependent on log format
        # Simplified implementation that looks for request/response keywords
        pairs = []
        pending_requests = {}
        
        for log in sorted_logs:
            message = log.get('message', '').lower()
            context = log.get('context', {})
            request_id = context.get('request_id', '')
            
            if request_id:
                if 'request' in message and request_id not in pending_requests:
                    pending_requests[request_id] = log
                elif 'response' in message and request_id in pending_requests:
                    request_log = pending_requests.pop(request_id)
                    pairs.append((request_log, log))
        
        return pairs
    
    def _find_connection_issues(self, sorted_logs: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
        """
        Identify connection issues from logs.
        
        Args:
            sorted_logs: Chronologically sorted log entries
            
        Returns:
            List of detected connection issues
        """
        connection_issues = []
        
        # Keywords that might indicate connection problems
        connection_keywords = [
            'connection refused', 'connection reset', 'timeout', 
            'network error', 'socket error', 'connection failed'
        ]
        
        for log in sorted_logs:
            message = log.get('message', '').lower()
            component = log.get('component', 'unknown')
            
            # Check if message contains any connection issue keywords
            if any(keyword in message for keyword in connection_keywords):
                connection_issues.append({
                    "timestamp": log.get('timestamp'),
                    "component": component,
                    "message": log.get('message'),
                    "level": log.get('level')
                })
        
        return connection_issues
    
    def _find_recurring_patterns(self, sorted_logs: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
        """
        Find recurring message patterns that might indicate problems.
        
        Args:
            sorted_logs: Chronologically sorted log entries
            
        Returns:
            List of recurring patterns with frequency
        """
        # Count message frequencies within short time windows
        window_size = timedelta(minutes=5)
        windows = {}
        
        for log in sorted_logs:
            timestamp = log.get('timestamp', datetime.now())
            message = log.get('message', '')
            
            # Create a window key by truncating to 5-minute intervals
            window_key = timestamp.replace(
                minute=timestamp.minute - (timestamp.minute % 5),
                second=0,
                microsecond=0
            )
            
            if window_key not in windows:
                windows[window_key] = Counter()
            
            windows[window_key][message] += 1
        
        # Find messages that occur frequently in multiple windows
        message_windows = {}
        for window_key, counts in windows.items():
            for message, count in counts.items():
                if count >= 5:  # Only consider messages that appear at least 5 times in a window
                    if message not in message_windows:
                        message_windows[message] = []
                    message_windows[message].append((window_key, count))
        
        # Filter to messages that appear in multiple windows
        recurring_patterns = [
            {
                "message": message,
                "window_count": len(windows_data),
                "total_occurrences": sum(count for _, count in windows_data),
                "first_seen": min(window for window, _ in windows_data),
                "last_seen": max(window for window, _ in windows_data)
            }
            for message, windows_data in message_windows.items()
            if len(windows_data) >= 2  # Appears in at least 2 time windows
        ]
        
        # Sort by total occurrences
        recurring_patterns.sort(key=lambda x: x["total_occurrences"], reverse=True)
        
        return recurring_patterns[:10]  # Return top 10 patterns
    
    def _analyze_time_distribution(self, logs: List[Dict[str, Any]]) -> Dict[str, Any]:
        """
        Analyze the time distribution of log entries.
        
        Args:
            logs: List of log entries
            
        Returns:
            Dictionary with time distribution analysis
        """
        # Create a DataFrame for time series analysis
        timestamps = [log.get('timestamp', datetime.now()) for log in logs]
        levels = [log.get('level', 'unknown') for log in logs]
        components = [log.get('component', 'unknown') for log in logs]
        
        df = pd.DataFrame({
            'timestamp': timestamps,
            'level': levels,
            'component': components
        })
        
        # Ensure timestamps are sorted
        df = df.sort_values('timestamp')
        
        # Create time bins (hourly)
        df['hour'] = df['timestamp'].apply(lambda x: x.replace(minute=0, second=0, microsecond=0))
        hourly_counts = df.groupby('hour').size()
        
        # Get hourly counts by level
        level_counts = df.groupby(['hour', 'level']).size().unstack(fill_value=0)
        
        # Get hourly counts by component
        component_counts = df.groupby(['hour', 'component']).size().unstack(fill_value=0)
        
        # Calculate the log velocity (logs per minute)
        if len(timestamps) >= 2:
            time_span = max(timestamps) - min(timestamps)
            minutes = time_span.total_seconds() / 60
            velocity = len(logs) / minutes if minutes > 0 else 0
        else:
            velocity = 0
        
        return {
            "hourly_counts": hourly_counts.to_dict(),
            "level_distribution": {level: count for level, count in Counter(levels).items()},
            "component_distribution": {comp: count for comp, count in Counter(components).items()},
            "peak_hour": hourly_counts.idxmax() if not hourly_counts.empty else None,
            "peak_count": hourly_counts.max() if not hourly_counts.empty else 0,
            "velocity": velocity,
            "velocity_unit": "logs per minute"
        }
    
    async def _find_correlated_events(self, logs: List[Dict[str, Any]], 
                                     start_time: datetime, end_time: datetime) -> Dict[str, Any]:
        """
        Find system events that correlate with log patterns.
        
        Args:
            logs: List of log entries
            start_time: Start of analysis window
            end_time: End of analysis window
            
        Returns:
            Dictionary with correlated events
        """
        # Get error timestamps
        error_logs = [log for log in logs if log.get('level', '').lower() in ('error', 'critical')]
        error_timestamps = [log.get('timestamp', datetime.now()) for log in error_logs]
        
        if not error_timestamps:
            return {}
        
        # Prepare time windows around errors
        time_windows = []
        for ts in error_timestamps:
            window_start = ts - timedelta(minutes=5)
            window_end = ts + timedelta(minutes=5)
            time_windows.append((window_start, window_end))
        
        # Merge overlapping windows
        merged_windows = []
        current_start, current_end = time_windows[0]
        
        for window_start, window_end in sorted(time_windows[1:]):
            if window_start <= current_end:
                # Windows overlap, extend current window
                current_end = max(current_end, window_end)
            else:
                # No overlap, add current window and start a new one
                merged_windows.append((current_start, current_end))
                current_start, current_end = window_start, window_end
        
        # Add the last window
        merged_windows.append((current_start, current_end))
        
        # Find system metrics during these windows
        correlated_metrics = await self._find_anomalous_metrics(merged_windows)
        
        # Find deployment events
        deployments = await self._find_deployments(merged_windows)
        
        # Find configuration changes
        config_changes = await self._find_config_changes(merged_windows)
        
        return {
            "error_count": len(error_logs),
            "correlated_metrics": correlated_metrics,
            "deployments": deployments,
            "config_changes": config_changes
        }
    
    async def _find_anomalous_metrics(self, time_windows: List[Tuple[datetime, datetime]]) -> List[Dict[str, Any]]:
        """
        Find metrics with anomalous values during specified time windows.
        
        Args:
            time_windows: List of (start_time, end_time) tuples
            
        Returns:
            List of anomalous metrics information
        """
        # Key system metrics to check
        key_metrics = [
            'system.cpu.usage',
            'system.memory.used_percent',
            'network.connections',
            'system.disk.io_time',
            'application.latency',
            'database.query_time'
        ]
        
        anomalous_metrics = []
        
        for metric_name in key_metrics:
            for start_time, end_time in time_windows:
                # Get metric data for the time window
                metric_data = await self.fetch_data(metric_name, start_time, end_time)
                
                if metric_data.empty or 'value' not in metric_data.columns:
                    continue
                
                # Get baseline data from before the window
                baseline_start = start_time - timedelta(hours=24)
                baseline_end = start_time - timedelta(minutes=30)
                baseline_data = await self.fetch_data(metric_name, baseline_start, baseline_end)
                
                if baseline_data.empty or 'value' not in baseline_data.columns:
                    continue
                
                # Calculate baseline statistics
                baseline_mean = baseline_data['value'].mean()
                baseline_std = baseline_data['value'].std()
                
                # Find anomalies
                if baseline_std > 0:
                    z_scores = (metric_data['value'] - baseline_mean) / baseline_std
                    anomalies = metric_data[abs(z_scores) > 3]
                    
                    if not anomalies.empty:
                        max_anomaly = anomalies.loc[abs(z_scores[anomalies.index]).idxmax()]
                        anomalous_metrics.append({
                            "metric": metric_name,
                            "timestamp": max_anomaly.get('timestamp', start_time),
                            "value": float(max_anomaly.get('value', 0)),
                            "baseline_mean": float(baseline_mean),
                            "z_score": float(z_scores[max_anomaly.name]),
                            "window_start": start_time,
                            "window_end": end_time
                        })
        
        return anomalous_metrics
    
    async def _find_deployments(self, time_windows: List[Tuple[datetime, datetime]]) -> List[Dict[str, Any]]:
        """
        Find deployment events during specified time windows.
        
        Args:
            time_windows: List of (start_time, end_time) tuples
            
        Returns:
            List of deployment events
        """
        deployments = []
        
        try:
            for start_time, end_time in time_windows:
                query = """
                    SELECT id, timestamp, component, version, status, user
                    FROM deployments
                    WHERE timestamp BETWEEN %s AND %s
                    ORDER BY timestamp
                """
                params = (datetime_to_timestamp(start_time), datetime_to_timestamp(end_time))
                
                result = await self.operational_db.execute_query(query, params)
                
                for deployment in result:
                    deployments.append({
                        "id": deployment.get('id'),
                        "timestamp": timestamp_to_datetime(deployment.get('timestamp')),
                        "component": deployment.get('component'),
                        "version": deployment.get('version'),
                        "status": deployment.get('status'),
                        "user": deployment.get('user')
                    })
        except Exception as e:
            self.logger.error(f"Error finding deployments: {str(e)}")
        
        return deployments
    
    async def _find_config_changes(self, time_windows: List[Tuple[datetime, datetime]]) -> List[Dict[str, Any]]:
        """
        Find configuration changes during specified time windows.
        
        Args:
            time_windows: List of (start_time, end_time) tuples
            
        Returns:
            List of configuration change events
        """
        config_changes = []
        
        try:
            for start_time, end_time in time_windows:
                query = """
                    SELECT id, timestamp, component, parameter, old_value, new_value, user
                    FROM configuration_changes
                    WHERE timestamp BETWEEN %s AND %s
                    ORDER BY timestamp
                """
                params = (datetime_to_timestamp(start_time), datetime_to_timestamp(end_time))
                
                result = await self.operational_db.execute_query(query, params)
                
                for change in result:
                    config_changes.append({
                        "id": change.get('id'),
                        "timestamp": timestamp_to_datetime(change.get('timestamp')),
                        "component": change.get('component'),
                        "parameter": change.get('parameter'),
                        "old_value": change.get('old_value'),
                        "new_value": change.get('new_value'),
                        "user": change.get('user')
                    })
        except Exception as e:
            self.logger.error(f"Error finding config changes: {str(e)}")
        
        return config_changes
    
    def _detect_log_anomalies(self, logs: List[Dict[str, Any]]) -> Dict[str, Any]:
        """
        Detect anomalies in log volume and patterns.
        
        Args:
            logs: List of log entries
            
        Returns:
            Dictionary with detected anomalies
        """
        # Convert to DataFrame for analysis
        df = pd.DataFrame([
            {'timestamp': log.get('timestamp', datetime.now()),
             'level': log.get('level', 'unknown'),
             'component': log.get('component', 'unknown')}
            for log in logs
        ])
        
        if df.empty:
            return {}
        
        # Ensure timestamps are sorted
        df = df.sort_values('timestamp')
        
        # Create time bins (minute-level)
        df['minute'] = df['timestamp'].apply(lambda x: x.replace(second=0, microsecond=0))
        volume_by_minute = df.groupby('minute').size()
        
        # Calculate volume statistics
        if len(volume_by_minute) > 1:
            mean_volume = volume_by_minute.mean()
            std_volume = volume_by_minute.std()
            threshold = mean_volume + 3 * std_volume  # 3 sigma threshold
            
            # Find volume anomalies
            volume_anomalies = volume_by_minute[volume_by_minute > threshold]
            
            # Find error rate anomalies
            error_df = df[df['level'].isin(['error', 'critical'])]
            error_by_minute = error_df.groupby('minute').size()
            error_rate_by_minute = error_by_minute / volume_by_minute
            
            mean_error_rate = error_rate_by_minute.mean()
            std_error_rate = error_rate_by_minute.std()
            
            if std_error_rate > 0:
                error_threshold = mean_error_rate + 3 * std_error_rate
                error_anomalies = error_rate_by_minute[error_rate_by_minute > error_threshold]
            else:
                error_anomalies = pd.Series()
        else:
            volume_anomalies = pd.Series()
            error_anomalies = pd.Series()
            mean_volume = volume_by_minute.mean() if not volume_by_minute.empty else 0
            mean_error_rate = 0
        
        return {
            "volume_anomalies": {
                str(minute): int(volume) 
                for minute, volume in volume_anomalies.items()
            },
            "error_rate_anomalies": {
                str(minute): float(rate) 
                for minute, rate in error_anomalies.items()
            },
            "mean_volume_per_minute": float(mean_volume),
            "mean_error_rate": float(mean_error_rate),
            "anomaly_count": len(volume_anomalies) + len(error_anomalies)
        }
    
    def _generate_summary(self, logs: List[Dict[str, Any]]) -> Dict[str, Any]:
        """
        Generate a summary of the log analysis.
        
        Args:
            logs: List of log entries
            
        Returns:
            Dictionary with summary information
        """
        # Count logs by level
        level_counts = self._count_by_level(logs)
        
        # Calculate error rate
        total_logs = len(logs)
        error_count = sum(level_counts.get(level, 0) for level in ['error', 'critical'])
        error_rate = error_count / total_logs if total_logs > 0 else 0
        
        # Get time range
        timestamps = [log.get('timestamp', datetime.now()) for log in logs]
        start_time = min(timestamps) if timestamps else datetime.now()
        end_time = max(timestamps) if timestamps else datetime.now()
        time_span = end_time - start_time
        
        return {
            "total_logs": total_logs,
            "error_count": error_count,
            "error_rate": error_rate,
            "time_span_seconds": time_span.total_seconds(),
            "start_time": start_time,
            "end_time": end_time,
            "components": list(set(log.get('component', 'unknown') for log in logs)),
            "most_active_component": max(
                self._count_by_component(logs).items(), 
                key=lambda x: x[1]
            )[0] if logs else "unknown"
        }