# python/monitoring/alerts/oncall_manager.py

import logging
import uuid
from datetime import datetime, timedelta
from enum import Enum
from typing import Dict, List, Optional, Any, Set, Tuple

from ..database.operational_db import OperationalDB
from ..utils.notification import NotificationService

logger = logging.getLogger(__name__)

class ScheduleRepeatType(Enum):
    """Type of schedule repetition"""
    NONE = 0
    DAILY = 1
    WEEKLY = 2
    BIWEEKLY = 3
    MONTHLY = 4

class OnCallUser:
    """Represents a user who can be on-call"""
    
    def __init__(
        self,
        username: str,
        full_name: str,
        email: str,
        phone: str = None,
        slack_id: str = None,
        teams: List[str] = None,
        user_id: str = None
    ):
        self.id = user_id or str(uuid.uuid4())
        self.username = username
        self.full_name = full_name
        self.email = email
        self.phone = phone
        self.slack_id = slack_id
        self.teams = teams or []
        self.preferences = {
            "notification_channels": ["email", "slack"],
            "notify_sms_only_critical": True
        }
    
    def to_dict(self) -> Dict:
        """Convert to dictionary"""
        return {
            "id": self.id,
            "username": self.username,
            "full_name": self.full_name,
            "email": self.email,
            "phone": self.phone,
            "slack_id": self.slack_id,
            "teams": self.teams,
            "preferences": self.preferences
        }
    
    @classmethod
    def from_dict(cls, data: Dict) -> 'OnCallUser':
        """Create from dictionary"""
        user = cls(
            username=data["username"],
            full_name=data["full_name"],
            email=data["email"],
            phone=data.get("phone"),
            slack_id=data.get("slack_id"),
            teams=data.get("teams", []),
            user_id=data["id"]
        )
        user.preferences = data.get("preferences", {
            "notification_channels": ["email", "slack"],
            "notify_sms_only_critical": True
        })
        
        if "preferences" in data:
            user.preferences = data["preferences"]
            
        return user


class OnCallSchedule:
    """Represents an on-call schedule"""
    
    def __init__(
        self,
        name: str,
        description: str,
        team_id: str,
        schedule_id: str = None
    ):
        self.id = schedule_id or str(uuid.uuid4())
        self.name = name
        self.description = description
        self.team_id = team_id
        self.rotations = []  # List of rotation periods
        self.overrides = []  # List of temporary schedule overrides
        self.created_at = datetime.now()
        self.updated_at = datetime.now()
    
    def add_rotation(
        self,
        start_time: datetime,
        end_time: datetime,
        users: List[str],
        repeat_type: ScheduleRepeatType = ScheduleRepeatType.NONE,
        repeat_interval: int = 1
    ) -> str:
        """Add a rotation period to the schedule"""
        rotation_id = str(uuid.uuid4())
        rotation = {
            "id": rotation_id,
            "start_time": start_time,
            "end_time": end_time,
            "users": users,
            "repeat_type": repeat_type,
            "repeat_interval": repeat_interval
        }
        self.rotations.append(rotation)
        self.updated_at = datetime.now()
        return rotation_id
    
    def add_override(
        self,
        user_id: str,
        start_time: datetime,
        end_time: datetime,
        reason: str = ""
    ) -> str:
        """Add a temporary override to the schedule"""
        override_id = str(uuid.uuid4())
        override = {
            "id": override_id,
            "user_id": user_id,
            "start_time": start_time,
            "end_time": end_time,
            "reason": reason
        }
        self.overrides.append(override)
        self.updated_at = datetime.now()
        return override_id
    
    def get_oncall_user(self, time: datetime = None) -> Optional[str]:
        """Get the user on call at a specific time"""
        if time is None:
            time = datetime.now()
            
        # First check for overrides
        for override in self.overrides:
            if override["start_time"] <= time < override["end_time"]:
                return override["user_id"]
                
        # Then check rotations
        for rotation in self.rotations:
            user_id = self._check_rotation(rotation, time)
            if user_id:
                return user_id
                
        return None
    
    def _check_rotation(self, rotation: Dict, time: datetime) -> Optional[str]:
        """Check if a time falls within a rotation period"""
        start_time = rotation["start_time"]
        end_time = rotation["end_time"]
        
        # No repeat, simple time range check
        if rotation["repeat_type"] == ScheduleRepeatType.NONE:
            if start_time <= time < end_time:
                return self._get_user_for_rotation(rotation, time)
            return None
            
        # Handle repeating schedules
        time_delta = time - start_time
        
        # Daily repeat
        if rotation["repeat_type"] == ScheduleRepeatType.DAILY:
            days = time_delta.days
            if days < 0:
                return None
                
            # Calculate the equivalent time in the first rotation period
            days_offset = days % rotation["repeat_interval"]
            equivalent_time = time - timedelta(days=days_offset)
            
            if start_time <= equivalent_time < end_time:
                return self._get_user_for_rotation(rotation, time)
                
        # Weekly repeat
        elif rotation["repeat_type"] == ScheduleRepeatType.WEEKLY:
            weeks = time_delta.days // 7
            if weeks < 0:
                return None
                
            # Calculate the equivalent time in the first rotation period
            weeks_offset = weeks % rotation["repeat_interval"]
            equivalent_time = time - timedelta(days=weeks_offset*7)
            
            if start_time <= equivalent_time < end_time:
                return self._get_user_for_rotation(rotation, time)
                
        # Biweekly repeat
        elif rotation["repeat_type"] == ScheduleRepeatType.BIWEEKLY:
            weeks = time_delta.days // 7
            if weeks < 0:
                return None
                
            # Calculate the equivalent time in the first rotation period
            biweeks_offset = (weeks // 2) % rotation["repeat_interval"]
            equivalent_time = time - timedelta(days=biweeks_offset*14)
            
            if start_time <= equivalent_time < end_time:
                return self._get_user_for_rotation(rotation, time)
                
        # Monthly repeat
        elif rotation["repeat_type"] == ScheduleRepeatType.MONTHLY:
            # Calculate months difference (approximate)
            months = (time.year - start_time.year) * 12 + (time.month - start_time.month)
            if months < 0:
                return None
                
            # Check if we're in the right day of month
            if time.day == start_time.day:
                hour_start = start_time.hour
                hour_end = end_time.hour
                
                if hour_start <= time.hour < hour_end:
                    return self._get_user_for_rotation(rotation, time)
                    
        return None
    
    def _get_user_for_rotation(self, rotation: Dict, time: datetime) -> str:
        """Get the user for a specific time in a rotation"""
        users = rotation["users"]
        if not users:
            return None
            
        # For rotations with a single user, it's simple
        if len(users) == 1:
            return users[0]
            
        # For multi-user rotations, calculate based on time since start
        start_time = rotation["start_time"]
        time_delta = time - start_time
        
        # Calculate a user index based on days since start
        days = time_delta.days
        user_index = days % len(users)
        
        return users[user_index]
    
    def to_dict(self) -> Dict:
        """Convert to dictionary"""
        return {
            "id": self.id,
            "name": self.name,
            "description": self.description,
            "team_id": self.team_id,
            "rotations": [
                {
                    "id": r["id"],
                    "start_time": r["start_time"].isoformat(),
                    "end_time": r["end_time"].isoformat(),
                    "users": r["users"],
                    "repeat_type": r["repeat_type"].name,
                    "repeat_interval": r["repeat_interval"]
                }
                for r in self.rotations
            ],
            "overrides": [
                {
                    "id": o["id"],
                    "user_id": o["user_id"],
                    "start_time": o["start_time"].isoformat(),
                    "end_time": o["end_time"].isoformat(),
                    "reason": o["reason"]
                }
                for o in self.overrides
            ],
            "created_at": self.created_at.isoformat(),
            "updated_at": self.updated_at.isoformat()
        }
    
    @classmethod
    def from_dict(cls, data: Dict) -> 'OnCallSchedule':
        """Create from dictionary"""
        schedule = cls(
            name=data["name"],
            description=data["description"],
            team_id=data["team_id"],
            schedule_id=data["id"]
        )
        
        schedule.created_at = datetime.fromisoformat(data["created_at"])
        schedule.updated_at = datetime.fromisoformat(data["updated_at"])
        
        # Convert rotations
        schedule.rotations = []
        for r in data.get("rotations", []):
            rotation = {
                "id": r["id"],
                "start_time": datetime.fromisoformat(r["start_time"]),
                "end_time": datetime.fromisoformat(r["end_time"]),
                "users": r["users"],
                "repeat_type": ScheduleRepeatType[r["repeat_type"]],
                "repeat_interval": r["repeat_interval"]
            }
            schedule.rotations.append(rotation)
            
        # Convert overrides
        schedule.overrides = []
        for o in data.get("overrides", []):
            override = {
                "id": o["id"],
                "user_id": o["user_id"],
                "start_time": datetime.fromisoformat(o["start_time"]),
                "end_time": datetime.fromisoformat(o["end_time"]),
                "reason": o["reason"]
            }
            schedule.overrides.append(override)
            
        return schedule


class Team:
    """Represents a team that can be on-call"""
    
    def __init__(
        self,
        name: str,
        description: str = "",
        members: List[str] = None,
        services: List[str] = None,
        team_id: str = None
    ):
        self.id = team_id or str(uuid.uuid4())
        self.name = name
        self.description = description
        self.members = members or []
        self.services = services or []
        self.created_at = datetime.now()
        self.updated_at = datetime.now()
    
    def add_member(self, user_id: str) -> None:
        """Add a member to the team"""
        if user_id not in self.members:
            self.members.append(user_id)
            self.updated_at = datetime.now()
    
    def remove_member(self, user_id: str) -> bool:
        """Remove a member from the team"""
        if user_id in self.members:
            self.members.remove(user_id)
            self.updated_at = datetime.now()
            return True
        return False
    
    def add_service(self, service: str) -> None:
        """Add a service to the team"""
        if service not in self.services:
            self.services.append(service)
            self.updated_at = datetime.now()
    
    def remove_service(self, service: str) -> bool:
        """Remove a service from the team"""
        if service in self.services:
            self.services.remove(service)
            self.updated_at = datetime.now()
            return True
        return False
    
    def to_dict(self) -> Dict:
        """Convert to dictionary"""
        return {
            "id": self.id,
            "name": self.name,
            "description": self.description,
            "members": self.members,
            "services": self.services,
            "created_at": self.created_at.isoformat(),
            "updated_at": self.updated_at.isoformat()
        }
    
    @classmethod
    def from_dict(cls, data: Dict) -> 'Team':
        """Create from dictionary"""
        team = cls(
            name=data["name"],
            description=data["description"],
            members=data.get("members", []),
            services=data.get("services", []),
            team_id=data["id"]
        )
        
        team.created_at = datetime.fromisoformat(data["created_at"])
        team.updated_at = datetime.fromisoformat(data["updated_at"])
        
        return team


class OnCallManager:
    """Manages on-call schedules and rotations"""
    
    def __init__(
        self,
        operational_db: OperationalDB,
        notification_service: NotificationService = None
    ):
        self.operational_db = operational_db
        self.notification_service = notification_service
        
        # Cache of objects
        self.users = {}  # id -> OnCallUser
        self.teams = {}  # id -> Team
        self.schedules = {}  # id -> OnCallSchedule
        self.service_to_team = {}  # service -> team_id
        
        # Load data from database
        self._load_data()
    
    def _load_data(self) -> None:
        """Load data from database"""
        # Load users
        users_data = self.operational_db.get_all("oncall_users")
        for data in users_data:
            user = OnCallUser.from_dict(data)
            self.users[user.id] = user
            
        # Load teams
        teams_data = self.operational_db.get_all("oncall_teams")
        for data in teams_data:
            team = Team.from_dict(data)
            self.teams[team.id] = team
            
            # Update service to team mapping
            for service in team.services:
                self.service_to_team[service] = team.id
            
        # Load schedules
        schedules_data = self.operational_db.get_all("oncall_schedules")
        for data in schedules_data:
            schedule = OnCallSchedule.from_dict(data)
            self.schedules[schedule.id] = schedule
    
    def add_user(self, **kwargs) -> str:
        """Add a new on-call user"""
        user = OnCallUser(**kwargs)
        self.users[user.id] = user
        
        # Save to database
        self.operational_db.upsert("oncall_users", user.to_dict(), {"id": user.id})
        
        return user.id
    
    def update_user(self, user_id: str, **kwargs) -> bool:
        """Update an existing user"""
        if user_id not in self.users:
            return False
            
        user = self.users[user_id]
        
        # Update fields
        for key, value in kwargs.items():
            if hasattr(user, key):
                setattr(user, key, value)
                
        # Save to database
        self.operational_db.upsert("oncall_users", user.to_dict(), {"id": user.id})
        
        return True
    
    def get_user(self, user_id: str) -> Optional[OnCallUser]:
        """Get a user by ID"""
        return self.users.get(user_id)
    
    def get_user_by_username(self, username: str) -> Optional[OnCallUser]:
        """Get a user by username"""
        for user in self.users.values():
            if user.username == username:
                return user
        return None
    
    def get_user_contact_info(self, user_id: str) -> Optional[Dict]:
        """Get contact info for a user"""
        user = self.get_user(user_id)
        if not user:
            return None
            
        return {
            "name": user.full_name,
            "email": user.email,
            "phone": user.phone,
            "slack_id": user.slack_id
        }
    
    def add_team(self, **kwargs) -> str:
        """Add a new team"""
        team = Team(**kwargs)
        self.teams[team.id] = team
        
        # Update service to team mapping
        for service in team.services:
            self.service_to_team[service] = team.id
        
        # Save to database
        self.operational_db.upsert("oncall_teams", team.to_dict(), {"id": team.id})
        
        return team.id
    
    def update_team(self, team_id: str, **kwargs) -> bool:
        """Update an existing team"""
        if team_id not in self.teams:
            return False
            
        team = self.teams[team_id]
        
        # Update fields
        for key, value in kwargs.items():
            if hasattr(team, key):
                setattr(team, key, value)
                
        # Update service to team mapping
        for service in team.services:
            self.service_to_team[service] = team.id
                
        # Save to database
        self.operational_db.upsert("oncall_teams", team.to_dict(), {"id": team.id})
        
        return True
    
    def get_team(self, team_id: str) -> Optional[Team]:
        """Get a team by ID"""
        return self.teams.get(team_id)
    
    def get_team_by_name(self, name: str) -> Optional[Team]:
        """Get a team by name"""
        for team in self.teams.values():
            if team.name == name:
                return team
        return None
    
    def get_team_for_service(self, service: str) -> Optional[str]:
        """Get the team responsible for a service"""
        return self.service_to_team.get(service)
    
    def get_team_members(self, team_id: str) -> List[str]:
        """Get all members of a team"""
        team = self.get_team(team_id)
        if not team:
            return []
            
        return team.members
    
    def add_schedule(self, **kwargs) -> str:
        """Add a new on-call schedule"""
        schedule = OnCallSchedule(**kwargs)
        self.schedules[schedule.id] = schedule
        
        # Save to database
        self.operational_db.upsert("oncall_schedules", schedule.to_dict(), {"id": schedule.id})
        
        return schedule.id
    
    def update_schedule(self, schedule_id: str, **kwargs) -> bool:
        """Update an existing schedule"""
        if schedule_id not in self.schedules:
            return False
            
        schedule = self.schedules[schedule_id]
        
        # Update fields
        for key, value in kwargs.items():
            if hasattr(schedule, key):
                setattr(schedule, key, value)
                
        # Save to database
        self.operational_db.upsert("oncall_schedules", schedule.to_dict(), {"id": schedule.id})
        
        return True
    
    def get_schedule(self, schedule_id: str) -> Optional[OnCallSchedule]:
        """Get a schedule by ID"""
        return self.schedules.get(schedule_id)
    
    def get_team_schedule(self, team_id: str) -> Optional[OnCallSchedule]:
        """Get the schedule for a team"""
        for schedule in self.schedules.values():
            if schedule.team_id == team_id:
                return schedule
        return None
    
    def get_oncall_user_for_team(self, team_id: str, time: datetime = None) -> Optional[str]:
        """Get the user on call for a team at a specific time"""
        schedule = self.get_team_schedule(team_id)
        if not schedule:
            return None
            
        return schedule.get_oncall_user(time)
    
    def get_oncall_user_for_service(self, service: str, time: datetime = None) -> Optional[str]:
        """Get the user on call for a service at a specific time"""
        team_id = self.get_team_for_service(service)
        if not team_id:
            return None
            
        return self.get_oncall_user_for_team(team_id, time)
    
    def add_rotation_to_schedule(self, schedule_id: str, **kwargs) -> Optional[str]:
        """Add a rotation to a schedule"""
        schedule = self.get_schedule(schedule_id)
        if not schedule:
            return None
            
        rotation_id = schedule.add_rotation(**kwargs)
        
        # Save to database
        self.operational_db.upsert("oncall_schedules", schedule.to_dict(), {"id": schedule.id})
        
        return rotation_id
    
    def add_override_to_schedule(self, schedule_id: str, **kwargs) -> Optional[str]:
        """Add an override to a schedule"""
        schedule = self.get_schedule(schedule_id)
        if not schedule:
            return None
            
        override_id = schedule.add_override(**kwargs)
        
        # Save to database
        self.operational_db.upsert("oncall_schedules", schedule.to_dict(), {"id": schedule.id})
        
        # Notify the user of the override
        if self.notification_service:
            self._notify_override(schedule, kwargs["user_id"], kwargs["start_time"], kwargs["end_time"])
        
        return override_id
    
    def _notify_override(self, schedule: OnCallSchedule, user_id: str, start_time: datetime, end_time: datetime) -> None:
        """Notify a user of an override"""
        user = self.get_user(user_id)
        if not user or not self.notification_service:
            return
            
        team = self.get_team(schedule.team_id)
        team_name = team.name if team else "Unknown team"
        
        subject = f"On-Call Override: {team_name}"
        message = f"You have been assigned as the on-call engineer for {team_name} from {start_time.strftime('%Y-%m-%d %H:%M')} to {end_time.strftime('%Y-%m-%d %H:%M')}."
        
        # Send email notification
        try:
            self.notification_service.send_notification(
                channel="email",
                subject=subject,
                message=message,
                recipient={
                    "email": user.email,
                    "name": user.full_name
                }
            )
        except Exception as e:
            logger.error(f"Error sending override notification: {str(e)}")
    
    def get_current_oncall_users(self) -> Dict[str, str]:
        """Get all currently on-call users by team"""
        now = datetime.now()
        result = {}
        
        for team_id in self.teams.keys():
            user_id = self.get_oncall_user_for_team(team_id, now)
            if user_id:
                result[team_id] = user_id
                
        return result
    
    def get_user_oncall_schedule(self, user_id: str, start_date: datetime, end_date: datetime) -> List[Dict]:
        """Get all on-call periods for a user in a date range"""
        periods = []
        
        # Check each day in the range
        current_date = start_date
        while current_date <= end_date:
            for team_id, team in self.teams.items():
                oncall_user = self.get_oncall_user_for_team(team_id, current_date)
                if oncall_user == user_id:
                    # Found a day where the user is on call
                    # Determine the start and end of this continuous period
                    period_start = current_date
                    period_end = current_date
                    
                    # Move forward day by day until we find a day where the user is no longer on call
                    next_day = current_date + timedelta(days=1)
                    while next_day <= end_date:
                        if self.get_oncall_user_for_team(team_id, next_day) != user_id:
                            break
                        period_end = next_day
                        next_day += timedelta(days=1)
                    
                    # Add this period to the result
                    periods.append({
                        "team_id": team_id,
                        "team_name": team.name,
                        "start": period_start,
                        "end": period_end
                    })
                    
                    # Skip ahead to the end of this period
                    current_date = period_end
                    
            current_date += timedelta(days=1)
            
        return periods
    
    def send_shift_change_notifications(self) -> None:
        """Send notifications for upcoming shift changes"""
        now = datetime.now()
        tomorrow = now + timedelta(days=1)
        
        # Get current and next day's on-call users
        current_oncall = self.get_current_oncall_users()
        next_oncall = {}
        
        for team_id in self.teams.keys():
            next_oncall[team_id] = self.get_oncall_user_for_team(team_id, tomorrow)
        
        # Send notifications for changes
        for team_id, current_user in current_oncall.items():
            next_user = next_oncall.get(team_id)
            
            if next_user and next_user != current_user:
                team = self.get_team(team_id)
                team_name = team.name if team else "Unknown team"
                
                # Notify the user coming on call
                self._notify_upcoming_oncall(next_user, team_id, team_name)
                
                # Notify the user going off call
                self._notify_ending_oncall(current_user, team_id, team_name)
    
    def _notify_upcoming_oncall(self, user_id: str, team_id: str, team_name: str) -> None:
        """Notify a user of an upcoming on-call shift"""
        user = self.get_user(user_id)
        if not user or not self.notification_service:
            return
            
        tomorrow = datetime.now() + timedelta(days=1)
        
        subject = f"Upcoming On-Call Shift: {team_name}"
        message = f"You are scheduled to be on-call for {team_name} starting tomorrow ({tomorrow.strftime('%Y-%m-%d')})."
        
        # Send email notification
        try:
            self.notification_service.send_notification(
                channel="email",
                subject=subject,
                message=message,
                recipient={
                    "email": user.email,
                    "name": user.full_name
                }
            )
        except Exception as e:
            logger.error(f"Error sending upcoming on-call notification: {str(e)}")
    
    def _notify_ending_oncall(self, user_id: str, team_id: str, team_name: str) -> None:
        """Notify a user of an ending on-call shift"""
        user = self.get_user(user_id)
        if not user or not self.notification_service:
            return
            
        tomorrow = datetime.now() + timedelta(days=1)
        
        subject = f"Ending On-Call Shift: {team_name}"
        message = f"Your on-call shift for {team_name} is ending tomorrow ({tomorrow.strftime('%Y-%m-%d')})."
        
        # Send email notification
        try:
            self.notification_service.send_notification(
                channel="email",
                subject=subject,
                message=message,
                recipient={
                    "email": user.email,
                    "name": user.full_name
                }
            )
        except Exception as e:
            logger.error(f"Error sending ending on-call notification: {str(e)}")
        