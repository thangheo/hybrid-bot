import asyncio
import logging
import time
from typing import Dict, List, Any, Optional, Callable
from collections import deque
import statistics
import uuid

logger = logging.getLogger(__name__)

class LatencyTracker:
    """
    Tracks and analyzes latency across different system components and operations.
    """
    
    def __init__(self, config=None):
        """
        Initialize the latency tracker.
        
        Args:
            config: Configuration dictionary for the latency tracker
        """
        self.config = config or {}
        self.latency_history = {}  # Maps path names to deque of recent latency measurements
        self.latency_stats = {}    # Maps path names to current stats
        self.history_size = self.config.get('history_size', 1000)  # Number of measurements to keep
        self.percentiles = self.config.get('percentiles', [50, 90, 95, 99])  # Percentiles to calculate
        self.active_spans = {}     # For tracking ongoing operations
    
    def start_span(self, path: str, span_id: Optional[str] = None) -> str:
        """
        Start tracking latency for a specific operation path.
        
        Args:
            path: Name of the operation path (e.g., 'exchange.binance.order_placement')
            span_id: Optional identifier for the span, auto-generated if not provided
            
        Returns:
            span_id: Identifier for the span, used to end it
        """
        if span_id is None:
            span_id = str(uuid.uuid4())
        
        self.active_spans[span_id] = {
            'path': path,
            'start_time': time.time(),
            'metadata': {}
        }
        
        return span_id
    
    def add_span_metadata(self, span_id: str, key: str, value: Any) -> None:
        """
        Add metadata to an active span.
        
        Args:
            span_id: Identifier for the span
            key: Metadata key
            value: Metadata value
        """
        if span_id not in self.active_spans:
            logger.warning(f"Cannot add metadata to unknown span: {span_id}")
            return
        
        self.active_spans[span_id]['metadata'][key] = value
    
    def end_span(self, span_id: str) -> Optional[float]:
        """
        End tracking latency for a specific span and record the measurement.
        
        Args:
            span_id: Identifier for the span
            
        Returns:
            latency: Measured latency in milliseconds, or None if span not found
        """
        if span_id not in self.active_spans:
            logger.warning(f"Cannot end unknown span: {span_id}")
            return None
        
        span = self.active_spans.pop(span_id)
        end_time = time.time()
        latency_ms = (end_time - span['start_time']) * 1000  # Convert to milliseconds
        
        self._record_latency(span['path'], latency_ms, span['metadata'])
        return latency_ms
    
    def track_operation(self, path: str):
        """
        Context manager for tracking operation latency.
        
        Args:
            path: Name of the operation path
            
        Returns:
            Context manager that measures latency
        """
        class LatencyContext:
            def __init__(self, tracker, path):
                self.tracker = tracker
                self.path = path
                self.span_id = None
                
            def __enter__(self):
                self.span_id = self.tracker.start_span(self.path)
                return self
                
            def __exit__(self, exc_type, exc_val, exc_tb):
                if self.span_id:
                    self.tracker.end_span(self.span_id)
                
            def add_metadata(self, key, value):
                if self.span_id:
                    self.tracker.add_span_metadata(self.span_id, key, value)
        
        return LatencyContext(self, path)
    
    async def track_async_operation(self, path: str, coro):
        """
        Track latency of an async coroutine.
        
        Args:
            path: Name of the operation path
            coro: Coroutine to execute and measure
            
        Returns:
            result: Result of the coroutine
        """
        span_id = self.start_span(path)
        try:
            return await coro
        finally:
            self.end_span(span_id)
    
    def _record_latency(self, path: str, latency_ms: float, metadata: Dict[str, Any] = None) -> None:
        """
        Record a latency measurement for a specific path.
        
        Args:
            path: Name of the operation path
            latency_ms: Measured latency in milliseconds
            metadata: Additional metadata about the operation
        """
        if path not in self.latency_history:
            self.latency_history[path] = deque(maxlen=self.history_size)
            self.latency_stats[path] = {
                'count': 0,
                'min': float('inf'),
                'max': 0,
                'mean': 0,
                'median': 0,
                'std_dev': 0,
                'percentiles': {p: 0 for p in self.percentiles}
            }
        
        # Record the measurement with timestamp and metadata
        self.latency_history[path].append({
            'latency': latency_ms,
            'timestamp': time.time(),
            'metadata': metadata or {}
        })
        
        # Update stats if we have enough data
        if len(self.latency_history[path]) >= 5:  # Need some minimum data for stats
            self._update_stats(path)
    
    def _update_stats(self, path: str) -> None:
        """
        Update statistical metrics for a path.
        
        Args:
            path: Name of the operation path
        """
        history = self.latency_history[path]
        latencies = [entry['latency'] for entry in history]
        
        if not latencies:
            return
        
        try:
            self.latency_stats[path] = {
                'count': len(latencies),
                'min': min(latencies),
                'max': max(latencies),
                'mean': statistics.mean(latencies),
                'median': statistics.median(latencies),
                'std_dev': statistics.stdev(latencies) if len(latencies) > 1 else 0,
                'percentiles': {
                    p: statistics.quantiles(latencies, n=100)[p-1] if len(latencies) >= 100 else 
                       statistics.quantiles(latencies, n=len(latencies))[int(p*len(latencies)/100)-1]
                    for p in self.percentiles
                }
            }
        except Exception as e:
            logger.error(f"Error calculating latency stats for {path}: {str(e)}", exc_info=True)
    
    def get_latency_stats(self, path: Optional[str] = None) -> Dict[str, Any]:
        """
        Get current latency statistics.
        
        Args:
            path: Optional path name to get stats for. If None, returns stats for all paths.
            
        Returns:
            stats: Dictionary of latency statistics
        """
        if path:
            return self.latency_stats.get(path, {})
        else:
            return self.latency_stats
    
    def get_latency_history(self, path: str, limit: int = None) -> List[Dict[str, Any]]:
        """
        Get recent latency measurements for a path.
        
        Args:
            path: Name of the operation path
            limit: Maximum number of measurements to return
            
        Returns:
            history: List of latency measurements
        """
        if path not in self.latency_history:
            return []
        
        history = list(self.latency_history[path])
        if limit:
            history = history[-limit:]
        
        return history
    
    def reset_stats(self, path: Optional[str] = None) -> None:
        """
        Reset latency statistics.
        
        Args:
            path: Optional path name to reset. If None, resets all paths.
        """
        if path:
            if path in self.latency_history:
                self.latency_history[path].clear()
                self.latency_stats[path] = {
                    'count': 0,
                    'min': float('inf'),
                    'max': 0,
                    'mean': 0,
                    'median': 0,
                    'std_dev': 0,
                    'percentiles': {p: 0 for p in self.percentiles}
                }
        else:
            self.latency_history = {}
            self.latency_stats = {}
    
    def collect_metrics(self) -> Dict[str, Any]:
        """
        Collect current latency metrics for all paths.
        
        Returns:
            metrics: Dictionary of latency metrics
        """
        metrics = {
            'timestamp': time.time(),
            'paths': {}
        }
        
        for path, stats in self.latency_stats.items():
            metrics['paths'][path] = {
                'count': stats['count'],
                'min_ms': stats['min'],
                'max_ms': stats['max'],
                'mean_ms': stats['mean'],
                'median_ms': stats['median'],
                'std_dev_ms': stats['std_dev'],
                'percentiles_ms': stats['percentiles']
            }
        
        return metrics