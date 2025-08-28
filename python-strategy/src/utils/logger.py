"""
Enhanced Logging Utility for Trading Bot
Handles file logging, console logging, and nohup detection
"""

import logging
import logging.config
import sys
import os
from pathlib import Path
from typing import Dict, Optional
import yaml

def setup_global_logging(
    log_level: str = "INFO",
    log_file: str = "logs/trading_bot.log",
    config_path: Optional[str] = None
) -> logging.Logger:
    """
    Setup global logging configuration
    
    Args:
        log_level: Logging level (DEBUG, INFO, WARNING, ERROR)
        log_file: Path to log file
        config_path: Optional path to logging config file
        
    Returns:
        Configured logger instance
    """
    
    # Create logs directory
    os.makedirs(os.path.dirname(log_file), exist_ok=True)
    
    # Detect if running with nohup/background
    is_background = not sys.stdout.isatty()
    
    # Load config from file if provided
    if config_path and os.path.exists(config_path):
        logging_config = load_logging_config(config_path)
        
        # Remove console handlers if running in background
        if is_background:
            logging_config = remove_console_handlers(logging_config)
        
        logging.config.dictConfig(logging_config)
        
    else:
        # Use default configuration
        setup_default_logging(log_level, log_file, is_background)
    
    # Get main logger
    logger = logging.getLogger(__name__)
    
    # Log setup completion
    mode = "background (nohup)" if is_background else "interactive"
    logger.info(f"🚀 Global logging setup complete - Mode: {mode}")
    logger.info(f"📄 Log file: {os.path.abspath(log_file)}")
    
    return logger

def setup_default_logging(log_level: str, log_file: str, is_background: bool):
    """Setup default logging configuration"""
    
    # Create formatters
    detailed_formatter = logging.Formatter(
        '%(asctime)s - %(name)s - %(levelname)s - [%(funcName)s:%(lineno)d] - %(message)s'
    )
    
    simple_formatter = logging.Formatter(
        '%(asctime)s - %(levelname)s - %(message)s'
    )
    
    # Create handlers
    handlers = []
    
    # File handler (always present)
    file_handler = logging.FileHandler(log_file, mode='a', encoding='utf-8')
    file_handler.setLevel(logging.DEBUG)
    file_handler.setFormatter(detailed_formatter)
    handlers.append(file_handler)
    
    # Console handler (only if not background)
    if not is_background:
        console_handler = logging.StreamHandler(sys.stdout)
        console_handler.setLevel(getattr(logging, log_level))
        console_handler.setFormatter(detailed_formatter)
        handlers.append(console_handler)
    
    # Configure root logger
    logging.basicConfig(
        level=logging.DEBUG,  # Set to DEBUG for file, handlers control actual level
        handlers=handlers,
        force=True
    )
    
    # Set component-specific levels
    set_component_log_levels()

# def set_component_log_levels():
#     """Set specific log levels for different components"""
#     component_levels = {
#         'apscheduler': logging.WARNING,
#         'asyncio': logging.WARNING,
#         'aiohttp': logging.WARNING,
#         'urllib3': logging.WARNING,
#         'src.data.etf_flow_manager': logging.DEBUG,
#         'src.strategy.unified_manager': logging.INFO,
#         'src.strategy.strategies': logging.INFO,
#         'src.data.manager': logging.INFO,
#         'src.risk.manager': logging.INFO,
#         'src.execution.order_manager': logging.INFO,
#         'src.portfolio.manager': logging.INFO,
#         'src.monitoring': logging.INFO
#     }
    
#     for component, level in component_levels.items():
#         logging.getLogger(component).setLevel(level)
def set_component_log_levels():
    """Set specific log levels for different components"""
    component_levels = {
        'apscheduler': logging.WARNING,
        'asyncio': logging.WARNING,
        'aiohttp': logging.WARNING,
        'urllib3': logging.WARNING,
        
        # 🔧 FIX: External trading libraries
        'ccxt': logging.WARNING,
        'ccxt.base': logging.ERROR,
        'ccxt.binance': logging.ERROR,  
        'ccxt.exchange': logging.ERROR,
        'requests': logging.WARNING,
        'requests.packages.urllib3': logging.WARNING,
        
        # Your app components
        'src.data.etf_flow_manager': logging.DEBUG,
        'src.strategy.unified_manager': logging.INFO,
        'src.strategy.strategies': logging.INFO,
        'src.data.manager': logging.INFO,
        'src.risk.manager': logging.INFO,
        'src.execution.order_manager': logging.INFO,  # 🔧 Có thể đổi thành WARNING
        'src.portfolio.manager': logging.INFO,
        'src.monitoring': logging.INFO
    }
    
    for component, level in component_levels.items():
        logging.getLogger(component).setLevel(level)
def load_logging_config(config_path: str) -> Dict:
    """Load logging configuration from YAML file"""
    try:
        with open(config_path, 'r', encoding='utf-8') as f:
            config = yaml.safe_load(f)
        
        # Return logging section if exists, otherwise default
        if 'logging' in config:
            return config['logging']
        else:
            return get_default_config_dict()
            
    except Exception as e:
        print(f"⚠️ Failed to load logging config from {config_path}: {e}")
        return get_default_config_dict()

def remove_console_handlers(config: Dict) -> Dict:
    """Remove console handlers from logging config for background mode"""
    if 'handlers' in config:
        # Remove console handler from handlers list
        config['handlers'] = {
            k: v for k, v in config['handlers'].items() 
            if v.get('class') != 'logging.StreamHandler'
        }
        
        # Remove console handler references from loggers
        for logger_config in config.get('loggers', {}).values():
            if 'handlers' in logger_config:
                logger_config['handlers'] = [
                    h for h in logger_config['handlers'] if h != 'console'
                ]
        
        # Remove from root logger
        if 'root' in config and 'handlers' in config['root']:
            config['root']['handlers'] = [
                h for h in config['root']['handlers'] if h != 'console'
            ]
    
    return config

def get_default_config_dict() -> Dict:
    """Get default logging configuration as dictionary"""
    return {
        'version': 1,
        'disable_existing_loggers': False,
        
        'formatters': {
            'detailed': {
                'format': '%(asctime)s - %(name)s - %(levelname)s - [%(funcName)s:%(lineno)d] - %(message)s'
            },
            'simple': {
                'format': '%(asctime)s - %(levelname)s - %(message)s'
            }
        },
        
        'handlers': {
            'file': {
                'class': 'logging.FileHandler',
                'filename': 'logs/trading_bot.log',
                'formatter': 'detailed',
                'level': 'DEBUG',
                'mode': 'a',
                'encoding': 'utf-8'
            },
            'console': {
                'class': 'logging.StreamHandler',
                'stream': 'ext://sys.stdout',
                'formatter': 'detailed',
                'level': 'INFO'
            }
        },
        
        'loggers': {
            'src.data.etf_flow_manager': {
                'level': 'DEBUG',
                'handlers': ['file', 'console'],
                'propagate': False
            },
            'src.strategy.unified_manager': {
                'level': 'INFO', 
                'handlers': ['file', 'console'],
                'propagate': False
            },
            'apscheduler': {
                'level': 'WARNING',
                'handlers': ['file'],
                'propagate': False
            },
            'asyncio': {
                'level': 'WARNING',
                'handlers': ['file'],
                'propagate': False
            }
        },
        
        'root': {
            'level': 'INFO',
            'handlers': ['file', 'console']
        }
    }

def get_logger(name: str) -> logging.Logger:
    """Get logger for specific module"""
    return logging.getLogger(name)

# 🚀 CONVENIENCE FUNCTIONS
def get_main_logger() -> logging.Logger:
    """Get main application logger"""
    return logging.getLogger('main')

def get_strategy_logger() -> logging.Logger:
    """Get strategy-specific logger"""
    return logging.getLogger('strategy')

def get_data_logger() -> logging.Logger:
    """Get data management logger"""
    return logging.getLogger('data')

def get_execution_logger() -> logging.Logger:
    """Get execution logger"""
    return logging.getLogger('execution')

# 🎯 LOGGING DECORATORS
def log_function_call(logger: logging.Logger = None):
    """Decorator to log function calls"""
    def decorator(func):
        def wrapper(*args, **kwargs):
            func_logger = logger or logging.getLogger(func.__module__)
            func_logger.debug(f"📞 Calling {func.__name__}")
            try:
                result = func(*args, **kwargs)
                func_logger.debug(f"✅ {func.__name__} completed")
                return result
            except Exception as e:
                func_logger.error(f"❌ {func.__name__} failed: {e}")
                raise
        return wrapper
    return decorator

def log_execution_time(logger: logging.Logger = None):
    """Decorator to log function execution time"""
    import time
    
    def decorator(func):
        def wrapper(*args, **kwargs):
            func_logger = logger or logging.getLogger(func.__module__)
            start_time = time.time()
            
            try:
                result = func(*args, **kwargs)
                execution_time = time.time() - start_time
                func_logger.debug(f"⏱️  {func.__name__} took {execution_time:.3f}s")
                return result
            except Exception as e:
                execution_time = time.time() - start_time
                func_logger.error(f"❌ {func.__name__} failed after {execution_time:.3f}s: {e}")
                raise
        return wrapper
    return decorator

# 📊 LOG ANALYSIS HELPERS
def get_log_stats(log_file: str = "logs/trading_bot.log") -> Dict:
    """Get basic statistics about log file"""
    if not os.path.exists(log_file):
        return {'error': 'Log file not found'}
    
    try:
        with open(log_file, 'r', encoding='utf-8') as f:
            lines = f.readlines()
        
        stats = {
            'total_lines': len(lines),
            'file_size_mb': os.path.getsize(log_file) / (1024 * 1024),
            'levels': {'DEBUG': 0, 'INFO': 0, 'WARNING': 0, 'ERROR': 0, 'CRITICAL': 0}
        }
        
        for line in lines:
            for level in stats['levels']:
                if f' - {level} - ' in line:
                    stats['levels'][level] += 1
                    break
        
        return stats
        
    except Exception as e:
        return {'error': str(e)}

if __name__ == "__main__":
    # Test the logger
    logger = setup_global_logging()
    
    logger.info("🧪 Testing logger functionality")
    logger.debug("🔍 Debug message")
    logger.warning("⚠️ Warning message")
    logger.error("❌ Error message")
    
    # Test component loggers
    etf_logger = logging.getLogger('src.data.etf_flow_manager')
    etf_logger.debug("📈 ETF debug message")
    
    strategy_logger = logging.getLogger('src.strategy.unified_manager')
    strategy_logger.info("🧠 Strategy info message")
    
    print("\n📊 Log Stats:")
    stats = get_log_stats()
    for key, value in stats.items():
        print(f"   {key}: {value}")