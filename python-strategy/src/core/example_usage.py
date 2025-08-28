# example_usage.py
"""
Bio-Inspired Trading System - Foundation Layer Usage Example
Demonstrates how to use the foundation layer components
"""

import asyncio
import logging
from typing import Dict, Any
from datetime import datetime

# Import foundation layer components
from src.core import (
    FoundationLayer, BioSystemContext, initialize_bio_system,
    Event, EventType, EventPriority, LayerType,
    SystemHealth, MarketState, TradingSignal, MarketRegime,
    handle_exceptions, create_market_data_exception,
    BioTradingException, ErrorSeverity
)


# Setup logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)


class ExamplePerceptionLayer:
    """
    Example perception layer implementation showing how to integrate
    with the foundation layer
    """
    
    def __init__(self, foundation: FoundationLayer):
        self.foundation = foundation
        self.message_bus = foundation.get_message_bus()
        self.database = foundation.get_database_manager()
        self.config = foundation.get_config_manager()
        self.recovery = foundation.get_recovery_manager()
        
        # Layer state
        self.is_running = False
        self.processed_events = 0
    
    async def initialize(self) -> bool:
        """Initialize the perception layer"""
        try:
            logger.info("Initializing example perception layer...")
            
            # Register event handlers with message bus
            handler_id = self.message_bus.register_handler(
                handler=self.handle_market_data_event,
                layer_type=LayerType.PERCEPTION,
                event_types={EventType.MARKET_DATA_UPDATE, EventType.MARKET_REGIME_CHANGE}
            )
            
            logger.info(f"Registered event handler: {handler_id}")
            
            # Subscribe to configuration changes
            self.config.add_reload_callback(self.on_config_change)
            
            self.is_running = True
            logger.info("Example perception layer initialized successfully")
            return True
            
        except Exception as e:
            logger.error(f"Failed to initialize perception layer: {e}")
            return False
    
    async def handle_market_data_event(self, event: Event) -> None:
        """Handle market data events"""
        try:
            self.processed_events += 1
            logger.info(f"Processing market data event {event.event_id}")
            
            # Simulate market data processing
            if event.event_type == EventType.MARKET_DATA_UPDATE:
                await self._process_market_data(event.data)
            elif event.event_type == EventType.MARKET_REGIME_CHANGE:
                await self._process_regime_change(event.data)
            
            logger.debug(f"Successfully processed event {event.event_id}")
            
        except Exception as e:
            # Use foundation's recovery system
            await self.recovery.handle_exception(e)
    
    @handle_exceptions(reraise=False, fallback_value=None)
    async def _process_market_data(self, data: Dict[str, Any]) -> None:
        """Process market data with automatic exception handling"""
        symbol = data.get('symbol', 'UNKNOWN')
        logger.info(f"Processing market data for {symbol}")
        
        # Simulate some processing that might fail
        if symbol == 'ERROR_TEST':
            raise create_market_data_exception(
                "Simulated market data error",
                symbol=symbol,
                severity=ErrorSeverity.MEDIUM
            )
        
        # Simulate creating a market state
        market_state = MarketState(
            regime=MarketRegime.TREND,
            confidence=0.85,
            volatility=0.02,
            trend_strength=0.7,
            mean_reversion_strength=0.3,
            liquidity_score=0.9,
            timestamp=datetime.now(),
            symbol=symbol
        )
        
        # Store in database
        await self._store_market_state(market_state)
        
        # Generate trading signal
        signal = TradingSignal(
            symbol=symbol,
            action="BUY",
            strength=0.8,
            confidence=0.85,
            position_size=0.05,
            regime=MarketRegime.TREND,
            timestamp=datetime.now()
        )
        
        # Publish signal event
        signal_event = Event(
            event_type=EventType.SIGNAL_GENERATED,
            priority=EventPriority.HIGH,
            source_layer=LayerType.PERCEPTION,
            target_layers={LayerType.HOMEOSTASIS, LayerType.EXECUTION},
            data=signal.__dict__
        )
        
        await self.message_bus.publish(signal_event)
        logger.info(f"Published trading signal for {symbol}")
    
    async def _process_regime_change(self, data: Dict[str, Any]) -> None:
        """Process market regime change"""
        symbol = data.get('symbol', 'UNKNOWN')
        new_regime = data.get('new_regime', 'unknown')
        
        logger.info(f"Market regime changed for {symbol}: {new_regime}")
        
        # Update internal state based on regime change
        # This would contain actual regime adaptation logic
    
    async def _store_market_state(self, market_state: MarketState) -> None:
        """Store market state in database"""
        try:
            query = """
            INSERT INTO market_states 
            (symbol, regime, confidence, volatility, trend_strength, 
             mean_reversion_strength, liquidity_score, timestamp)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            """
            
            params = {
                'symbol': market_state.symbol,
                'regime': market_state.regime.value,
                'confidence': market_state.confidence,
                'volatility': market_state.volatility,
                'trend_strength': market_state.trend_strength,
                'mean_reversion_strength': market_state.mean_reversion_strength,
                'liquidity_score': market_state.liquidity_score,
                'timestamp': market_state.timestamp.isoformat()
            }
            
            await self.database.execute_query(query, params)
            
        except Exception as e:
            logger.error(f"Failed to store market state: {e}")
    
    async def on_config_change(self, section: str, config: Dict[str, Any]) -> None:
        """Handle configuration changes"""
        logger.info(f"Configuration changed for section: {section}")
        
        if section == 'perception':
            # Update perception layer configuration
            logger.info("Updating perception layer configuration")
    
    async def shutdown(self) -> None:
        """Shutdown the perception layer"""
        self.is_running = False
        logger.info("Example perception layer shutdown")


async def demonstrate_foundation_usage():
    """
    Comprehensive demonstration of foundation layer usage
    """
    print("\n🧬 Bio-Inspired Trading System Foundation Usage Demo")
    print("=" * 60)
    
    try:
        # Method 1: Using context manager (recommended)
        print("\n📋 Method 1: Using BioSystemContext")
        async with BioSystemContext() as foundation:
            await _run_foundation_demo(foundation)
        
        print("\n📋 Method 2: Manual initialization")
        # Method 2: Manual initialization
        foundation = await initialize_bio_system()
        try:
            await _run_foundation_demo(foundation)
        finally:
            await foundation.shutdown()
        
        print("\n✅ All demonstrations completed successfully!")
        
    except Exception as e:
        print(f"\n❌ Demonstration failed: {e}")
        logger.exception("Demo failed with exception")


async def _run_foundation_demo(foundation: FoundationLayer):
    """Run the actual foundation demonstration"""
    
    # 1. System Health Check
    print("\n🏥 System Health Check")
    print("-" * 30)
    health = await foundation.get_system_health()
    print(f"Overall Status: {health['status']}")
    print(f"Uptime: {health['uptime_seconds']:.2f} seconds")
    
    for component, details in health['components'].items():
        status = "✅" if details['healthy'] else "❌"
        print(f"{status} {component}: {'Healthy' if details['healthy'] else 'Unhealthy'}")
    
    # 2. Configuration Management
    print("\n⚙️ Configuration Management")
    print("-" * 30)
    config_manager = foundation.get_config_manager()
    
    # Get configuration value
    log_level = config_manager.get('logging.level', 'INFO')
    print(f"Current log level: {log_level}")
    
    # Set runtime configuration
    config_manager.set('demo.runtime_setting', 'test_value')
    test_value = config_manager.get('demo.runtime_setting')
    print(f"Runtime setting: {test_value}")
    
    # 3. Database Operations
    print("\n🗄️ Database Operations")
    print("-" * 30)
    database = foundation.get_database_manager()
    
    # Create example table
    await database.execute_query("""
        CREATE TABLE IF NOT EXISTS market_states (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            symbol TEXT NOT NULL,
            regime TEXT NOT NULL,
            confidence REAL NOT NULL,
            volatility REAL NOT NULL,
            trend_strength REAL NOT NULL,
            mean_reversion_strength REAL NOT NULL,
            liquidity_score REAL NOT NULL,
            timestamp TEXT NOT NULL
        )
    """)
    
    # Store sample system health
    sample_health = SystemHealth(
        stability_score=85.5,
        risk_level=0.15,
        performance_score=92.0,
        adaptation_score=78.5,
        state=SystemState.HEALTHY,
        timestamp=datetime.now(),
        details={"demo": True},
        alerts=[]
    )
    
    success = await database.store_system_health(sample_health)
    print(f"Stored system health: {success}")
    
    # 4. Message Bus Demonstration
    print("\n📨 Message Bus Operations")
    print("-" * 30)
    message_bus = foundation.get_message_bus()
    
    # Create example layer
    example_layer = ExamplePerceptionLayer(foundation)
    await example_layer.initialize()
    
    # Publish test events
    test_events = [
        Event(
            event_type=EventType.MARKET_DATA_UPDATE,
            priority=EventPriority.NORMAL,
            data={'symbol': 'BTCUSDT', 'price': 50000.0}
        ),
        Event(
            event_type=EventType.MARKET_DATA_UPDATE,
            priority=EventPriority.NORMAL,
            data={'symbol': 'ETHUSDT', 'price': 3000.0}
        ),
        Event(
            event_type=EventType.MARKET_DATA_UPDATE,
            priority=EventPriority.HIGH,
            data={'symbol': 'ERROR_TEST', 'price': 1000.0}  # This will trigger an error
        )
    ]
    
    for event in test_events:
        success = await message_bus.publish(event)
        print(f"Published event {event.event_id}: {success}")
    
    # Wait for processing
    await asyncio.sleep(1)
    
    print(f"Events processed by example layer: {example_layer.processed_events}")
    
    # 5. Recovery System Demonstration
    print("\n🔧 Recovery System")
    print("-" * 30)
    recovery_manager = foundation.get_recovery_manager()
    
    # Create a test exception
    test_exception = create_market_data_exception(
        "Demonstration exception",
        symbol="TEST",
        severity=ErrorSeverity.MEDIUM
    )
    
    # Handle the exception
    recovery_success = await recovery_manager.handle_exception(test_exception)
    print(f"Recovery handled exception: {recovery_success}")
    
    # Get recovery metrics
    recovery_metrics = recovery_manager.get_recovery_metrics()
    print(f"Total recoveries attempted: {recovery_metrics['total_recoveries']}")
    print(f"Recovery success rate: {recovery_metrics['success_rate']:.2%}")
    
    # 6. Performance Metrics
    print("\n📊 Performance Metrics")
    print("-" * 30)
    
    # Message bus metrics
    bus_metrics = message_bus.get_metrics()
    print(f"Events processed: {bus_metrics['events_processed']}")
    print(f"Event success rate: {bus_metrics['success_rate']:.2%}")
    print(f"Average processing time: {bus_metrics['avg_processing_time_ms']:.2f}ms")
    
    # Database metrics
    db_metrics = database.get_metrics()
    print(f"Database queries: {db_metrics['total_queries']}")
    print(f"Query success rate: {db_metrics['query_success_rate']:.2%}")
    print(f"Average query time: {db_metrics['avg_query_time_ms']:.2f}ms")
    
    # Cleanup example layer
    await example_layer.shutdown()


async def demonstrate_error_handling():
    """Demonstrate advanced error handling capabilities"""
    print("\n🚨 Error Handling Demonstration")
    print("=" * 40)
    
    async with BioSystemContext() as foundation:
        recovery_manager = foundation.get_recovery_manager()
        
        # Test different exception types
        exceptions_to_test = [
            BioTradingException("Test generic exception", severity=ErrorSeverity.LOW),
            create_market_data_exception("Market data error", "BTCUSDT", ErrorSeverity.MEDIUM),
            BioTradingException("Critical system error", severity=ErrorSeverity.CRITICAL)
        ]
        
        for i, exception in enumerate(exceptions_to_test, 1):
            print(f"\nTest {i}: {type(exception).__name__}")
            print(f"Severity: {exception.severity.value}")
            
            success = await recovery_manager.handle_exception(exception)
            print(f"Recovery successful: {success}")
        
        # Show final recovery metrics
        metrics = recovery_manager.get_recovery_metrics()
        print(f"\nFinal Recovery Metrics:")
        print(f"- Total recoveries: {metrics['total_recoveries']}")
        print(f"- Success rate: {metrics['success_rate']:.2%}")
        print(f"- Active circuit breakers: {metrics['active_circuit_breakers']}")


async def demonstrate_custom_layer_integration():
    """Demonstrate how to create and integrate custom layers"""
    print("\n🔧 Custom Layer Integration Demo")
    print("=" * 40)
    
    async with BioSystemContext() as foundation:
        # Create custom layer that demonstrates all foundation integrations
        custom_layer = ExamplePerceptionLayer(foundation)
        
        print("Initializing custom layer...")
        success = await custom_layer.initialize()
        print(f"Custom layer initialized: {success}")
        
        # Simulate market data events
        message_bus = foundation.get_message_bus()
        
        market_events = [
            {'symbol': 'BTCUSDT', 'price': 45000, 'volume': 1000},
            {'symbol': 'ETHUSDT', 'price': 2800, 'volume': 500},
            {'symbol': 'ADAUSDT', 'price': 1.2, 'volume': 10000}
        ]
        
        print(f"\nPublishing {len(market_events)} market data events...")
        for market_data in market_events:
            event = Event(
                event_type=EventType.MARKET_DATA_UPDATE,
                priority=EventPriority.NORMAL,
                source_layer=LayerType.PERCEPTION,
                data=market_data
            )
            await message_bus.publish(event)
        
        # Wait for processing
        await asyncio.sleep(0.5)
        
        print(f"Events processed: {custom_layer.processed_events}")
        
        # Cleanup
        await custom_layer.shutdown()
        print("Custom layer shutdown complete")


async def main():
    """Main demonstration function"""
    print("🚀 Starting Bio-Inspired Trading System Foundation Demonstrations")
    
    # Run all demonstrations
    await demonstrate_foundation_usage()
    await demonstrate_error_handling()
    await demonstrate_custom_layer_integration()
    
    print("\n🎉 All demonstrations completed successfully!")
    print("\nThe foundation layer provides:")
    print("✅ Centralized configuration management with hot-reload")
    print("✅ Event-driven messaging with adaptive routing")
    print("✅ Database abstraction with connection pooling")
    print("✅ Comprehensive error handling and recovery")
    print("✅ System health monitoring and metrics")
    print("✅ Easy integration for custom layers")


if __name__ == "__main__":
    # Run the demonstrations
    asyncio.run(main())