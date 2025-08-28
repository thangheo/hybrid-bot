import asyncio
import logging
from datetime import datetime
from typing import Dict, List, Any, Optional, Union, Callable

logger = logging.getLogger(__name__)

class MetricsProcessor:
    """
    Central component for processing, transforming, and routing metrics
    from various collectors to storage and visualization systems.
    """
    
    def __init__(self, config=None):
        """
        Initialize the metrics processor.
        
        Args:
            config: Configuration dictionary for the metrics processor
        """
        self.config = config or {}
        self.collectors = {}
        self.storage_backends = {}
        self.processing_rules = {}
        self.running = False
        self.processing_interval = self.config.get('processing_interval', 5)  # seconds
        self._processing_task = None
        
    def register_collector(self, name: str, collector) -> None:
        """
        Register a metrics collector with the processor.
        
        Args:
            name: Unique name for the collector
            collector: Collector instance that provides metrics
        """
        if name in self.collectors:
            logger.warning(f"Collector '{name}' already registered. Replacing.")
        
        self.collectors[name] = collector
        logger.info(f"Registered collector: {name}")
        
    def register_storage_backend(self, name: str, backend) -> None:
        """
        Register a storage backend for processed metrics.
        
        Args:
            name: Unique name for the storage backend
            backend: Backend instance with store_metrics method
        """
        if not hasattr(backend, 'store_metrics'):
            raise ValueError("Storage backend must implement 'store_metrics' method")
        
        self.storage_backends[name] = backend
        logger.info(f"Registered storage backend: {name}")
    
    def add_processing_rule(self, rule_name: str, source_collector: str, 
                           processor_func: Callable, target_backends: List[str]) -> None:
        """
        Add a rule for processing metrics from a specific collector.
        
        Args:
            rule_name: Unique name for the rule
            source_collector: Name of the source collector
            processor_func: Function to process the metrics
            target_backends: List of storage backend names to receive processed metrics
        """
        if source_collector not in self.collectors:
            raise ValueError(f"Unknown collector: {source_collector}")
        
        for backend in target_backends:
            if backend not in self.storage_backends:
                raise ValueError(f"Unknown storage backend: {backend}")
        
        self.processing_rules[rule_name] = {
            'source': source_collector,
            'processor': processor_func,
            'targets': target_backends
        }
        logger.info(f"Added processing rule: {rule_name}")
    
    async def start(self) -> None:
        """Start the metrics processing loop."""
        if self.running:
            logger.warning("Metrics processor already running")
            return
        
        self.running = True
        self._processing_task = asyncio.create_task(self._processing_loop())
        logger.info("Metrics processor started")
    
    async def stop(self) -> None:
        """Stop the metrics processing loop."""
        if not self.running:
            logger.warning("Metrics processor not running")
            return
        
        self.running = False
        if self._processing_task:
            await self._processing_task
            self._processing_task = None
        logger.info("Metrics processor stopped")
    
    async def _processing_loop(self) -> None:
        """Main processing loop that runs at regular intervals."""
        while self.running:
            try:
                await self._process_all_metrics()
                await asyncio.sleep(self.processing_interval)
            except Exception as e:
                logger.error(f"Error in metrics processing loop: {str(e)}", exc_info=True)
                await asyncio.sleep(1)  # Short delay before retrying after error
    
    async def _process_all_metrics(self) -> None:
        """Process metrics according to all defined rules."""
        for rule_name, rule in self.processing_rules.items():
            try:
                collector = self.collectors[rule['source']]
                raw_metrics = await self._collect_metrics(collector)
                
                if not raw_metrics:
                    continue
                
                processed_metrics = rule['processor'](raw_metrics)
                
                for backend_name in rule['targets']:
                    backend = self.storage_backends[backend_name]
                    await self._store_metrics(backend, processed_metrics)
                
            except Exception as e:
                logger.error(f"Error processing rule '{rule_name}': {str(e)}", exc_info=True)
    
    async def _collect_metrics(self, collector) -> Dict[str, Any]:
        """Collect metrics from a collector, handling both sync and async collectors."""
        try:
            if hasattr(collector, 'collect_metrics_async'):
                return await collector.collect_metrics_async()
            else:
                # For synchronous collectors, run in a thread pool
                loop = asyncio.get_running_loop()
                return await loop.run_in_executor(None, collector.collect_metrics)
        except Exception as e:
            logger.error(f"Error collecting metrics from {collector}: {str(e)}", exc_info=True)
            return {}
    
    async def _store_metrics(self, backend, metrics: Dict[str, Any]) -> None:
        """Store metrics in a backend, handling both sync and async backends."""
        try:
            if hasattr(backend, 'store_metrics_async'):
                await backend.store_metrics_async(metrics)
            else:
                # For synchronous backends, run in a thread pool
                loop = asyncio.get_running_loop()
                await loop.run_in_executor(None, backend.store_metrics, metrics)
        except Exception as e:
            logger.error(f"Error storing metrics to {backend}: {str(e)}", exc_info=True)
    
    def get_collector(self, name: str):
        """Get a registered collector by name."""
        return self.collectors.get(name)
    
    def process_metrics_now(self, collector_name: str = None) -> None:
        """
        Trigger immediate metrics processing for testing or manual execution.
        
        Args:
            collector_name: Optional name of specific collector to process
        """
        if collector_name and collector_name not in self.collectors:
            raise ValueError(f"Unknown collector: {collector_name}")
        
        # Create a task to run the processing
        asyncio.create_task(self._process_metrics_now(collector_name))
    
    async def _process_metrics_now(self, collector_name: str = None) -> None:
        """Internal method to process metrics immediately."""
        if collector_name:
            # Process only rules for the specified collector
            for rule_name, rule in self.processing_rules.items():
                if rule['source'] == collector_name:
                    collector = self.collectors[collector_name]
                    raw_metrics = await self._collect_metrics(collector)
                    processed_metrics = rule['processor'](raw_metrics)
                    
                    for backend_name in rule['targets']:
                        backend = self.storage_backends[backend_name]
                        await self._store_metrics(backend, processed_metrics)
        else:
            # Process all rules
            await self._process_all_metrics()