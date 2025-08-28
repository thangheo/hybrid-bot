# src/core/events.py
"""
Bio-Inspired Trading System - Event System
Event-driven communication system inspired by neural networks
"""

from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Callable, Set
from enum import Enum
import uuid
import asyncio
from datetime import datetime
import json
import logging
from .interfaces import EventPriority, LayerType, SystemState, MarketRegime


logger = logging.getLogger(__name__)


class EventType(Enum):
    """Event types for the bio-inspired system"""
    # System-level events
    SYSTEM_STARTUP = "system.startup"
    SYSTEM_SHUTDOWN = "system.shutdown"
    SYSTEM_HEALTH_CHANGE = "system.health_change"
    SYSTEM_ERROR = "system.error"
    
    # Market perception events
    MARKET_REGIME_CHANGE = "market.regime_change"
    MARKET_DATA_UPDATE = "market.data_update"
    MARKET_ANOMALY_DETECTED = "market.anomaly_detected"
    PRICE_ALERT = "market.price_alert"
    
    # Trading events
    SIGNAL_GENERATED = "trading.signal_generated"
    TRADE_EXECUTED = "trading.trade_executed"
    POSITION_OPENED = "trading.position_opened"
    POSITION_CLOSED = "trading.position_closed"
    
    # Risk management events
    RISK_LIMIT_BREACH = "risk.limit_breach"
    STOP_LOSS_TRIGGERED = "risk.stop_loss_triggered"
    DRAWDOWN_WARNING = "risk.drawdown_warning"
    EMERGENCY_STOP = "risk.emergency_stop"
    
    # Adaptation events
    STRATEGY_OPTIMIZED = "adaptation.strategy_optimized"
    PERFORMANCE_UPDATE = "adaptation.performance_update"
    LEARNING_CYCLE_COMPLETE = "adaptation.learning_cycle_complete"
    PARAMETER_UPDATED = "adaptation.parameter_updated"
    
    # Layer communication events
    LAYER_READY = "layer.ready"
    LAYER_ERROR = "layer.error"
    LAYER_RECOVERY = "layer.recovery"
    INTER_LAYER_SYNC = "layer.sync"
    
    # Custom events
    CUSTOM = "custom"


@dataclass
class Event:
    """Base event class for bio-inspired system communication"""
    event_id: str = field(default_factory=lambda: str(uuid.uuid4()))
    event_type: EventType = EventType.CUSTOM
    priority: EventPriority = EventPriority.NORMAL
    timestamp: datetime = field(default_factory=datetime.now)
    source_layer: Optional[LayerType] = None
    target_layers: Set[LayerType] = field(default_factory=set)
    data: Any = None
    metadata: Dict[str, Any] = field(default_factory=dict)
    correlation_id: Optional[str] = None  # For tracing related events
    ttl_seconds: int = 300  # Time to live for event processing
    
    def __post_init__(self):
        """Validate event after initialization"""
        if self.correlation_id is None:
            self.correlation_id = self.event_id
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert event to dictionary for serialization"""
        return {
            "event_id": self.event_id,
            "event_type": self.event_type.value,
            "priority": self.priority.value,
            "timestamp": self.timestamp.isoformat(),
            "source_layer": self.source_layer.value if self.source_layer else None,
            "target_layers": [layer.value for layer in self.target_layers],
            "data": self.data,
            "metadata": self.metadata,
            "correlation_id": self.correlation_id,
            "ttl_seconds": self.ttl_seconds
        }
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> 'Event':
        """Create event from dictionary"""
        return cls(
            event_id=data["event_id"],
            event_type=EventType(data["event_type"]),
            priority=EventPriority(data["priority"]),
            timestamp=datetime.fromisoformat(data["timestamp"]),
            source_layer=LayerType(data["source_layer"]) if data["source_layer"] else None,
            target_layers={LayerType(layer) for layer in data["target_layers"]},
            data=data["data"],
            metadata=data["metadata"],
            correlation_id=data["correlation_id"],
            ttl_seconds=data["ttl_seconds"]
        )
    
    def is_expired(self) -> bool:
        """Check if event has expired"""
        elapsed = (datetime.now() - self.timestamp).total_seconds()
        return elapsed > self.ttl_seconds
    
    def should_route_to(self, layer_type: LayerType) -> bool:
        """Check if event should be routed to specific layer"""
        if not self.target_layers:
            return True  # Broadcast to all layers
        return layer_type in self.target_layers


# Specialized event classes for different system functions

@dataclass
class SystemHealthChangeEvent(Event):
    """Event for system health state changes"""
    def __init__(self, old_state: SystemState, new_state: SystemState, 
                 health_score: float, details: Dict[str, Any]):
        super().__init__(
            event_type=EventType.SYSTEM_HEALTH_CHANGE,
            priority=self._determine_priority(new_state),
            data={
                "old_state": old_state.value,
                "new_state": new_state.value,
                "health_score": health_score,
                "details": details
            }
        )
    
    @staticmethod
    def _determine_priority(state: SystemState) -> EventPriority:
        """Determine event priority based on system state"""
        priority_map = {
            SystemState.HEALTHY: EventPriority.LOW,
            SystemState.WARNING: EventPriority.NORMAL,
            SystemState.CRITICAL: EventPriority.HIGH,
            SystemState.EMERGENCY: EventPriority.EMERGENCY,
            SystemState.RECOVERING: EventPriority.HIGH
        }
        return priority_map.get(state, EventPriority.NORMAL)


@dataclass
class MarketRegimeChangeEvent(Event):
    """Event for market regime changes"""
    def __init__(self, symbol: str, old_regime: MarketRegime, 
                 new_regime: MarketRegime, confidence: float):
        super().__init__(
            event_type=EventType.MARKET_REGIME_CHANGE,
            priority=EventPriority.HIGH,
            source_layer=LayerType.PERCEPTION,
            data={
                "symbol": symbol,
                "old_regime": old_regime.value,
                "new_regime": new_regime.value,
                "confidence": confidence
            }
        )


@dataclass
class TradingSignalEvent(Event):
    """Event for trading signal generation"""
    def __init__(self, signal_data: Dict[str, Any]):
        super().__init__(
            event_type=EventType.SIGNAL_GENERATED,
            priority=EventPriority.HIGH,
            source_layer=LayerType.ADAPTATION,
            target_layers={LayerType.HOMEOSTASIS, LayerType.EXECUTION},
            data=signal_data
        )


@dataclass
class EmergencyStopEvent(Event):
    """Event for emergency stop conditions"""
    def __init__(self, reason: str, triggered_by: LayerType, 
                 severity: int, immediate_actions: List[str]):
        super().__init__(
            event_type=EventType.EMERGENCY_STOP,
            priority=EventPriority.EMERGENCY,
            source_layer=triggered_by,
            data={
                "reason": reason,
                "severity": severity,
                "immediate_actions": immediate_actions,
                "timestamp": datetime.now().isoformat()
            },
            ttl_seconds=60  # Emergency events expire quickly
        )


@dataclass
class PerformanceUpdateEvent(Event):
    """Event for performance metrics updates"""
    def __init__(self, metrics: Dict[str, Any], period: str):
        super().__init__(
            event_type=EventType.PERFORMANCE_UPDATE,
            priority=EventPriority.NORMAL,
            source_layer=LayerType.ADAPTATION,
            data={
                "metrics": metrics,
                "period": period,
                "update_time": datetime.now().isoformat()
            }
        )


class EventFilter:
    """Filter events based on various criteria"""
    
    def __init__(self):
        self.filters: List[Callable[[Event], bool]] = []
    
    def add_filter(self, filter_func: Callable[[Event], bool]) -> None:
        """Add event filter function"""
        self.filters.append(filter_func)
    
    def should_process(self, event: Event) -> bool:
        """Check if event should be processed based on filters"""
        return all(filter_func(event) for filter_func in self.filters)
    
    @staticmethod
    def priority_filter(min_priority: EventPriority) -> Callable[[Event], bool]:
        """Create filter for minimum priority level"""
        def filter_func(event: Event) -> bool:
            return event.priority.value >= min_priority.value
        return filter_func
    
    @staticmethod
    def layer_filter(allowed_layers: Set[LayerType]) -> Callable[[Event], bool]:
        """Create filter for specific source layers"""
        def filter_func(event: Event) -> bool:
            return event.source_layer in allowed_layers
        return filter_func
    
    @staticmethod
    def event_type_filter(allowed_types: Set[EventType]) -> Callable[[Event], bool]:
        """Create filter for specific event types"""
        def filter_func(event: Event) -> bool:
            return event.event_type in allowed_types
        return filter_func


class EventMetrics:
    """Track event system performance metrics"""
    
    def __init__(self):
        self.events_processed = 0
        self.events_failed = 0
        self.events_dropped = 0
        self.avg_processing_time = 0.0
        self.queue_sizes: Dict[str, int] = {}
        self.handler_performance: Dict[str, float] = {}
        self.start_time = datetime.now()
    
    def record_event_processed(self, processing_time: float) -> None:
        """Record successful event processing"""
        self.events_processed += 1
        # Update rolling average
        alpha = 0.1  # Smoothing factor
        self.avg_processing_time = (
            alpha * processing_time + 
            (1 - alpha) * self.avg_processing_time
        )
    
    def record_event_failed(self) -> None:
        """Record failed event processing"""
        self.events_failed += 1
    
    def record_event_dropped(self) -> None:
        """Record dropped event"""
        self.events_dropped += 1
    
    def get_metrics(self) -> Dict[str, Any]:
        """Get current metrics snapshot"""
        uptime = (datetime.now() - self.start_time).total_seconds()
        total_events = self.events_processed + self.events_failed + self.events_dropped
        
        return {
            "uptime_seconds": uptime,
            "events_processed": self.events_processed,
            "events_failed": self.events_failed,
            "events_dropped": self.events_dropped,
            "total_events": total_events,
            "success_rate": self.events_processed / max(total_events, 1),
            "avg_processing_time_ms": self.avg_processing_time * 1000,
            "throughput_events_per_second": self.events_processed / max(uptime, 1),
            "queue_sizes": self.queue_sizes.copy(),
            "handler_performance": self.handler_performance.copy()
        }


class EventValidator:
    """Validate events for security and integrity"""
    
    @staticmethod
    def validate_event(event: Event) -> bool:
        """Validate event structure and content"""
        try:
            # Basic structure validation
            if not event.event_id or not event.timestamp:
                return False
            
            # Check if event is expired
            if event.is_expired():
                logger.warning(f"Event {event.event_id} has expired")
                return False
            
            # Validate data size (prevent memory issues)
            if event.data and len(str(event.data)) > 1024 * 1024:  # 1MB limit
                logger.warning(f"Event {event.event_id} data size exceeds limit")
                return False
            
            return True
            
        except Exception as e:
            logger.error(f"Event validation failed: {e}")
            return False
    
    @staticmethod
    def sanitize_event_data(data: Any) -> Any:
        """Sanitize event data to prevent injection attacks"""
        if isinstance(data, dict):
            return {k: EventValidator.sanitize_event_data(v) for k, v in data.items()}
        elif isinstance(data, list):
            return [EventValidator.sanitize_event_data(item) for item in data]
        elif isinstance(data, str):
            # Basic string sanitization
            return data.replace('<', '&lt;').replace('>', '&gt;')
        else:
            return data


# Event factory functions for common events
def create_system_startup_event(system_config: Dict[str, Any]) -> Event:
    """Create system startup event"""
    return Event(
        event_type=EventType.SYSTEM_STARTUP,
        priority=EventPriority.HIGH,
        data={"config": system_config, "startup_time": datetime.now().isoformat()}
    )


def create_layer_ready_event(layer_type: LayerType, health_status: Dict[str, Any]) -> Event:
    """Create layer ready event"""
    return Event(
        event_type=EventType.LAYER_READY,
        priority=EventPriority.NORMAL,
        source_layer=layer_type,
        data={"layer": layer_type.value, "health": health_status}
    )


def create_market_data_update_event(symbol: str, data: Dict[str, Any]) -> Event:
    """Create market data update event"""
    return Event(
        event_type=EventType.MARKET_DATA_UPDATE,
        priority=EventPriority.NORMAL,
        source_layer=LayerType.PERCEPTION,
        data={"symbol": symbol, "market_data": data}
    )


def create_error_event(error: Exception, layer: LayerType, 
                      context: Dict[str, Any]) -> Event:
    """Create error event"""
    return Event(
        event_type=EventType.SYSTEM_ERROR,
        priority=EventPriority.HIGH,
        source_layer=layer,
        data={
            "error_type": type(error).__name__,
            "error_message": str(error),
            "context": context,
            "layer": layer.value
        }
    )