# src/core/logging_setup.py
"""
Logging Configuration and Setup
Provides structured logging with multiple handlers and formatters
"""

import os
import sys
import logging
import logging.handlers
from pathlib import Path
from typing import Dict, Any, Optional
from datetime import datetime
import json


class StructuredFormatter(logging.Formatter):
    """Custom formatter for structured JSON logging"""
    
    def format(self, record):
        # Create structured log entry
        log_entry = {
            'timestamp': datetime.fromtimestamp(record.created).isoformat(),
            'level': record.levelname,
            'logger': record.name,
            'message': record.getMessage(),
            'module': record.module,
            'function': record.funcName,
            'line': record.lineno
        }
        
        # Add exception info if present
        if record.exc_info:
            log_entry['exception'] = self.formatException(record.exc_info)
            
        # Add extra fields if present
        if hasattr(record, 'extra_fields'):
            log_entry.update(record.extra_fields)
            
        return json.dumps(log_entry)


class TradingBotFormatter(logging.Formatter):
    """Human-readable formatter for trading bot logs"""
    
    def __init__(self):
        # Color codes for different log levels
        self.colors = {
            'DEBUG': '\033[36m',      # Cyan
            'INFO': '\033[32m',       # Green  
            'WARNING': '\033[33m',    # Yellow
            'ERROR': '\033[31m',      # Red
            'CRITICAL': '\033[35m',   # Magenta
            'RESET': '\033[0m'        # Reset
        }
        
        super().__init__(
            fmt='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
            datefmt='%Y-%m-%d %H:%M:%S'
        )
    
    def format(self, record):
        # Add color for console output
        if hasattr(record, 'use_color') and record.use_color:
            color = self.colors.get(record.levelname, '')
            reset = self.colors['RESET']
            record.levelname = f"{color}{record.levelname}{reset}"
            
        return super().format(record)


class TradingLogger:
    """Enhanced logger for trading bot with multiple handlers"""
    
    def __init__(self, name: str = 'trading_bot'):
        self.name = name
        self.logger = logging.getLogger(name)
        self.handlers_configured = False
        
    def setup_logging(self, config: Dict[str, Any]) -> None:
        """Setup logging based on configuration"""
        if self.handlers_configured:
            return
            
        # Clear any existing handlers
        self.logger.handlers.clear()
        
        # Set log level
        log_level = config.get('level', 'INFO').upper()
        self.logger.setLevel(getattr(logging, log_level))
        
        # Create log directory if needed
        log_file = config.get('file', 'logs/trading_bot.log')
        log_path = Path(log_file)
        log_path.parent.mkdir(parents=True, exist_ok=True)
        
        # Console handler
        if config.get('console', True):
            console_handler = logging.StreamHandler(sys.stdout)
            console_handler.setLevel(getattr(logging, log_level))
            
            # Use colored formatter for console
            console_formatter = TradingBotFormatter()
            console_handler.setFormatter(console_formatter)
            
            # Add filter to enable colors
            console_handler.addFilter(lambda record: setattr(record, 'use_color', True) or True)
            
            self.logger.addHandler(console_handler)
        
        # File handler with rotation
        if log_file:
            max_bytes = self._parse_size(config.get('max_size', '10MB'))
            backup_count = config.get('backup_count', 5)
            
            file_handler = logging.handlers.RotatingFileHandler(
                log_file,
                maxBytes=max_bytes,
                backupCount=backup_count,
                encoding='utf-8'
            )
            file_handler.setLevel(getattr(logging, log_level))
            
            # Use standard formatter for files
            file_formatter = TradingBotFormatter()
            file_handler.setFormatter(file_formatter)
            
            self.logger.addHandler(file_handler)
        
        # Error file handler
        error_file = config.get('error_file', 'logs/error.log')
        if error_file:
            error_path = Path(error_file)
            error_path.parent.mkdir(parents=True, exist_ok=True)
            
            error_handler = logging.handlers.RotatingFileHandler(
                error_file,
                maxBytes=self._parse_size('5MB'),
                backupCount=3,
                encoding='utf-8'
            )
            error_handler.setLevel(logging.ERROR)
            
            error_formatter = TradingBotFormatter()
            error_handler.setFormatter(error_formatter)
            
            self.logger.addHandler(error_handler)
        
        # Audit log handler (for trading activities)
        audit_file = config.get('audit_file', 'logs/audit.log')
        if audit_file:
            audit_path = Path(audit_file)
            audit_path.parent.mkdir(parents=True, exist_ok=True)
            
            audit_handler = logging.handlers.RotatingFileHandler(
                audit_file,
                maxBytes=self._parse_size('50MB'),
                backupCount=10,
                encoding='utf-8'
            )
            audit_handler.setLevel(logging.INFO)
            
            # Use structured formatter for audit logs
            audit_formatter = StructuredFormatter()
            audit_handler.setFormatter(audit_formatter)
            
            # Create separate audit logger
            audit_logger = logging.getLogger(f'{self.name}.audit')
            audit_logger.addHandler(audit_handler)
            audit_logger.setLevel(logging.INFO)
            audit_logger.propagate = False
        
        # JSON log handler (for structured logging)
        if config.get('json_file'):
            json_handler = logging.handlers.RotatingFileHandler(
                config['json_file'],
                maxBytes=self._parse_size('20MB'),
                backupCount=5,
                encoding='utf-8'
            )
            json_handler.setLevel(getattr(logging, log_level))
            
            json_formatter = StructuredFormatter()
            json_handler.setFormatter(json_formatter)
            
            self.logger.addHandler(json_handler)
        
        # Prevent propagation to root logger
        self.logger.propagate = False
        
        self.handlers_configured = True
        self.logger.info("Logging system initialized")
    
    def _parse_size(self, size_str: str) -> int:
        """Parse size string like '10MB' to bytes"""
        size_str = size_str.upper().strip()
        
        if size_str.endswith('KB'):
            return int(size_str[:-2]) * 1024
        elif size_str.endswith('MB'):
            return int(size_str[:-2]) * 1024 * 1024
        elif size_str.endswith('GB'):
            return int(size_str[:-2]) * 1024 * 1024 * 1024
        else:
            return int(size_str)
    
    def get_logger(self, name: Optional[str] = None) -> logging.Logger:
        """Get logger instance"""
        if name:
            return logging.getLogger(f'{self.name}.{name}')
        return self.logger
    
    def get_audit_logger(self) -> logging.Logger:
        """Get audit logger for trading activities"""
        return logging.getLogger(f'{self.name}.audit')


# Global logger instance
_trading_logger = TradingLogger()


def setup_logging(config: Dict[str, Any]) -> None:
    """Setup global logging configuration"""
    _trading_logger.setup_logging(config)


def get_logger(name: str = 'trading_bot') -> logging.Logger:
    """Get logger instance with optional name"""
    return _trading_logger.get_logger(name)


def get_audit_logger() -> logging.Logger:
    """Get audit logger for trading activities"""
    return _trading_logger.get_audit_logger()


def log_trade_action(action: str, symbol: str, **kwargs) -> None:
    """Log trading action to audit log"""
    audit_logger = get_audit_logger()
    
    # Create structured log entry for trading action
    log_data = {
        'action': action,
        'symbol': symbol,
        'timestamp': datetime.now().isoformat(),
        **kwargs
    }
    
    # Add extra fields to log record
    audit_logger.info(
        f"Trading action: {action} for {symbol}",
        extra={'extra_fields': log_data}
    )


def log_system_event(event: str, **kwargs) -> None:
    """Log system event"""
    logger = get_logger('system')
    
    log_data = {
        'event': event,
        'timestamp': datetime.now().isoformat(),
        **kwargs
    }
    
    logger.info(
        f"System event: {event}",
        extra={'extra_fields': log_data}
    )


def log_error_with_context(error: Exception, context: Dict[str, Any]) -> None:
    """Log error with additional context"""
    logger = get_logger('error')
    
    log_data = {
        'error_type': type(error).__name__,
        'error_message': str(error),
        'context': context,
        'timestamp': datetime.now().isoformat()
    }
    
    logger.error(
        f"Error occurred: {error}",
        extra={'extra_fields': log_data},
        exc_info=True
    )


class LoggerMixin:
    """Mixin to add logging capabilities to any class"""
    
    @property
    def logger(self) -> logging.Logger:
        """Get logger for this class"""
        class_name = self.__class__.__name__
        module_name = self.__class__.__module__.split('.')[-1]
        return get_logger(f'{module_name}.{class_name}')
    
    def log_trade(self, action: str, symbol: str, **kwargs) -> None:
        """Log trading action"""
        log_trade_action(action, symbol, **kwargs)
    
    def log_system(self, event: str, **kwargs) -> None:
        """Log system event"""
        log_system_event(event, **kwargs)
    
    def log_error(self, error: Exception, **context) -> None:
        """Log error with context"""
        log_error_with_context(error, context)


# Context managers for performance logging
class PerformanceLogger:
    """Context manager for logging performance metrics"""
    
    def __init__(self, operation: str, logger_name: str = 'performance'):
        self.operation = operation
        self.logger = get_logger(logger_name)
        self.start_time = None
        
    def __enter__(self):
        self.start_time = datetime.now()
        self.logger.debug(f"Starting {self.operation}")
        return self
        
    def __exit__(self, exc_type, exc_val, exc_tb):
        duration = (datetime.now() - self.start_time).total_seconds()
        
        if exc_type is None:
            self.logger.info(
                f"Completed {self.operation} in {duration:.3f}s",
                extra={'extra_fields': {
                    'operation': self.operation,
                    'duration_seconds': duration,
                    'status': 'success'
                }}
            )
        else:
            self.logger.error(
                f"Failed {self.operation} after {duration:.3f}s: {exc_val}",
                extra={'extra_fields': {
                    'operation': self.operation,
                    'duration_seconds': duration,
                    'status': 'error',
                    'error_type': exc_type.__name__,
                    'error_message': str(exc_val)
                }}
            )


def log_performance(operation: str):
    """Decorator for logging function performance"""
    def decorator(func):
        def wrapper(*args, **kwargs):
            with PerformanceLogger(f"{func.__module__}.{func.__name__}.{operation}"):
                return func(*args, **kwargs)
        return wrapper
    return decorator