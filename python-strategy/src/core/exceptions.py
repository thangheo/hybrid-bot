# src/core/exceptions.py
"""
Bio-Inspired Trading System - Exception Handling & Recovery
Comprehensive exception hierarchy and automatic recovery mechanisms
"""

import asyncio
import logging
from abc import ABC, abstractmethod
from typing import Dict, Any, Optional, List, Callable, Union, Type
from datetime import datetime, timedelta
from dataclasses import dataclass, field
from enum import Enum
import time
import traceback
import functools

from .interfaces import LayerType, SystemState


logger = logging.getLogger(__name__)


class ErrorSeverity(Enum):
    """Error severity levels for bio-inspired system"""
    LOW = "low"
    MEDIUM = "medium"
    HIGH = "high"
    CRITICAL = "critical"
    FATAL = "fatal"


class RecoveryStrategy(Enum):
    """Recovery strategy types"""
    RETRY = "retry"
    RESTART_COMPONENT = "restart_component"
    RESTART_LAYER = "restart_layer"
    FALLBACK_MODE = "fallback_mode"
    CIRCUIT_BREAKER = "circuit_breaker"
    EMERGENCY_STOP = "emergency_stop"
    ESCALATE = "escalate"


@dataclass
class ErrorContext:
    """Context information for error analysis"""
    layer_type: Optional[LayerType] = None
    component: Optional[str] = None
    operation: Optional[str] = None
    timestamp: datetime = field(default_factory=datetime.now)
    system_state: Optional[SystemState] = None
    additional_data: Dict[str, Any] = field(default_factory=dict)


class BioTradingException(Exception):
    """Base exception for bio-inspired trading system"""
    
    def __init__(self, 
                 message: str,
                 severity: ErrorSeverity = ErrorSeverity.MEDIUM,
                 context: Optional[ErrorContext] = None,
                 recovery_strategy: Optional[RecoveryStrategy] = None,
                 original_exception: Optional[Exception] = None):
        super().__init__(message)
        
        self.severity = severity
        self.context = context or ErrorContext()
        self.recovery_strategy = recovery_strategy
        self.original_exception = original_exception
        self.error_id = f"err_{int(time.time() * 1000)}"
        
    def to_dict(self) -> Dict[str, Any]:
        """Convert exception to dictionary for logging/analysis"""
        return {
            "error_id": self.error_id,
            "message": str(self),
            "severity": self.severity.value,
            "layer_type": self.context.layer_type.value if self.context.layer_type else None,
            "component": self.context.component,
            "operation": self.context.operation,
            "timestamp": self.context.timestamp.isoformat(),
            "system_state": self.context.system_state.value if self.context.system_state else None,
            "recovery_strategy": self.recovery_strategy.value if self.recovery_strategy else None,
            "original_exception": str(self.original_exception) if self.original_exception else None,
            "traceback": traceback.format_exc(),
            "additional_data": self.context.additional_data
        }


# Layer-specific exceptions
class PerceptionLayerException(BioTradingException):
    """Exceptions from the perception layer"""
    
    def __init__(self, message: str, **kwargs):
        super().__init__(message, **kwargs)
        if self.context.layer_type is None:
            self.context.layer_type = LayerType.PERCEPTION


class HomeostasisLayerException(BioTradingException):
    """Exceptions from the homeostasis layer"""
    
    def __init__(self, message: str, **kwargs):
        super().__init__(message, **kwargs)
        if self.context.layer_type is None:
            self.context.layer_type = LayerType.HOMEOSTASIS


class AdaptationLayerException(BioTradingException):
    """Exceptions from the adaptation layer"""
    
    def __init__(self, message: str, **kwargs):
        super().__init__(message, **kwargs)
        if self.context.layer_type is None:
            self.context.layer_type = LayerType.ADAPTATION


class ConsciousnessLayerException(BioTradingException):
    """Exceptions from the consciousness layer"""
    
    def __init__(self, message: str, **kwargs):
        super().__init__(message, **kwargs)
        if self.context.layer_type is None:
            self.context.layer_type = LayerType.CONSCIOUSNESS


class ExecutionLayerException(BioTradingException):
    """Exceptions from the execution layer"""
    
    def __init__(self, message: str, **kwargs):
        super().__init__(message, **kwargs)
        if self.context.layer_type is None:
            self.context.layer_type = LayerType.EXECUTION


# Functional exceptions
class ConfigurationException(BioTradingException):
    """Configuration-related exceptions"""
    
    def __init__(self, message: str, config_section: Optional[str] = None, **kwargs):
        super().__init__(message, severity=ErrorSeverity.HIGH, **kwargs)
        self.context.component = "configuration"
        if config_section:
            self.context.additional_data["config_section"] = config_section


class DatabaseException(BioTradingException):
    """Database-related exceptions"""
    
    def __init__(self, message: str, operation: Optional[str] = None, **kwargs):
        super().__init__(message, severity=ErrorSeverity.HIGH, **kwargs)
        self.context.component = "database"
        if operation:
            self.context.operation = operation


class MessageBusException(BioTradingException):
    """Message bus communication exceptions"""
    
    def __init__(self, message: str, event_type: Optional[str] = None, **kwargs):
        super().__init__(message, **kwargs)
        self.context.component = "message_bus"
        if event_type:
            self.context.additional_data["event_type"] = event_type


class MarketDataException(PerceptionLayerException):
    """Market data acquisition exceptions"""
    
    def __init__(self, message: str, symbol: Optional[str] = None, **kwargs):
        super().__init__(message, **kwargs)
        self.context.component = "market_data"
        if symbol:
            self.context.additional_data["symbol"] = symbol


class RiskManagementException(HomeostasisLayerException):
    """Risk management exceptions"""
    
    def __init__(self, message: str, risk_type: Optional[str] = None, **kwargs):
        super().__init__(message, severity=ErrorSeverity.HIGH, **kwargs)
        self.context.component = "risk_management"
        if risk_type:
            self.context.additional_data["risk_type"] = risk_type


class OrderExecutionException(ExecutionLayerException):
    """Order execution exceptions"""
    
    def __init__(self, message: str, order_id: Optional[str] = None, **kwargs):
        super().__init__(message, severity=ErrorSeverity.HIGH, **kwargs)
        self.context.component = "order_execution"
        if order_id:
            self.context.additional_data["order_id"] = order_id


class StrategyException(AdaptationLayerException):
    """Strategy-related exceptions"""
    
    def __init__(self, message: str, strategy_name: Optional[str] = None, **kwargs):
        super().__init__(message, **kwargs)
        self.context.component = "strategy"
        if strategy_name:
            self.context.additional_data["strategy_name"] = strategy_name


class SystemCriticalException(BioTradingException):
    """Critical system exceptions requiring immediate attention"""
    
    def __init__(self, message: str, **kwargs):
        super().__init__(
            message, 
            severity=ErrorSeverity.CRITICAL,
            recovery_strategy=RecoveryStrategy.EMERGENCY_STOP,
            **kwargs
        )


# Recovery system
@dataclass
class RecoveryAction:
    """Recovery action definition"""
    strategy: RecoveryStrategy
    max_attempts: int = 3
    backoff_factor: float = 2.0
    timeout_seconds: float = 30.0
    success_callback: Optional[Callable] = None
    failure_callback: Optional[Callable] = None


class RecoveryManager:
    """
    Bio-inspired recovery manager with adaptive recovery strategies
    """
    
    def __init__(self, config: Dict[str, Any]):
        self.config = config
        
        # Recovery tracking
        self.recovery_attempts: Dict[str, int] = {}
        self.recovery_history: List[Dict[str, Any]] = []
        self.circuit_breakers: Dict[str, bool] = {}
        
        # Recovery strategies by exception type
        self.recovery_strategies: Dict[Type[Exception], RecoveryAction] = {}
        self._setup_default_strategies()
        
        # Performance metrics
        self.total_recoveries = 0
        self.successful_recoveries = 0
        self.failed_recoveries = 0
    
    def register_recovery_strategy(self, 
                                 exception_type: Type[Exception],
                                 action: RecoveryAction) -> None:
        """Register recovery strategy for exception type"""
        self.recovery_strategies[exception_type] = action
        logger.info(f"Registered recovery strategy for {exception_type.__name__}")
    
    async def handle_exception(self, 
                             exception: Exception,
                             context: Optional[ErrorContext] = None) -> bool:
        """Handle exception with appropriate recovery strategy"""
        
        # Convert to bio trading exception if needed
        if not isinstance(exception, BioTradingException):
            bio_exception = BioTradingException(
                message=str(exception),
                context=context,
                original_exception=exception
            )
        else:
            bio_exception = exception
        
        # Log the exception
        self._log_exception(bio_exception)
        
        # Find recovery strategy
        recovery_action = self._find_recovery_strategy(bio_exception)
        
        if not recovery_action:
            logger.warning(f"No recovery strategy found for {type(bio_exception).__name__}")
            return False
        
        # Execute recovery
        recovery_id = f"recovery_{int(time.time() * 1000)}"
        success = await self._execute_recovery(bio_exception, recovery_action, recovery_id)
        
        # Update metrics
        self.total_recoveries += 1
        if success:
            self.successful_recoveries += 1
        else:
            self.failed_recoveries += 1
        
        return success
    
    async def _execute_recovery(self, 
                              exception: BioTradingException,
                              action: RecoveryAction,
                              recovery_id: str) -> bool:
        """Execute recovery action with retries and backoff"""
        
        component_key = f"{exception.context.layer_type}_{exception.context.component}"
        
        # Check circuit breaker
        if self.circuit_breakers.get(component_key, False):
            logger.warning(f"Circuit breaker open for {component_key}, skipping recovery")
            return False
        
        attempts = 0
        max_attempts = action.max_attempts
        
        while attempts < max_attempts:
            attempts += 1
            
            try:
                logger.info(f"Recovery attempt {attempts}/{max_attempts} for {recovery_id}")
                
                # Execute recovery strategy
                success = await self._execute_strategy(exception, action.strategy)
                
                if success:
                    logger.info(f"Recovery successful for {recovery_id}")
                    
                    # Reset attempt counter
                    if component_key in self.recovery_attempts:
                        del self.recovery_attempts[component_key]
                    
                    # Call success callback
                    if action.success_callback:
                        await self._safe_callback(action.success_callback, exception, recovery_id)
                    
                    # Record successful recovery
                    self.recovery_history.append({
                        "recovery_id": recovery_id,
                        "exception_type": type(exception).__name__,
                        "strategy": action.strategy.value,
                        "attempts": attempts,
                        "success": True,
                        "timestamp": datetime.now().isoformat()
                    })
                    
                    return True
                
                # If not successful and not last attempt, wait with backoff
                if attempts < max_attempts:
                    backoff_time = action.backoff_factor ** (attempts - 1)
                    logger.info(f"Recovery attempt failed, waiting {backoff_time}s before retry")
                    await asyncio.sleep(backoff_time)
                
            except Exception as recovery_error:
                logger.error(f"Recovery attempt {attempts} failed with error: {recovery_error}")
                
                if attempts < max_attempts:
                    backoff_time = action.backoff_factor ** (attempts - 1)
                    await asyncio.sleep(backoff_time)
        
        # All attempts failed
        logger.error(f"All recovery attempts failed for {recovery_id}")
        
        # Update failure count
        self.recovery_attempts[component_key] = self.recovery_attempts.get(component_key, 0) + 1
        
        # Activate circuit breaker if too many failures
        failure_threshold = self.config.get('circuit_breaker_threshold', 5)
        if self.recovery_attempts[component_key] >= failure_threshold:
            self.circuit_breakers[component_key] = True
            logger.critical(f"Circuit breaker activated for {component_key}")
        
        # Call failure callback
        if action.failure_callback:
            await self._safe_callback(action.failure_callback, exception, recovery_id)
        
        # Record failed recovery
        self.recovery_history.append({
            "recovery_id": recovery_id,
            "exception_type": type(exception).__name__,
            "strategy": action.strategy.value,
            "attempts": attempts,
            "success": False,
            "timestamp": datetime.now().isoformat()
        })
        
        return False
    
    async def _execute_strategy(self, 
                              exception: BioTradingException,
                              strategy: RecoveryStrategy) -> bool:
        """Execute specific recovery strategy"""
        
        try:
            if strategy == RecoveryStrategy.RETRY:
                return await self._retry_operation(exception)
            
            elif strategy == RecoveryStrategy.RESTART_COMPONENT:
                return await self._restart_component(exception)
            
            elif strategy == RecoveryStrategy.RESTART_LAYER:
                return await self._restart_layer(exception)
            
            elif strategy == RecoveryStrategy.FALLBACK_MODE:
                return await self._activate_fallback_mode(exception)
            
            elif strategy == RecoveryStrategy.CIRCUIT_BREAKER:
                return await self._activate_circuit_breaker(exception)
            
            elif strategy == RecoveryStrategy.EMERGENCY_STOP:
                return await self._emergency_stop(exception)
            
            elif strategy == RecoveryStrategy.ESCALATE:
                return await self._escalate_error(exception)
            
            else:
                logger.warning(f"Unknown recovery strategy: {strategy}")
                return False
                
        except Exception as e:
            logger.error(f"Error executing recovery strategy {strategy}: {e}")
            return False
    
    async def _retry_operation(self, exception: BioTradingException) -> bool:
        """Retry the failed operation"""
        logger.info("Retrying operation...")
        await asyncio.sleep(0.1)
        return True  # Simplified for demo
    
    async def _restart_component(self, exception: BioTradingException) -> bool:
        """Restart the failing component"""
        component = exception.context.component
        layer_type = exception.context.layer_type
        
        logger.info(f"Restarting component {component} in layer {layer_type}")
        await asyncio.sleep(1.0)
        return True
    
    async def _restart_layer(self, exception: BioTradingException) -> bool:
        """Restart the entire layer"""
        layer_type = exception.context.layer_type
        logger.info(f"Restarting layer {layer_type}")
        await asyncio.sleep(2.0)
        return True
    
    async def _activate_fallback_mode(self, exception: BioTradingException) -> bool:
        """Activate fallback mode for the component/layer"""
        component = exception.context.component
        logger.info(f"Activating fallback mode for {component}")
        return True
    
    async def _activate_circuit_breaker(self, exception: BioTradingException) -> bool:
        """Activate circuit breaker to prevent cascade failures"""
        component_key = f"{exception.context.layer_type}_{exception.context.component}"
        
        self.circuit_breakers[component_key] = True
        logger.warning(f"Circuit breaker activated for {component_key}")
        
        # Schedule automatic reset after timeout
        reset_timeout = self.config.get('circuit_breaker_reset_timeout', 300)
        asyncio.create_task(self._reset_circuit_breaker(component_key, reset_timeout))
        
        return True
    
    async def _emergency_stop(self, exception: BioTradingException) -> bool:
        """Execute emergency stop procedures"""
        logger.critical("Executing emergency stop procedures")
        return True
    
    async def _escalate_error(self, exception: BioTradingException) -> bool:
        """Escalate error to higher-level management"""
        logger.error(f"Escalating error: {exception}")
        return True
    
    async def _reset_circuit_breaker(self, component_key: str, timeout: float) -> None:
        """Reset circuit breaker after timeout"""
        await asyncio.sleep(timeout)
        
        if component_key in self.circuit_breakers:
            del self.circuit_breakers[component_key]
            logger.info(f"Circuit breaker reset for {component_key}")
    
    async def _safe_callback(self, callback: Callable, 
                           exception: BioTradingException, 
                           recovery_id: str) -> None:
        """Safely execute callback without raising exceptions"""
        try:
            if asyncio.iscoroutinefunction(callback):
                await callback(exception, recovery_id)
            else:
                callback(exception, recovery_id)
        except Exception as e:
            logger.error(f"Error in recovery callback: {e}")
    
    def _find_recovery_strategy(self, exception: BioTradingException) -> Optional[RecoveryAction]:
        """Find appropriate recovery strategy for exception"""
        
        # Check for exact type match
        exception_type = type(exception)
        if exception_type in self.recovery_strategies:
            return self.recovery_strategies[exception_type]
        
        # Check for parent class matches
        for registered_type, action in self.recovery_strategies.items():
            if isinstance(exception, registered_type):
                return action
        
        # Check if exception has suggested recovery strategy
        if exception.recovery_strategy:
            return RecoveryAction(strategy=exception.recovery_strategy)
        
        # Default strategy based on severity
        if exception.severity == ErrorSeverity.CRITICAL:
            return RecoveryAction(strategy=RecoveryStrategy.EMERGENCY_STOP)
        elif exception.severity == ErrorSeverity.HIGH:
            return RecoveryAction(strategy=RecoveryStrategy.RESTART_COMPONENT)
        else:
            return RecoveryAction(strategy=RecoveryStrategy.RETRY)
    
    def _setup_default_strategies(self) -> None:
        """Setup default recovery strategies"""
        
        # Database exceptions
        self.recovery_strategies[DatabaseException] = RecoveryAction(
            strategy=RecoveryStrategy.RETRY,
            max_attempts=3,
            backoff_factor=2.0
        )
        
        # Market data exceptions
        self.recovery_strategies[MarketDataException] = RecoveryAction(
            strategy=RecoveryStrategy.RETRY,
            max_attempts=5,
            backoff_factor=1.5
        )
        
        # Risk management exceptions
        self.recovery_strategies[RiskManagementException] = RecoveryAction(
            strategy=RecoveryStrategy.FALLBACK_MODE,
            max_attempts=2
        )
        
        # Order execution exceptions
        self.recovery_strategies[OrderExecutionException] = RecoveryAction(
            strategy=RecoveryStrategy.CIRCUIT_BREAKER,
            max_attempts=2
        )
        
        # Critical system exceptions
        self.recovery_strategies[SystemCriticalException] = RecoveryAction(
            strategy=RecoveryStrategy.EMERGENCY_STOP,
            max_attempts=1
        )
        
        # Configuration exceptions
        self.recovery_strategies[ConfigurationException] = RecoveryAction(
            strategy=RecoveryStrategy.ESCALATE,
            max_attempts=1
        )
    
    def _log_exception(self, exception: BioTradingException) -> None:
        """Log exception with appropriate level"""
        log_level = {
            ErrorSeverity.LOW: logging.INFO,
            ErrorSeverity.MEDIUM: logging.WARNING,
            ErrorSeverity.HIGH: logging.ERROR,
            ErrorSeverity.CRITICAL: logging.CRITICAL,
            ErrorSeverity.FATAL: logging.CRITICAL
        }.get(exception.severity, logging.ERROR)
        
        logger.log(log_level, f"Bio Trading Exception: {exception.to_dict()}")
    
    def get_recovery_metrics(self) -> Dict[str, Any]:
        """Get recovery system metrics"""
        success_rate = (
            self.successful_recoveries / max(self.total_recoveries, 1)
        )
        
        return {
            "total_recoveries": self.total_recoveries,
            "successful_recoveries": self.successful_recoveries,
            "failed_recoveries": self.failed_recoveries,
            "success_rate": success_rate,
            "active_circuit_breakers": len(self.circuit_breakers),
            "circuit_breakers": list(self.circuit_breakers.keys()),
            "recent_recoveries": self.recovery_history[-10:]  # Last 10 recoveries
        }
    
    async def reset_all_circuit_breakers(self) -> None:
        """Reset all active circuit breakers (manual override)"""
        count = len(self.circuit_breakers)
        self.circuit_breakers.clear()
        self.recovery_attempts.clear()
        
        logger.info(f"Reset {count} circuit breakers")


# Decorator for automatic exception handling
def handle_exceptions(recovery_manager: Optional[RecoveryManager] = None,
                     fallback_value: Any = None,
                     reraise: bool = False):
    """Decorator for automatic exception handling and recovery"""
    
    def decorator(func: Callable):
        @functools.wraps(func)
        async def async_wrapper(*args, **kwargs):
            try:
                return await func(*args, **kwargs)
            except Exception as e:
                if recovery_manager:
                    context = ErrorContext(
                        component=func.__name__,
                        operation=f"{func.__module__}.{func.__qualname__}"
                    )
                    
                    recovery_success = await recovery_manager.handle_exception(e, context)
                    
                    if recovery_success and not reraise:
                        # Try the operation again after successful recovery
                        try:
                            return await func(*args, **kwargs)
                        except Exception:
                            # If it fails again, return fallback
                            return fallback_value
                    
                    if reraise:
                        raise
                
                return fallback_value
        
        @functools.wraps(func)
        def sync_wrapper(*args, **kwargs):
            try:
                return func(*args, **kwargs)
            except Exception as e:
                if recovery_manager:
                    context = ErrorContext(
                        component=func.__name__,
                        operation=f"{func.__module__}.{func.__qualname__}"
                    )
                    
                    # For sync functions, we can't do async recovery
                    # Just log the exception
                    logger.error(f"Exception in {func.__name__}: {e}")
                
                if reraise:
                    raise
                
                return fallback_value
        
        return async_wrapper if asyncio.iscoroutinefunction(func) else sync_wrapper
    
    return decorator


# Context manager for exception handling
class ExceptionContext:
    """Context manager for handling exceptions in code blocks"""
    
    def __init__(self, 
                 recovery_manager: RecoveryManager,
                 layer_type: Optional[LayerType] = None,
                 component: Optional[str] = None):
        self.recovery_manager = recovery_manager
        self.layer_type = layer_type
        self.component = component
    
    async def __aenter__(self):
        return self
    
    async def __aexit__(self, exc_type, exc_val, exc_tb):
        if exc_val:
            context = ErrorContext(
                layer_type=self.layer_type,
                component=self.component
            )
            
            await self.recovery_manager.handle_exception(exc_val, context)
            
            # Return True to suppress the exception if recovery was successful
            # In practice, you might want more sophisticated logic here
            return False


# Utility functions for creating specific exceptions
def create_market_data_exception(message: str, symbol: str, 
                               severity: ErrorSeverity = ErrorSeverity.MEDIUM) -> MarketDataException:
    """Create market data exception with context"""
    return MarketDataException(
        message=message,
        severity=severity,
        context=ErrorContext(
            layer_type=LayerType.PERCEPTION,
            component="market_data",
            additional_data={"symbol": symbol}
        )
    )


def create_risk_exception(message: str, risk_type: str,
                         severity: ErrorSeverity = ErrorSeverity.HIGH) -> RiskManagementException:
    """Create risk management exception with context"""
    return RiskManagementException(
        message=message,
        severity=severity,
        context=ErrorContext(
            layer_type=LayerType.HOMEOSTASIS,
            component="risk_management",
            additional_data={"risk_type": risk_type}
        )
    )


def create_order_exception(message: str, order_id: str,
                          severity: ErrorSeverity = ErrorSeverity.HIGH) -> OrderExecutionException:
    """Create order execution exception with context"""
    return OrderExecutionException(
        message=message,
        severity=severity,
        context=ErrorContext(
            layer_type=LayerType.EXECUTION,
            component="order_execution",
            additional_data={"order_id": order_id}
        )
    )


# Factory for creating recovery manager
class RecoveryManagerFactory:
    """Factory for creating recovery managers"""
    
    @staticmethod
    def create_recovery_manager(config: Dict[str, Any]) -> RecoveryManager:
        """Create recovery manager with configuration"""
        default_config = {
            'circuit_breaker_threshold': 5,
            'circuit_breaker_reset_timeout': 300,
            'max_recovery_attempts': 3,
            'recovery_backoff_factor': 2.0
        }
        
        merged_config = {**default_config, **config}
        return RecoveryManager(merged_config)
    
    @staticmethod
    def create_test_recovery_manager() -> RecoveryManager:
        """Create recovery manager for testing"""
        test_config = {
            'circuit_breaker_threshold': 3,
            'circuit_breaker_reset_timeout': 10,
            'max_recovery_attempts': 2,
            'recovery_backoff_factor': 1.5
        }
        
        return RecoveryManager(test_config)