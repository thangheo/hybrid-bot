import asyncio
import logging
import time
from typing import Dict, List, Any, Optional, Callable
from dataclasses import dataclass

logger = logging.getLogger(__name__)

@dataclass
class HealthStatus:
    """Data class to store health status information for a component."""
    status: str  # "healthy", "degraded", "unhealthy"
    message: str = ""
    details: Dict[str, Any] = None
    timestamp: float = None
    
    def __post_init__(self):
        if self.timestamp is None:
            self.timestamp = time.time()
        if self.details is None:
            self.details = {}

class HealthMonitor:
    """
    Monitors the health status of various trading system components.
    """
    
    # Health status constants
    HEALTHY = "healthy"
    DEGRADED = "degraded"
    UNHEALTHY = "unhealthy"
    
    def __init__(self, config=None):
        """
        Initialize the health monitor.
        
        Args:
            config: Configuration dictionary for the health monitor
        """
        self.config = config or {}
        self.component_checks = {}  # Maps component names to check functions
        self.health_status = {}     # Maps component names to current health status
        self.dependencies = {}      # Maps component names to their dependencies
        self.subscribers = []       # Callbacks for health status changes
        self.check_interval = self.config.get('check_interval', 60)  # seconds
        self.running = False
        self._monitor_task = None
    
    def register_component(self, component_name: str, check_func: Callable[[], HealthStatus],
                         dependencies: List[str] = None) -> None:
        """
        Register a component to be monitored.
        
        Args:
            component_name: Name of the component
            check_func: Function that returns HealthStatus
            dependencies: List of component names this component depends on
        """
        self.component_checks[component_name] = check_func
        self.dependencies[component_name] = dependencies or []
        self.health_status[component_name] = HealthStatus(
            status=self.HEALTHY,
            message="Initial state, not yet checked"
        )
        logger.info(f"Registered component for health monitoring: {component_name}")
    
    def subscribe(self, callback: Callable[[str, HealthStatus, HealthStatus], None]) -> None:
        """
        Subscribe to health status changes.
        
        Args:
            callback: Function called with (component_name, old_status, new_status)
        """
        self.subscribers.append(callback)
    
    async def start(self) -> None:
        """Start the health monitoring loop."""
        if self.running:
            logger.warning("Health monitor already running")
            return
        
        self.running = True
        self._monitor_task = asyncio.create_task(self._monitoring_loop())
        logger.info("Health monitor started")
    
    async def stop(self) -> None:
        """Stop the health monitoring loop."""
        if not self.running:
            logger.warning("Health monitor not running")
            return
        
        self.running = False
        if self._monitor_task:
            await self._monitor_task
            self._monitor_task = None
        logger.info("Health monitor stopped")
    
    async def _monitoring_loop(self) -> None:
        """Main monitoring loop that runs at regular intervals."""
        while self.running:
            try:
                await self._check_all_components()
                await asyncio.sleep(self.check_interval)
            except Exception as e:
                logger.error(f"Error in health monitoring loop: {str(e)}", exc_info=True)
                await asyncio.sleep(1)  # Short delay before retrying after error
    
    async def _check_all_components(self) -> None:
        """Check the health of all registered components."""
        for component_name in self.component_checks:
            await self._check_component(component_name)
    
    async def _check_component(self, component_name: str) -> None:
        """
        Check the health of a specific component and update its status.
        
        Args:
            component_name: Name of the component to check
        """
        if component_name not in self.component_checks:
            logger.warning(f"Attempted to check unknown component: {component_name}")
            return
        
        try:
            check_func = self.component_checks[component_name]
            
            # Run the check function (handle both sync and async)
            if asyncio.iscoroutinefunction(check_func):
                new_status = await check_func()
            else:
                loop = asyncio.get_running_loop()
                new_status = await loop.run_in_executor(None, check_func)
            
            # Apply dependency logic
            dependency_status = self._check_dependencies(component_name)
            if dependency_status.status != self.HEALTHY:
                # Override status based on dependencies
                new_status = dependency_status
            
            old_status = self.health_status[component_name]
            self.health_status[component_name] = new_status
            
            # Notify subscribers if status changed
            if old_status.status != new_status.status:
                for callback in self.subscribers:
                    try:
                        if asyncio.iscoroutinefunction(callback):
                            await callback(component_name, old_status, new_status)
                        else:
                            callback(component_name, old_status, new_status)
                    except Exception as e:
                        logger.error(f"Error in health status subscriber: {str(e)}", exc_info=True)
            
        except Exception as e:
            logger.error(f"Error checking health of {component_name}: {str(e)}", exc_info=True)
            # Set to unhealthy on check failure
            old_status = self.health_status[component_name]
            new_status = HealthStatus(
                status=self.UNHEALTHY,
                message=f"Health check failed: {str(e)}",
                details={"error": str(e), "traceback": str(e.__traceback__)}
            )
            self.health_status[component_name] = new_status
            
            # Notify subscribers about the failure
            for callback in self.subscribers:
                try:
                    if asyncio.iscoroutinefunction(callback):
                        await callback(component_name, old_status, new_status)
                    else:
                        callback(component_name, old_status, new_status)
                except Exception as e:
                    logger.error(f"Error in health status subscriber: {str(e)}", exc_info=True)
    
    def _check_dependencies(self, component_name: str) -> HealthStatus:
        """
        Check if all dependencies of a component are healthy.
        
        Args:
            component_name: Name of the component
            
        Returns:
            HealthStatus indicating the status based on dependencies
        """
        if component_name not in self.dependencies:
            return HealthStatus(status=self.HEALTHY)
        
        unhealthy_deps = []
        degraded_deps = []
        
        for dep in self.dependencies[component_name]:
            if dep not in self.health_status:
                logger.warning(f"Dependency {dep} not registered for {component_name}")
                continue
                
            dep_status = self.health_status[dep]
            if dep_status.status == self.UNHEALTHY:
                unhealthy_deps.append(dep)
            elif dep_status.status == self.DEGRADED:
                degraded_deps.append(dep)
        
        if unhealthy_deps:
            return HealthStatus(
                status=self.UNHEALTHY,
                message=f"Unhealthy dependencies: {', '.join(unhealthy_deps)}",
                details={"unhealthy_dependencies": unhealthy_deps}
            )
        elif degraded_deps:
            return HealthStatus(
                status=self.DEGRADED,
                message=f"Degraded dependencies: {', '.join(degraded_deps)}",
                details={"degraded_dependencies": degraded_deps}
            )
        
        return HealthStatus(status=self.HEALTHY)
    
    def get_component_status(self, component_name: str) -> Optional[HealthStatus]:
        """Get the current health status of a component."""
        return self.health_status.get(component_name)
    
    def get_all_statuses(self) -> Dict[str, HealthStatus]:
        """Get the current health status of all components."""
        return self.health_status.copy()
    
    def get_system_status(self) -> HealthStatus:
        """Get the overall system health status."""
        if not self.health_status:
            return HealthStatus(status=self.HEALTHY, message="No components registered")
        
        unhealthy_components = []
        degraded_components = []
        
        for component, status in self.health_status.items():
            if status.status == self.UNHEALTHY:
                unhealthy_components.append(component)
            elif status.status == self.DEGRADED:
                degraded_components.append(component)
        
        if unhealthy_components:
            return HealthStatus(
                status=self.UNHEALTHY,
                message=f"Unhealthy components: {', '.join(unhealthy_components)}",
                details={
                    "unhealthy_components": unhealthy_components,
                    "degraded_components": degraded_components
                }
            )
        elif degraded_components:
            return HealthStatus(
                status=self.DEGRADED,
                message=f"Degraded components: {', '.join(degraded_components)}",
                details={"degraded_components": degraded_components}
            )
        
        return HealthStatus(status=self.HEALTHY, message="All components healthy")
    
    def check_component_now(self, component_name: str) -> None:
        """
        Trigger an immediate health check for a component.
        
        Args:
            component_name: Name of the component to check
        """
        if component_name not in self.component_checks:
            raise ValueError(f"Unknown component: {component_name}")
        
        # Create a task to run the check
        asyncio.create_task(self._check_component(component_name))