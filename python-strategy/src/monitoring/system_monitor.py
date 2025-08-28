# src/monitoring/system_monitor.py
import psutil
import time
import asyncio
from datetime import datetime, timedelta
from dataclasses import dataclass
from typing import Dict, List, Optional
import logging

logger = logging.getLogger(__name__)

@dataclass
class SystemMetrics:
    timestamp: datetime
    cpu_percent: float
    memory_percent: float
    disk_percent: float
    memory_available: int
    disk_free: int
    process_memory: int
    process_cpu: float

class SystemMonitor:
    """Simple system monitoring using psutil"""
    
    def __init__(self, config: dict):
        self.config = config
        self.metrics_history: List[SystemMetrics] = []
        
        # Thresholds
        self.memory_threshold = config.get('memory_threshold', 70)  # 80%
        self.cpu_threshold = config.get('cpu_threshold', 70)       # 70%
        self.disk_threshold = config.get('disk_threshold', 90)     # 90%
        
    # make this async
    async def collect_metrics(self) -> SystemMetrics:
        """Collect current system metrics"""
        # System metrics
        memory = psutil.virtual_memory()
        cpu_percent = psutil.cpu_percent(interval=1)
        disk = psutil.disk_usage('/')
        
        # Process metrics
        process = psutil.Process()
        process_memory = process.memory_info()
        
        metrics = SystemMetrics(
            timestamp=datetime.now(),
            cpu_percent=cpu_percent,
            memory_percent=memory.percent,
            disk_percent=disk.percent,
            memory_available=memory.available,
            disk_free=disk.free,
            process_memory=process_memory.rss,
            process_cpu=process.cpu_percent()
        )
        
        # Store metrics (keep last 24 hours)
        self.metrics_history.append(metrics)
        cutoff_time = datetime.now() - timedelta(hours=24)
        self.metrics_history = [m for m in self.metrics_history if m.timestamp > cutoff_time]
        
        return metrics

    async def check_health(self, metrics: SystemMetrics) -> Dict[str, bool]:
        """Check if system is healthy"""
        return {
            'memory_ok': metrics.memory_percent < self.memory_threshold,
            'cpu_ok': metrics.cpu_percent < self.cpu_threshold,
            'disk_ok': metrics.disk_percent < self.disk_threshold,
            'process_healthy': metrics.process_memory < 1e9 ,
            'all_ok': all([
                metrics.memory_percent < self.memory_threshold,
                metrics.cpu_percent < self.cpu_threshold,
                metrics.disk_percent < self.disk_threshold,
                metrics.process_memory < 1e9
            ])
        }
    
    def get_summary(self, hours: int = 24) -> Dict:
        """Get performance summary"""
        cutoff_time = datetime.now() - timedelta(hours=hours)
        recent_metrics = [m for m in self.metrics_history if m.timestamp > cutoff_time]
        
        if not recent_metrics:
            return {}
        
        return {
            'period_hours': hours,
            'data_points': len(recent_metrics),
            'avg_memory_usage': sum(m.memory_percent for m in recent_metrics) / len(recent_metrics),
            'avg_cpu_usage': sum(m.cpu_percent for m in recent_metrics) / len(recent_metrics),
            'max_memory_usage': max(m.memory_percent for m in recent_metrics),
            'max_cpu_usage': max(m.cpu_percent for m in recent_metrics)
        }

