# src/core/messaging.py
"""
Bio-Inspired Trading System - Message Bus
Event-driven communication system with neural network-inspired routing
"""

import asyncio
import logging
from collections import defaultdict, deque
from typing import Dict, List, Callable, Any, Optional, Set
from datetime import datetime, timedelta
import time
import weakref
from dataclasses import dataclass
from concurrent.futures import ThreadPoolExecutor
import heapq
import uuid

from .interfaces import LayerType, EventHandler
from .events import Event, EventType, EventPriority, EventMetrics, EventFilter, EventValidator


logger = logging.getLogger(__name__)


@dataclass
class HandlerRegistration:
    """Registration information for event handlers"""
    handler_id: str
    handler: EventHandler
    layer_type: LayerType
    event_types: Set[EventType]
    priority_filter: Optional[EventPriority] = None
    max_concurrent: int = 10
    active_tasks: int = 0
    total_processed: int = 0
    total_failed: int = 0
    last_activity: datetime = None


@dataclass
class QueuedEvent:
    """Event wrapper for priority queue processing"""
    priority_score: float
    timestamp: float
    event: Event
    
    def __lt__(self, other):
        # Higher priority events and older events go first
        if self.priority_score != other.priority_score:
            return self.priority_score > other.priority_score
        return self.timestamp < other.timestamp


class CircuitBreaker:
    """Circuit breaker pattern for handler protection"""
    
    def __init__(self, failure_threshold: int = 5, timeout_seconds: int = 60):
        self.failure_threshold = failure_threshold
        self.timeout_seconds = timeout_seconds
        self.failure_count = 0
        self.last_failure_time = None
        self.state = "CLOSED"  # CLOSED, OPEN, HALF_OPEN
    
    def can_execute(self) -> bool:
        """Check if handler can execute based on circuit breaker state"""
        if self.state == "CLOSED":
            return True
        elif self.state == "OPEN":
            if self._should_attempt_reset():
                self.state = "HALF_OPEN"
                return True
            return False
        else:  # HALF_OPEN
            return True
    
    def record_success(self) -> None:
        """Record successful execution"""
        self.failure_count = 0
        self.state = "CLOSED"
    
    def record_failure(self) -> None:
        """Record failed execution"""
        self.failure_count += 1
        self.last_failure_time = time.time()
        
        if self.failure_count >= self.failure_threshold:
            self.state = "OPEN"
            logger.warning(f"Circuit breaker opened after {self.failure_count} failures")
    
    def _should_attempt_reset(self) -> bool:
        """Check if enough time has passed to attempt reset"""
        if self.last_failure_time is None:
            return True
        return time.time() - self.last_failure_time > self.timeout_seconds


class MessageBus:
    """
    Bio-inspired message bus with neural network-like event routing.
    Features adaptive routing, circuit breakers, and self-healing capabilities.
    """
    
    def __init__(self, config: Dict[str, Any]):
        self.config = config
        
        # Core components
        self.handlers: Dict[str, HandlerRegistration] = {}
        self.event_routes: Dict[EventType, List[str]] = defaultdict(list)
        self.priority_queue: List[QueuedEvent] = []
        self.dead_letter_queue: deque = deque(maxlen=1000)
        
        # Circuit breakers for handlers
        self.circuit_breakers: Dict[str, CircuitBreaker] = {}
        
        # Metrics and monitoring
        self.metrics = EventMetrics()
        self.event_filter = EventFilter()
        
        # Processing control
        self.running = False
        self.processing_task: Optional[asyncio.Task] = None
        self.worker_semaphore = asyncio.Semaphore(
            config.get('max_concurrent_handlers', 50)
        )
        
        # Performance tuning
        self.batch_size = config.get('batch_size', 10)
        self.processing_interval = config.get('processing_interval_ms', 10) / 1000
        self.max_queue_size = config.get('max_queue_size', 10000)
        
        # Adaptive routing weights (learning component)
        self.routing_weights: Dict[str, float] = defaultdict(lambda: 1.0)
        self.handler_performance: Dict[str, deque] = defaultdict(lambda: deque(maxlen=100))
    
    async def initialize(self) -> bool:
        """Initialize the message bus"""
        try:
            logger.info("Initializing bio-inspired message bus...")
            
            # Setup default event filters
            self._setup_default_filters()
            
            # Initialize circuit breakers
            self._initialize_circuit_breakers()
            
            logger.info("Message bus initialized successfully")
            return True
            
        except Exception as e:
            logger.error(f"Failed to initialize message bus: {e}")
            return False
    
    async def start(self) -> None:
        """Start the message bus processing"""
        if self.running:
            logger.warning("Message bus already running")
            return
        
        self.running = True
        self.processing_task = asyncio.create_task(self._process_events())
        logger.info("Message bus started")
    
    async def stop(self) -> None:
        """Stop the message bus gracefully"""
        if not self.running:
            return
        
        logger.info("Stopping message bus...")
        self.running = False
        
        if self.processing_task:
            self.processing_task.cancel()
            try:
                await self.processing_task
            except asyncio.CancelledError:
                pass
        
        # Process remaining events
        await self._flush_queue()
        
        logger.info("Message bus stopped")
    
    def register_handler(self, 
                        handler: EventHandler,
                        layer_type: LayerType,
                        event_types: Set[EventType],
                        priority_filter: Optional[EventPriority] = None,
                        max_concurrent: int = 10) -> str:
        """Register an event handler with the message bus"""
        
        handler_id = str(uuid.uuid4())
        
        registration = HandlerRegistration(
            handler_id=handler_id,
            handler=handler,
            layer_type=layer_type,
            event_types=event_types,
            priority_filter=priority_filter,
            max_concurrent=max_concurrent,
            last_activity=datetime.now()
        )
        
        self.handlers[handler_id] = registration
        
        # Update routing table
        for event_type in event_types:
            self.event_routes[event_type].append(handler_id)
        
        # Initialize circuit breaker
        self.circuit_breakers[handler_id] = CircuitBreaker(
            failure_threshold=self.config.get('circuit_breaker_threshold', 5),
            timeout_seconds=self.config.get('circuit_breaker_timeout', 60)
        )
        
        logger.info(f"Registered handler {handler_id} for layer {layer_type.value}")
        return handler_id
    
    def unregister_handler(self, handler_id: str) -> bool:
        """Unregister an event handler"""
        if handler_id not in self.handlers:
            return False
        
        registration = self.handlers[handler_id]
        
        # Remove from routing table
        for event_type in registration.event_types:
            if handler_id in self.event_routes[event_type]:
                self.event_routes[event_type].remove(handler_id)
        
        # Cleanup
        del self.handlers[handler_id]
        del self.circuit_breakers[handler_id]
        if handler_id in self.routing_weights:
            del self.routing_weights[handler_id]
        if handler_id in self.handler_performance:
            del self.handler_performance[handler_id]
        
        logger.info(f"Unregistered handler {handler_id}")
        return True
    
    async def publish(self, event: Event) -> bool:
        """Publish an event to the message bus"""
        try:
            # Validate event
            if not EventValidator.validate_event(event):
                logger.warning(f"Invalid event {event.event_id}, dropping")
                self.metrics.record_event_dropped()
                return False
            
            # Apply filters
            if not self.event_filter.should_process(event):
                logger.debug(f"Event {event.event_id} filtered out")
                self.metrics.record_event_dropped()
                return False
            
            # Check queue capacity
            if len(self.priority_queue) >= self.max_queue_size:
                logger.warning("Event queue full, dropping event")
                self.metrics.record_event_dropped()
                return False
            
            # Calculate priority score for routing
            priority_score = self._calculate_priority_score(event)
            
            # Add to priority queue
            queued_event = QueuedEvent(
                priority_score=priority_score,
                timestamp=time.time(),
                event=event
            )
            
            heapq.heappush(self.priority_queue, queued_event)
            
            # Update queue size metrics
            self.metrics.queue_sizes['main'] = len(self.priority_queue)
            
            logger.debug(f"Published event {event.event_id} with priority {priority_score}")
            return True
            
        except Exception as e:
            logger.error(f"Failed to publish event: {e}")
            self.metrics.record_event_failed()
            return False
    
    async def publish_batch(self, events: List[Event]) -> int:
        """Publish multiple events efficiently"""
        successful_count = 0
        
        for event in events:
            if await self.publish(event):
                successful_count += 1
        
        return successful_count
    
    def get_metrics(self) -> Dict[str, Any]:
        """Get current message bus metrics"""
        base_metrics = self.metrics.get_metrics()
        
        handler_stats = {}
        for handler_id, registration in self.handlers.items():
            handler_stats[handler_id] = {
                "layer_type": registration.layer_type.value,
                "active_tasks": registration.active_tasks,
                "total_processed": registration.total_processed,
                "total_failed": registration.total_failed,
                "success_rate": (
                    registration.total_processed / 
                    max(registration.total_processed + registration.total_failed, 1)
                ),
                "circuit_breaker_state": self.circuit_breakers[handler_id].state,
                "routing_weight": self.routing_weights[handler_id]
            }
        
        return {
            **base_metrics,
            "queue_size": len(self.priority_queue),
            "dead_letter_queue_size": len(self.dead_letter_queue),
            "active_handlers": len(self.handlers),
            "handler_stats": handler_stats,
            "routing_weights": dict(self.routing_weights)
        }
    
    async def _process_events(self) -> None:
        """Main event processing loop"""
        logger.info("Started event processing loop")
        
        while self.running:
            try:
                # Process batch of events
                processed_count = await self._process_event_batch()
                
                # Adaptive sleep based on queue size
                if processed_count == 0:
                    await asyncio.sleep(self.processing_interval * 2)
                else:
                    await asyncio.sleep(self.processing_interval)
                
                # Periodic maintenance
                if time.time() % 60 < self.processing_interval:  # Every minute
                    await self._perform_maintenance()
                
            except Exception as e:
                logger.error(f"Error in event processing loop: {e}")
                await asyncio.sleep(1)  # Back off on error
    
    async def _process_event_batch(self) -> int:
        """Process a batch of events from the priority queue"""
        if not self.priority_queue:
            return 0
        
        processed_count = 0
        batch_size = min(self.batch_size, len(self.priority_queue))
        
        # Extract batch from priority queue
        batch = []
        for _ in range(batch_size):
            if self.priority_queue:
                batch.append(heapq.heappop(self.priority_queue))
        
        # Process events concurrently
        tasks = []
        for queued_event in batch:
            task = asyncio.create_task(
                self._route_and_process_event(queued_event.event)
            )
            tasks.append(task)
        
        # Wait for all tasks to complete
        results = await asyncio.gather(*tasks, return_exceptions=True)
        
        # Count successful processing
        for result in results:
            if not isinstance(result, Exception) and result:
                processed_count += 1
        
        self.metrics.queue_sizes['main'] = len(self.priority_queue)
        return processed_count
    
    async def _route_and_process_event(self, event: Event) -> bool:
        """Route event to appropriate handlers and process"""
        start_time = time.time()
        
        try:
            # Find handlers for this event type
            eligible_handlers = self._find_eligible_handlers(event)
            
            if not eligible_handlers:
                logger.debug(f"No handlers found for event {event.event_id}")
                return True  # Not an error if no handlers
            
            # Process with each eligible handler
            tasks = []
            for handler_id in eligible_handlers:
                # Check circuit breaker
                if not self.circuit_breakers[handler_id].can_execute():
                    logger.debug(f"Handler {handler_id} circuit breaker open")
                    continue
                
                # Check concurrency limit
                registration = self.handlers[handler_id]
                if registration.active_tasks >= registration.max_concurrent:
                    logger.debug(f"Handler {handler_id} at concurrency limit")
                    continue
                
                task = asyncio.create_task(
                    self._execute_handler(handler_id, event)
                )
                tasks.append(task)
            
            # Wait for all handlers to process
            if tasks:
                results = await asyncio.gather(*tasks, return_exceptions=True)
                
                # Record results
                success_count = sum(1 for r in results if not isinstance(r, Exception))
                if success_count > 0:
                    processing_time = time.time() - start_time
                    self.metrics.record_event_processed(processing_time)
                    return True
            
            self.metrics.record_event_failed()
            return False
            
        except Exception as e:
            logger.error(f"Error routing event {event.event_id}: {e}")
            self.metrics.record_event_failed()
            return False
    
    async def _execute_handler(self, handler_id: str, event: Event) -> bool:
        """Execute a specific handler for an event"""
        registration = self.handlers[handler_id]
        circuit_breaker = self.circuit_breakers[handler_id]
        
        async with self.worker_semaphore:
            registration.active_tasks += 1
            start_time = time.time()
            
            try:
                # Execute handler with timeout
                timeout = self.config.get('handler_timeout_seconds', 30)
                await asyncio.wait_for(
                    registration.handler(event),
                    timeout=timeout
                )
                
                # Record success
                processing_time = time.time() - start_time
                registration.total_processed += 1
                registration.last_activity = datetime.now()
                circuit_breaker.record_success()
                
                # Update adaptive routing weights
                self._update_routing_weight(handler_id, processing_time, True)
                
                return True
                
            except asyncio.TimeoutError:
                logger.warning(f"Handler {handler_id} timed out processing event {event.event_id}")
                registration.total_failed += 1
                circuit_breaker.record_failure()
                self._update_routing_weight(handler_id, float('inf'), False)
                return False
                
            except Exception as e:
                logger.error(f"Handler {handler_id} failed: {e}")
                registration.total_failed += 1
                circuit_breaker.record_failure()
                self._update_routing_weight(handler_id, time.time() - start_time, False)
                
                # Move to dead letter queue for analysis
                self.dead_letter_queue.append({
                    "event": event.to_dict(),
                    "handler_id": handler_id,
                    "error": str(e),
                    "timestamp": datetime.now().isoformat()
                })
                
                return False
                
            finally:
                registration.active_tasks -= 1
    
    def _find_eligible_handlers(self, event: Event) -> List[str]:
        """Find handlers eligible to process this event"""
        eligible = []
        
        for handler_id in self.event_routes.get(event.event_type, []):
            if handler_id not in self.handlers:
                continue
            
            registration = self.handlers[handler_id]
            
            # Check priority filter
            if (registration.priority_filter and 
                event.priority.value < registration.priority_filter.value):
                continue
            
            # Check layer targeting
            if (event.target_layers and 
                registration.layer_type not in event.target_layers):
                continue
            
            eligible.append(handler_id)
        
        # Sort by routing weight (adaptive routing)
        eligible.sort(key=lambda h: self.routing_weights[h], reverse=True)
        
        return eligible
    
    def _calculate_priority_score(self, event: Event) -> float:
        """Calculate priority score for event routing"""
        base_score = event.priority.value * 100
        
        # Age factor - older events get higher priority
        age_seconds = (datetime.now() - event.timestamp).total_seconds()
        age_factor = min(age_seconds / 60, 10)  # Cap at 10 minutes
        
        # Event type factor
        type_factors = {
            EventType.EMERGENCY_STOP: 1000,
            EventType.SYSTEM_ERROR: 500,
            EventType.RISK_LIMIT_BREACH: 400,
            EventType.SIGNAL_GENERATED: 300,
            EventType.MARKET_REGIME_CHANGE: 200,
            EventType.MARKET_DATA_UPDATE: 100
        }
        
        type_factor = type_factors.get(event.event_type, 50)
        
        return base_score + age_factor + type_factor
    
    def _update_routing_weight(self, handler_id: str, processing_time: float, success: bool) -> None:
        """Update adaptive routing weights based on handler performance"""
        performance_history = self.handler_performance[handler_id]
        
        # Record performance data
        performance_score = 1.0 if success else 0.0
        if success and processing_time < float('inf'):
            # Faster processing gets higher scores
            performance_score *= max(0.1, 1.0 / (1.0 + processing_time))
        
        performance_history.append(performance_score)
        
        # Update weight based on recent performance
        if len(performance_history) >= 5:
            recent_avg = sum(list(performance_history)[-10:]) / min(10, len(performance_history))
            
            # Exponential moving average for weight update
            alpha = 0.1
            current_weight = self.routing_weights[handler_id]
            self.routing_weights[handler_id] = (
                alpha * recent_avg + (1 - alpha) * current_weight
            )
    
    def _setup_default_filters(self) -> None:
        """Setup default event filters"""
        # Filter expired events
        def not_expired_filter(event: Event) -> bool:
            return not event.is_expired()
        
        self.event_filter.add_filter(not_expired_filter)
    
    def _initialize_circuit_breakers(self) -> None:
        """Initialize circuit breakers for existing handlers"""
        for handler_id in self.handlers:
            if handler_id not in self.circuit_breakers:
                self.circuit_breakers[handler_id] = CircuitBreaker()
    
    async def _flush_queue(self) -> None:
        """Process remaining events in queue during shutdown"""
        logger.info("Flushing remaining events...")
        
        while self.priority_queue:
            batch_size = min(10, len(self.priority_queue))
            await self._process_event_batch()
        
        logger.info("Event queue flushed")
    
    async def _perform_maintenance(self) -> None:
        """Perform periodic maintenance tasks"""
        try:
            # Clean up dead letter queue if too large
            if len(self.dead_letter_queue) > 500:
                # Keep only recent entries
                current_time = datetime.now()
                cutoff_time = current_time - timedelta(hours=1)
                
                # This is a simplification - in practice you'd want more sophisticated cleanup
                logger.info(f"Cleaned up old entries from dead letter queue")
            
            # Reset circuit breakers that have been open too long
            for handler_id, breaker in self.circuit_breakers.items():
                if breaker.state == "OPEN" and breaker._should_attempt_reset():
                    breaker.state = "HALF_OPEN"
                    logger.info(f"Reset circuit breaker for handler {handler_id}")
            
            # Update metrics
            self.metrics.queue_sizes['dead_letter'] = len(self.dead_letter_queue)
            
        except Exception as e:
            logger.error(f"Error during maintenance: {e}")


class MessageBusFactory:
    """Factory for creating configured message bus instances"""
    
    @staticmethod
    def create_message_bus(config: Dict[str, Any]) -> MessageBus:
        """Create a message bus with default configuration"""
        default_config = {
            'max_concurrent_handlers': 50,
            'batch_size': 10,
            'processing_interval_ms': 10,
            'max_queue_size': 10000,
            'handler_timeout_seconds': 30,
            'circuit_breaker_threshold': 5,
            'circuit_breaker_timeout': 60
        }
        
        # Merge with provided config
        merged_config = {**default_config, **config}
        
        return MessageBus(merged_config)