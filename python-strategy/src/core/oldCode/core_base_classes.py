# src/core/base_classes.py
"""
Abstract Base Classes for Trading Bot
Provides common interfaces and base functionality for all modules
"""

from abc import ABC, abstractmethod
from typing import Dict, Any, List, Optional, Union
from datetime import datetime, timedelta
from dataclasses import dataclass, field
from enum import Enum
import pandas as pd
import asyncio
import logging

from .logging_setup import LoggerMixin
from ..exceptions import TradingBotException


class ModuleState(Enum):
    """States for system modules"""
    UNINITIALIZED = "uninitialized"
    INITIALIZING = "initializing"
    READY = "ready"
    RUNNING = "running"
    PAUSED = "paused"
    STOPPING = "stopping"
    STOPPED = "stopped"
    ERROR = "error"


@dataclass
class ModuleStatus:
    """Status information for a module"""
    state: ModuleState
    last_update: datetime = field(default_factory=datetime.now)
    uptime: timedelta = field(default_factory=lambda: timedelta(0))
    error_count: int = 0
    last_error: Optional[str] = None
    metrics: Dict[str, Any] = field(default_factory=dict)


class BaseModule(ABC, LoggerMixin):
    """
    Base class for all trading bot modules.
    Provides common functionality like initialization, state management, and monitoring.
    """
    
    def __init__(self, config: Dict[str, Any], name: Optional[str] = None):
        self.config = config
        self.name = name or self.__class__.__name__
        self.state = ModuleState.UNINITIALIZED
        self.status = ModuleStatus(state=self.state)
        self.start_time: Optional[datetime] = None
        self._shutdown_event = asyncio.Event()
        
    @abstractmethod
    async def initialize(self) -> None:
        """Initialize the module"""
        pass
    
    @abstractmethod
    async def start(self) -> None:
        """Start the module"""
        pass
    
    @abstractmethod
    async def stop(self) -> None:
        """Stop the module"""
        pass
    
    @abstractmethod
    async def health_check(self) -> Dict[str, Any]:
        """Perform health check and return status"""
        pass
    
    async def cleanup(self) -> None:
        """Cleanup resources (optional override)"""
        pass
    
    def get_status(self) -> ModuleStatus:
        """Get current module status"""
        if self.start_time:
            self.status.uptime = datetime.now() - self.start_time
        return self.status
    
    def update_state(self, new_state: ModuleState, error: Optional[str] = None) -> None:
        """Update module state"""
        old_state = self.state
        self.state = new_state
        self.status.state = new_state
        self.status.last_update = datetime.now()
        
        if error:
            self.status.error_count += 1
            self.status.last_error = error
            self.logger.error(f"Module {self.name} error: {error}")
        
        self.logger.info(f"Module {self.name} state changed: {old_state.value} -> {new_state.value}")
    
    def update_metrics(self, metrics: Dict[str, Any]) -> None:
        """Update module metrics"""
        self.status.metrics.update(metrics)
    
    def is_running(self) -> bool:
        """Check if module is running"""
        return self.state == ModuleState.RUNNING
    
    def is_ready(self) -> bool:
        """Check if module is ready"""
        return self.state in [ModuleState.READY, ModuleState.RUNNING]
    
    def should_shutdown(self) -> bool:
        """Check if module should shutdown"""
        return self._shutdown_event.is_set()
    
    def request_shutdown(self) -> None:
        """Request module shutdown"""
        self._shutdown_event.set()


class BaseStrategy(ABC, LoggerMixin):
    """
    Abstract base class for all trading strategies.
    Defines the interface that all strategies must implement.
    """
    
    def __init__(self, config: Dict[str, Any]):
        self.config = config
        self.name = self.__class__.__name__
        self.indicators: Dict[str, pd.Series] = {}
        self.signals: Dict[str, Any] = {}
        self.parameters = config.get('params', {})
        
    @abstractmethod
    def calculate_indicators(self, data: pd.DataFrame) -> Dict[str, pd.Series]:
        """Calculate required indicators for the strategy"""
        pass
    
    @abstractmethod
    def generate_signals(self, data: pd.DataFrame, indicators: Dict[str, pd.Series]) -> pd.DataFrame:
        """Generate buy/sell/hold signals"""
        pass
    
    def validate_data(self, data: pd.DataFrame) -> bool:
        """Validate input data quality (optional override)"""
        if data.empty:
            self.logger.warning("Empty data provided to strategy")
            return False
            
        required_columns = ['open', 'high', 'low', 'close', 'volume']
        missing_columns = [col for col in required_columns if col not in data.columns]
        
        if missing_columns:
            self.logger.error(f"Missing required columns: {missing_columns}")
            return False
            
        # Check for NaN values
        if data[required_columns].isnull().any().any():
            self.logger.warning("Data contains NaN values")
            return False
            
        return True
    
    def get_required_history(self) -> int:
        """Get minimum number of periods required for strategy (optional override)"""
        return 50  # Default minimum
    
    def get_parameter(self, name: str, default: Any = None) -> Any:
        """Get strategy parameter with default"""
        return self.parameters.get(name, default)
    
    def set_parameter(self, name: str, value: Any) -> None:
        """Set strategy parameter"""
        self.parameters[name] = value
    
    def reset(self) -> None:
        """Reset strategy state (optional override)"""
        self.indicators.clear()
        self.signals.clear()


class BaseIndicator(ABC, LoggerMixin):
    """
    Abstract base class for technical indicators.
    """
    
    def __init__(self, period: int, **kwargs):
        self.period = period
        self.parameters = kwargs
        self.name = self.__class__.__name__
        
    @abstractmethod
    def calculate(self, data: pd.Series) -> pd.Series:
        """Calculate indicator values"""
        pass
    
    def validate_data(self, data: pd.Series) -> bool:
        """Validate input data for indicator calculation"""
        if len(data) < self.period:
            self.logger.warning(f"Insufficient data for {self.name}: need {self.period}, have {len(data)}")
            # send to slack for alerting
            
            return False
            
        if data.isnull().any():
            self.logger.warning(f"Data contains NaN values for {self.name}")
            return False
            
        return True
    
    def get_required_periods(self) -> int:
        """Get minimum number of periods required"""
        return self.period


class BaseRiskManager(ABC, LoggerMixin):
    """
    Abstract base class for risk management components.
    """
    
    def __init__(self, config: Dict[str, Any]):
        self.config = config
        self.risk_limits = config.get('limits', {})
        
    @abstractmethod
    def evaluate_risk(self, portfolio: Dict[str, Any], signal: Dict[str, Any]) -> Dict[str, Any]:
        """Evaluate risk for a potential trade"""
        pass
    
    @abstractmethod
    def calculate_position_size(self, signal: Dict[str, Any], portfolio: Dict[str, Any]) -> float:
        """Calculate appropriate position size"""
        pass
    
    def check_risk_limits(self, metrics: Dict[str, Any]) -> List[str]:
        """Check if any risk limits are violated"""
        violations = []
        
        for limit_name, limit_value in self.risk_limits.items():
            if limit_name in metrics:
                current_value = metrics[limit_name]
                if current_value > limit_value:
                    violations.append(f"{limit_name}: {current_value} > {limit_value}")
                    
        return violations


class BaseDataProvider(ABC, LoggerMixin):
    """
    Abstract base class for data providers.
    """
    
    def __init__(self, config: Dict[str, Any]):
        self.config = config
        self.symbols = config.get('symbols', [])
        self.timeframes = config.get('timeframes', ['1d'])
        
    @abstractmethod
    async def fetch_data(self, symbol: str, timeframe: str, 
                        start_time: Optional[datetime] = None,
                        end_time: Optional[datetime] = None) -> pd.DataFrame:
        """Fetch market data"""
        pass
    
    @abstractmethod
    async def get_latest_price(self, symbol: str) -> float:
        """Get latest price for symbol"""
        pass
    
    @abstractmethod
    async def validate_symbol(self, symbol: str) -> bool:
        """Validate if symbol is tradeable"""
        pass
    
    def format_symbol(self, symbol: str) -> str:
        """Format symbol for the data provider (optional override)"""
        return symbol.upper()


class BaseOrderExecutor(ABC, LoggerMixin):
    """
    Abstract base class for order execution.
    """
    
    def __init__(self, config: Dict[str, Any]):
        self.config = config
        self.api_config = config.get('api', {})
        
    @abstractmethod
    async def place_order(self, order: Dict[str, Any]) -> Dict[str, Any]:
        """Place an order"""
        pass
    
    @abstractmethod
    async def cancel_order(self, order_id: str) -> bool:
        """Cancel an order"""
        pass
    
    @abstractmethod
    async def get_order_status(self, order_id: str) -> Dict[str, Any]:
        """Get order status"""
        pass
    
    @abstractmethod
    async def get_account_balance(self) -> Dict[str, float]:
        """Get account balance"""
        pass
    
    def validate_order(self, order: Dict[str, Any]) -> bool:
        """Validate order parameters (optional override)"""
        required_fields = ['symbol', 'side', 'type', 'quantity']
        
        for field in required_fields:
            if field not in order:
                self.logger.error(f"Missing required order field: {field}")
                return False
                
        if order['quantity'] <= 0:
            self.logger.error("Order quantity must be positive")
            return False
            
        return True


class BaseBacktester(ABC, LoggerMixin):
    """
    Abstract base class for backtesting engines.
    """
    
    def __init__(self, config: Dict[str, Any]):
        self.config = config
        self.initial_capital = config.get('initial_capital', 100000)
        self.commission = config.get('commission', 0.001)
        
    @abstractmethod
    def run_backtest(self, strategy: BaseStrategy, data: pd.DataFrame,
                    start_date: datetime, end_date: datetime) -> Dict[str, Any]:
        """Run backtest for strategy"""
        pass
    
    @abstractmethod
    def calculate_performance_metrics(self, returns: pd.Series) -> Dict[str, float]:
        """Calculate performance metrics"""
        pass
    
    def validate_backtest_data(self, data: pd.DataFrame) -> bool:
        """Validate data for backtesting"""
        if data.empty:
            self.logger.error("No data provided for backtesting")
            return False
            
        if not data.index.is_monotonic_increasing:
            self.logger.error("Data index must be sorted chronologically")
            return False
            
        return True


class SingletonMeta(type):
    """Metaclass for creating singleton instances"""
    _instances = {}
    
    def __call__(cls, *args, **kwargs):
        if cls not in cls._instances:
            cls._instances[cls] = super().__call__(*args, **kwargs)
        return cls._instances[cls]


class EventBus(metaclass=SingletonMeta):
    """
    Simple event bus for inter-module communication.
    Implements observer pattern for decoupled communication.
    """
    
    def __init__(self):
        self.subscribers: Dict[str, List[callable]] = {}
        self.logger = logging.getLogger('EventBus')
    
    def subscribe(self, event_type: str, callback: callable) -> None:
        """Subscribe to an event type"""
        if event_type not in self.subscribers:
            self.subscribers[event_type] = []
        self.subscribers[event_type].append(callback)
        self.logger.debug(f"Subscribed to event: {event_type}")
    
    def unsubscribe(self, event_type: str, callback: callable) -> None:
        """Unsubscribe from an event type"""
        if event_type in self.subscribers:
            try:
                self.subscribers[event_type].remove(callback)
                self.logger.debug(f"Unsubscribed from event: {event_type}")
            except ValueError:
                self.logger.warning(f"Callback not found for event: {event_type}")
    
    async def publish(self, event_type: str, data: Any = None) -> None:
        """Publish an event to all subscribers"""
        if event_type not in self.subscribers:
            return
            
        self.logger.debug(f"Publishing event: {event_type}")
        
        for callback in self.subscribers[event_type]:
            try:
                if asyncio.iscoroutinefunction(callback):
                    await callback(event_type, data)
                else:
                    callback(event_type, data)
            except Exception as e:
                self.logger.error(f"Error in event callback for {event_type}: {e}")
    
    def get_subscriber_count(self, event_type: str) -> int:
        """Get number of subscribers for an event type"""
        return len(self.subscribers.get(event_type, []))


# Common event types
class EventTypes:
    """Common event types used throughout the system"""
    
    # Data events
    DATA_UPDATED = "data.updated"
    DATA_ERROR = "data.error"
    
    # Strategy events
    SIGNAL_GENERATED = "strategy.signal_generated"
    STRATEGY_ERROR = "strategy.error"
    
    # Risk events
    RISK_LIMIT_EXCEEDED = "risk.limit_exceeded"
    EMERGENCY_STOP = "risk.emergency_stop"
    
    # Order events
    ORDER_PLACED = "order.placed"
    ORDER_FILLED = "order.filled"
    ORDER_CANCELLED = "order.cancelled"
    ORDER_REJECTED = "order.rejected"
    
    # System events
    MODULE_STARTED = "system.module_started"
    MODULE_STOPPED = "system.module_stopped"
    MODULE_ERROR = "system.module_error"
    HEALTH_CHECK = "system.health_check"


# Utility decorators for base classes
def require_state(required_state: ModuleState):
    """Decorator to ensure module is in required state"""
    def decorator(func):
        def wrapper(self, *args, **kwargs):
            if not isinstance(self, BaseModule):
                raise TypeError("require_state can only be used with BaseModule subclasses")
            
            if self.state != required_state:
                raise TradingBotException(
                    f"Module {self.name} must be in state {required_state.value}, currently {self.state.value}"
                )
            
            return func(self, *args, **kwargs)
        return wrapper
    return decorator


def validate_config(required_keys: List[str]):
    """Decorator to validate required configuration keys"""
    def decorator(func):
        def wrapper(self, *args, **kwargs):
            if not hasattr(self, 'config'):
                raise TradingBotException("Object must have 'config' attribute")
            
            missing_keys = [key for key in required_keys if key not in self.config]
            if missing_keys:
                raise TradingBotException(f"Missing required configuration keys: {missing_keys}")
            
            return func(self, *args, **kwargs)
        return wrapper
    return decorator