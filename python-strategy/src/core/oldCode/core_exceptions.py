# src/core/exceptions.py
"""
Custom Exceptions for Trading Bot
Provides structured error handling across all modules
"""

from typing import Dict, Any, Optional
from datetime import datetime


class TradingBotException(Exception):
    """Base exception for all trading bot errors"""
    
    def __init__(self, message: str, details: Optional[Dict[str, Any]] = None, 
                 error_code: Optional[str] = None):
        self.message = message
        self.details = details or {}
        self.error_code = error_code
        self.timestamp = datetime.now()
        super().__init__(self.message)
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert exception to dictionary for logging"""
        return {
            'error_type': self.__class__.__name__,
            'message': self.message,
            'error_code': self.error_code,
            'details': self.details,
            'timestamp': self.timestamp.isoformat()
        }
    
    def __str__(self):
        if self.error_code:
            return f"[{self.error_code}] {self.message}"
        return self.message


# Configuration Exceptions
class ConfigurationException(TradingBotException):
    """Base exception for configuration-related errors"""
    pass


class ConfigValidationError(ConfigurationException):
    """Raised when configuration validation fails"""
    
    def __init__(self, section: str, field: str, message: str):
        self.section = section
        self.field = field
        super().__init__(
            message=f"Configuration validation error in {section}.{field}: {message}",
            details={'section': section, 'field': field},
            error_code='CONFIG_VALIDATION'
        )


class ConfigFileNotFoundError(ConfigurationException):
    """Raised when configuration file is not found"""
    
    def __init__(self, config_path: str):
        self.config_path = config_path
        super().__init__(
            message=f"Configuration file not found: {config_path}",
            details={'config_path': config_path},
            error_code='CONFIG_FILE_NOT_FOUND'
        )


class EnvironmentVariableError(ConfigurationException):
    """Raised when required environment variable is missing"""
    
    def __init__(self, variable_name: str):
        self.variable_name = variable_name
        super().__init__(
            message=f"Required environment variable not set: {variable_name}",
            details={'variable_name': variable_name},
            error_code='ENV_VAR_MISSING'
        )


# Data Ingestion Exceptions
class DataIngestionException(TradingBotException):
    """Base exception for data ingestion errors"""
    pass


class APIConnectionError(DataIngestionException):
    """Raised when API connection fails"""
    
    def __init__(self, api_name: str, message: str, status_code: Optional[int] = None):
        self.api_name = api_name
        self.status_code = status_code
        super().__init__(
            message=f"API connection error for {api_name}: {message}",
            details={'api_name': api_name, 'status_code': status_code},
            error_code='API_CONNECTION_ERROR'
        )


class RateLimitError(DataIngestionException):
    """Raised when API rate limit is exceeded"""
    
    def __init__(self, api_name: str, retry_after: Optional[int] = None):
        self.api_name = api_name
        self.retry_after = retry_after
        super().__init__(
            message=f"Rate limit exceeded for {api_name}",
            details={'api_name': api_name, 'retry_after': retry_after},
            error_code='RATE_LIMIT_EXCEEDED'
        )


class DataValidationError(DataIngestionException):
    """Raised when data validation fails"""
    
    def __init__(self, symbol: str, field: str, message: str):
        self.symbol = symbol
        self.field = field
        super().__init__(
            message=f"Data validation error for {symbol}.{field}: {message}",
            details={'symbol': symbol, 'field': field},
            error_code='DATA_VALIDATION_ERROR'
        )


class DatabaseError(DataIngestionException):
    """Raised when database operations fail"""
    
    def __init__(self, operation: str, message: str, table: Optional[str] = None):
        self.operation = operation
        self.table = table
        super().__init__(
            message=f"Database error during {operation}: {message}",
            details={'operation': operation, 'table': table},
            error_code='DATABASE_ERROR'
        )


# Strategy Exceptions
class StrategyException(TradingBotException):
    """Base exception for strategy-related errors"""
    pass


class IndicatorCalculationError(StrategyException):
    """Raised when indicator calculation fails"""
    
    def __init__(self, indicator_name: str, symbol: str, message: str):
        self.indicator_name = indicator_name
        self.symbol = symbol
        super().__init__(
            message=f"Indicator calculation error for {indicator_name} on {symbol}: {message}",
            details={'indicator_name': indicator_name, 'symbol': symbol},
            error_code='INDICATOR_CALCULATION_ERROR'
        )


class InsufficientDataError(StrategyException):
    """Raised when there's insufficient data for strategy calculation"""
    
    def __init__(self, symbol: str, required_periods: int, available_periods: int):
        self.symbol = symbol
        self.required_periods = required_periods
        self.available_periods = available_periods
        super().__init__(
            message=f"Insufficient data for {symbol}: need {required_periods}, have {available_periods}",
            details={'symbol': symbol, 'required_periods': required_periods, 'available_periods': available_periods},
            error_code='INSUFFICIENT_DATA'
        )


class SignalGenerationError(StrategyException):
    """Raised when signal generation fails"""
    
    def __init__(self, strategy_name: str, symbol: str, message: str):
        self.strategy_name = strategy_name
        self.symbol = symbol
        super().__init__(
            message=f"Signal generation error in {strategy_name} for {symbol}: {message}",
            details={'strategy_name': strategy_name, 'symbol': symbol},
            error_code='SIGNAL_GENERATION_ERROR'
        )


class BacktestError(StrategyException):
    """Raised when backtesting fails"""
    
    def __init__(self, strategy_name: str, message: str):
        self.strategy_name = strategy_name
        super().__init__(
            message=f"Backtest error for {strategy_name}: {message}",
            details={'strategy_name': strategy_name},
            error_code='BACKTEST_ERROR'
        )


# Risk Management Exceptions
class RiskManagementException(TradingBotException):
    """Base exception for risk management errors"""
    pass


class RiskLimitExceeded(RiskManagementException):
    """Raised when risk limits are exceeded"""
    
    def __init__(self, limit_type: str, current_value: float, limit_value: float, symbol: Optional[str] = None):
        self.limit_type = limit_type
        self.current_value = current_value
        self.limit_value = limit_value
        self.symbol = symbol
        
        symbol_str = f" for {symbol}" if symbol else ""
        super().__init__(
            message=f"Risk limit exceeded{symbol_str}: {limit_type} = {current_value:.4f} > {limit_value:.4f}",
            details={
                'limit_type': limit_type,
                'current_value': current_value,
                'limit_value': limit_value,
                'symbol': symbol
            },
            error_code='RISK_LIMIT_EXCEEDED'
        )


class PositionSizingError(RiskManagementException):
    """Raised when position sizing calculation fails"""
    
    def __init__(self, symbol: str, method: str, message: str):
        self.symbol = symbol
        self.method = method
        super().__init__(
            message=f"Position sizing error for {symbol} using {method}: {message}",
            details={'symbol': symbol, 'method': method},
            error_code='POSITION_SIZING_ERROR'
        )


class DrawdownLimitExceeded(RiskManagementException):
    """Raised when maximum drawdown limit is exceeded"""
    
    def __init__(self, current_drawdown: float, max_drawdown: float):
        self.current_drawdown = current_drawdown
        self.max_drawdown = max_drawdown
        super().__init__(
            message=f"Maximum drawdown exceeded: {current_drawdown:.2%} > {max_drawdown:.2%}",
            details={'current_drawdown': current_drawdown, 'max_drawdown': max_drawdown},
            error_code='DRAWDOWN_LIMIT_EXCEEDED'
        )


# Order Execution Exceptions
class OrderExecutionException(TradingBotException):
    """Base exception for order execution errors"""
    pass


class OrderValidationError(OrderExecutionException):
    """Raised when order validation fails"""
    
    def __init__(self, order_id: str, field: str, message: str):
        self.order_id = order_id
        self.field = field
        super().__init__(
            message=f"Order validation error for {order_id}.{field}: {message}",
            details={'order_id': order_id, 'field': field},
            error_code='ORDER_VALIDATION_ERROR'
        )


class OrderRejectedError(OrderExecutionException):
    """Raised when order is rejected by exchange"""
    
    def __init__(self, order_id: str, symbol: str, reason: str, exchange_error: Optional[str] = None):
        self.order_id = order_id
        self.symbol = symbol
        self.reason = reason
        self.exchange_error = exchange_error
        super().__init__(
            message=f"Order {order_id} for {symbol} rejected: {reason}",
            details={
                'order_id': order_id,
                'symbol': symbol,
                'reason': reason,
                'exchange_error': exchange_error
            },
            error_code='ORDER_REJECTED'
        )


class InsufficientFundsError(OrderExecutionException):
    """Raised when there are insufficient funds for order"""
    
    def __init__(self, symbol: str, required_amount: float, available_amount: float):
        self.symbol = symbol
        self.required_amount = required_amount
        self.available_amount = available_amount
        super().__init__(
            message=f"Insufficient funds for {symbol}: need {required_amount}, have {available_amount}",
            details={
                'symbol': symbol,
                'required_amount': required_amount,
                'available_amount': available_amount
            },
            error_code='INSUFFICIENT_FUNDS'
        )


class SlippageExceededError(OrderExecutionException):
    """Raised when order slippage exceeds acceptable limits"""
    
    def __init__(self, order_id: str, symbol: str, expected_price: float, 
                 actual_price: float, max_slippage: float):
        self.order_id = order_id
        self.symbol = symbol
        self.expected_price = expected_price
        self.actual_price = actual_price
        self.max_slippage = max_slippage
        
        slippage = abs(actual_price - expected_price) / expected_price
        super().__init__(
            message=f"Slippage exceeded for order {order_id}: {slippage:.4f} > {max_slippage:.4f}",
            details={
                'order_id': order_id,
                'symbol': symbol,
                'expected_price': expected_price,
                'actual_price': actual_price,
                'slippage': slippage,
                'max_slippage': max_slippage
            },
            error_code='SLIPPAGE_EXCEEDED'
        )


class PositionSyncError(OrderExecutionException):
    """Raised when position synchronization fails"""
    
    def __init__(self, symbol: str, local_position: float, exchange_position: float):
        self.symbol = symbol
        self.local_position = local_position
        self.exchange_position = exchange_position
        super().__init__(
            message=f"Position sync error for {symbol}: local={local_position}, exchange={exchange_position}",
            details={
                'symbol': symbol,
                'local_position': local_position,
                'exchange_position': exchange_position
            },
            error_code='POSITION_SYNC_ERROR'
        )


# System Exceptions
class SystemException(TradingBotException):
    """Base exception for system-level errors"""
    pass


class SystemResourceError(SystemException):
    """Raised when system resources are exhausted"""
    
    def __init__(self, resource_type: str, current_usage: float, threshold: float):
        self.resource_type = resource_type
        self.current_usage = current_usage
        self.threshold = threshold
        super().__init__(
            message=f"System resource limit exceeded: {resource_type} = {current_usage:.2%} > {threshold:.2%}",
            details={
                'resource_type': resource_type,
                'current_usage': current_usage,
                'threshold': threshold
            },
            error_code='SYSTEM_RESOURCE_ERROR'
        )


class SchedulingError(SystemException):
    """Raised when task scheduling fails"""
    
    def __init__(self, task_name: str, message: str):
        self.task_name = task_name
        super().__init__(
            message=f"Scheduling error for task {task_name}: {message}",
            details={'task_name': task_name},
            error_code='SCHEDULING_ERROR'
        )


class MonitoringError(SystemException):
    """Raised when monitoring systems fail"""
    
    def __init__(self, monitor_type: str, message: str):
        self.monitor_type = monitor_type
        super().__init__(
            message=f"Monitoring error in {monitor_type}: {message}",
            details={'monitor_type': monitor_type},
            error_code='MONITORING_ERROR'
        )


# Emergency Stop Exception
class EmergencyStopException(TradingBotException):
    """Raised when emergency stop is triggered"""
    
    def __init__(self, reason: str, trigger_conditions: Dict[str, Any]):
        self.reason = reason
        self.trigger_conditions = trigger_conditions
        super().__init__(
            message=f"Emergency stop triggered: {reason}",
            details={'reason': reason, 'trigger_conditions': trigger_conditions},
            error_code='EMERGENCY_STOP'
        )


# Exception Utilities
def handle_exception(exception: Exception, context: Dict[str, Any]) -> TradingBotException:
    """Convert generic exceptions to trading bot exceptions with context"""
    if isinstance(exception, TradingBotException):
        # Add context to existing trading bot exception
        exception.details.update(context)
        return exception
    
    # Convert generic exception to trading bot exception
    return TradingBotException(
        message=f"Unexpected error: {str(exception)}",
        details={
            'original_exception': type(exception).__name__,
            'original_message': str(exception),
            **context
        },
        error_code='UNEXPECTED_ERROR'
    )


def is_retryable_error(exception: Exception) -> bool:
    """Check if an exception is retryable"""
    if isinstance(exception, (APIConnectionError, RateLimitError, DatabaseError)):
        return True
    
    if isinstance(exception, OrderExecutionException):
        # Some order execution errors are retryable
        return exception.error_code in ['API_CONNECTION_ERROR', 'NETWORK_ERROR']
    
    return False


def get_retry_delay(exception: Exception, attempt: int) -> float:
    """Get retry delay based on exception type and attempt number"""
    base_delay = 1.0
    
    if isinstance(exception, RateLimitError):
        # Use retry_after if provided, otherwise exponential backoff
        return exception.retry_after if exception.retry_after else min(60, base_delay * (2 ** attempt))
    
    if isinstance(exception, APIConnectionError):
        # Exponential backoff for API errors
        return min(30, base_delay * (2 ** attempt))
    
    # Standard exponential backoff
    return min(15, base_delay * (2 ** attempt))