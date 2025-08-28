# python/monitoring/alerts/incident_manager.py

import logging
import uuid
from datetime import datetime
from enum import Enum
from typing import Dict, List, Optional, Any

from ..database.operational_db import OperationalDB
from ..utils.notification import NotificationService

logger = logging.getLogger(__name__)

class IncidentSeverity(Enum):
    """Incident severity levels"""
    MINOR = 0
    MAJOR = 1
    CRITICAL = 2
    DISASTER = 3

class IncidentStatus(Enum):
    """Incident status in its lifecycle"""
    INVESTIGATING = 0
    IDENTIFIED = 1
    MITIGATING = 2
    RESOLVED = 3
    CLOSED = 4

class Incident:
    """Represents an incident in the system"""
    
    def __init__(
        self,
        title: str,
        description: str,
        severity: IncidentSeverity,
        detected_by: str,
        tags: Dict[str, str] = None,
        incident_id: str = None
    ):
        self.id = incident_id or str(uuid.uuid4())
        self.title = title
        self.description = description
        self.severity = severity
        self.detected_by = detected_by
        self.tags = tags or {}
        self.status = IncidentStatus.INVESTIGATING
        self.created_at = datetime.now()
        self.updated_at = datetime.now()
        self.resolved_at = None
        self.closed_at = None
        self.assigned_to = None
        self.commander = None
        self.communications_lead = None
        self.notes = []
        self.updates = []
        self.related_alerts = []
        self.affected_services = []
        self.impact_description = ""
        self.postmortem_url = ""
        self.timeline = []
        
        # Add initial timeline entry
        self.add_timeline_entry("Incident created", detected_by)
    
    def add_timeline_entry(self, description: str, user: str) -> None:
        """Add an entry to the incident timeline"""
        self.timeline.append({
            "timestamp": datetime.now(),
            "description": description,
            "user": user
        })
        self.updated_at = datetime.now()
    
    def set_status(self, status: IncidentStatus, user: str) -> None:
        """Update the incident status"""
        old_status = self.status
        self.status = status
        
        # Add to timeline
        self.add_timeline_entry(
            f"Status changed from {old_status.name} to {status.name}",
            user
        )
        
        # Set timestamps for specific statuses
        if status == IncidentStatus.RESOLVED:
            self.resolved_at = datetime.now()
        elif status == IncidentStatus.CLOSED:
            self.closed_at = datetime.now()
    
    def assign(self, user_id: str, assigner: str) -> None:
        """Assign the incident to a user"""
        old_assignee = self.assigned_to
        self.assigned_to = user_id
        
        # Add to timeline
        if old_assignee:
            self.add_timeline_entry(
                f"Reassigned from {old_assignee} to {user_id}",
                assigner
            )
        else:
            self.add_timeline_entry(
                f"Assigned to {user_id}",
                assigner
            )
    
    def set_commander(self, user_id: str, assigner: str) -> None:
        """Set the incident commander"""
        old_commander = self.commander
        self.commander = user_id
        
        # Add to timeline
        if old_commander:
            self.add_timeline_entry(
                f"Incident commander changed from {old_commander} to {user_id}",
                assigner
            )
        else:
            self.add_timeline_entry(
                f"Incident commander set to {user_id}",
                assigner
            )
    
    def set_communications_lead(self, user_id: str, assigner: str) -> None:
        """Set the communications lead"""
        old_lead = self.communications_lead
        self.communications_lead = user_id
        
        # Add to timeline
        if old_lead:
            self.add_timeline_entry(
                f"Communications lead changed from {old_lead} to {user_id}",
                assigner
            )
        else:
            self.add_timeline_entry(
                f"Communications lead set to {user_id}",
                assigner
            )
    
    def add_note(self, content: str, user: str) -> None:
        """Add a note to the incident"""
        self.notes.append({
            "timestamp": datetime.now(),
            "content": content,
            "user": user
        })
        self.updated_at = datetime.now()
    
    def add_update(self, content: str, user: str, public: bool = False) -> None:
        """Add an update to the incident"""
        self.updates.append({
            "timestamp": datetime.now(),
            "content": content,
            "user": user,
            "public": public
        })
        self.updated_at = datetime.now()
        
        # Add to timeline
        self.add_timeline_entry(
            "Added update" + (" (public)" if public else ""),
            user
        )
    
    def add_related_alert(self, alert_id: str, user: str) -> None:
        """Add a related alert to the incident"""
        if alert_id not in self.related_alerts:
            self.related_alerts.append(alert_id)
            self.updated_at = datetime.now()
            
            # Add to timeline
            self.add_timeline_entry(
                f"Added related alert {alert_id}",
                user
            )
    
    def add_affected_service(self, service: str, user: str) -> None:
        """Add an affected service to the incident"""
        if service not in self.affected_services:
            self.affected_services.append(service)
            self.updated_at = datetime.now()
            
            # Add to timeline
            self.add_timeline_entry(
                f"Added affected service: {service}",
                user
            )
    
    def set_impact_description(self, description: str, user: str) -> None:
        """Set the impact description"""
        self.impact_description = description
        self.updated_at = datetime.now()
        
        # Add to timeline
        self.add_timeline_entry(
            "Updated impact description",
            user
        )
    
    def set_postmortem_url(self, url: str, user: str) -> None:
        """Set the postmortem URL"""
        self.postmortem_url = url
        self.updated_at = datetime.now()
        
        # Add to timeline
        self.add_timeline_entry(
            "Added postmortem URL",
            user
        )
    
    def to_dict(self) -> Dict:
        """Convert incident to dictionary for persistence"""
        return {
            "id": self.id,
            "title": self.title,
            "description": self.description,
            "severity": self.severity.name,
            "detected_by": self.detected_by,
            "tags": self.tags,
            "status": self.status.name,
            "created_at": self.created_at.isoformat(),
            "updated_at": self.updated_at.isoformat(),
            "resolved_at": self.resolved_at.isoformat() if self.resolved_at else None,
            "closed_at": self.closed_at.isoformat() if self.closed_at else None,
            "assigned_to": self.assigned_to,
            "commander": self.commander,
            "communications_lead": self.communications_lead,
            "notes": self.notes,
            "updates": self.updates,
            "related_alerts": self.related_alerts,
            "affected_services": self.affected_services,
            "impact_description": self.impact_description,
            "postmortem_url": self.postmortem_url,
            "timeline": [{
                "timestamp": entry["timestamp"].isoformat(),
                "description": entry["description"],
                "user": entry["user"]
            } for entry in self.timeline]
        }
    
    @classmethod
    def from_dict(cls, data: Dict) -> 'Incident':
        """Create an incident from dictionary"""
        incident = cls(
            title=data["title"],
            description=data["description"],
            severity=IncidentSeverity[data["severity"]],
            detected_by=data["detected_by"],
            tags=data.get("tags", {}),
            incident_id=data["id"]
        )
        
        incident.status = IncidentStatus[data["status"]]
        incident.created_at = datetime.fromisoformat(data["created_at"])
        incident.updated_at = datetime.fromisoformat(data["updated_at"])
        
        if data.get("resolved_at"):
            incident.resolved_at = datetime.fromisoformat(data["resolved_at"])
        if data.get("closed_at"):
            incident.closed_at = datetime.fromisoformat(data["closed_at"])
            
        incident.assigned_to = data.get("assigned_to")
        incident.commander = data.get("commander")
        incident.communications_lead = data.get("communications_lead")
        incident.notes = data.get("notes", [])
        incident.updates = data.get("updates", [])
        incident.related_alerts = data.get("related_alerts", [])
        incident.affected_services = data.get("affected_services", [])
        incident.impact_description = data.get("impact_description", "")
        incident.postmortem_url = data.get("postmortem_url", "")
        
        # Convert timeline entries back to datetime objects
        incident.timeline = []
        for entry in data.get("timeline", []):
            incident.timeline.append({
                "timestamp": datetime.fromisoformat(entry["timestamp"]),
                "description": entry["description"],
                "user": entry["user"]
            })
            
        return incident


class IncidentManager:
    """Manages incidents throughout their lifecycle"""
    
    def __init__(
        self,
        operational_db: OperationalDB,
        notification_service: NotificationService,
        alert_manager: Any = None,
        oncall_manager: Any = None
    ):
        self.operational_db = operational_db
        self.notification_service = notification_service
        self.alert_manager = alert_manager
        self.oncall_manager = oncall_manager
        self.active_incidents = {}  # id -> Incident
        
        self._load_active_incidents()
    
    def _load_active_incidents(self) -> None:
        """Load active incidents from database"""
        incidents_data = self.operational_db.get_all(
            "incidents", 
            query={"status": ["INVESTIGATING", "IDENTIFIED", "MITIGATING"]}
        )
        for data in incidents_data:
            incident = Incident.from_dict(data)
            self.active_incidents[incident.id] = incident
    
    def create_incident(self, **kwargs) -> str:
        """Create a new incident"""
        incident = Incident(**kwargs)
        incident_id = self.save_incident(incident)
        
        # Send notifications
        self._send_incident_notification(incident, "created")
        
        # Auto-assign if oncall manager is available
        if self.oncall_manager and not incident.assigned_to:
            oncall_user = self.oncall_manager.get_oncall_user_for_service("general")
            if oncall_user:
                incident.assign(oncall_user, "system")
                self.save_incident(incident)
        
        return incident_id
    
    def save_incident(self, incident: Incident) -> str:
        """Save an incident to the database"""
        incident_dict = incident.to_dict()
        self.operational_db.upsert("incidents", incident_dict, {"id": incident.id})
        
        # Update active incidents cache
        if incident.status in [IncidentStatus.INVESTIGATING, IncidentStatus.IDENTIFIED, IncidentStatus.MITIGATING]:
            self.active_incidents[incident.id] = incident
        elif incident.id in self.active_incidents:
            del self.active_incidents[incident.id]
            
        return incident.id
    
    def get_incident(self, incident_id: str) -> Optional[Incident]:
        """Get an incident by ID"""
        if incident_id in self.active_incidents:
            return self.active_incidents[incident_id]
            
        # Try to load from database
        incident_data = self.operational_db.get_one("incidents", {"id": incident_id})
        if incident_data:
            return Incident.from_dict(incident_data)
            
        return None
    
    def update_incident_status(self, incident_id: str, status: IncidentStatus, user: str) -> bool:
        """Update an incident's status"""
        incident = self.get_incident(incident_id)
        if not incident:
            return False
            
        incident.set_status(status, user)
        self.save_incident(incident)
        
        # Send notifications
        self._send_incident_notification(incident, "status_changed")
        
        return True
    
    def assign_incident(self, incident_id: str, assignee: str, assigner: str) -> bool:
        """Assign an incident to a user"""
        incident = self.get_incident(incident_id)
        if not incident:
            return False
            
        incident.assign(assignee, assigner)
        self.save_incident(incident)
        
        # Send notification to the assignee
        self._send_assignment_notification(incident, assignee)
        
        return True
    
    def set_incident_commander(self, incident_id: str, commander: str, assigner: str) -> bool:
        """Set the incident commander"""
        incident = self.get_incident(incident_id)
        if not incident:
            return False
            
        incident.set_commander(commander, assigner)
        self.save_incident(incident)
        
        # Send notification to the commander
        self._send_role_notification(incident, commander, "commander")
        
        return True
    
    def add_note_to_incident(self, incident_id: str, note: str, user: str) -> bool:
        """Add a note to an incident"""
        incident = self.get_incident(incident_id)
        if not incident:
            return False
            
        incident.add_note(note, user)
        self.save_incident(incident)
        
        return True
    
    def add_update_to_incident(self, incident_id: str, update: str, user: str, public: bool = False) -> bool:
        """Add an update to an incident"""
        incident = self.get_incident(incident_id)
        if not incident:
            return False
            
        incident.add_update(update, user, public)
        self.save_incident(incident)
        
        # If public update, send broader notification
        if public:
            self._send_incident_notification(incident, "public_update")
        
        return True
    
    def add_related_alert(self, incident_id: str, alert_id: str, user: str) -> bool:
        """Add a related alert to an incident"""
        incident = self.get_incident(incident_id)
        if not incident:
            return False
            
        incident.add_related_alert(alert_id, user)
        self.save_incident(incident)
        
        # Update the alert to reference this incident if alert manager is available
        if self.alert_manager:
            alert = self.alert_manager.get_alert(alert_id)
            if alert:
                alert.incident_id = incident_id
                self.alert_manager.save_alert(alert)
        
        return True
    
    def create_incident_from_alert(self, alert_id: str, user: str) -> Optional[str]:
        """Create a new incident from an alert"""
        if not self.alert_manager:
            logger.error("Alert manager not available, cannot create incident from alert")
            return None
            
        alert = self.alert_manager.get_alert(alert_id)
        if not alert:
            logger.error(f"Alert {alert_id} not found")
            return None
            
        # Create the incident
        incident = Incident(
            title=f"Incident from alert: {alert.name}",
            description=alert.description,
            severity=self._map_alert_severity_to_incident(alert.severity),
            detected_by=user,
            tags=alert.tags.copy()
        )
        
        # Add the alert as related
        incident.add_related_alert(alert_id, user)
        
        # Save the incident
        incident_id = self.save_incident(incident)
        
        # Update the alert to reference this incident
        alert.incident_id = incident_id
        self.alert_manager.save_alert(alert)
        
        # Send notifications
        self._send_incident_notification(incident, "created")
        
        return incident_id
    
    def _map_alert_severity_to_incident(self, alert_severity: Any) -> IncidentSeverity:
        """Map alert severity to incident severity"""
        if str(alert_severity) == "EMERGENCY":
            return IncidentSeverity.DISASTER
        elif str(alert_severity) == "CRITICAL":
            return IncidentSeverity.CRITICAL
        elif str(alert_severity) == "WARNING":
            return IncidentSeverity.MAJOR
        else:
            return IncidentSeverity.MINOR
    
    def search_incidents(self, query: Dict) -> List[Incident]:
        """Search for incidents using a query"""
        incidents_data = self.operational_db.get_all("incidents", query)
        return [Incident.from_dict(data) for data in incidents_data]
    
    def get_active_incidents(self) -> List[Incident]:
        """Get all active incidents"""
        return list(self.active_incidents.values())
    
    def get_incidents_by_severity(self, severity: IncidentSeverity) -> List[Incident]:
        """Get incidents by severity"""
        return [i for i in self.active_incidents.values() if i.severity == severity]
    
    def get_recent_incidents(self, days: int = 7) -> List[Incident]:
        """Get recent incidents"""
        cutoff = datetime.now() - timedelta(days=days)
        query = {"created_at": {"$gte": cutoff.isoformat()}}
        return self.search_incidents(query)
    
    def _send_incident_notification(self, incident: Incident, notification_type: str) -> None:
        """Send a notification about an incident"""
        # Determine recipients based on incident severity and notification type
        recipients = self._get_notification_recipients(incident, notification_type)
        
        # Prepare notification content
        subject = f"[{incident.severity.name}] {notification_type.upper()}: {incident.title}"
        
        message = f"Incident: {incident.title}\n"
        message += f"Severity: {incident.severity.name}\n"
        message += f"Status: {incident.status.name}\n\n"
        message += incident.description
        
        if notification_type == "status_changed":
            message += f"\n\nStatus changed to: {incident.status.name}"
        elif notification_type == "public_update":
            # Add the most recent public update
            public_updates = [u for u in incident.updates if u.get("public", False)]
            if public_updates:
                latest = public_updates[-1]
                message += f"\n\nUpdate ({latest['timestamp'].strftime('%Y-%m-%d %H:%M:%S')}):\n"
                message += latest["content"]
        
        # Send to each recipient
        for channel, recipient_list in recipients.items():
            for recipient in recipient_list:
                try:
                    self.notification_service.send_notification(
                        channel=channel,
                        subject=subject,
                        message=message,
                        recipient=recipient,
                        context={
                            "incident": incident.to_dict(),
                            "notification_type": notification_type,
                            "dashboard_url": f"/incidents/view/{incident.id}"
                        }
                    )
                except Exception as e:
                    logger.error(f"Error sending incident notification via {channel}: {str(e)}")
    
    def _send_assignment_notification(self, incident: Incident, assignee: str) -> None:
        """Send notification to an assignee"""
        if not self.oncall_manager:
            logger.warning("OnCall manager not available, cannot send assignment notification")
            return
            
        user_info = self.oncall_manager.get_user_contact_info(assignee)
        if not user_info:
            logger.warning(f"No contact info found for user {assignee}")
            return
            
        subject = f"[ASSIGNED] {incident.severity.name} Incident: {incident.title}"
        message = f"You have been assigned to the following incident:\n\n"
        message += f"Incident: {incident.title}\n"
        message += f"Severity: {incident.severity.name}\n"
        message += f"Status: {incident.status.name}\n\n"
        message += incident.description
        
        try:
            self.notification_service.send_notification(
                channel="email",
                subject=subject,
                message=message,
                recipient=user_info,
                context={
                    "incident": incident.to_dict(),
                    "notification_type": "assignment",
                    "dashboard_url": f"/incidents/view/{incident.id}"
                }
            )
            
            # Also send SMS for high severity incidents
            if incident.severity in [IncidentSeverity.CRITICAL, IncidentSeverity.DISASTER]:
                short_message = f"ASSIGNED: {incident.severity.name} incident '{incident.title}'. Check email for details."
                self.notification_service.send_notification(
                    channel="sms",
                    subject="",
                    message=short_message,
                    recipient=user_info,
                    context={
                        "incident": incident.to_dict(),
                        "notification_type": "assignment",
                        "dashboard_url": f"/incidents/view/{incident.id}"
                    }
                )
        except Exception as e:
            logger.error(f"Error sending assignment notification: {str(e)}")
    
    def _send_role_notification(self, incident: Incident, user_id: str, role: str) -> None:
        """Send notification to a user about a role assignment"""
        if not self.oncall_manager:
            logger.warning("OnCall manager not available, cannot send role notification")
            return
            
        user_info = self.oncall_manager.get_user_contact_info(user_id)
        if not user_info:
            logger.warning(f"No contact info found for user {user_id}")
            return
            
        role_display = role.replace("_", " ").title()
        subject = f"[{role_display.upper()}] {incident.severity.name} Incident: {incident.title}"
        message = f"You have been assigned as {role_display} for the following incident:\n\n"
        message += f"Incident: {incident.title}\n"
        message += f"Severity: {incident.severity.name}\n"
        message += f"Status: {incident.status.name}\n\n"
        message += incident.description
        
        try:
            self.notification_service.send_notification(
                channel="email",
                subject=subject,
                message=message,
                recipient=user_info,
                context={
                    "incident": incident.to_dict(),
                    "notification_type": "role_assignment",
                    "role": role,
                    "dashboard_url": f"/incidents/view/{incident.id}"
                }
            )
        except Exception as e:
            logger.error(f"Error sending role notification: {str(e)}")
    
    def _get_notification_recipients(self, incident: Incident, notification_type: str) -> Dict[str, List[Dict]]:
        """Determine notification recipients based on incident and notification type"""
        recipients = {
            "email": [],
            "sms": [],
            "slack": [],
            "pagerduty": []
        }
        
        # Always send to #incidents Slack channel
        recipients["slack"].append({"channel": "incidents"})
        
        # For high severity incidents, use more channels
        if incident.severity in [IncidentSeverity.CRITICAL, IncidentSeverity.DISASTER]:
            # Add PagerDuty
            recipients["pagerduty"].append({"service": "trading-platform"})
            
            # Add SMS for SRE team
            if self.oncall_manager:
                sre_team = self.oncall_manager.get_team_members("sre-team")
                for member in sre_team:
                    contact_info = self.oncall_manager.get_user_contact_info(member)
                    if contact_info and "phone" in contact_info:
                        recipients["sms"].append(contact_info)
        
        # For public updates, add more distribution channels
        if notification_type == "public_update":
            recipients["email"].append({"distribution_list": "incident-updates"})
            recipients["slack"].append({"channel": "general"})
        
        # If assigned, always notify the assignee
        if incident.assigned_to and self.oncall_manager:
            contact_info = self.oncall_manager.get_user_contact_info(incident.assigned_to)
            if contact_info:
                recipients["email"].append(contact_info)
                
                # Add SMS for high severity
                if incident.severity in [IncidentSeverity.CRITICAL, IncidentSeverity.DISASTER] and "phone" in contact_info:
                    recipients["sms"].append(contact_info)
        
        return recipients