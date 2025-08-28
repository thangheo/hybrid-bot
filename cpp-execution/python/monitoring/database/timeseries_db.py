# monitoring/database/timeseries_db.py

import logging
from datetime import datetime, timedelta
from influxdb import InfluxDBClient
from ..config.settings import settings

logger = logging.getLogger(__name__)

class TimeSeriesDB:
    """Interface for time-series database operations."""
    
    def __init__(self):
        self._client = None
        self._connected = False
        
    def connect(self):
        """Connect to the InfluxDB server."""
        try:
            host = settings.get("database.timeseries.host", "localhost")
            port = settings.get("database.timeseries.port", 8086)
            username = settings.get("database.timeseries.username", "")
            password = settings.get("database.timeseries.password", "")
            database = settings.get("database.timeseries.database", "monitoring")
            
            self._client = InfluxDBClient(
                host=host,
                port=port,
                username=username,
                password=password
            )
            
            # Create database if it doesn't exist
            dbs = self._client.get_list_database()
            if database not in [db['name'] for db in dbs]:
                logger.info(f"Creating database {database}")
                self._client.create_database(database)
                
            self._client.switch_database(database)
            self._connected = True
            logger.info(f"Connected to InfluxDB at {host}:{port}")
            return True
        except Exception as e:
            logger.error(f"Error connecting to InfluxDB: {e}")
            self._connected = False
            return False
            
    def write_points(self, measurement, fields, tags=None, timestamp=None):
        """Write data points to the time-series database."""
        if not self._connected and not self.connect():
            logger.error("Failed to write points - not connected")
            return False
            
        try:
            point = {
                "measurement": measurement,
                "tags": tags or {},
                "fields": fields,
            }
            
            if timestamp:
                if isinstance(timestamp, datetime):
                    point["time"] = timestamp.isoformat()
                else:
                    point["time"] = timestamp
                    
            self._client.write_points([point])
            return True
        except Exception as e:
            logger.error(f"Error writing to InfluxDB: {e}")
            return False
            
    def query(self, query):
        """Execute a query against the time-series database."""
        if not self._connected and not self.connect():
            logger.error("Failed to query - not connected")
            return None
            
        try:
            return self._client.query(query)
        except Exception as e:
            logger.error(f"Error querying InfluxDB: {e}")
            return None
            
    def get_metrics(self, measurement, fields=None, tags=None, 
                   start_time=None, end_time=None, group_by=None):
        """Query metrics with specific filters."""
        if not self._connected and not self.connect():
            logger.error("Failed to get metrics - not connected")
            return None
            
        try:
            # Build query
            field_list = "*"
            if fields:
                field_list = ", ".join(fields)
                
            query = f"SELECT {field_list} FROM {measurement}"
            
            # Add tag filters
            conditions = []
            if tags:
                for tag, value in tags.items():
                    conditions.append(f'"{tag}" = \'{value}\'')
                    
            # Add time range
            if start_time:
                if isinstance(start_time, datetime):
                    start_str = start_time.isoformat()
                else:
                    start_str = start_time
                conditions.append(f'time >= \'{start_str}\'')
                
            if end_time:
                if isinstance(end_time, datetime):
                    end_str = end_time.isoformat()
                else:
                    end_str = end_time
                conditions.append(f'time <= \'{end_str}\'')
                
            if conditions:
                query += " WHERE " + " AND ".join(conditions)
                
            # Add group by
            if group_by:
                query += f" GROUP BY {group_by}"
                
            return self._client.query(query)
        except Exception as e:
            logger.error(f"Error getting metrics from InfluxDB: {e}")
            return None
            
    def close(self):
        """Close the database connection."""
        if self._client:
            self._client.close()
            self._connected = False
            logger.info("Closed connection to InfluxDB")

# Create singleton instance
timeseries_db = TimeSeriesDB()