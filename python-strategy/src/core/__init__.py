# src/core/__init__.py
"""
Bio-Inspired Trading System - Foundation Layer
Core infrastructure initialization and coordination
"""

import asyncio
import logging
from typing import Dict, Any, Optional
from datetime import datetime

from .interfaces import *
from .events import *
from .messaging import MessageBus, MessageBusFactory
from .config import ConfigManager, ConfigurationFactory
from .database import BioDatabaseManager, DatabaseFactory
from .exceptions import RecoveryManager, RecoveryManagerFactory, BioTradingException


logger = logging.getLogger(__name__)


class FoundationLayer:
    """
    Bio-inspired foundation layer that provides core infrastructure
    for the entire trading system
    """
    
    def __init__(self, config_path: Optional[str] = None):
        self.config_path = config_path
        
        # Core components
        self.config_manager: Optional[ConfigManager] = None
        self.message_bus: Optional[MessageBus] = None
        self.database_manager: Optional[BioDatabaseManager] = None
        self.recovery_manager: Optional[RecoveryManager] = None
        
        # System state
        self.is_initialized = False
        self.start_time: Optional[datetime] = None
        
        # Health monitoring
        self.health_metrics = {
            "config_health": False,
            "database_health": False,
            "message_bus_health": False,
            "recovery_system_health": False
        }
    
    async def initialize(self) -> bool:
        """Initialize all foundation components"""
        try:
            logger.info("Initializing Bio-Inspired Trading System Foundation Layer...")
            self.start_time = datetime.now()
            
            # 1. Initialize configuration manager
            if not await self._initialize_config():
                return False
            
            # 2. Initialize database
            if not await self._initialize_database():
                return False
            
            # 3. Initialize recovery system
            if not await self._initialize_recovery():
                return False
            
            # 4. Initialize message bus
            if not await self._initialize_message_bus():
                return False
            
            # 5. Setup cross-component integration
            await self._setup_integration()
            
            self.is_initialized = True
            logger.info("Foundation layer initialized successfully")
            
            # Publish system startup event
            await self._publish_startup_event()
            
            return True
            
        except Exception as e:
            logger.error(f"Failed to initialize foundation layer: {e}")
            await self._cleanup_on_failure()
            return False
    
    async def shutdown(self) -> None:
        """Gracefully shutdown foundation layer"""
        if not self.is_initialized:
            return
        
        logger.info("Shutting down foundation layer...")
        
        try:
            # Publish shutdown event
            if self.message_bus:
                shutdown_event = Event(
                    event_type=EventType.SYSTEM_SHUTDOWN,
                    priority=EventPriority.HIGH,
                    data={"shutdown_time": datetime.now().isoformat()}
                )
                await self.message_bus.publish(shutdown_event)
            
            # Stop message bus
            if self.message_bus:
                await self.message_bus.stop()
            
            # Close database connections
            if self.database_manager:
                await self.database_manager.disconnect()
            
            # Shutdown config manager
            if self.config_manager:
                await self.config_manager.shutdown()
            
            self.is_initialized = False
            logger.info("Foundation layer shutdown complete")
            
        except Exception as e:
            logger.error(f"Error during foundation layer shutdown: {e}")
    
    async def get_system_health(self) -> Dict[str, Any]:
        """Get comprehensive system health status"""
        if not self.is_initialized:
            return {"status": "not_initialized", "components": {}}
        
        # Update health metrics
        await self._update_health_metrics()
        
        uptime = (datetime.now() - self.start_time).total_seconds() if self.start_time else 0
        
        system_health = {
            "status": "healthy" if all(self.health_metrics.values()) else "unhealthy",
            "uptime_seconds": uptime,
            "components": {
                "config_manager": {
                    "healthy": self.health_metrics["config_health"],
                    "metrics": self.config_manager.get_config_health() if self.config_manager else {}
                },
                "database": {
                    "healthy": self.health_metrics["database_health"],
                    "metrics": self.database_manager.get_metrics() if self.database_manager else {}
                },
                "message_bus": {
                    "healthy": self.health_metrics["message_bus_health"],
                    "metrics": self.message_bus.get_metrics() if self.message_bus else {}
                },
                "recovery_system": {
                    "healthy": self.health_metrics["recovery_system_health"],
                    "metrics": self.recovery_manager.get_recovery_metrics() if self.recovery_manager else {}
                }
            },
            "timestamp": datetime.now().isoformat()
        }
        
        return system_health
    
    def get_config_manager(self) -> ConfigManager:
        """Get configuration manager instance"""
        if not self.config_manager:
            raise BioTradingException("Configuration manager not initialized")
        return self.config_manager
    
    def get_message_bus(self) -> MessageBus:
        """Get message bus instance"""
        if not self.message_bus:
            raise BioTradingException("Message bus not initialized")
        return self.message_bus
    
    def get_database_manager(self) -> BioDatabaseManager:
        """Get database manager instance"""
        if not self.database_manager:
            raise BioTradingException("Database manager not initialized")
        return self.database_manager
    
    def get_recovery_manager(self) -> RecoveryManager:
        """Get recovery manager instance"""
        if not self.recovery_manager:
            raise BioTradingException("Recovery manager not initialized")
        return self.recovery_manager
    
    async def _initialize_config(self) -> bool:
        """Initialize configuration manager"""
        try:
            logger.info("Initializing configuration manager...")
            
            self.config_manager = ConfigurationFactory.create_default_config_manager(
                self.config_path
            )
            
            success = await self.config_manager.initialize()
            if success:
                self.health_metrics["config_health"] = True
                logger.info("Configuration manager initialized successfully")
            
            return success
            
        except Exception as e:
            logger.error(f"Failed to initialize configuration manager: {e}")
            return False
    
    async def _initialize_database(self) -> bool:
        """Initialize database manager"""
        try:
            logger.info("Initializing database manager...")
            
            if not self.config_manager:
                raise BioTradingException("Config manager required for database initialization")
            
            db_config = self.config_manager.get('database', {})
            if not db_config:
                logger.warning("No database configuration found, using default SQLite")
                db_config = {'url': 'data/bio_trading.db'}
            
            self.database_manager = DatabaseFactory.create_database_manager(db_config)
            
            success = await self.database_manager.connect()
            if success:
                self.health_metrics["database_health"] = True
                logger.info("Database manager initialized successfully")
            
            return success
            
        except Exception as e:
            logger.error(f"Failed to initialize database manager: {e}")
            return False
    
    async def _initialize_recovery(self) -> bool:
        """Initialize recovery manager"""
        try:
            logger.info("Initializing recovery manager...")
            
            recovery_config = {}
            if self.config_manager:
                recovery_config = self.config_manager.get('recovery', {})
            
            self.recovery_manager = RecoveryManagerFactory.create_recovery_manager(recovery_config)
            
            self.health_metrics["recovery_system_health"] = True
            logger.info("Recovery manager initialized successfully")
            
            return True
            
        except Exception as e:
            logger.error(f"Failed to initialize recovery manager: {e}")
            return False
    
    async def _initialize_message_bus(self) -> bool:
        """Initialize message bus"""
        try:
            logger.info("Initializing message bus...")
            
            bus_config = {}
            if self.config_manager:
                bus_config = self.config_manager.get('message_bus', {})
            
            self.message_bus = MessageBusFactory.create_message_bus(bus_config)
            
            success = await self.message_bus.initialize()
            if success:
                await self.message_bus.start()
                self.health_metrics["message_bus_health"] = True
                logger.info("Message bus initialized successfully")
            
            return success
            
        except Exception as e:
            logger.error(f"Failed to initialize message bus: {e}")
            return False
    
    async def _setup_integration(self) -> None:
        """Setup integration between components"""
        try:
            logger.info("Setting up component integration...")
            
            # Setup config reload callbacks
            if self.config_manager and self.message_bus:
                async def config_change_handler(section: str, config: Dict[str, Any]):
                    event = Event(
                        event_type=EventType.SYSTEM_HEALTH_CHANGE,
                        priority=EventPriority.NORMAL,
                        data={
                            "type": "config_change",
                            "section": section,
                            "timestamp": datetime.now().isoformat()
                        }
                    )
                    await self.message_bus.publish(event)
                
                self.config_manager.add_reload_callback(config_change_handler)
            
            # Register system health monitoring handler
            if self.message_bus:
                async def health_monitoring_handler(event: Event):
                    if event.event_type == EventType.SYSTEM_ERROR:
                        # Store error in database
                        if self.database_manager:
                            try:
                                await self.database_manager.execute_query(
                                    """INSERT INTO system_errors 
                                       (error_type, message, layer_type, timestamp, details) 
                                       VALUES (?, ?, ?, ?, ?)""",
                                    {
                                        'error_type': 'system_error',
                                        'message': event.data.get('error_message', ''),
                                        'layer_type': event.data.get('layer', ''),
                                        'timestamp': event.timestamp.isoformat(),
                                        'details': str(event.data)
                                    }
                                )
                            except Exception as e:
                                logger.error(f"Failed to store error in database: {e}")
                
                self.message_bus.register_handler(
                    handler=health_monitoring_handler,
                    layer_type=LayerType.CONSCIOUSNESS,  # Foundation acts as consciousness for errors
                    event_types={EventType.SYSTEM_ERROR, EventType.LAYER_ERROR}
                )
            
            logger.info("Component integration setup complete")
            
        except Exception as e:
            logger.error(f"Failed to setup component integration: {e}")
            raise
    
    async def _publish_startup_event(self) -> None:
        """Publish system startup event"""
        if self.message_bus and self.config_manager:
            startup_event = Event(
                event_type=EventType.SYSTEM_STARTUP,
                priority=EventPriority.HIGH,
                data={
                    "startup_time": self.start_time.isoformat() if self.start_time else None,
                    "foundation_version": "1.0.0",
                    "components_initialized": list(self.health_metrics.keys()),
                    "config_sections": list(self.config_manager.get_full_config().keys())
                }
            )
            
            await self.message_bus.publish(startup_event)
            logger.info("Published system startup event")
    
    async def _update_health_metrics(self) -> None:
        """Update health metrics for all components"""
        try:
            # Config manager health
            if self.config_manager:
                config_health = self.config_manager.get_config_health()
                self.health_metrics["config_health"] = config_health["invalid_sections"] == 0
            
            # Database health
            if self.database_manager:
                db_health = await self.database_manager.health_check()
                self.health_metrics["database_health"] = db_health
            
            # Message bus health (check if it's running and processing)
            if self.message_bus:
                bus_metrics = self.message_bus.get_metrics()
                # Consider healthy if success rate > 90% and it's processing events
                success_rate = bus_metrics.get("success_rate", 0)
                self.health_metrics["message_bus_health"] = success_rate > 0.9
            
            # Recovery system health
            if self.recovery_manager:
                recovery_metrics = self.recovery_manager.get_recovery_metrics()
                # Consider healthy if success rate > 80%
                success_rate = recovery_metrics.get("success_rate", 1.0)
                self.health_metrics["recovery_system_health"] = success_rate > 0.8
            
        except Exception as e:
            logger.error(f"Error updating health metrics: {e}")
    
    async def _cleanup_on_failure(self) -> None:
        """Cleanup resources on initialization failure"""
        try:
            if self.message_bus:
                await self.message_bus.stop()
            
            if self.database_manager:
                await self.database_manager.disconnect()
            
            if self.config_manager:
                await self.config_manager.shutdown()
                
        except Exception as e:
            logger.error(f"Error during cleanup: {e}")


# Factory for creating foundation layer
class FoundationLayerFactory:
    """Factory for creating foundation layer instances"""
    
    @staticmethod
    def create_foundation_layer(config_path: Optional[str] = None) -> FoundationLayer:
        """Create foundation layer with optional config path"""
        return FoundationLayer(config_path)
    
    @staticmethod
    def create_test_foundation_layer() -> FoundationLayer:
        """Create foundation layer for testing"""
        # Create temporary test configuration
        test_config = {
            'database': {'url': ':memory:'},
            'message_bus': {'max_queue_size': 100},
            'recovery': {'circuit_breaker_threshold': 3}
        }
        
        config_manager = ConfigurationFactory.create_test_config_manager(test_config)
        
        foundation = FoundationLayer()
        foundation.config_manager = config_manager
        
        return foundation


# Utility function for system initialization
async def initialize_bio_system(config_path: Optional[str] = None) -> FoundationLayer:
    """
    Initialize the complete bio-inspired trading system foundation
    
    Args:
        config_path: Path to configuration file
        
    Returns:
        Initialized foundation layer
        
    Raises:
        BioTradingException: If initialization fails
    """
    foundation = FoundationLayerFactory.create_foundation_layer(config_path)
    
    success = await foundation.initialize()
    if not success:
        raise BioTradingException(
            "Failed to initialize bio-inspired trading system foundation",
            severity=ErrorSeverity.CRITICAL
        )
    
    return foundation


# Context manager for foundation layer lifecycle
class BioSystemContext:
    """Context manager for bio-inspired system lifecycle management"""
    
    def __init__(self, config_path: Optional[str] = None):
        self.config_path = config_path
        self.foundation: Optional[FoundationLayer] = None
    
    async def __aenter__(self) -> FoundationLayer:
        """Initialize system on context entry"""
        self.foundation = await initialize_bio_system(self.config_path)
        return self.foundation
    
    async def __aexit__(self, exc_type, exc_val, exc_tb):
        """Cleanup system on context exit"""
        if self.foundation:
            await self.foundation.shutdown()


# Health check utility
async def check_system_health(foundation: FoundationLayer) -> bool:
    """
    Perform comprehensive system health check
    
    Args:
        foundation: Initialized foundation layer
        
    Returns:
        True if system is healthy, False otherwise
    """
    try:
        health_status = await foundation.get_system_health()
        
        # Check overall status
        if health_status["status"] != "healthy":
            logger.warning("System health check failed: overall status unhealthy")
            return False
        
        # Check individual components
        components = health_status.get("components", {})
        for component_name, component_health in components.items():
            if not component_health.get("healthy", False):
                logger.warning(f"Component {component_name} is unhealthy")
                return False
        
        logger.info("System health check passed")
        return True
        
    except Exception as e:
        logger.error(f"Health check failed with error: {e}")
        return False


# Performance monitoring utility
async def get_system_performance_metrics(foundation: FoundationLayer) -> Dict[str, Any]:
    """
    Get comprehensive system performance metrics
    
    Args:
        foundation: Initialized foundation layer
        
    Returns:
        Dictionary containing performance metrics
    """
    try:
        health_status = await foundation.get_system_health()
        
        performance_metrics = {
            "system_uptime": health_status.get("uptime_seconds", 0),
            "overall_health": health_status.get("status", "unknown"),
            "component_metrics": {}
        }
        
        # Extract metrics from each component
        components = health_status.get("components", {})
        for component_name, component_data in components.items():
            metrics = component_data.get("metrics", {})
            performance_metrics["component_metrics"][component_name] = metrics
        
        # Calculate aggregate metrics
        message_bus_metrics = performance_metrics["component_metrics"].get("message_bus", {})
        database_metrics = performance_metrics["component_metrics"].get("database", {})
        
        performance_metrics["aggregate_metrics"] = {
            "total_events_processed": message_bus_metrics.get("events_processed", 0),
            "event_processing_rate": message_bus_metrics.get("throughput_events_per_second", 0),
            "database_query_success_rate": database_metrics.get("query_success_rate", 0),
            "average_response_time_ms": message_bus_metrics.get("avg_processing_time_ms", 0)
        }
        
        return performance_metrics
        
    except Exception as e:
        logger.error(f"Failed to get performance metrics: {e}")
        return {"error": str(e)}


# Example usage and demonstration
async def demo_foundation_layer():
    """Demonstrate foundation layer capabilities"""
    print("🧬 Bio-Inspired Trading System Foundation Demo")
    print("=" * 50)
    
    try:
        # Initialize system using context manager
        async with BioSystemContext() as foundation:
            print("✅ Foundation layer initialized successfully")
            
            # Get system health
            health = await foundation.get_system_health()
            print(f"📊 System Status: {health['status']}")
            print(f"⏱️  Uptime: {health['uptime_seconds']:.2f} seconds")
            
            # Demonstrate message bus
            message_bus = foundation.get_message_bus()
            
            # Create and publish test event
            test_event = Event(
                event_type=EventType.SYSTEM_HEALTH_CHANGE,
                priority=EventPriority.NORMAL,
                data={"test": "foundation_demo", "timestamp": datetime.now().isoformat()}
            )
            
            success = await message_bus.publish(test_event)
            print(f"📨 Test event published: {success}")
            
            # Demonstrate configuration
            config_manager = foundation.get_config_manager()
            config_health = config_manager.get_config_health()
            print(f"⚙️  Configuration sections: {config_health['total_sections']}")
            
            # Demonstrate database
            db_manager = foundation.get_database_manager()
            db_health = await db_manager.health_check()
            print(f"🗄️  Database healthy: {db_health}")
            
            # Get performance metrics
            performance = await get_system_performance_metrics(foundation)
            print(f"📈 Event processing rate: {performance['aggregate_metrics']['event_processing_rate']:.2f} events/sec")
            
            # Wait a moment to let everything process
            await asyncio.sleep(1)
            
            print("✅ Foundation layer demo completed successfully")
    
    except Exception as e:
        print(f"❌ Demo failed: {e}")


# Export all public interfaces
__all__ = [
    # Core interfaces
    'LayerInterface', 'PerceptionInterface', 'HomeostasisInterface',
    'AdaptationInterface', 'ConsciousnessInterface', 'ExecutionInterface',
    'DatabaseInterface',
    
    # Data models
    'SystemState', 'MarketRegime', 'LayerType', 'EventPriority',
    'SystemHealth', 'MarketState', 'TradingSignal', 'PerformanceMetrics', 'LayerHealth',
    
    # Event system
    'Event', 'EventType', 'EventMetrics', 'EventFilter', 'EventValidator',
    'SystemHealthChangeEvent', 'MarketRegimeChangeEvent', 'TradingSignalEvent',
    'EmergencyStopEvent', 'PerformanceUpdateEvent',
    
    # Message bus
    'MessageBus', 'MessageBusFactory',
    
    # Configuration
    'ConfigManager', 'ConfigurationFactory', 'ConfigValidator',
    
    # Database
    'BioDatabaseManager', 'DatabaseFactory', 'ConnectionPool',
    
    # Exception handling
    'BioTradingException', 'RecoveryManager', 'RecoveryManagerFactory',
    'ErrorSeverity', 'RecoveryStrategy', 'ErrorContext',
    'PerceptionLayerException', 'HomeostasisLayerException', 'AdaptationLayerException',
    'ConsciousnessLayerException', 'ExecutionLayerException',
    'ConfigurationException', 'DatabaseException', 'MessageBusException',
    'MarketDataException', 'RiskManagementException', 'OrderExecutionException',
    'StrategyException', 'SystemCriticalException',
    'handle_exceptions', 'ExceptionContext',
    
    # Foundation layer
    'FoundationLayer', 'FoundationLayerFactory', 'BioSystemContext',
    
    # Utilities
    'initialize_bio_system', 'check_system_health', 'get_system_performance_metrics',
    'demo_foundation_layer'
]


# Version information
__version__ = "1.0.0"
__author__ = "Bio-Inspired Trading System Team"
__description__ = "Foundation layer for bio-inspired algorithmic trading system"


if __name__ == "__main__":
    # Run demo if executed directly
    asyncio.run(demo_foundation_layer())