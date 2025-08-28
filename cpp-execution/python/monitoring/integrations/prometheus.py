import logging
from typing import Dict, Any, List, Optional, Union
import time
from prometheus_client import Counter, Gauge, Histogram, Summary, start_http_server, push_to_gateway
from prometheus_client.core import CollectorRegistry

logger = logging.getLogger(__name__)

class PrometheusExporter:
    """
    Exports metrics to Prometheus using either a HTTP server or push gateway.
    """
    
    def __init__(self, config=None):
        """
        Initialize the Prometheus exporter.
        
        Args:
            config: Configuration dictionary for the exporter
        """
        self.config = config or {}
        self.mode = self.config.get('mode', 'server')  # 'server' or 'push'
        
        if self.mode == 'server':
            self.port = self.config.get('port', 8000)
            self.server_started = False
        elif self.mode == 'push':
            self.push_gateway = self.config.get('push_gateway', 'localhost:9091')
            self.push_interval = self.config.get('push_interval', 10)  # seconds
            self.job_name = self.config.get('job_name', 'crypto_trading_monitoring')
            self.registry = CollectorRegistry()
        else:
            raise ValueError(f"Invalid Prometheus exporter mode: {self.mode}")
        
        # Dictionary of metrics
        self.metrics = {}
        
        # Component name for grouping metrics
        self.component = self.config.get('component', 'monitoring')
        
        # Enable default metrics if configured
        if self.config.get('default_metrics', True):
            self._register_default_metrics()
    
    def start(self):
        """Start the Prometheus exporter."""
        if self.mode == 'server' and not self.server_started:
            try:
                start_http_server(self.port)
                self.server_started = True
                logger.info(f"Started Prometheus HTTP server on port {self.port}")
            except Exception as e:
                logger.error(f"Error starting Prometheus HTTP server: {str(e)}", exc_info=True)
        elif self.mode == 'push':
            logger.info(f"Prometheus push gateway exporter configured (gateway: {self.push_gateway})")
    
    def register_gauge(self, name: str, description: str, labels: Optional[List[str]] = None) -> None:
        """
        Register a Prometheus gauge metric.
        
        Args:
            name: Metric name
            description: Metric description
            labels: Optional list of label names
        """
        labels = labels or []
        metric_name = f"{self.component}_{name}"
        
        if self.mode == 'push':
            gauge = Gauge(metric_name, description, labels, registry=self.registry)
        else:
            gauge = Gauge(metric_name, description, labels)
        
        self.metrics[name] = {
            'type': 'gauge',
            'metric': gauge
        }
        
        logger.debug(f"Registered Prometheus gauge: {metric_name}")
    
    def register_counter(self, name: str, description: str, labels: Optional[List[str]] = None) -> None:
        """
        Register a Prometheus counter metric.
        
        Args:
            name: Metric name
            description: Metric description
            labels: Optional list of label names
        """
        labels = labels or []
        metric_name = f"{self.component}_{name}"
        
        if self.mode == 'push':
            counter = Counter(metric_name, description, labels, registry=self.registry)
        else:
            counter = Counter(metric_name, description, labels)
        
        self.metrics[name] = {
            'type': 'counter',
            'metric': counter
        }
        
        logger.debug(f"Registered Prometheus counter: {metric_name}")
    
    def register_histogram(self, name: str, description: str, labels: Optional[List[str]] = None,
                         buckets: Optional[List[float]] = None) -> None:
        """
        Register a Prometheus histogram metric.
        
        Args:
            name: Metric name
            description: Metric description
            labels: Optional list of label names
            buckets: Optional list of bucket boundaries
        """
        labels = labels or []
        buckets = buckets or [0.01, 0.1, 0.5, 1.0, 5.0, 10.0, 30.0, 60.0, 120.0, 300.0, 600.0]
        metric_name = f"{self.component}_{name}"
        
        if self.mode == 'push':
            histogram = Histogram(metric_name, description, labels, buckets=buckets, registry=self.registry)
        else:
            histogram = Histogram(metric_name, description, labels, buckets=buckets)
        
        self.metrics[name] = {
            'type': 'histogram',
            'metric': histogram
        }
        
        logger.debug(f"Registered Prometheus histogram: {metric_name}")
    
    def register_summary(self, name: str, description: str, labels: Optional[List[str]] = None) -> None:
        """
        Register a Prometheus summary metric.
        
        Args:
            name: Metric name
            description: Metric description
            labels: Optional list of label names
        """
        labels = labels or []
        metric_name = f"{self.component}_{name}"
        
        if self.mode == 'push':
            summary = Summary(metric_name, description, labels, registry=self.registry)
        else:
            summary = Summary(metric_name, description, labels)
        
        self.metrics[name] = {
            'type': 'summary',
            'metric': summary
        }
        
        logger.debug(f"Registered Prometheus summary: {metric_name}")
    
    def update_gauge(self, name: str, value: float, labels: Optional[Dict[str, str]] = None) -> None:
        """
        Update a gauge metric.
        
        Args:
            name: Metric name
            value: Metric value
            labels: Optional dictionary of label values
        """
        if name not in self.metrics:
            logger.warning(f"Unknown metric: {name}")
            return
        
        metric_info = self.metrics[name]
        if metric_info['type'] != 'gauge':
            logger.warning(f"Metric {name} is not a gauge")
            return
        
        gauge = metric_info['metric']
        
        if labels:
            gauge.labels(**labels).set(value)
        else:
            gauge.set(value)
    
    def increment_counter(self, name: str, value: float = 1.0, labels: Optional[Dict[str, str]] = None) -> None:
        """
        Increment a counter metric.
        
        Args:
            name: Metric name
            value: Increment value
            labels: Optional dictionary of label values
        """
        if name not in self.metrics:
            logger.warning(f"Unknown metric: {name}")
            return
        
        metric_info = self.metrics[name]
        if metric_info['type'] != 'counter':
            logger.warning(f"Metric {name} is not a counter")
            return
        
        counter = metric_info['metric']
        
        if labels:
            counter.labels(**labels).inc(value)
        else:
            counter.inc(value)
    
    def observe_histogram(self, name: str, value: float, labels: Optional[Dict[str, str]] = None) -> None:
        """
        Observe a value for a histogram metric.
        
        Args:
            name: Metric name
            value: Observed value
            labels: Optional dictionary of label values
        """
        if name not in self.metrics:
            logger.warning(f"Unknown metric: {name}")
            return
        
        metric_info = self.metrics[name]
        if metric_info['type'] != 'histogram':
            logger.warning(f"Metric {name} is not a histogram")
            return
        
        histogram = metric_info['metric']
        
        if labels:
            histogram.labels(**labels).observe(value)
        else:
            histogram.observe(value)
    
    def observe_summary(self, name: str, value: float, labels: Optional[Dict[str, str]] = None) -> None:
        """
        Observe a value for a summary metric.
        
        Args:
            name: Metric name
            value: Observed value
            labels: Optional dictionary of label values
        """
        if name not in self.metrics:
            logger.warning(f"Unknown metric: {name}")
            return
        
        metric_info = self.metrics[name]
        if metric_info['type'] != 'summary':
            logger.warning(f"Metric {name} is not a summary")
            return
        
        summary = metric_info['metric']
        
        if labels:
            summary.labels(**labels).observe(value)
        else:
            summary.observe(value)
    
    def push_metrics(self) -> None:
        """Push metrics to the Prometheus push gateway."""
        if self.mode != 'push':
            logger.warning("push_metrics() called but exporter is not in push mode")
            return
        
        try:
            push_to_gateway(self.push_gateway, job=self.job_name, registry=self.registry)
            logger.debug(f"Pushed metrics to gateway: {self.push_gateway}")
        except Exception as e:
            logger.error(f"Error pushing metrics to gateway: {str(e)}", exc_info=True)
    
    def store_metrics(self, metrics: Dict[str, Any]) -> None:
        """
        Store metrics in Prometheus format.
        
        Args:
            metrics: Dictionary of metrics to store
        """
        timestamp = metrics.get('timestamp', time.time())
        
        # Process CPU metrics
        if 'cpu' in metrics:
            cpu_metrics = metrics['cpu']
            if 'percent' in cpu_metrics:
                self.update_gauge('cpu_utilization_percent', cpu_metrics['percent'])
            
            if 'percent_per_cpu' in cpu_metrics:
                for i, percent in enumerate(cpu_metrics['percent_per_cpu']):
                    self.update_gauge('cpu_core_utilization_percent', percent, {'core': str(i)})
            
            if 'load_avg' in cpu_metrics and len(cpu_metrics['load_avg']) >= 3:
                self.update_gauge('load_avg_1m', cpu_metrics['load_avg'][0])
                self.update_gauge('load_avg_5m', cpu_metrics['load_avg'][1])
                self.update_gauge('load_avg_15m', cpu_metrics['load_avg'][2])
        
        # Process memory metrics
        if 'memory' in metrics:
            memory_metrics = metrics['memory']
            
            if 'virtual' in memory_metrics:
                vm = memory_metrics['virtual']
                self.update_gauge('memory_total_bytes', vm.get('total', 0))
                self.update_gauge('memory_available_bytes', vm.get('available', 0))
                self.update_gauge('memory_used_bytes', vm.get('used', 0))
                self.update_gauge('memory_utilization_percent', vm.get('percent', 0))
            
            if 'swap' in memory_metrics:
                swap = memory_metrics['swap']
                self.update_gauge('swap_total_bytes', swap.get('total', 0))
                self.update_gauge('swap_used_bytes', swap.get('used', 0))
                self.update_gauge('swap_utilization_percent', swap.get('percent', 0))
        
        # Process disk metrics
        if 'disk' in metrics and 'usage' in metrics['disk']:
            for path, usage in metrics['disk']['usage'].items():
                labels = {'path': path}
                self.update_gauge('disk_total_bytes', usage.get('total', 0), labels)
                self.update_gauge('disk_used_bytes', usage.get('used', 0), labels)
                self.update_gauge('disk_free_bytes', usage.get('free', 0), labels)
                self.update_gauge('disk_utilization_percent', usage.get('percent', 0), labels)
        
        # Process network metrics
        if 'network' in metrics and 'rates' in metrics['network']:
            for nic, rate_info in metrics['network']['rates'].items():
                labels = {'interface': nic}
                self.update_gauge('network_bytes_sent_per_sec', rate_info.get('bytes_sent_per_sec', 0), labels)
                self.update_gauge('network_bytes_recv_per_sec', rate_info.get('bytes_recv_per_sec', 0), labels)
                self.update_gauge('network_packets_sent_per_sec', rate_info.get('packets_sent_per_sec', 0), labels)
                self.update_gauge('network_packets_recv_per_sec', rate_info.get('packets_recv_per_sec', 0), labels)
                self.update_gauge('network_errors_per_sec', 
                                rate_info.get('errin_per_sec', 0) + rate_info.get('errout_per_sec', 0), 
                                labels)
        
        # Process metrics for individual processes
        if 'processes' in metrics:
            for proc_name, proc_info in metrics['processes'].items():
                if 'error' in proc_info:
                    continue
                
                labels = {'process': proc_name, 'pid': str(proc_info.get('pid', 0))}
                self.update_gauge('process_cpu_percent', proc_info.get('cpu_percent', 0), labels)
                self.update_gauge('process_memory_rss', proc_info.get('memory_rss', 0), labels)
                self.update_gauge('process_memory_percent', proc_info.get('memory_percent', 0), labels)
                
                if 'open_files' in proc_info and proc_info['open_files'] != -1:
                    self.update_gauge('process_open_files', proc_info['open_files'], labels)
                
                if 'connections' in proc_info and proc_info['connections'] != -1:
                    self.update_gauge('process_connections', proc_info['connections'], labels)
        
        # Push metrics if in push mode
        if self.mode == 'push':
            self.push_metrics()
    
    def _register_default_metrics(self) -> None:
        """Register default metrics for typical monitoring needs."""
        # CPU metrics
        self.register_gauge('cpu_utilization_percent', 'CPU utilization percentage')
        self.register_gauge('cpu_core_utilization_percent', 'CPU utilization percentage per core', ['core'])
        self.register_gauge('load_avg_1m', 'System load average over 1 minute')
        self.register_gauge('load_avg_5m', 'System load average over 5 minutes')
        self.register_gauge('load_avg_15m', 'System load average over 15 minutes')
        
        # Memory metrics
        self.register_gauge('memory_total_bytes', 'Total memory in bytes')
        self.register_gauge('memory_available_bytes', 'Available memory in bytes')
        self.register_gauge('memory_used_bytes', 'Used memory in bytes')
        self.register_gauge('memory_utilization_percent', 'Memory utilization percentage')
        self.register_gauge('swap_total_bytes', 'Total swap memory in bytes')
        self.register_gauge('swap_used_bytes', 'Used swap memory in bytes')
        self.register_gauge('swap_utilization_percent', 'Swap utilization percentage')
        
        # Disk metrics
        self.register_gauge('disk_total_bytes', 'Total disk space in bytes', ['path'])
        self.register_gauge('disk_used_bytes', 'Used disk space in bytes', ['path'])
        self.register_gauge('disk_free_bytes', 'Free disk space in bytes', ['path'])
        self.register_gauge('disk_utilization_percent', 'Disk utilization percentage', ['path'])
        
        # Network metrics
        self.register_gauge('network_bytes_sent_per_sec', 'Network bytes sent per second', ['interface'])
        self.register_gauge('network_bytes_recv_per_sec', 'Network bytes received per second', ['interface'])
        self.register_gauge('network_packets_sent_per_sec', 'Network packets sent per second', ['interface'])
        self.register_gauge('network_packets_recv_per_sec', 'Network packets received per second', ['interface'])
        self.register_gauge('network_errors_per_sec', 'Network errors per second', ['interface'])
        
        # Process metrics
        self.register_gauge('process_cpu_percent', 'Process CPU utilization percentage', ['process', 'pid'])
        self.register_gauge('process_memory_rss', 'Process memory RSS in bytes', ['process', 'pid'])
        self.register_gauge('process_memory_percent', 'Process memory utilization percentage', ['process', 'pid'])
        self.register_gauge('process_open_files', 'Number of files opened by process', ['process', 'pid'])
        self.register_gauge('process_connections', 'Number of network connections by process', ['process', 'pid'])
        
        # Trading system metrics
        self.register_counter('order_count', 'Number of orders placed', ['exchange', 'symbol', 'side'])
        self.register_counter('trade_count', 'Number of trades executed', ['exchange', 'symbol', 'side'])
        self.register_gauge('order_book_imbalance', 'Order book imbalance ratio', ['exchange', 'symbol'])
        self.register_histogram('order_latency_seconds', 'Order placement latency in seconds', 
                           ['exchange', 'symbol', 'type'])
        self.register_histogram('execution_quality_price_improvement', 'Execution price improvement percentage', 
                           ['exchange', 'symbol', 'side'])