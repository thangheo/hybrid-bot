# python/monitoring/alerts/escalation_policy.py

import logging
import uuid
from datetime import datetime, timedelta
from typing import Dict, List, Optional, Any

from ..database.operational_db import OperationalDB
from ..utils.notification import NotificationService

logger = logging.getLogger(__name__)

class EscalationStep:
    """Represents a single step in an escalation policy"""
    
    def __init__(
        self,
        level: int,
        wait_minutes: int,
        notify_targets: List[Dict[str, str]],
        step_id: str = None
    ):
        self.id = step_id or str(uuid.uuid4())
        self.level = level
        self.wait_minutes = wait_minutes
        self.notify_targets = notify_targets  # List of {type: "user|team|channel", id: "..."}
    
    def to_dict(self) -> Dict:
        """Convert escalation step to dictionary"""
        return {
            "id": self.id,
            "level": self.level,
            "wait_minutes": self.wait_minutes,
            "notify_targets": self.notify_targets
        }
    
    @classmethod
    def from_dict(cls, data: Dict) -> 'EscalationStep':
        """Create an escalation step from dictionary"""
        return cls(
            level=data["level"],
            wait_minutes=data["wait_minutes"],
            notify_targets=data["notify_targets"],
            step_id=data["id"]
        )


class EscalationPolicy:
    """Defines how alerts should be escalated if not addressed"""
    
    def __init__(
        self,
        name: str,
        description: str,
        steps: List[EscalationStep],
        repeat_count: int = 0,
        policy_id: str = None,
        operational_db: OperationalDB = None,
        notification_service: Optional[Any] = None,
        oncall_manager: Optional[Any] = None
    ):
        self.id = policy_id or str(uuid.uuid4())
        self.name = name
        self.description = description
        self.steps = sorted(steps, key=lambda s: s.level)
        self.repeat_count = repeat_count
        self.operational_db = operational_db
        self.notification_service = notification_service
        self.oncall_manager = oncall_manager
        
    def to_dict(self) -> Dict:
        """Convert escalation policy to dictionary"""
        return {
            "id": self.id,
            "name": self.name,
            "description": self.description,
            "steps": [step.to_dict() for step in self.steps],
            "repeat_count": self.repeat_count
        }
    
    @classmethod
    def from_dict(cls, data: Dict, operational_db=None, notification_service=None, oncall_manager=None) -> 'EscalationPolicy':
        """Create an escalation policy from dictionary"""
        steps = [EscalationStep.from_dict(step_data) for step_data in data["steps"]]
        return cls(
            name=data["name"],
            description=data["description"],
            steps=steps,
            repeat_count=data["repeat_count"],
            policy_id=data["id"],
            operational_db=operational_db,
            notification_service=notification_service,
            oncall_manager=oncall_manager
        )
    
    def get_current_step(self, alert: Any) -> Optional[EscalationStep]:
        """Determine the current escalation step for an alert"""
        if not alert.event_history:
            return None
            
        # Count escalation events
        escalation_events = [e for e in alert.event_history if e["event"].startswith("Escalated to level")]
        
        # If we've reached the maximum number of steps and exhausted repeats, return None
        if len(escalation_events) >= len(self.steps) * (self.repeat_count + 1):
            return None
            
        # Calculate which step we're on
        current_step_index = len(escalation_events) % len(self.steps)
        
        # Return the current step
        return self.steps[current_step_index]
    
    def should_escalate(self, alert: Any) -> bool:
        """Determine if an alert should be escalated"""
        # Don't escalate acknowledged or resolved alerts
        if alert.state not in ["PENDING", "FIRING"]:
            return False
            
        # Get the alert's current escalation level
        escalation_events = [e for e in alert.event_history if e["event"].startswith("Escalated to level")]
        
        # If no escalations yet, check if it's time for the first one
        if not escalation_events:
            # Calculate time since alert was created
            alert_age = datetime.now() - alert.created_at
            # If we've waited long enough for the first step, escalate
            return alert_age.total_seconds() > (self.steps[0].wait_minutes * 60)
        
        # Get the most recent escalation
        last_escalation = escalation_events[-1]
        last_time = datetime.fromisoformat(last_escalation["timestamp"])
        
        # Determine current step
        current_step = self.get_current_step(alert)
        if not current_step:
            return False
            
        # Check if we've waited long enough since the last escalation
        time_since_last = datetime.now() - last_time
        return time_since_last.total_seconds() > (current_step.wait_minutes * 60)
    
    def process_alert(self, alert: Any) -> bool:
        """Process escalation for an alert"""
        if not self.should_escalate(alert):
            return False
            
        current_step = self.get_current_step(alert)
        if not current_step:
            return False
            
        # Record the escalation event
        alert.event_history.append({
            "timestamp": datetime.now().isoformat(),
            "event": f"Escalated to level {current_step.level}"
        })
        
        # Send notifications to escalation targets
        for target in current_step.notify_targets:
            self._notify_target(alert, target)
        
        # Save the alert with the new escalation event
        if self.operational_db:
            self.operational_db.upsert("alerts", alert.to_dict(), {"id": alert.id})
            
        return True
    
    def _notify_target(self, alert: Any, target: Dict[str, str]) -> None:
        """Send notification to an escalation target"""
        if not self.notification_service:
            logger.warning("No notification service available for escalation")
            return
            
        target_type = target["type"]
        target_id = target["id"]
        
        # Different notification strategies based on target type
        if target_type == "user":
            # Get user contact info from oncall manager
            if self.oncall_manager:
                user_info = self.oncall_manager.get_user_contact_info(target_id)
                if user_info:
                    channels = ["email", "sms"] if alert.severity.name in ["CRITICAL", "EMERGENCY"] else ["email"]
                    for channel in channels:
                        self._send_notification(channel, user_info, alert)
        
        elif target_type == "team":
            # Get all on-call users for the team
            if self.oncall_manager:
                oncall_users = self.oncall_manager.get_oncall_users(target_id)
                for user_id in oncall_users:
                    user_info = self.oncall_manager.get_user_contact_info(user_id)
                    if user_info:
                        channels = ["email", "sms"] if alert.severity.name in ["CRITICAL", "EMERGENCY"] else ["email"]
                        for channel in channels:
                            self._send_notification(channel, user_info, alert)
        
        elif target_type == "channel":
            # Channel could be slack, teams, etc.
            channel_parts = target_id.split(":")
            if len(channel_parts) == 2:
                channel_type, channel_id = channel_parts
                self._send_notification(channel_type, {"id": channel_id}, alert)
    
    def _send_notification(self, channel: str, recipient: Dict, alert: Any) -> None:
        """Send a notification through the notification service"""
        try:
            self.notification_service.send_notification(
                channel=channel,
                subject=f"[ESCALATION] {alert.severity.name}: {alert.name}",
                message=f"Alert {alert.name} has been escalated.\n\n{alert.description}",
                recipient=recipient,
                context={
                    "alert": alert.to_dict(),
                    "dashboard_url": f"/alerts/view/{alert.id}",
                    "escalation": True
                }
            )
        except Exception as e:
            logger.error(f"Error sending escalation notification via {channel}: {str(e)}")


class EscalationPolicyManager:
    """Manages escalation policies"""
    
    def __init__(
        self,
        operational_db: OperationalDB,
        notification_service: Any,
        oncall_manager: Any
    ):
        self.operational_db = operational_db
        self.notification_service = notification_service
        self.oncall_manager = oncall_manager
        self.policies = {}  # id -> EscalationPolicy
        
        self._load_policies()
    
    def _load_policies(self) -> None:
        """Load policies from the database"""
        policies_data = self.operational_db.get_all("escalation_policies")
        for data in policies_data:
            policy = EscalationPolicy.from_dict(
                data, 
                self.operational_db,
                self.notification_service,
                self.oncall_manager
            )
            self.policies[policy.id] = policy
    
    def get_policy(self, policy_id: str) -> Optional[EscalationPolicy]:
        """Get a policy by ID"""
        return self.policies.get(policy_id)
    
    def get_default_policy(self) -> EscalationPolicy:
        """Get the default escalation policy"""
        # Try to find a policy named "Default"
        for policy in self.policies.values():
            if policy.name == "Default":
                return policy
                
        # If no default policy found, create one
        return self.create_default_policy()
    
    def create_default_policy(self) -> EscalationPolicy:
        """Create a default escalation policy"""
        steps = [
            EscalationStep(
                level=1,
                wait_minutes=15,
                notify_targets=[{"type": "channel", "id": "slack:alerts"}]
            ),
            EscalationStep(
                level=2,
                wait_minutes=15,
                notify_targets=[{"type": "team", "id": "sre-team"}]
            ),
            EscalationStep(
                level=3,
                wait_minutes=15,
                notify_targets=[{"type": "user", "id": "engineering-manager"}]
            )
        ]
        
        policy = EscalationPolicy(
            name="Default",
            description="Default escalation policy",
            steps=steps,
            repeat_count=1,
            operational_db=self.operational_db,
            notification_service=self.notification_service,
            oncall_manager=self.oncall_manager
        )
        
        # Save to database
        self.operational_db.upsert("escalation_policies", policy.to_dict(), {"id": policy.id})
        self.policies[policy.id] = policy
        
        return policy
    
    def create_policy(self, **kwargs) -> str:
        """Create a new escalation policy"""
        policy = EscalationPolicy(
            **kwargs,
            operational_db=self.operational_db,
            notification_service=self.notification_service,
            oncall_manager=self.oncall_manager
        )
        
        # Save to database
        self.operational_db.upsert("escalation_policies", policy.to_dict(), {"id": policy.id})
        self.policies[policy.id] = policy
        
        return policy.id
    
    def update_policy(self, policy_id: str, **kwargs) -> bool:
        """Update an existing policy"""
        policy = self.get_policy(policy_id)
        if not policy:
            return False
            
        # Update fields
        for key, value in kwargs.items():
            if hasattr(policy, key):
                setattr(policy, key, value)
                
        # Save to database
        self.operational_db.upsert("escalation_policies", policy.to_dict(), {"id": policy.id})
        
        return True
    
    def delete_policy(self, policy_id: str) -> bool:
        """Delete a policy"""
        if policy_id not in self.policies:
            return False
            
        # Remove from database
        self.operational_db.delete("escalation_policies", {"id": policy_id})
        
        # Remove from cache
        del self.policies[policy_id]
        
        return True