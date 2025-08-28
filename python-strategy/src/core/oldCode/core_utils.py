# src/core/utils.py
"""
Common Utilities for Trading Bot
Provides helper functions and utilities used across all modules
"""

import os
import sys
import time
import json
import hashlib
import asyncio
import functools
from typing import Any, Dict, List, Optional, Union, Callable, Tuple
from datetime import datetime, timedelta, timezone
from decimal import Decimal, ROUND_HALF_UP
from pathlib import Path
import pandas as pd
import numpy as np
from contextlib import asynccontextmanager, contextmanager
import logging

logger = logging.getLogger(__name__)


# Time utilities
def get_utc_now() -> datetime:
    """Get current UTC datetime"""
    return datetime.now(timezone.utc)


def to_utc(dt: datetime) -> datetime:
    """Convert datetime to UTC"""
    if dt.tzinfo is None:
        return dt.replace(tzinfo=timezone.utc)
    return dt.astimezone(timezone.utc)


def from_timestamp(timestamp: Union[int, float], unit: str = 'seconds') -> datetime:
    """Convert timestamp to datetime"""
    if unit == 'milliseconds':
        timestamp = timestamp / 1000
    return datetime.fromtimestamp(timestamp, tz=timezone.utc)


def to_timestamp(dt: datetime, unit: str = 'seconds') -> Union[int, float]:
    """Convert datetime to timestamp"""
    timestamp = dt.timestamp()
    if unit == 'milliseconds':
        return int(timestamp * 1000)
    return int(timestamp)


def parse_timeframe(timeframe: str) -> timedelta:
    """Parse timeframe string to timedelta"""
    timeframe_map = {
        '1m': timedelta(minutes=1),
        '5m': timedelta(minutes=5),
        '15m': timedelta(minutes=15),
        '30m': timedelta(minutes=30),
        '1h': timedelta(hours=1),
        '4h': timedelta(hours=4),
        '1d': timedelta(days=1),
        '1w': timedelta(weeks=1)
    }
    
    if timeframe not in timeframe_map:
        raise ValueError(f"Unsupported timeframe: {timeframe}")
    
    return timeframe_map[timeframe]


def round_to_timeframe(dt: datetime, timeframe: str) -> datetime:
    """Round datetime to timeframe boundary"""
    if timeframe == '1m':
        return dt.replace(second=0, microsecond=0)
    elif timeframe == '5m':
        minute = (dt.minute // 5) * 5
        return dt.replace(minute=minute, second=0, microsecond=0)
    elif timeframe == '15m':
        minute = (dt.minute // 15) * 15
        return dt.replace(minute=minute, second=0, microsecond=0)
    elif timeframe == '30m':
        minute = (dt.minute // 30) * 30
        return dt.replace(minute=minute, second=0, microsecond=0)
    elif timeframe == '1h':
        return dt.replace(minute=0, second=0, microsecond=0)
    elif timeframe == '4h':
        hour = (dt.hour // 4) * 4
        return dt.replace(hour=hour, minute=0, second=0, microsecond=0)
    elif timeframe == '1d':
        return dt.replace(hour=0, minute=0, second=0, microsecond=0)
    elif timeframe == '1w':
        days_since_monday = dt.weekday()
        monday = dt - timedelta(days=days_since_monday)
        return monday.replace(hour=0, minute=0, second=0, microsecond=0)
    else:
        raise ValueError(f"Unsupported timeframe: {timeframe}")


# Financial utilities
def round_price(price: float, precision: int = 8) -> float:
    """Round price to specified precision"""
    if price == 0:
        return 0.0
    
    decimal_price = Decimal(str(price))
    factor = Decimal('0.1') ** precision
    rounded = decimal_price.quantize(factor, rounding=ROUND_HALF_UP)
    return float(rounded)


def round_quantity(quantity: float, step_size: float) -> float:
    """Round quantity to exchange step size"""
    if step_size == 0:
        return quantity
    
    # Calculate number of decimal places in step_size
    step_str = f"{step_size:.10f}".rstrip('0')
    if '.' in step_str:
        decimals = len(step_str.split('.')[1])
    else:
        decimals = 0
    
    # Round to step size
    rounded = round(quantity / step_size) * step_size
    return round(rounded, decimals)


def calculate_percentage_change(old_value: float, new_value: float) -> float:
    """Calculate percentage change between two values"""
    if old_value == 0:
        return 0.0
    return ((new_value - old_value) / old_value) * 100


def calculate_compound_return(returns: List[float]) -> float:
    """Calculate compound return from list of period returns"""
    if not returns:
        return 0.0
    
    compound = 1.0
    for ret in returns:
        compound *= (1 + ret)
    
    return compound - 1.0


def calculate_volatility(returns: List[float], annualization_factor: float = 252) -> float:
    """Calculate annualized volatility from returns"""
    if len(returns) < 2:
        return 0.0
    
    return float(np.std(returns) * np.sqrt(annualization_factor))


def calculate_sharpe_ratio(returns: List[float], risk_free_rate: float = 0.0,
                          annualization_factor: float = 252) -> float:
    """Calculate Sharpe ratio"""
    if len(returns) < 2:
        return 0.0
    
    excess_returns = [r - risk_free_rate/annualization_factor for r in returns]
    mean_excess = np.mean(excess_returns)
    std_excess = np.std(excess_returns)
    
    if std_excess == 0:
        return 0.0
    
    return float((mean_excess * annualization_factor) / (std_excess * np.sqrt(annualization_factor)))


def calculate_max_drawdown(equity_curve: List[float]) -> float:
    """Calculate maximum drawdown from equity curve"""
    if len(equity_curve) < 2:
        return 0.0
    
    peak = equity_curve[0]
    max_dd = 0.0
    
    for value in equity_curve[1:]:
        if value > peak:
            peak = value
        else:
            drawdown = (peak - value) / peak
            max_dd = max(max_dd, drawdown)
    
    return max_dd


# Data validation utilities
def validate_ohlcv_data(data: pd.DataFrame) -> Tuple[bool, List[str]]:
    """Validate OHLCV data structure and quality"""
    errors = []
    
    # Check required columns
    required_columns = ['open', 'high', 'low', 'close', 'volume']
    missing_columns = [col for col in required_columns if col not in data.columns]
    if missing_columns:
        errors.append(f"Missing columns: {missing_columns}")
    
    if errors:
        return False, errors
    
    # Check data types
    for col in required_columns:
        if not pd.api.types.is_numeric_dtype(data[col]):
            errors.append(f"Column {col} must be numeric")
    
    # Check for negative values where inappropriate
    if (data['open'] < 0).any() or (data['high'] < 0).any() or \
       (data['low'] < 0).any() or (data['close'] < 0).any():
        errors.append("Price values cannot be negative")
    
    if (data['volume'] < 0).any():
        errors.append("Volume cannot be negative")
    
    # Check OHLC relationships
    invalid_ohlc = (data['high'] < data['low']).any() or \
                   (data['high'] < data['open']).any() or \
                   (data['high'] < data['close']).any() or \
                   (data['low'] > data['open']).any() or \
                   (data['low'] > data['close']).any()
    
    if invalid_ohlc:
        errors.append("Invalid OHLC relationships detected")
    
    # Check for NaN values
    if data[required_columns].isnull().any().any():
        errors.append("Data contains NaN values")
    
    # Check for extreme outliers (>100x median)
    for col in ['open', 'high', 'low', 'close']:
        median_val = data[col].median()
        if median_val > 0:
            outliers = (data[col] > median_val * 100) | (data[col] < median_val / 100)
            if outliers.any():
                errors.append(f"Extreme outliers detected in {col}")
    
    return len(errors) == 0, errors


def clean_ohlcv_data(data: pd.DataFrame) -> pd.DataFrame:
    """Clean and normalize OHLCV data"""
    if data.empty:
        return data
    
    # Remove rows with all NaN values
    data = data.dropna(how='all')
    
    # Forward fill missing values (be conservative)
    data = data.fillna(method='ffill')
    
    # Remove any remaining NaN values
    data = data.dropna()
    
    # Sort by index (timestamp)
    data = data.sort_index()
    
    # Remove duplicates
    data = data[~data.index.duplicated(keep='last')]
    
    return data


# File and directory utilities
def ensure_directory(path: Union[str, Path]) -> Path:
    """Ensure directory exists, create if not"""
    path = Path(path)
    path.mkdir(parents=True, exist_ok=True)
    return path


def safe_file_write(filepath: Union[str, Path], content: str, 
                   backup: bool = True) -> None:
    """Safely write content to file with atomic operation"""
    filepath = Path(filepath)
    
    # Create backup if requested and file exists
    if backup and filepath.exists():
        backup_path = filepath.with_suffix(f"{filepath.suffix}.bak")
        filepath.rename(backup_path)
    
    # Write to temporary file first
    temp_path = filepath.with_suffix(f"{filepath.suffix}.tmp")
    
    try:
        with open(temp_path, 'w', encoding='utf-8') as f:
            f.write(content)
            f.flush()
            os.fsync(f.fileno())  # Ensure data is written to disk
        
        # Atomic move
        temp_path.rename(filepath)
        
    except Exception as e:
        # Cleanup temp file on error
        if temp_path.exists():
            temp_path.unlink()
        raise e


def load_json_file(filepath: Union[str, Path]) -> Dict[str, Any]:
    """Load JSON file with error handling"""
    filepath = Path(filepath)
    
    if not filepath.exists():
        raise FileNotFoundError(f"JSON file not found: {filepath}")
    
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            return json.load(f)
    except json.JSONDecodeError as e:
        raise ValueError(f"Invalid JSON in {filepath}: {e}")


def save_json_file(filepath: Union[str, Path], data: Dict[str, Any], 
                  indent: int = 2) -> None:
    """Save data to JSON file"""
    filepath = Path(filepath)
    ensure_directory(filepath.parent)
    
    content = json.dumps(data, indent=indent, default=str)
    safe_file_write(filepath, content)


# Hashing and security utilities
def generate_hash(data: str, algorithm: str = 'sha256') -> str:
    """Generate hash of string data"""
    hasher = hashlib.new(algorithm)
    hasher.update(data.encode('utf-8'))
    return hasher.hexdigest()


def generate_id(prefix: str = '', suffix: str = '') -> str:
    """Generate unique ID with optional prefix/suffix"""
    timestamp = int(time.time() * 1000000)  # microseconds
    return f"{prefix}{timestamp}{suffix}"


def mask_sensitive_data(data: str, mask_char: str = '*', 
                       show_chars: int = 4) -> str:
    """Mask sensitive data showing only first/last characters"""
    if len(data) <= show_chars * 2:
        return mask_char * len(data)
    
    return data[:show_chars] + mask_char * (len(data) - show_chars * 2) + data[-show_chars:]


# Async utilities
async def retry_async(func: Callable, max_attempts: int = 3, 
                     delay: float = 1.0, backoff_factor: float = 2.0,
                     exceptions: Tuple = (Exception,)) -> Any:
    """Retry async function with exponential backoff"""
    last_exception = None
    
    for attempt in range(max_attempts):
        try:
            return await func()
        except exceptions as e:
            last_exception = e
            if attempt == max_attempts - 1:
                break
            
            wait_time = delay * (backoff_factor ** attempt)
            logger.warning(f"Attempt {attempt + 1} failed: {e}. Retrying in {wait_time:.1f}s")
            await asyncio.sleep(wait_time)
    
    raise last_exception


@asynccontextmanager
async def timeout_context(seconds: float):
    """Async context manager with timeout"""
    try:
        await asyncio.wait_for(asyncio.create_task(asyncio.sleep(0)), timeout=seconds)
        yield
    except asyncio.TimeoutError:
        raise asyncio.TimeoutError(f"Operation timed out after {seconds} seconds")


def run_with_timeout(coro, timeout: float):
    """Run coroutine with timeout"""
    return asyncio.wait_for(coro, timeout=timeout)


# Performance utilities
class Timer:
    """Simple timer for performance measurement"""
    
    def __init__(self):
        self.start_time = None
        self.end_time = None
    
    def start(self):
        """Start the timer"""
        self.start_time = time.time()
        return self
    
    def stop(self):
        """Stop the timer"""
        self.end_time = time.time()
        return self
    
    def elapsed(self) -> float:
        """Get elapsed time in seconds"""
        if self.start_time is None:
            return 0.0
        
        end = self.end_time if self.end_time else time.time()
        return end - self.start_time
    
    def __enter__(self):
        return self.start()
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        self.stop()


def measure_time(func: Callable) -> Callable:
    """Decorator to measure function execution time"""
    @functools.wraps(func)
    def wrapper(*args, **kwargs):
        with Timer() as timer:
            result = func(*args, **kwargs)
        
        logger.debug(f"{func.__name__} executed in {timer.elapsed():.3f}s")
        return result
    
    return wrapper


def measure_time_async(func: Callable) -> Callable:
    """Decorator to measure async function execution time"""
    @functools.wraps(func)
    async def wrapper(*args, **kwargs):
        with Timer() as timer:
            result = await func(*args, **kwargs)
        
        logger.debug(f"{func.__name__} executed in {timer.elapsed():.3f}s")
        return result
    
    return wrapper


# System utilities
def get_system_info() -> Dict[str, Any]:
    """Get basic system information"""
    return {
        'platform': sys.platform,
        'python_version': sys.version,
        'executable': sys.executable,
        'path': sys.path[:3],  # First 3 paths only
        'cwd': os.getcwd(),
        'pid': os.getpid(),
        'timestamp': get_utc_now().isoformat()
    }


def check_memory_usage() -> Dict[str, float]:
    """Check current memory usage"""
    try:
        import psutil
        process = psutil.Process()
        memory_info = process.memory_info()
        
        return {
            'rss_mb': memory_info.rss / 1024 / 1024,
            'vms_mb': memory_info.vms / 1024 / 1024,
            'percent': process.memory_percent()
        }
    except ImportError:
        logger.warning("psutil not available for memory monitoring")
        return {}


def format_bytes(bytes_value: Union[int, float]) -> str:
    """Format bytes to human readable string"""
    for unit in ['B', 'KB', 'MB', 'GB', 'TB']:
        if bytes_value < 1024.0:
            return f"{bytes_value:.1f} {unit}"
        bytes_value /= 1024.0
    return f"{bytes_value:.1f} PB"


# Configuration utilities
def deep_merge_dicts(dict1: Dict[str, Any], dict2: Dict[str, Any]) -> Dict[str, Any]:
    """Deep merge two dictionaries"""
    result = dict1.copy()
    
    for key, value in dict2.items():
        if key in result and isinstance(result[key], dict) and isinstance(value, dict):
            result[key] = deep_merge_dicts(result[key], value)
        else:
            result[key] = value
    
    return result


def flatten_dict(d: Dict[str, Any], parent_key: str = '', sep: str = '.') -> Dict[str, Any]:
    """Flatten nested dictionary"""
    items = []
    
    for k, v in d.items():
        new_key = f"{parent_key}{sep}{k}" if parent_key else k
        
        if isinstance(v, dict):
            items.extend(flatten_dict(v, new_key, sep=sep).items())
        else:
            items.append((new_key, v))
    
    return dict(items)


def unflatten_dict(d: Dict[str, Any], sep: str = '.') -> Dict[str, Any]:
    """Unflatten dictionary with dot notation keys"""
    result = {}
    
    for key, value in d.items():
        keys = key.split(sep)
        current = result
        
        for k in keys[:-1]:
            if k not in current:
                current[k] = {}
            current = current[k]
        
        current[keys[-1]] = value
    
    return result


# Validation utilities
def validate_symbol_format(symbol: str) -> bool:
    """Validate trading symbol format"""
    if not symbol or not isinstance(symbol, str):
        return False
    
    # Basic validation - symbol should be uppercase and contain only letters
    return symbol.isupper() and symbol.replace('USDT', '').replace('BTC', '').replace('ETH', '').isalpha()


def validate_timeframe(timeframe: str) -> bool:
    """Validate timeframe format"""
    valid_timeframes = ['1m', '5m', '15m', '30m', '1h', '4h', '1d', '1w']
    return timeframe in valid_timeframes


def validate_percentage(value: float, min_val: float = 0.0, max_val: float = 1.0) -> bool:
    """Validate percentage value"""
    return isinstance(value, (int, float)) and min_val <= value <= max_val


# Debugging utilities
def print_dataframe_info(df: pd.DataFrame, name: str = "DataFrame") -> None:
    """Print comprehensive DataFrame information"""
    print(f"\n{name} Info:")
    print(f"Shape: {df.shape}")
    print(f"Memory usage: {df.memory_usage(deep=True).sum() / 1024 / 1024:.2f} MB")
    print(f"Index: {df.index.dtype} ({df.index.min()} to {df.index.max()})")
    print(f"Columns: {list(df.columns)}")
    print(f"Data types:\n{df.dtypes}")
    print(f"Missing values:\n{df.isnull().sum()}")
    if not df.empty:
        print(f"First row:\n{df.iloc[0]}")
        print(f"Last row:\n{df.iloc[-1]}")


@contextmanager
def suppress_warnings():
    """Context manager to suppress warnings"""
    import warnings
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        yield


# Constants for common operations
class Constants:
    """Common constants used throughout the application"""
    
    # Time constants
    SECONDS_PER_MINUTE = 60
    SECONDS_PER_HOUR = 3600
    SECONDS_PER_DAY = 86400
    MILLISECONDS_PER_SECOND = 1000
    
    # Financial constants
    TRADING_DAYS_PER_YEAR = 252
    HOURS_PER_TRADING_DAY = 24  # Crypto trades 24/7
    
    # Default precision
    PRICE_PRECISION = 8
    QUANTITY_PRECISION = 8
    
    # Common file extensions
    JSON_EXT = '.json'
    CSV_EXT = '.csv'
    LOG_EXT = '.log'
    
    # Environment variables
    ENV_API_KEY = 'BINANCE_API_KEY'
    ENV_API_SECRET = 'BINANCE_API_SECRET'
    ENV_DB_PASSWORD = 'DB_PASSWORD'