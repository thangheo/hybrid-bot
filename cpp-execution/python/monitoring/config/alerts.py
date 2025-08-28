# monitoring/config/alerts.py

from .settings import settings

class AlertsConfig:
    """Configuration for alerting system."""
    
    @property
    def enabled(self):
        """Whether the alerting system is enabled."""
        return settings.get("alerts.enabled", True)
        
    @property
    def notification_channels(self):
        """Configured notification channels."""
        return settings.get("alerts.notification_channels", {
            "email": {
                "enabled": True,
                "recipients": ["alerts@example.com"]
            },
            "slack": {
                "enabled": True,
                "webhook_url": "https://hooks.slack.com/services/xxx/yyy/zzz",
                "channel": "#alerts"
            }
        })
        
    @property
    def throttling(self):
        """Alert throttling configuration."""
        return {
            "max_alerts_per_minute": settings.get("alerts.throttling.max_per_minute", 10),
            "repeat_interval": settings.get("alerts.throttling.repeat_interval", 300)
        }
        
    @property
    def severity_levels(self):
        """Alert severity level definitions."""
        return settings.get("alerts.severity_levels", {
            "critical": {"color": "red", "priority": 1},
            "warning": {"color": "amber", "priority": 2},
            "info": {"color": "blue", "priority": 3}
        })

# Create singleton instance
alerts_config = AlertsConfig()