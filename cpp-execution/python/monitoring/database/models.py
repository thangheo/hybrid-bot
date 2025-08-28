# monitoring/database/models.py

from datetime import datetime
from enum import Enum
import uuid

class AlertSeverity(str, Enum):
    CRITICAL = "critical"
    WARNING = "warning"
    INFO = "info"
    
class AlertState(str, Enum):
    FIRING = "firing"
    ACKNOWLEDGED = "acknowledged"
    RESOLVED = "resolved"
    
class IncidentState(str, Enum):
    OPEN = "open"
    INVESTIGATING = "investigating"
    MITIGATING = "mitigating"
    RESOLVED = "resolved"
    CLOSED = "closed"
    
class ResourceType(str, Enum):
    CPU = "cpu"
    MEMORY = "memory"
    DISK = "disk"
    NETWORK = "network"
    
class Alert:
    """Alert model for representing monitoring alerts."""
    
    def __init__(self, 
                 name, 
                 severity, 
                 source, 
                 description, 
                 state=AlertState.FIRING,
                 alert_id=None, 
                 created_at=None, 
                 updated_at=None, 
                 acknowledged_at=None, 
                 resolved_at=None, 
                 assigned_to=None, 
                 tags=None, 
                 metrics=None):
        self.alert_id = alert_id or str(uuid.uuid4())
        self.name = name
        self.severity = severity
        self.source = source
        self.description = description
        self.state = state
        self.created_at = created_at or datetime.utcnow()
        self.updated_at = updated_at or datetime.utcnow()
        self.acknowledged_at = acknowledged_at
        self.resolved_at = resolved_at
        self.assigned_to = assigned_to
        self.tags = tags or {}
        self.metrics = metrics or {}
        
    def to_dict(self):
        """Convert alert to dictionary."""
        return {
            "alert_id": self.alert_id,
            "name": self.name,
            "severity": self.severity,
            "source": self.source,
            "description": self.description,
            "state": self.state,
            "created_at": self.created_at.isoformat(),
            "updated_at": self.updated_at.isoformat(),
            "acknowledged_at": self.acknowledged_at.isoformat() if self.acknowledged_at else None,
            "resolved_at": self.resolved_at.isoformat() if self.resolved_at else None,
            "assigned_to": self.assigned_to,
            "tags": self.tags,
            "metrics": self.metrics
        }
        
    @classmethod
    def from_dict(cls, data):
        """Create alert from dictionary."""
        # Convert string dates back to datetime objects
        for date_field in ['created_at', 'updated_at', 'acknowledged_at', 'resolved_at']:
            if data.get(date_field):
                if isinstance(data[date_field], str):
                    data[date_field] = datetime.fromisoformat(data[date_field])
                    
        return cls(**data)
        
class Incident:
    """Incident model for representing operational incidents."""
    
    def __init__(self, 
                 title, 
                 description, 
                 state=IncidentState.OPEN, 
                 incident_id=None,
                 created_at=None, 
                 updated_at=None, 
                 resolved_at=None, 
                 closed_at=None,
                 severity=AlertSeverity.WARNING, 
                 owner=None, 
                 alerts=None, 
                 tags=None,
                 timeline=None):
        self.incident_id = incident_id or str(uuid.uuid4())
        self.title = title
        self.description = description
        self.state = state
        self.created_at = created_at or datetime.utcnow()
        self.updated_at = updated_at or datetime.utcnow()
        self.resolved_at = resolved_at
        self.closed_at = closed_at
        self.severity = severity
        self.owner = owner
        self.alerts = alerts or []  # List of related alert IDs
        self.tags = tags or {}
        self.timeline = timeline or []  # List of incident events
        
    def add_timeline_event(self, event_type, description, author=None, timestamp=None):
        """Add an event to the incident timeline."""
        self.timeline.append({
            "event_type": event_type,
            "description": description,
            "author": author,
            "timestamp": timestamp or datetime.utcnow()
        })
        self.updated_at = datetime.utcnow()
        
    def to_dict(self):
        """Convert incident to dictionary."""
        return {
            "incident_id": self.incident_id,
            "title": self.title,
            "description": self.description,
            "state": self.state,
            "created_at": self.created_at.isoformat(),
            "updated_at": self.updated_at.isoformat(),
            "resolved_at": self.resolved_at.isoformat() if self.resolved_at else None,
            "closed_at": self.closed_at.isoformat() if self.closed_at else None,
            "severity": self.severity,
            "owner": self.owner,
            "alerts": self.alerts,
            "tags": self.tags,
            "timeline": [
                {**event, "timestamp": event["timestamp"].isoformat()} 
                for event in self.timeline
            ]
        }
        
    @classmethod
    def from_dict(cls, data):
        """Create incident from dictionary."""
        # Handle timeline conversion
        if "timeline" in data:
            for event in data["timeline"]:
                if isinstance(event["timestamp"], str):
                    event["timestamp"] = datetime.fromisoformat(event["timestamp"])
                    
        # Convert string dates back to datetime objects
        for date_field in ['created_at', 'updated_at', 'resolved_at', 'closed_at']:
            if data.get(date_field):
                if isinstance(data[date_field], str):
                    data[date_field] = datetime.fromisoformat(data[date_field])
                    
        return cls(**data)
        
class ResourceMetric:
    """Model for resource utilization metrics."""
    
    def __init__(self, 
                 resource_type, 
                 value, 
                 host, 
                 timestamp=None, 
                 component=None, 
                 metadata=None):
        self.resource_type = resource_type
        self.value = value
        self.host = host
        self.timestamp = timestamp or datetime.utcnow()
        self.component = component
        self.metadata = metadata or {}
        
    def to_dict(self):
        """Convert resource metric to dictionary."""
        return {
            "resource_type": self.resource_type,
            "value": self.value,
            "host": self.host,
            "timestamp": self.timestamp.isoformat(),
            "component": self.component,
            "metadata": self.metadata
        }
        
    @classmethod
    def from_dict(cls, data):
        """Create resource metric from dictionary."""
        if "timestamp" in data and isinstance(data["timestamp"], str):
            data["timestamp"] = datetime.fromisoformat(data["timestamp"])
        return cls(**data)

class LatencyMetric:
    """Model for latency metrics."""
    
    def __init__(self, 
                 source, 
                 destination, 
                 latency_ms, 
                 timestamp=None, 
                 operation=None, 
                 metadata=None):
        self.source = source
        self.destination = destination
        self.latency_ms = latency_ms
        self.timestamp = timestamp or datetime.utcnow()
        self.operation = operation
        self.metadata = metadata or {}
        
    def to_dict(self):
        """Convert latency metric to dictionary."""
        return {
            "source": self.source,
            "destination": self.destination,
            "latency_ms": self.latency_ms,
            "timestamp": self.timestamp.isoformat(),
            "operation": self.operation,
            "metadata": self.metadata
        }
        
    @classmethod
    def from_dict(cls, data):
        """Create latency metric from dictionary."""
        if "timestamp" in data and isinstance(data["timestamp"], str):
            data["timestamp"] = datetime.fromisoformat(data["timestamp"])
        return cls(**data)