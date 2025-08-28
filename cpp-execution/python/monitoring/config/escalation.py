# monitoring/config/escalation.py

from .settings import settings

class EscalationConfig:
    """Configuration for alert escalation policies."""
    
    @property
    def enabled(self):
        """Whether alert escalation is enabled."""
        return settings.get("escalation.enabled", True)
        
    @property
    def policies(self):
        """Defined escalation policies."""
        return settings.get("escalation.policies", {
            "default": {
                "steps": [
                    {"wait_minutes": 5, "notify": ["primary_oncall"]},
                    {"wait_minutes": 15, "notify": ["secondary_oncall"]},
                    {"wait_minutes": 30, "notify": ["team_manager"]}
                ],
                "repeat_count": 3
            },
            "critical": {
                "steps": [
                    {"wait_minutes": 2, "notify": ["primary_oncall", "secondary_oncall"]},
                    {"wait_minutes": 5, "notify": ["team_manager"]},
                    {"wait_minutes": 10, "notify": ["director"]}
                ],
                "repeat_count": 5
            }
        })
        
    @property
    def oncall_schedule(self):
        """On-call schedule configuration."""
        return settings.get("escalation.oncall", {
            "rotation_days": 7,
            "timezone": "UTC"
        })
        
    @property
    def oncall_contacts(self):
        """On-call contact information."""
        return settings.get("escalation.contacts", {
            "primary_oncall": {"name": "Primary On-Call", "methods": ["slack", "sms", "email"]},
            "secondary_oncall": {"name": "Secondary On-Call", "methods": ["slack", "email"]},
            "team_manager": {"name": "Team Manager", "methods": ["slack", "sms", "email", "call"]},
            "director": {"name": "Director", "methods": ["sms", "call"]}
        })

# Create singleton instance
escalation_config = EscalationConfig()