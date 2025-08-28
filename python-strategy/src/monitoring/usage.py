import pandas as pd
import numpy as np
from typing import Dict, Any
from psutil import cpu_percent, virtual_memory, disk_usage
import logging
from dataclasses import dataclass
from datetime import datetime
from alert_manager import AlertManager
#yaml
import yaml
# ================================================================
logger = logging.getLogger(__name__)
@dataclass
class SystemMetrics:
    """Dataclass to hold system metrics"""
    cpu_percent: float
    memory_percent: float
    disk_percent: float
    timestamp: datetime = datetime.now()
class SystemMonitor:
    """Monitors system resources and checks health status"""
    
    def __init__(self, config: Dict[str, Any]):
        self.config = config
        self.memory_threshold = config.get('memory_threshold', 70)
        self.cpu_threshold = config.get('cpu_threshold', 70)
        self.disk_threshold = config.get('disk_threshold', 50)
    
    def collect_metrics(self) -> SystemMetrics:
        """Collects current system metrics"""
        cpu = cpu_percent(interval=1)
        memory = virtual_memory().percent
        disk = disk_usage('/').percent
        
        return SystemMetrics(cpu_percent=cpu, memory_percent=memory, disk_percent=disk)
    
    def check_health(self, metrics: SystemMetrics) -> Dict[str, bool]:
        """Checks if system metrics are within configured thresholds"""
        return {
            'cpu_ok': metrics.cpu_percent < self.cpu_threshold,
            'memory_ok': metrics.memory_percent < self.memory_threshold,
            'disk_ok': metrics.disk_percent < self.disk_threshold
        }

# Example usage and configuration
if __name__ == "__main__":
    import asyncio
    # read configuration from config/monitoring.yaml 
    with open('config/monitoring.yaml', 'r') as file:
        config = yaml.safe_load(file)
    # Initialize logging
    logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
    logger.info("Starting system monitoring...")
    
    async def test_monitoring():
        # Initialize components
        monitor = SystemMonitor(config)
        alert_manager = AlertManager(config)
        
        # Collect metrics
        metrics = monitor.collect_metrics()
        health = monitor.check_health(metrics)
        
        print(f"CPU: {metrics.cpu_percent}%")
        print(f"Memory: {metrics.memory_percent}%") 
        print(f"Disk: {metrics.disk_percent}%")
        print(f"Health: {health}")
        
        # Test alert
        await alert_manager.send_alert(
            'INFO',
            'Monitoring system started',
            {'cpu': metrics.cpu_percent, 'memory': metrics.memory_percent}
        )
        
        # Check for issues and alert
        if not health['memory_ok']:
            await alert_manager.send_alert(
                'WARNING',
                f'High memory usage: {metrics.memory_percent}%',
                {'threshold': config['memory_threshold']}
            )
        
        if not health['cpu_ok']:
            await alert_manager.send_alert(
                'WARNING', 
                f'High CPU usage: {metrics.cpu_percent}%',
                {'threshold': config['cpu_threshold']}
            )
    
    # Run test
    asyncio.run(test_monitoring())