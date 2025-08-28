# monitoring/config/system_monitoring.py

from .settings import settings

class SystemMonitoringConfig:
    """Configuration for system monitoring components."""
    
    @property
    def health_check_interval(self):
        """Interval (in seconds) between health checks."""
        return settings.get("system.health_check_interval", 60)
    
    @property
    def resource_check_interval(self):
        """Interval (in seconds) between resource monitoring checks."""
        return settings.get("system.resource_check_interval", 30)
    
    @property
    def log_collection_interval(self):
        """Interval (in seconds) between log collection runs."""
        return settings.get("system.log_collection_interval", 120)
        
    @property
    def latency_thresholds(self):
        """Thresholds for latency alerting (in ms)."""
        return {
            "critical": settings.get("system.latency_thresholds.critical", 500),
            "warning": settings.get("system.latency_thresholds.warning", 200),
            "normal": settings.get("system.latency_thresholds.normal", 100),
        }
        
    @property
    def cpu_thresholds(self):
        """Thresholds for CPU usage alerting (percentage)."""
        return {
            "critical": settings.get("system.cpu_thresholds.critical", 90),
            "warning": settings.get("system.cpu_thresholds.warning", 75),
            "normal": settings.get("system.cpu_thresholds.normal", 60),
        }
        
    @property
    def memory_thresholds(self):
        """Thresholds for memory usage alerting (percentage)."""
        return {
            "critical": settings.get("system.memory_thresholds.critical", 90),
            "warning": settings.get("system.memory_thresholds.warning", 80),
            "normal": settings.get("system.memory_thresholds.normal", 70),
        }
        
    @property
    def disk_thresholds(self):
        """Thresholds for disk usage alerting (percentage)."""
        return {
            "critical": settings.get("system.disk_thresholds.critical", 90),
            "warning": settings.get("system.disk_thresholds.warning", 80),
            "normal": settings.get("system.disk_thresholds.normal", 70),
        }
        
    @property
    def monitored_services(self):
        """List of services to monitor."""
        return settings.get("system.monitored_services", [
            "data_ingestion", 
            "strategy_engine", 
            "execution_engine", 
            "risk_management"
        ])

# Create singleton instance
system_config = SystemMonitoringConfig()