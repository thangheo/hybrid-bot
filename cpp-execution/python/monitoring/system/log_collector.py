import asyncio
import logging
import time
import os
import re
import json
from typing import Dict, List, Any, Optional, Set, Pattern
from dataclasses import dataclass, field
import aiofiles
from datetime import datetime, timedelta

logger = logging.getLogger(__name__)

@dataclass
class LogEntry:
    """Data class to store individual log entries."""
    timestamp: float
    level: str
    source: str
    message: str
    details: Dict[str, Any] = field(default_factory=dict)
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert log entry to dictionary format."""
        return {
            'timestamp': self.timestamp,
            'datetime': datetime.fromtimestamp(self.timestamp).isoformat(),
            'level': self.level,
            'source': self.source,
            'message': self.message,
            'details': self.details
        }

@dataclass
class LogPattern:
    """Data class to define a log pattern for parsing."""
    name: str
    pattern: Pattern
    fields: List[str]
    level_map: Dict[str, str] = field(default_factory=dict)

class LogCollector:
    """
    Collects and processes log data from multiple sources.
    """
    
    # Log level mapping
    DEFAULT_LEVEL_MAP = {
        'CRITICAL': 'critical',
        'FATAL': 'critical',
        'ERROR': 'error',
        'WARNING': 'warning',
        'WARN': 'warning',
        'INFO': 'info',
        'DEBUG': 'debug',
        'NOTSET': 'debug'
    }
    
    def __init__(self, config=None):
        """
        Initialize the log collector.
        
        Args:
            config: Configuration dictionary for the log collector
        """
        self.config = config or {}
        self.log_sources = {}  # Maps source names to file paths or other source identifiers
        self.log_patterns = {}  # Maps source names to pattern objects for parsing
        self.log_buffer = []  # Recent log entries kept in memory
        self.max_buffer_size = self.config.get('max_buffer_size', 10000)
        self.listeners = []  # Callbacks for new log entries
        self.running = False
        self._collector_tasks = set()
        self.filter_patterns = {}  # Regex patterns to filter logs
    
    def register_file_source(self, source_name: str, file_path: str, pattern: LogPattern = None) -> None:
        """
        Register a log file as a source.
        
        Args:
            source_name: Name of the source
            file_path: Path to the log file
            pattern: LogPattern object for parsing the log format
        """
        if not os.path.exists(file_path):
            logger.warning(f"Log file does not exist: {file_path}")
        
        self.log_sources[source_name] = {
            'type': 'file',
            'path': file_path,
            'position': 0  # Start at beginning of file
        }
        
        if pattern:
            self.log_patterns[source_name] = pattern
        
        logger.info(f"Registered log file source: {source_name} -> {file_path}")
    
    def register_json_source(self, source_name: str, file_path: str, 
                           timestamp_field: str = 'timestamp', 
                           level_field: str = 'level',
                           message_field: str = 'message') -> None:
        """
        Register a JSON log file as a source.
        
        Args:
            source_name: Name of the source
            file_path: Path to the log file
            timestamp_field: Field name for the timestamp
            level_field: Field name for the log level
            message_field: Field name for the message
        """
        if not os.path.exists(file_path):
            logger.warning(f"Log file does not exist: {file_path}")
        
        self.log_sources[source_name] = {
            'type': 'json',
            'path': file_path,
            'position': 0,  # Start at beginning of file
            'timestamp_field': timestamp_field,
            'level_field': level_field,
            'message_field': message_field
        }
        
        logger.info(f"Registered JSON log source: {source_name} -> {file_path}")
    
    def register_socket_source(self, source_name: str, host: str, port: int, pattern: LogPattern = None) -> None:
        """
        Register a socket as a log source (e.g., syslog).
        
        Args:
            source_name: Name of the source
            host: Host address
            port: Port number
            pattern: LogPattern object for parsing the log format
        """
        self.log_sources[source_name] = {
            'type': 'socket',
            'host': host,
            'port': port
        }
        
        if pattern:
            self.log_patterns[source_name] = pattern
        
        logger.info(f"Registered socket log source: {source_name} -> {host}:{port}")
    
    def add_filter_pattern(self, name: str, pattern: str, include: bool = True) -> None:
        """
        Add a regex pattern to filter log entries.
        
        Args:
            name: Name of the filter
            pattern: Regex pattern
            include: If True, include matching entries; if False, exclude them
        """
        try:
            compiled_pattern = re.compile(pattern)
            self.filter_patterns[name] = {
                'pattern': compiled_pattern,
                'include': include
            }
            logger.info(f"Added log filter: {name} ({'include' if include else 'exclude'} {pattern})")
        except re.error as e:
            logger.error(f"Invalid regex pattern for filter {name}: {str(e)}")
    
    def add_listener(self, callback) -> None:
        """
        Add a listener callback for new log entries.
        
        Args:
            callback: Function called with each new LogEntry
        """
        self.listeners.append(callback)
    
    async def start(self) -> None:
        """Start log collection from all sources."""
        if self.running:
            logger.warning("Log collector already running")
            return
        
        self.running = True
        
        # Start a task for each source
        for source_name, source_info in self.log_sources.items():
            if source_info['type'] == 'file' or source_info['type'] == 'json':
                task = asyncio.create_task(self._collect_from_file(source_name, source_info))
                self._collector_tasks.add(task)
                task.add_done_callback(self._collector_tasks.discard)
            elif source_info['type'] == 'socket':
                task = asyncio.create_task(self._collect_from_socket(source_name, source_info))
                self._collector_tasks.add(task)
                task.add_done_callback(self._collector_tasks.discard)
        
        logger.info("Log collector started")
    
    async def stop(self) -> None:
        """Stop log collection."""
        if not self.running:
            logger.warning("Log collector not running")
            return
        
        self.running = False
        
        # Cancel all collector tasks
        for task in self._collector_tasks:
            task.cancel()
        
        # Wait for all tasks to complete
        if self._collector_tasks:
            await asyncio.gather(*self._collector_tasks, return_exceptions=True)
            self._collector_tasks.clear()
        
        logger.info("Log collector stopped")
    
    async def _collect_from_file(self, source_name: str, source_info: Dict[str, Any]) -> None:
        """
        Collect logs from a file source.
        
        Args:
            source_name: Name of the source
            source_info: Source configuration
        """
        file_path = source_info['path']
        position = source_info['position']
        
        while self.running:
            try:
                # If file doesn't exist or is not readable, wait and retry
                if not os.path.exists(file_path) or not os.access(file_path, os.R_OK):
                    await asyncio.sleep(5)
                    continue
                
                # Open the file and seek to the last position
                async with aiofiles.open(file_path, 'r') as f:
                    await f.seek(position)
                    
                    while self.running:
                        line = await f.readline()
                        
                        if not line:  # EOF
                            # Get the current position for next time
                            position = await f.tell()
                            source_info['position'] = position
                            await asyncio.sleep(1)  # Wait for more data
                            break
                        
                        # Parse the line according to the source type
                        if source_info['type'] == 'json':
                            try:
                                entry = json.loads(line)
                                log_entry = self._parse_json_log(source_name, entry, source_info)
                                if log_entry:
                                    self._add_log_entry(log_entry)
                            except json.JSONDecodeError:
                                logger.warning(f"Invalid JSON in log file {file_path}: {line.strip()}")
                        else:  # Regular text log
                            log_entry = self._parse_log_line(source_name, line.strip())
                            if log_entry:
                                self._add_log_entry(log_entry)
            
            except asyncio.CancelledError:
                logger.info(f"Log collection from {source_name} cancelled")
                break
            except Exception as e:
                logger.error(f"Error collecting logs from {source_name}: {str(e)}", exc_info=True)
                await asyncio.sleep(5)  # Wait before retrying
    
    async def _collect_from_socket(self, source_name: str, source_info: Dict[str, Any]) -> None:
        """
        Collect logs from a socket source.
        
        Args:
            source_name: Name of the source
            source_info: Source configuration
        """
        host = source_info['host']
        port = source_info['port']
        
        while self.running:
            try:
                reader, writer = await asyncio.open_connection(host, port)
                logger.info(f"Connected to log socket {host}:{port}")
                
                while self.running:
                    data = await reader.readline()
                    if not data:  # Connection closed
                        break
                    
                    line = data.decode('utf-8', errors='replace').strip()
                    log_entry = self._parse_log_line(source_name, line)
                    if log_entry:
                        self._add_log_entry(log_entry)
                
                writer.close()
                await writer.wait_closed()
                
            except asyncio.CancelledError:
                logger.info(f"Log collection from {source_name} cancelled")
                break
            except Exception as e:
                logger.error(f"Error collecting logs from {source_name}: {str(e)}", exc_info=True)
                await asyncio.sleep(5)  # Wait before retrying
    
    def _parse_log_line(self, source_name: str, line: str) -> Optional[LogEntry]:
        """
        Parse a log line using the source's pattern.
        
        Args:
            source_name: Name of the source
            line: Log line to parse
            
        Returns:
            LogEntry object or None if parsing failed
        """
        # Skip empty lines
        if not line:
            return None
        
        # Apply filters
        if not self._apply_filters(line):
            return None
        
        # Get the pattern for this source
        pattern = self.log_patterns.get(source_name)
        
        if pattern:
            # Try to parse with the pattern
            match = pattern.pattern.match(line)
            if match:
                groups = match.groupdict()
                
                # Extract fields according to the pattern
                timestamp = self._parse_timestamp(groups.get('timestamp', ''))
                level = groups.get('level', 'INFO')
                message = groups.get('message', line)
                
                # Map the level to a standard format
                level = pattern.level_map.get(level, level)
                level = self.DEFAULT_LEVEL_MAP.get(level.upper(), level.lower())
                
                # Extract any additional fields into details
                details = {k: v for k, v in groups.items() 
                          if k not in ['timestamp', 'level', 'message'] and v is not None}
                
                return LogEntry(timestamp, level, source_name, message, details)
        
        # If we don't have a pattern or it didn't match, use a basic approach
        return LogEntry(
            timestamp=time.time(),
            level='info',
            source=source_name,
            message=line,
            details={}
        )
    
    def _parse_json_log(self, source_name: str, entry: Dict[str, Any], 
                       source_info: Dict[str, Any]) -> Optional[LogEntry]:
        """
        Parse a JSON log entry.
        
        Args:
            source_name: Name of the source
            entry: Parsed JSON log entry
            source_info: Source configuration
            
        Returns:
            LogEntry object or None if parsing failed
        """
        # Apply filters to the JSON string
        if not self._apply_filters(json.dumps(entry)):
            return None
        
        # Extract fields using the configuration
        timestamp_field = source_info.get('timestamp_field', 'timestamp')
        level_field = source_info.get('level_field', 'level')
        message_field = source_info.get('message_field', 'message')
        
        # Get timestamp, defaulting to current time
        timestamp = entry.get(timestamp_field, time.time())
        if isinstance(timestamp, str):
            timestamp = self._parse_timestamp(timestamp)
        
        # Get level, defaulting to INFO
        level = entry.get(level_field, 'INFO')
        level = self.DEFAULT_LEVEL_MAP.get(level.upper(), level.lower())
        
        # Get message
        message = entry.get(message_field, str(entry))
        
        # Extract details (all fields except the main ones)
        details = {k: v for k, v in entry.items() 
                  if k not in [timestamp_field, level_field, message_field]}
        
        return LogEntry(timestamp, level, source_name, message, details)
    
    def _parse_timestamp(self, timestamp_str: str) -> float:
        """
        Parse a timestamp string into a Unix timestamp.
        
        Args:
            timestamp_str: String representation of a timestamp
            
        Returns:
            Unix timestamp (seconds since epoch)
        """
        if not timestamp_str:
            return time.time()
        
        try:
            # Try parsing as ISO format
            dt = datetime.fromisoformat(timestamp_str.replace('Z', '+00:00'))
            return dt.timestamp()
        except ValueError:
            try:
                # Try common formats
                formats = [
                    '%Y-%m-%d %H:%M:%S,%f',  # 2023-05-07 12:34:56,789
                    '%Y-%m-%d %H:%M:%S.%f',  # 2023-05-07 12:34:56.789
                    '%Y/%m/%d %H:%M:%S',     # 2023/05/07 12:34:56
                    '%d/%b/%Y:%H:%M:%S',     # 07/May/2023:12:34:56
                    '%b %d %H:%M:%S',        # May 07 12:34:56
                ]
                
                for fmt in formats:
                    try:
                        dt = datetime.strptime(timestamp_str, fmt)
                        # If year is missing, assume current year
                        if dt.year == 1900:
                            dt = dt.replace(year=datetime.now().year)
                        return dt.timestamp()
                    except ValueError:
                        continue
                
                # If all formats failed, try to extract Unix timestamp
                if timestamp_str.isdigit():
                    # Assume milliseconds if length > 11 (as of 2023)
                    if len(timestamp_str) > 11:
                        return float(timestamp_str) / 1000
                    else:
                        return float(timestamp_str)
                
                # Default to current time if all parsing fails
                return time.time()
            except Exception:
                return time.time()
    
    def _apply_filters(self, text: str) -> bool:
        """
        Apply filter patterns to determine if a log entry should be included.
        
        Args:
            text: Text to filter
            
        Returns:
            True if the entry should be included, False otherwise
        """
        # If no filters, include everything
        if not self.filter_patterns:
            return True
        
        # First, check exclude filters (if any match, exclude the entry)
        for name, filter_info in self.filter_patterns.items():
            if not filter_info['include'] and filter_info['pattern'].search(text):
                return False
        
        # Then, check include filters (if any exist, at least one must match)
        include_filters = [f for f in self.filter_patterns.values() if f['include']]
        if include_filters:
            return any(f['pattern'].search(text) for f in include_filters)
        
        # If no include filters, include by default
        return True
    
    def _add_log_entry(self, entry: LogEntry) -> None:
        """
        Add a log entry to the buffer and notify listeners.
        
        Args:
            entry: LogEntry to add
        """
        # Add to buffer, removing oldest if full
        self.log_buffer.append(entry)
        if len(self.log_buffer) > self.max_buffer_size:
            self.log_buffer.pop(0)
        
        # Notify listeners
        for callback in self.listeners:
            try:
                if asyncio.iscoroutinefunction(callback):
                    # Schedule the coroutine but don't wait for it
                    asyncio.create_task(callback(entry))
                else:
                    callback(entry)
            except Exception as e:
                logger.error(f"Error in log entry listener: {str(e)}", exc_info=True)
    
    def get_logs(self, sources: Optional[List[str]] = None, 
               levels: Optional[List[str]] = None,
               start_time: Optional[float] = None,
               end_time: Optional[float] = None,
               search_text: Optional[str] = None,
               limit: int = 100) -> List[Dict[str, Any]]:
        """
        Get logs matching the specified criteria.
        
        Args:
            sources: Optional list of source names to include
            levels: Optional list of log levels to include
            start_time: Optional start time (Unix timestamp)
            end_time: Optional end time (Unix timestamp)
            search_text: Optional text to search for
            limit: Maximum number of entries to return
            
        Returns:
            List of log entries as dictionaries
        """
        # Default to all sources if not specified
        if sources is None:
            sources = list(self.log_sources.keys())
        
        # Convert levels to lowercase for consistent comparison
        if levels is not None:
            levels = [level.lower() for level in levels]
        
        # Compile search regex if provided
        search_pattern = None
        if search_text:
            try:
                search_pattern = re.compile(search_text, re.IGNORECASE)
            except re.error:
                # If invalid regex, use literal text search
                search_text = re.escape(search_text)
                search_pattern = re.compile(search_text, re.IGNORECASE)
        
        # Filter logs
        filtered_logs = []
        for entry in reversed(self.log_buffer):  # Start with most recent
            # Filter by source
            if entry.source not in sources:
                continue
            
            # Filter by level
            if levels is not None and entry.level.lower() not in levels:
                continue
            
            # Filter by time range
            if start_time is not None and entry.timestamp < start_time:
                continue
            if end_time is not None and entry.timestamp > end_time:
                continue
            
            # Filter by search text
            if search_pattern:
                # Search in message and serialized details
                message_match = search_pattern.search(entry.message)
                details_match = search_pattern.search(json.dumps(entry.details))
                if not message_match and not details_match:
                    continue
            
            # Add matching entry to results
            filtered_logs.append(entry.to_dict())
            
            # Stop if we've reached the limit
            if len(filtered_logs) >= limit:
                break
        
        return filtered_logs
    
    def clear_logs(self, older_than: Optional[float] = None) -> int:
        """
        Clear logs from the buffer.
        
        Args:
            older_than: Optional timestamp to only clear logs older than this
            
        Returns:
            Number of entries cleared
        """
        if older_than is None:
            # Clear all logs
            count = len(self.log_buffer)
            self.log_buffer.clear()
            return count
        else:
            # Clear only older logs
            original_count = len(self.log_buffer)
            self.log_buffer = [entry for entry in self.log_buffer if entry.timestamp >= older_than]
            return original_count - len(self.log_buffer)
    
    def collect_metrics(self) -> Dict[str, Any]:
        """
        Collect metrics about log collection.
        
        Returns:
            Dictionary of log metrics
        """
        level_counts = {}
        source_counts = {}
        recent_counts = 0
        
        # Count last hour
        one_hour_ago = time.time() - 3600
        
        for entry in self.log_buffer:
            # Count by level
            level = entry.level.lower()
            level_counts[level] = level_counts.get(level, 0) + 1
            
            # Count by source
            source_counts[entry.source] = source_counts.get(entry.source, 0) + 1
            
            # Count recent
            if entry.timestamp >= one_hour_ago:
                recent_counts += 1
        
        return {
            'timestamp': time.time(),
            'total_entries': len(self.log_buffer),
            'recent_entries': recent_counts,
            'level_counts': level_counts,
            'source_counts': source_counts
        }