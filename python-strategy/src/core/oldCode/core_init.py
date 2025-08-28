# src/core/__init__.py
"""
Core Module for Crypto Trading Bot

This module provides foundational components used throughout the trading bot:
- Configuration management with hot-reloading and validation
- Structured logging with multiple handlers and formatters  
- Custom exceptions for error handling
- Base classes defining common interfaces
- Utility functions for common operations

Usage:
    from core import ConfigurationManager, setup_logging, get_logger
    from core.exceptions import TradingBotException, ConfigValidationError
    from core.base_classes import BaseModule, BaseStrategy
    from core.utils import round_price, validate_ohlcv_data
"""

# Core configuration management
from .config_manager import (
    ConfigurationManager,
    ConfigValidationError
)

# Logging system
from .logging_setup import (
    setup_logging,
    get_logger,
    get_audit_logger,
    log_trade_action,
    log_system_event,
    log_error_with_context,
    LoggerMixin,
    PerformanceLogger,
    log_performance,
    TradingLogger,
    StructuredFormatter,
    TradingBotFormatter
)

# Exception hierarchy
from ..exceptions import (
    # Base exceptions
    TradingBotException,
    
    # Configuration exceptions
    ConfigurationException,
    ConfigFileNotFoundError,
    EnvironmentVariableError,
    
    # Data ingestion exceptions
    DataIngestionException,
    APIConnectionError,
    RateLimitError,
    DataValidationError,
    DatabaseError,
    
    # Strategy exceptions
    StrategyException,
    IndicatorCalculationError,
    InsufficientDataError,
    SignalGenerationError,
    BacktestError,
    
    # Risk management exceptions
    RiskManagementException,
    RiskLimitExceeded,
    PositionSizingError,
    DrawdownLimitExceeded,
    
    # Order execution exceptions
    OrderExecutionException,
    OrderValidationError,
    OrderRejectedError,
    InsufficientFundsError,
    SlippageExceededError,
    PositionSyncError,
    
    # System exceptions
    SystemException,
    SystemResourceError,
    SchedulingError,
    MonitoringError,
    EmergencyStopException,
    
    # Utility functions
    handle_exception,
    is_retryable_error,
    get_retry_delay
)

# Base classes and interfaces
from .base_classes import (
    # Enums and data classes
    ModuleState,
    ModuleStatus,
    
    # Base classes
    BaseModule,
    BaseStrategy,
    BaseIndicator,
    BaseRiskManager,
    BaseDataProvider,
    BaseOrderExecutor,
    BaseBacktester,
    
    # Utility classes
    EventBus,
    EventTypes,
    SingletonMeta,
    
    # Decorators
    require_state,
    validate_config
)

# Utility functions
from ..utils import (
    # Time utilities
    get_utc_now,
    to_utc,
    from_timestamp,
    to_timestamp,
    parse_timeframe,
    round_to_timeframe,
    
    # Financial utilities
    round_price,
    round_quantity,
    calculate_percentage_change,
    calculate_compound_return,
    calculate_volatility,
    calculate_sharpe_ratio,
    calculate_max_drawdown,
    
    # Data validation utilities
    validate_ohlcv_data,
    clean_ohlcv_data,
    
    # File utilities
    ensure_directory,
    safe_file_write,
    load_json_file,
    save_json_file,
    
    # Security utilities
    generate_hash,
    generate_id,
    mask_sensitive_data,
    
    # Async utilities
    retry_async,
    timeout_context,
    run_with_timeout,
    
    # Performance utilities
    Timer,
    measure_time,
    measure_time_async,
    
    # System utilities
    get_system_info,
    check_memory_usage,
    format_bytes,
    
    # Configuration utilities
    deep_merge_dicts,
    flatten_dict,
    unflatten_dict,
    
    # Validation utilities
    validate_symbol_format,
    validate_timeframe,
    validate_percentage,
    
    # Debugging utilities
    print_dataframe_info,
    suppress_warnings,
    
    # Constants
    Constants
)

# Version information
__version__ = "1.0.0"
__author__ = "Trading Bot Team"
__email__ = "team@tradingbot.com"

# Module metadata
__all__ = [
    # Configuration
    'ConfigurationManager',
    'ConfigValidationError',
    
    # Logging
    'setup_logging',
    'get_logger',
    'get_audit_logger',
    'log_trade_action',
    'log_system_event', 
    'log_error_with_context',
    'LoggerMixin',
    'PerformanceLogger',
    'log_performance',
    'TradingLogger',
    'StructuredFormatter',
    'TradingBotFormatter',
    
    # Exceptions
    'TradingBotException',
    'ConfigurationException',
    'ConfigFileNotFoundError',
    'EnvironmentVariableError',
    'DataIngestionException',
    'APIConnectionError',
    'RateLimitError',
    'DataValidationError',
    'DatabaseError',
    'StrategyException',
    'IndicatorCalculationError',
    'InsufficientDataError',
    'SignalGenerationError',
    'BacktestError',
    'RiskManagementException',
    'RiskLimitExceeded',
    'PositionSizingError',
    'DrawdownLimitExceeded',
    'OrderExecutionException',
    'OrderValidationError',
    'OrderRejectedError',
    'InsufficientFundsError',
    'SlippageExceededError',
    'PositionSyncError',
    'SystemException',
    'SystemResourceError',
    'SchedulingError',
    'MonitoringError',
    'EmergencyStopException',
    'handle_exception',
    'is_retryable_error',
    'get_retry_delay',
    
    # Base classes
    'ModuleState',
    'ModuleStatus',
    'BaseModule',
    'BaseStrategy',
    'BaseIndicator',
    'BaseRiskManager',
    'BaseDataProvider',
    'BaseOrderExecutor',
    'BaseBacktester',
    'EventBus',
    'EventTypes',
    'SingletonMeta',
    'require_state',
    'validate_config',
    
    # Utilities
    'get_utc_now',
    'to_utc',
    'from_timestamp',
    'to_timestamp',
    'parse_timeframe',
    'round_to_timeframe',
    'round_price',
    'round_quantity',
    'calculate_percentage_change',
    'calculate_compound_return',
    'calculate_volatility',
    'calculate_sharpe_ratio',
    'calculate_max_drawdown',
    'validate_ohlcv_data',
    'clean_ohlcv_data',
    'ensure_directory',
    'safe_file_write',
    'load_json_file',
    'save_json_file',
    'generate_hash',
    'generate_id',
    'mask_sensitive_data',
    'retry_async',
    'timeout_context',
    'run_with_timeout',
    'Timer',
    'measure_time',
    'measure_time_async',
    'get_system_info',
    'check_memory_usage',
    'format_bytes',
    'deep_merge_dicts',
    'flatten_dict',
    'unflatten_dict',
    'validate_symbol_format',
    'validate_timeframe',
    'validate_percentage',
    'print_dataframe_info',
    'suppress_warnings',
    'Constants'
]


def get_version() -> str:
    """Get the current version of the core module"""
    return __version__


def initialize_core(config_path: str, environment: str = "development") -> ConfigurationManager:
    """
    Initialize the core module with configuration and logging.
    
    This is a convenience function that sets up the essential core components:
    1. Loads configuration from the specified path
    2. Sets up logging based on configuration
    3. Returns the configuration manager for use by other modules
    
    Args:
        config_path: Path to the main configuration file
        environment: Environment name (development, staging, production)
        
    Returns:
        ConfigurationManager: Initialized configuration manager
        
    Raises:
        ConfigFileNotFoundError: If configuration file is not found
        ConfigValidationError: If configuration validation fails
    """
    try:
        # Initialize configuration manager
        config_manager = ConfigurationManager(config_path, environment)
        config = config_manager.load_config()
        
        # Setup logging
        logging_config = config.get('logging', {})
        setup_logging(logging_config)
        
        # Log successful initialization
        logger = get_logger('core')
        logger.info(f"Core module initialized successfully")
        logger.info(f"Environment: {environment}")
        logger.info(f"Config file: {config_path}")
        logger.info(f"Core version: {__version__}")
        
        return config_manager
        
    except Exception as e:
        # If logging isn't set up yet, use basic logging
        import logging
        logging.basicConfig(level=logging.ERROR)
        logging.error(f"Failed to initialize core module: {e}")
        raise


# Module initialization validation
def _validate_module_imports():
    """Validate that all module imports are working correctly"""
    try:
        # Test configuration manager
        assert ConfigurationManager is not None
        
        # Test logging components
        assert setup_logging is not None
        assert get_logger is not None
        
        # Test exception hierarchy
        assert TradingBotException is not None
        
        # Test base classes
        assert BaseModule is not None
        assert BaseStrategy is not None
        
        # Test utilities
        assert round_price is not None
        assert get_utc_now is not None
        
        return True
        
    except (ImportError, AssertionError, AttributeError) as e:
        import logging
        logging.error(f"Core module import validation failed: {e}")
        return False


# Validate imports on module load
if not _validate_module_imports():
    raise ImportError("Core module failed import validation")


# Optional: Auto-setup for development
def _auto_setup_development():
    """Auto-setup for development environment if config is available"""
    import os
    
    # Check if we're in development and have a default config
    if os.getenv('TRADING_BOT_ENV', '').lower() == 'development':
        default_config = os.getenv('TRADING_BOT_CONFIG', 'config/trading_bot.yaml')
        
        if os.path.exists(default_config):
            try:
                initialize_core(default_config, 'development')
            except Exception:
                pass  # Silently fail - manual initialization required


# Auto-setup if environment variable is set
_auto_setup_development()