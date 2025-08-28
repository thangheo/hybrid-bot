# python/monitoring/alerts/alert_manager.py

import logging
import uuid
from datetime import datetime, timedelta
from enum import Enum
from typing import Dict, List, Optional, Callable, Any, Union

from ..database.operational_db import OperationalDB
from ..database.timeseries_db import TimeSeriesDB
from ..utils.notification import NotificationService
from .escalation_policy import EscalationPolicy

logger = logging.getLogger(__name__)

class AlertSeverity(Enum):
    """Alert severity levels"""
    INFO = 0
    WARNING = 1
    CRITICAL = 2
    EMERGENCY = 3

class AlertState(Enum):
    """Alert state in its lifecycle"""
    PENDING = 0
    FIRING = 1
    ACKNOWLEDGED = 2
    RESOLVED = 3
    CLOSED = 4

class Alert:
    """Represents an alert in the system"""
    
    def __init__(
        self,
        name: str,
        description: str,
        severity: AlertSeverity,
        source: str,
        tags: Dict[str, str] = None,
        alert_id: str = None
    ):
        self.id = alert_id or str(uuid.uuid4())
        self.name = name
        self.description = description
        self.severity = severity
        self.source = source
        self.tags = tags or {}
        self.state = AlertState.PENDING
        self.created_at = datetime.now()
        self.updated_at = datetime.now()
        self.acknowledged_at = None
        self.resolved_at = None
        self.closed_at = None
        self.acknowledged_by = None
        self.resolved_by = None
        self.notes = []
        self.incident_id = None
        self.silence_until = None
        self.metrics = {}
        self.event_history = []
        
        self._record_event("Alert created")
    
    def _record_event(self, event: str) -> None:
        """Record an event in the alert's history"""
        self.event_history.append({
            "timestamp": datetime.now(),
            "event": event
        })
        self.updated_at = datetime.now()
    
    def fire(self) -> None:
        """Mark the alert as firing"""
        if self.state == AlertState.PENDING:
            self.state = AlertState.FIRING
            self._record_event("Alert started firing")
        
    def acknowledge(self, user: str) -> None:
        """Acknowledge the alert"""
        if self.state in [AlertState.PENDING, AlertState.FIRING]:
            self.state = AlertState.ACKNOWLEDGED
            self.acknowledged_at = datetime.now()
            self.acknowledged_by = user
            self._record_event(f"Acknowledged by {user}")
    
    def resolve(self, user: str) -> None:
        """Resolve the alert"""
        if self.state in [AlertState.PENDING, AlertState.FIRING, AlertState.ACKNOWLEDGED]:
            self.state = AlertState.RESOLVED
            self.resolved_at = datetime.now()
            self.resolved_by = user
            self._record_event(f"Resolved by {user}")
    
    def close(self) -> None:
        """Close the alert (terminal state)"""
        if self.state == AlertState.RESOLVED:
            self.state = AlertState.CLOSED
            self.closed_at = datetime.now()
            self._record_event("Alert closed")
    
    def add_note(self, user: str, note: str) -> None:
        """Add a note to the alert"""
        self.notes.append({
            "timestamp": datetime.now(),
            "user": user,
            "note": note
        })
        self._record_event(f"Note added by {user}")
    
    def add_metric(self, name: str, value: Any) -> None:
        """Add a metric to the alert"""
        self.metrics[name] = value
        self._record_event(f"Added metric: {name}={value}")
        
    def silence(self, duration_minutes: int) -> None:
        """Silence the alert for a specified duration"""
        self.silence_until = datetime.now() + timedelta(minutes=duration_minutes)
        self._record_event(f"Silenced for {duration_minutes} minutes")
    
    def is_silenced(self) -> bool:
        """Check if the alert is currently silenced"""
        if not self.silence_until:
            return False
        return datetime.now() < self.silence_until
    
    def to_dict(self) -> Dict:
        """Convert alert to dictionary for persistence"""
        return {
            "id": self.id,
            "name": self.name,
            "description": self.description,
            "severity": self.severity.name,
            "source": self.source,
            "tags": self.tags,
            "state": self.state.name,
            "created_at": self.created_at.isoformat(),
            "updated_at": self.updated_at.isoformat(),
            "acknowledged_at": self.acknowledged_at.isoformat() if self.acknowledged_at else None,
            "resolved_at": self.resolved_at.isoformat() if self.resolved_at else None,
            "closed_at": self.closed_at.isoformat() if self.closed_at else None,
            "acknowledged_by": self.acknowledged_by,
            "resolved_by": self.resolved_by,
            "notes": self.notes,
            "incident_id": self.incident_id,
            "silence_until": self.silence_until.isoformat() if self.silence_until else None,
            "metrics": self.metrics,
            "event_history": self.event_history
        }
    
    @classmethod
    def from_dict(cls, data: Dict) -> 'Alert':
        """Create an alert from a dictionary"""
        alert = cls(
            name=data["name"],
            description=data["description"],
            severity=AlertSeverity[data["severity"]],
            source=data["source"],
            tags=data.get("tags", {}),
            alert_id=data["id"]
        )
        
        alert.state = AlertState[data["state"]]
        alert.created_at = datetime.fromisoformat(data["created_at"])
        alert.updated_at = datetime.fromisoformat(data["updated_at"])
        
        if data.get("acknowledged_at"):
            alert.acknowledged_at = datetime.fromisoformat(data["acknowledged_at"])
        if data.get("resolved_at"):
            alert.resolved_at = datetime.fromisoformat(data["resolved_at"])
        if data.get("closed_at"):
            alert.closed_at = datetime.fromisoformat(data["closed_at"])
            
        alert.acknowledged_by = data.get("acknowledged_by")
        alert.resolved_by = data.get("resolved_by")
        alert.notes = data.get("notes", [])
        alert.incident_id = data.get("incident_id")
        
        if data.get("silence_until"):
            alert.silence_until = datetime.fromisoformat(data["silence_until"])
            
        alert.metrics = data.get("metrics", {})
        alert.event_history = data.get("event_history", [])
        
        return alert


class AlertCondition:
    """Represents a condition that can trigger an alert"""
    
    def __init__(
        self,
        name: str,
        description: str,
        severity: AlertSeverity,
        source: str,
        tags: Dict[str, str] = None,
        check_interval: int = 60,  # seconds
        evaluation_fn: Callable = None,
        metric_query: str = None,
        threshold: float = None,
        comparison: str = None,
        duration: int = 0,  # seconds
        notification_channels: List[str] = None
    ):
        self.id = str(uuid.uuid4())
        self.name = name
        self.description = description
        self.severity = severity
        self.source = source
        self.tags = tags or {}
        self.check_interval = check_interval
        self.evaluation_fn = evaluation_fn
        self.metric_query = metric_query
        self.threshold = threshold
        self.comparison = comparison
        self.duration = duration
        self.notification_channels = notification_channels or ["email"]
        self.last_check = datetime.now()
        self.consecutive_failures = 0
        self.is_active = True
        
    def to_dict(self) -> Dict:
        """Convert condition to dictionary for persistence"""
        return {
            "id": self.id,
            "name": self.name,
            "description": self.description,
            "severity": self.severity.name,
            "source": self.source,
            "tags": self.tags,
            "check_interval": self.check_interval,
            "metric_query": self.metric_query,
            "threshold": self.threshold,
            "comparison": self.comparison,
            "duration": self.duration,
            "notification_channels": self.notification_channels,
            "last_check": self.last_check.isoformat(),
            "consecutive_failures": self.consecutive_failures,
            "is_active": self.is_active
        }
    
    @classmethod
    def from_dict(cls, data: Dict) -> 'AlertCondition':
        """Create a condition from a dictionary"""
        condition = cls(
            name=data["name"],
            description=data["description"],
            severity=AlertSeverity[data["severity"]],
            source=data["source"],
            tags=data.get("tags", {}),
            check_interval=data["check_interval"],
            metric_query=data.get("metric_query"),
            threshold=data.get("threshold"),
            comparison=data.get("comparison"),
            duration=data.get("duration", 0),
            notification_channels=data.get("notification_channels", ["email"])
        )
        
        condition.id = data["id"]
        condition.last_check = datetime.fromisoformat(data["last_check"])
        condition.consecutive_failures = data["consecutive_failures"]
        condition.is_active = data["is_active"]
        
        return condition


class AlertManager:
    """Manages alerts throughout their lifecycle"""
    
    def __init__(
        self,
        operational_db: OperationalDB,
        timeseries_db: TimeSeriesDB,
        notification_service: NotificationService,
        escalation_policy: EscalationPolicy
    ):
        self.operational_db = operational_db
        self.timeseries_db = timeseries_db
        self.notification_service = notification_service
        self.escalation_policy = escalation_policy
        self.conditions = {}  # id -> AlertCondition
        self.active_alerts = {}  # id -> Alert
        
        self._load_conditions()
        self._load_active_alerts()
        
    def _load_conditions(self) -> None:
        """Load alert conditions from database"""
        conditions_data = self.operational_db.get_all("alert_conditions")
        for data in conditions_data:
            condition = AlertCondition.from_dict(data)
            self.conditions[condition.id] = condition
            
    def _load_active_alerts(self) -> None:
        """Load active alerts from database"""
        alerts_data = self.operational_db.get_all("alerts", 
                                                  query={"state": ["PENDING", "FIRING", "ACKNOWLEDGED"]})
        for data in alerts_data:
            alert = Alert.from_dict(data)
            self.active_alerts[alert.id] = alert

    def save_condition(self, condition: AlertCondition) -> str:
        """Save a condition to the database"""
        condition_dict = condition.to_dict()
        self.operational_db.upsert("alert_conditions", condition_dict, {"id": condition.id})
        self.conditions[condition.id] = condition
        return condition.id
    
    def save_alert(self, alert: Alert) -> str:
        """Save an alert to the database"""
        alert_dict = alert.to_dict()
        self.operational_db.upsert("alerts", alert_dict, {"id": alert.id})
        
        # Add to active alerts if not closed or resolved
        if alert.state not in [AlertState.CLOSED, AlertState.RESOLVED]:
            self.active_alerts[alert.id] = alert
        elif alert.id in self.active_alerts:
            del self.active_alerts[alert.id]
            
        return alert.id
    
    def create_condition(self, **kwargs) -> str:
        """Create a new alert condition"""
        condition = AlertCondition(**kwargs)
        return self.save_condition(condition)
    
    def create_alert(self, **kwargs) -> str:
        """Create a new alert"""
        alert = Alert(**kwargs)
        alert.fire()  # New alerts start firing immediately
        alert_id = self.save_alert(alert)
        
        # Send initial notification
        self._send_alert_notification(alert)
        
        return alert_id
    
    def evaluate_condition(self, condition_id: str) -> Optional[str]:
        """Evaluate a condition and create an alert if it's triggered"""
        condition = self.conditions.get(condition_id)
        if not condition or not condition.is_active:
            return None
        
        condition.last_check = datetime.now()
        self.save_condition(condition)
        
        # Custom function evaluation
        if condition.evaluation_fn:
            try:
                result = condition.evaluation_fn()
                if result:
                    return self._trigger_alert_from_condition(condition, result)
                condition.consecutive_failures = 0
                return None
            except Exception as e:
                logger.error(f"Error evaluating condition {condition.id}: {str(e)}")
                return None
        
        # Metric query evaluation
        if condition.metric_query and condition.threshold is not None:
            try:
                value = self._evaluate_metric_query(condition.metric_query)
                triggered = self._compare_value(value, condition.threshold, condition.comparison)
                
                if triggered:
                    condition.consecutive_failures += 1
                    if condition.duration == 0 or condition.consecutive_failures * condition.check_interval >= condition.duration:
                        return self._trigger_alert_from_condition(condition, value)
                else:
                    condition.consecutive_failures = 0
                
                self.save_condition(condition)
                return None
            except Exception as e:
                logger.error(f"Error evaluating metric query for condition {condition.id}: {str(e)}")
                return None
        
        return None
    
    def _evaluate_metric_query(self, query: str) -> Union[float, int, bool]:
        """Evaluate a metric query against the time series database"""
        # This would be implemented based on your time series database
        result = self.timeseries_db.query(query)
        if not result:
            return None
        
        # Return the most recent value
        return result[0].get("value")
    
    def _compare_value(self, value: float, threshold: float, comparison: str) -> bool:
        """Compare a value against a threshold"""
        if value is None:
            return False
            
        if comparison == "gt":
            return value > threshold
        elif comparison == "lt":
            return value < threshold
        elif comparison == "gte":
            return value >= threshold
        elif comparison == "lte":
            return value <= threshold
        elif comparison == "eq":
            return value == threshold
        elif comparison == "neq":
            return value != threshold
        else:
            logger.error(f"Unknown comparison operator: {comparison}")
            return False
    
    def _trigger_alert_from_condition(self, condition: AlertCondition, value: Any) -> str:
        """Create an alert from a triggered condition"""
        # Check if there's already an active alert for this condition
        for alert in self.active_alerts.values():
            if (alert.source == condition.source and 
                alert.name == condition.name and
                alert.state in [AlertState.PENDING, AlertState.FIRING, AlertState.ACKNOWLEDGED]):
                # Update existing alert
                alert.add_metric("latest_value", value)
                self.save_alert(alert)
                return alert.id
        
        # Create new alert
        alert = Alert(
            name=condition.name,
            description=condition.description,
            severity=condition.severity,
            source=condition.source,
            tags=condition.tags.copy()
        )
        alert.add_metric("latest_value", value)
        alert.fire()
        alert_id = self.save_alert(alert)
        
        # Send notification
        self._send_alert_notification(alert)
        
        return alert_id
    
    def _send_alert_notification(self, alert: Alert) -> None:
        """Send a notification for an alert"""
        if alert.is_silenced():
            logger.info(f"Not sending notification for silenced alert {alert.id}")
            return
            
        # Determine notification channels based on severity and tags
        channels = self._get_notification_channels(alert)
        
        # Send the notification
        for channel in channels:
            try:
                self.notification_service.send_notification(
                    channel=channel,
                    subject=f"[{alert.severity.name}] {alert.name}",
                    message=alert.description,
                    context={
                        "alert": alert.to_dict(),
                        "dashboard_url": f"/alerts/view/{alert.id}"
                    }
                )
            except Exception as e:
                logger.error(f"Error sending notification via {channel} for alert {alert.id}: {str(e)}")
    
    def _get_notification_channels(self, alert: Alert) -> List[str]:
        """Determine appropriate notification channels for an alert"""
        # Start with default channels based on severity
        if alert.severity == AlertSeverity.EMERGENCY:
            channels = ["email", "sms", "phone", "slack"]
        elif alert.severity == AlertSeverity.CRITICAL:
            channels = ["email", "sms", "slack"]
        elif alert.severity == AlertSeverity.WARNING:
            channels = ["email", "slack"]
        else:  # INFO
            channels = ["email"]
            
        # Add or remove channels based on alert tags
        if "silent" in alert.tags:
            channels = []
        if "slack_only" in alert.tags:
            channels = ["slack"]
        if "no_email" in alert.tags and "email" in channels:
            channels.remove("email")
            
        return channels
    
    def get_alert(self, alert_id: str) -> Optional[Alert]:
        """Get an alert by ID"""
        if alert_id in self.active_alerts:
            return self.active_alerts[alert_id]
            
        # Try to load from database
        alert_data = self.operational_db.get_one("alerts", {"id": alert_id})
        if alert_data:
            return Alert.from_dict(alert_data)
            
        return None
    
    def get_condition(self, condition_id: str) -> Optional[AlertCondition]:
        """Get a condition by ID"""
        if condition_id in self.conditions:
            return self.conditions[condition_id]
            
        # Try to load from database
        condition_data = self.operational_db.get_one("alert_conditions", {"id": condition_id})
        if condition_data:
            condition = AlertCondition.from_dict(condition_data)
            self.conditions[condition.id] = condition
            return condition
            
        return None
    
    def acknowledge_alert(self, alert_id: str, user: str) -> bool:
        """Acknowledge an alert"""
        alert = self.get_alert(alert_id)
        if not alert:
            return False
            
        alert.acknowledge(user)
        self.save_alert(alert)
        return True
    
    def resolve_alert(self, alert_id: str, user: str) -> bool:
        """Resolve an alert"""
        alert = self.get_alert(alert_id)
        if not alert:
            return False
            
        alert.resolve(user)
        self.save_alert(alert)
        return True
    
    def silence_alert(self, alert_id: str, duration_minutes: int) -> bool:
        """Silence an alert for a period of time"""
        alert = self.get_alert(alert_id)
        if not alert:
            return False
            
        alert.silence(duration_minutes)
        self.save_alert(alert)
        return True
    
    def add_note_to_alert(self, alert_id: str, user: str, note: str) -> bool:
        """Add a note to an alert"""
        alert = self.get_alert(alert_id)
        if not alert:
            return False
            
        alert.add_note(user, note)
        self.save_alert(alert)
        return True
    
    def search_alerts(self, query: Dict) -> List[Alert]:
        """Search for alerts using a query"""
        alerts_data = self.operational_db.get_all("alerts", query)
        return [Alert.from_dict(data) for data in alerts_data]
    
    def search_conditions(self, query: Dict) -> List[AlertCondition]:
        """Search for conditions using a query"""
        conditions_data = self.operational_db.get_all("alert_conditions", query)
        return [AlertCondition.from_dict(data) for data in conditions_data]
    
    def get_active_alerts(self) -> List[Alert]:
        """Get all active alerts"""
        return list(self.active_alerts.values())
    
    def get_alerts_by_severity(self, severity: AlertSeverity) -> List[Alert]:
        """Get active alerts by severity"""
        return [a for a in self.active_alerts.values() if a.severity == severity]
    
    def process_escalations(self) -> None:
        """Process escalations for alerts that have been firing too long"""
        for alert in self.active_alerts.values():
            if alert.state == AlertState.FIRING:
                self.escalation_policy.process_alert(alert)