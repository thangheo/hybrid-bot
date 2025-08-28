# monitoring/database/operational_db.py

import logging
import json
import os
from datetime import datetime
import psycopg2
from psycopg2.extras import DictCursor
from ..config.settings import settings

logger = logging.getLogger(__name__)

class OperationalDB:
    """Interface for operational database operations."""
    
    def __init__(self):
        self._conn = None
        self._connected = False
        
    def connect(self):
        """Connect to the PostgreSQL database."""
        try:
            host = settings.get("database.operational.host", "localhost")
            port = settings.get("database.operational.port", 5432)
            dbname = settings.get("database.operational.dbname", "monitoring")
            user = settings.get("database.operational.user", "postgres")
            password = settings.get("database.operational.password", "postgres")
            
            self._conn = psycopg2.connect(
                host=host,
                port=port,
                dbname=dbname,
                user=user,
                password=password
            )
            
            self._connected = True
            logger.info(f"Connected to PostgreSQL at {host}:{port}")
            return True
        except Exception as e:
            logger.error(f"Error connecting to PostgreSQL: {e}")
            self._connected = False
            return False
            
    def execute(self, query, params=None):
        """Execute a query against the database."""
        if not self._connected and not self.connect():
            logger.error("Failed to execute query - not connected")
            return None
            
        try:
            with self._conn.cursor() as cur:
                cur.execute(query, params or ())
                self._conn.commit()
            return True
        except Exception as e:
            logger.error(f"Error executing query: {e}")
            self._conn.rollback()
            return False
            
    def fetch_one(self, query, params=None):
        """Execute a query and fetch one result."""
        if not self._connected and not self.connect():
            logger.error("Failed to fetch_one - not connected")
            return None
            
        try:
            with self._conn.cursor(cursor_factory=DictCursor) as cur:
                cur.execute(query, params or ())
                return cur.fetchone()
        except Exception as e:
            logger.error(f"Error fetching from database: {e}")
            return None
            
    def fetch_all(self, query, params=None):
        """Execute a query and fetch all results."""
        if not self._connected and not self.connect():
            logger.error("Failed to fetch_all - not connected")
            return None
            
        try:
            with self._conn.cursor(cursor_factory=DictCursor) as cur:
                cur.execute(query, params or ())
                return cur.fetchall()
        except Exception as e:
            logger.error(f"Error fetching from database: {e}")
            return None
            
    # Alert-specific methods
    def save_alert(self, alert):
        """Save an alert to the database."""
        query = """
            INSERT INTO alerts (
                alert_id, name, severity, source, description, state,
                created_at, updated_at, acknowledged_at, resolved_at,
                assigned_to, tags, metrics
            ) VALUES (
                %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s
            )
            ON CONFLICT (alert_id) DO UPDATE
            SET name = EXCLUDED.name,
                severity = EXCLUDED.severity,
                source = EXCLUDED.source,
                description = EXCLUDED.description,
                state = EXCLUDED.state,
                updated_at = EXCLUDED.updated_at,
                acknowledged_at = EXCLUDED.acknowledged_at,
                resolved_at = EXCLUDED.resolved_at,
                assigned_to = EXCLUDED.assigned_to,
                tags = EXCLUDED.tags,
                metrics = EXCLUDED.metrics
        """
        
        params = (
            alert.alert_id,
            alert.name,
            alert.severity,
            alert.source,
            alert.description,
            alert.state,
            alert.created_at,
            alert.updated_at,
            alert.acknowledged_at,
            alert.resolved_at,
            alert.assigned_to,
            json.dumps(alert.tags),
            json.dumps(alert.metrics)
        )
        
        return self.execute(query, params)
        
    def get_alert(self, alert_id):
        """Get an alert by ID."""
        query = "SELECT * FROM alerts WHERE alert_id = %s"
        result = self.fetch_one(query, (alert_id,))
        
        if not result:
            return None
            
        # Convert JSON fields
        result['tags'] = json.loads(result['tags']) if result['tags'] else {}
        result['metrics'] = json.loads(result['metrics']) if result['metrics'] else {}
        
        from ..database.models import Alert
        return Alert.from_dict(dict(result))
        
    def get_alerts(self, state=None, severity=None, limit=100):
        """Get alerts with optional filters."""
        query = "SELECT * FROM alerts"
        params = []
        conditions = []
        
        if state:
            conditions.append("state = %s")
            params.append(state)
            
        if severity:
            conditions.append("severity = %s")
            params.append(severity)
            
        if conditions:
            query += " WHERE " + " AND ".join(conditions)
            
        query += " ORDER BY created_at DESC LIMIT %s"
        params.append(limit)
        
        results = self.fetch_all(query, params)
        
        if not results:
            return []
            
        from ..database.models import Alert
        alerts = []
        for result in results:
            # Convert JSON fields
            result['tags'] = json.loads(result['tags']) if result['tags'] else {}
            result['metrics'] = json.loads(result['metrics']) if result['metrics'] else {}
            alerts.append(Alert.from_dict(dict(result)))
            
        return alerts
        
    # Incident-specific methods
    def save_incident(self, incident):
        """Save an incident to the database."""
        query = """
            INSERT INTO incidents (
                incident_id, title, description, state,
                created_at, updated_at, resolved_at, closed_at,
                severity, owner, alerts, tags, timeline
            ) VALUES (
                %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s
            )
            ON CONFLICT (incident_id) DO UPDATE
            SET title = EXCLUDED.title,
                description = EXCLUDED.description,
                state = EXCLUDED.state,
                updated_at = EXCLUDED.updated_at,
                resolved_at = EXCLUDED.resolved_at,
                closed_at = EXCLUDED.closed_at,
                severity = EXCLUDED.severity,
                owner = EXCLUDED.owner,
                alerts = EXCLUDED.alerts,
                tags = EXCLUDED.tags,
                timeline = EXCLUDED.timeline
        """
        
        # Convert timeline timestamps to strings for JSON serialization
        timeline = []
        for event in incident.timeline:
            event_copy = event.copy()
            if isinstance(event_copy['timestamp'], datetime):
                event_copy['timestamp'] = event_copy['timestamp'].isoformat()
            timeline.append(event_copy)
        
        params = (
            incident.incident_id,
            incident.title,
            incident.description,
            incident.state,
            incident.created_at,
            incident.updated_at,
            incident.resolved_at,
            incident.closed_at,
            incident.severity,
            incident.owner,
            json.dumps(incident.alerts),
            json.dumps(incident.tags),
            json.dumps(timeline)
        )
        
        return self.execute(query, params)
    
    def close(self):
        """Close the database connection."""
        if self._conn:
            self._conn.close()
            self._connected = False
            logger.info("Closed connection to PostgreSQL")

# Create singleton instance
operational_db = OperationalDB()