import asyncio
import logging
import time
import os
import platform
import psutil
from typing import Dict, List, Any, Optional, Callable, Set
from dataclasses import dataclass
import socket
import json

logger = logging.getLogger(__name__)

@dataclass
class ResourceThresholds:
    """Data class for resource utilization thresholds."""
    cpu_percent: float = 80.0  # CPU utilization percentage
    memory_percent: float = 80.0  # Memory utilization percentage
    disk_percent: float = 90.0  # Disk utilization percentage
    network_errors_rate: float = 10.0  # Network errors per second
    open_files: int = 1000  # Number of open files
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert thresholds to dictionary."""
        return {
            'cpu_percent': self.cpu_percent,
            'memory_percent': self.memory_percent,
            'disk_percent': self.disk_percent,
            'network_errors_rate': self.network_errors_rate,
            'open_files': self.open_files
        }

class ResourceMonitor:
    """
    Monitors system resource utilization including CPU, memory, disk, and network.
    """
    
    def __init__(self, config=None):
        """
        Initialize the resource monitor.
        
        Args:
            config: Configuration dictionary for the resource monitor
        """
        self.config = config or {}
        self.collect_interval = self.config.get('collect_interval', 30)  # seconds
        self.history_size = self.config.get('history_size', 1000)  # Number of data points to keep
        self.alert_thresholds = ResourceThresholds(
            **self.config.get('thresholds', {})
        )
        
        # Resource metrics history
        self.metrics_history = []
        
        # Alert subscriptions
        self.alert_subscribers = []
        
        # Monitored processes
        self.monitored_processes = {}  # Maps process names or PIDs to process objects
        
        # Running state
        self.running = False
        self._monitor_task = None
        
        # Previous network stats for calculating rates
        self._prev_network_stats = None
        self._prev_network_time = None
        
        # Disk paths to monitor
        self.disk_paths = self.config.get('disk_paths', ['/'])
        
        # Component to monitor (can be the hostname or a specific name)
        self.component_name = self.config.get('component_name', socket.gethostname())
    
    def register_process(self, process_name: str, pid: Optional[int] = None) -> bool:
        """
        Register a process to monitor.
        
        Args:
            process_name: Name of the process (for identification)
            pid: Process ID. If None, will try to find by name.
            
        Returns:
            True if process was registered successfully, False otherwise
        """
        if pid is not None:
            try:
                process = psutil.Process(pid)
                self.monitored_processes[process_name] = {
                    'pid': pid,
                    'process': process
                }
                logger.info(f"Registered process {process_name} (PID: {pid})")
                return True
            except psutil.NoSuchProcess:
                logger.warning(f"Process with PID {pid} not found")
                return False
        else:
            # Try to find by name
            found = False
            for proc in psutil.process_iter(['pid', 'name']):
                if process_name.lower() in proc.info['name'].lower():
                    self.monitored_processes[process_name] = {
                        'pid': proc.info['pid'],
                        'process': proc
                    }
                    logger.info(f"Registered process {process_name} (PID: {proc.info['pid']})")
                    found = True
                    break
            
            if not found:
                logger.warning(f"No process found with name: {process_name}")
            
            return found
    
    def set_thresholds(self, thresholds: Dict[str, Any]) -> None:
        """
        Set resource utilization thresholds.
        
        Args:
            thresholds: Dictionary of threshold values
        """
        # Update only the provided thresholds
        for key, value in thresholds.items():
            if hasattr(self.alert_thresholds, key):
                setattr(self.alert_thresholds, key, value)
            else:
                logger.warning(f"Unknown threshold: {key}")
    
    def subscribe_alerts(self, callback: Callable[[str, Dict[str, Any]], None]) -> None:
        """
        Subscribe to resource alerts.
        
        Args:
            callback: Function called with (alert_type, alert_data)
        """
        self.alert_subscribers.append(callback)
    
    async def start(self) -> None:
        """Start resource monitoring."""
        if self.running:
            logger.warning("Resource monitor already running")
            return
        
        self.running = True
        self._monitor_task = asyncio.create_task(self._monitoring_loop())
        logger.info("Resource monitor started")
    
    async def stop(self) -> None:
        """Stop resource monitoring."""
        if not self.running:
            logger.warning("Resource monitor not running")
            return
        
        self.running = False
        if self._monitor_task:
            await self._monitor_task
            self._monitor_task = None
        logger.info("Resource monitor stopped")
    
    async def _monitoring_loop(self) -> None:
        """Main monitoring loop that collects resource metrics at regular intervals."""
        while self.running:
            try:
                metrics = await self._collect_resource_metrics()
                self._add_metrics_to_history(metrics)
                await self._check_thresholds(metrics)
                
                await asyncio.sleep(self.collect_interval)
            except Exception as e:
                logger.error(f"Error in resource monitoring loop: {str(e)}", exc_info=True)
                await asyncio.sleep(1)  # Short delay before retrying after error
    
    async def _collect_resource_metrics(self) -> Dict[str, Any]:
        """
        Collect current resource metrics.
        
        Returns:
            Dictionary of resource metrics
        """
        # Create event loop for running synchronous calls in a thread pool
        loop = asyncio.get_running_loop()
        
        # Collect metrics in a thread pool to avoid blocking
        cpu_percent = await loop.run_in_executor(None, self._get_cpu_metrics)
        memory_info = await loop.run_in_executor(None, self._get_memory_metrics)
        disk_info = await loop.run_in_executor(None, self._get_disk_metrics)
        network_info = await loop.run_in_executor(None, self._get_network_metrics)
        process_info = await loop.run_in_executor(None, self._get_process_metrics)
        
        # Combine all metrics
        metrics = {
            'timestamp': time.time(),
            'component': self.component_name,
            'system': {
                'name': platform.node(),
                'platform': platform.system(),
                'release': platform.release(),
                'version': platform.version(),
                'uptime': self._get_uptime()
            },
            'cpu': cpu_percent,
            'memory': memory_info,
            'disk': disk_info,
            'network': network_info,
            'processes': process_info
        }
        
        return metrics
    
    def _get_cpu_metrics(self) -> Dict[str, Any]:
        """
        Get CPU metrics.
        
        Returns:
            Dictionary of CPU metrics
        """
        # Overall CPU percentage
        cpu_percent = psutil.cpu_percent(interval=0.1)
        
        # Per-CPU percentages
        cpu_percent_per_cpu = psutil.cpu_percent(interval=0.1, percpu=True)
        
        # CPU times
        cpu_times = psutil.cpu_times()
        cpu_times_dict = {
            'user': cpu_times.user,
            'system': cpu_times.system,
            'idle': cpu_times.idle
        }
        if hasattr(cpu_times, 'iowait'):
            cpu_times_dict['iowait'] = cpu_times.iowait
        
        # Load average (Unix-like systems only)
        load_avg = os.getloadavg() if hasattr(os, 'getloadavg') else [0, 0, 0]
        
        return {
            'percent': cpu_percent,
            'percent_per_cpu': cpu_percent_per_cpu,
            'times': cpu_times_dict,
            'load_avg': load_avg,
            'count': psutil.cpu_count(logical=True)
        }
    
    def _get_memory_metrics(self) -> Dict[str, Any]:
        """
        Get memory metrics.
        
        Returns:
            Dictionary of memory metrics
        """
        # Virtual memory
        vm = psutil.virtual_memory()
        vm_dict = {
            'total': vm.total,
            'available': vm.available,
            'used': vm.used,
            'free': vm.free,
            'percent': vm.percent
        }
        
        # Swap memory
        swap = psutil.swap_memory()
        swap_dict = {
            'total': swap.total,
            'used': swap.used,
            'free': swap.free,
            'percent': swap.percent
        }
        
        return {
            'virtual': vm_dict,
            'swap': swap_dict
        }
    
    def _get_disk_metrics(self) -> Dict[str, Any]:
        """
        Get disk metrics.
        
        Returns:
            Dictionary of disk metrics
        """
        disk_usage = {}
        
        # Get disk usage for each specified path
        for path in self.disk_paths:
            try:
                usage = psutil.disk_usage(path)
                disk_usage[path] = {
                    'total': usage.total,
                    'used': usage.used,
                    'free': usage.free,
                    'percent': usage.percent
                }
            except Exception as e:
                logger.warning(f"Error getting disk usage for {path}: {str(e)}")
        
        # Get disk I/O counters
        try:
            io_counters = psutil.disk_io_counters()
            io_dict = {
                'read_count': io_counters.read_count,
                'write_count': io_counters.write_count,
                'read_bytes': io_counters.read_bytes,
                'write_bytes': io_counters.write_bytes,
                'read_time': io_counters.read_time,
                'write_time': io_counters.write_time
            }
        except Exception as e:
            logger.warning(f"Error getting disk I/O counters: {str(e)}")
            io_dict = {}
        
        return {
            'usage': disk_usage,
            'io_counters': io_dict
        }
    
    def _get_network_metrics(self) -> Dict[str, Any]:
        """
        Get network metrics.
        
        Returns:
            Dictionary of network metrics
        """
        # Get network I/O counters
        try:
            io_counters = psutil.net_io_counters(pernic=True)
            
            # Calculate rates if we have previous measurements
            current_time = time.time()
            rates = {}
            
            if self._prev_network_stats and self._prev_network_time:
                time_diff = current_time - self._prev_network_time
                
                for nic, counters in io_counters.items():
                    if nic in self._prev_network_stats:
                        prev_counters = self._prev_network_stats[nic]
                        
                        # Calculate rates (bytes/sec, packets/sec, etc.)
                        rates[nic] = {
                            'bytes_sent_per_sec': (counters.bytes_sent - prev_counters.bytes_sent) / time_diff,
                            'bytes_recv_per_sec': (counters.bytes_recv - prev_counters.bytes_recv) / time_diff,
                            'packets_sent_per_sec': (counters.packets_sent - prev_counters.packets_sent) / time_diff,
                            'packets_recv_per_sec': (counters.packets_recv - prev_counters.packets_recv) / time_diff,
                            'errin_per_sec': (counters.errin - prev_counters.errin) / time_diff,
                            'errout_per_sec': (counters.errout - prev_counters.errout) / time_diff,
                            'dropin_per_sec': (counters.dropin - prev_counters.dropin) / time_diff,
                            'dropout_per_sec': (counters.dropout - prev_counters.dropout) / time_diff
                        }
            
            # Store current measurements for next time
            self._prev_network_stats = io_counters
            self._prev_network_time = current_time
            
            # Get connection information
            connections = psutil.net_connections()
            conn_stats = {
                'total': len(connections),
                'established': sum(1 for conn in connections if conn.status == 'ESTABLISHED'),
                'listen': sum(1 for conn in connections if conn.status == 'LISTEN'),
                'time_wait': sum(1 for conn in connections if conn.status == 'TIME_WAIT'),
                'close_wait': sum(1 for conn in connections if conn.status == 'CLOSE_WAIT')
            }
            
            # Format counters for JSON serialization
            io_dict = {}
            for nic, counters in io_counters.items():
                io_dict[nic] = {
                    'bytes_sent': counters.bytes_sent,
                    'bytes_recv': counters.bytes_recv,
                    'packets_sent': counters.packets_sent,
                    'packets_recv': counters.packets_recv,
                    'errin': counters.errin,
                    'errout': counters.errout,
                    'dropin': counters.dropin,
                    'dropout': counters.dropout
                }
            
            return {
                'io_counters': io_dict,
                'rates': rates,
                'connections': conn_stats
            }
        
        except Exception as e:
            logger.warning(f"Error getting network metrics: {str(e)}")
            return {}
    
    def _get_process_metrics(self) -> Dict[str, Any]:
        """
        Get metrics for monitored processes.
        
        Returns:
            Dictionary of process metrics
        """
        process_metrics = {}
        
        for name, proc_info in list(self.monitored_processes.items()):
            try:
                process = proc_info['process']
                
                if not process.is_running():
                    logger.warning(f"Process {name} (PID: {proc_info['pid']}) is no longer running")
                    del self.monitored_processes[name]
                    continue
                
                # Get process information
                with process.oneshot():  # Get multiple info in one shot
                    cpu_percent = process.cpu_percent()
                    memory_info = process.memory_info()
                    memory_percent = process.memory_percent()
                    num_threads = process.num_threads()
                    
                    # Get open files (if we have permission)
                    try:
                        open_files = len(process.open_files())
                    except (psutil.AccessDenied, psutil.ZombieProcess):
                        open_files = -1
                    
                    # Get connections (if we have permission)
                    try:
                        connections = len(process.connections())
                    except (psutil.AccessDenied, psutil.ZombieProcess):
                        connections = -1
                
                process_metrics[name] = {
                    'pid': proc_info['pid'],
                    'cpu_percent': cpu_percent,
                    'memory_rss': memory_info.rss,
                    'memory_vms': memory_info.vms,
                    'memory_percent': memory_percent,
                    'num_threads': num_threads,
                    'open_files': open_files,
                    'connections': connections,
                    'status': process.status()
                }
            
            except (psutil.NoSuchProcess, psutil.ZombieProcess):
                logger.warning(f"Process {name} (PID: {proc_info['pid']}) no longer exists")
                del self.monitored_processes[name]
            except psutil.AccessDenied:
                logger.warning(f"Access denied for process {name} (PID: {proc_info['pid']})")
                process_metrics[name] = {
                    'pid': proc_info['pid'],
                    'error': 'Access denied'
                }
            except Exception as e:
                logger.error(f"Error getting metrics for process {name}: {str(e)}")
                process_metrics[name] = {
                    'pid': proc_info['pid'],
                    'error': str(e)
                }
        
        return process_metrics
    
    def _get_uptime(self) -> float:
        """
        Get system uptime in seconds.
        
        Returns:
            Uptime in seconds
        """
        try:
            return time.time() - psutil.boot_time()
        except Exception as e:
            logger.warning(f"Error getting system uptime: {str(e)}")
            return 0.0
    
    def _add_metrics_to_history(self, metrics: Dict[str, Any]) -> None:
        """
        Add metrics to history and trim if necessary.
        
        Args:
            metrics: Dictionary of resource metrics
        """
        self.metrics_history.append(metrics)
        
        # Trim history if it exceeds the maximum size
        while len(self.metrics_history) > self.history_size:
            self.metrics_history.pop(0)
    
    async def _check_thresholds(self, metrics: Dict[str, Any]) -> None:
        """
        Check if any resource metrics exceed thresholds and generate alerts.
        
        Args:
            metrics: Dictionary of resource metrics
        """
        alerts = []
        
        # Check CPU utilization
        if 'cpu' in metrics and 'percent' in metrics['cpu']:
            cpu_percent = metrics['cpu']['percent']
            if cpu_percent > self.alert_thresholds.cpu_percent:
                alerts.append({
                    'type': 'cpu_utilization',
                    'level': 'warning',
                    'message': f'CPU utilization {cpu_percent:.1f}% exceeds threshold {self.alert_thresholds.cpu_percent:.1f}%',
                    'value': cpu_percent,
                    'threshold': self.alert_thresholds.cpu_percent,
                    'timestamp': metrics['timestamp']
                })
        
        # Check memory utilization
        if 'memory' in metrics and 'virtual' in metrics['memory'] and 'percent' in metrics['memory']['virtual']:
            memory_percent = metrics['memory']['virtual']['percent']
            if memory_percent > self.alert_thresholds.memory_percent:
                alerts.append({
                    'type': 'memory_utilization',
                    'level': 'warning',
                    'message': f'Memory utilization {memory_percent:.1f}% exceeds threshold {self.alert_thresholds.memory_percent:.1f}%',
                    'value': memory_percent,
                    'threshold': self.alert_thresholds.memory_percent,
                    'timestamp': metrics['timestamp']
                })
        
        # Check disk utilization
        if 'disk' in metrics and 'usage' in metrics['disk']:
            for path, usage in metrics['disk']['usage'].items():
                if 'percent' in usage:
                    disk_percent = usage['percent']
                    if disk_percent > self.alert_thresholds.disk_percent:
                        alerts.append({
                            'type': 'disk_utilization',
                            'level': 'warning',
                            'message': f'Disk utilization for {path} is {disk_percent:.1f}%, exceeds threshold {self.alert_thresholds.disk_percent:.1f}%',
                            'path': path,
                            'value': disk_percent,
                            'threshold': self.alert_thresholds.disk_percent,
                            'timestamp': metrics['timestamp']
                        })
        
        # Check network error rates
        if 'network' in metrics and 'rates' in metrics['network']:
            for nic, rate_info in metrics['network']['rates'].items():
                if 'errin_per_sec' in rate_info and 'errout_per_sec' in rate_info:
                    error_rate = rate_info['errin_per_sec'] + rate_info['errout_per_sec']
                    if error_rate > self.alert_thresholds.network_errors_rate:
                        alerts.append({
                            'type': 'network_errors',
                            'level': 'warning',
                            'message': f'Network error rate for {nic} is {error_rate:.1f}/s, exceeds threshold {self.alert_thresholds.network_errors_rate:.1f}/s',
                            'interface': nic,
                            'value': error_rate,
                            'threshold': self.alert_thresholds.network_errors_rate,
                            'timestamp': metrics['timestamp']
                        })
        
        # Check process metrics
        if 'processes' in metrics:
            for proc_name, proc_info in metrics['processes'].items():
                if 'error' in proc_info:
                    continue
                
                # Check process CPU usage
                if 'cpu_percent' in proc_info:
                    cpu_percent = proc_info['cpu_percent']
                    if cpu_percent > self.alert_thresholds.cpu_percent:
                        alerts.append({
                            'type': 'process_cpu_utilization',
                            'level': 'warning',
                            'message': f'Process {proc_name} (PID: {proc_info["pid"]}) CPU utilization {cpu_percent:.1f}% exceeds threshold {self.alert_thresholds.cpu_percent:.1f}%',
                            'process': proc_name,
                            'pid': proc_info['pid'],
                            'value': cpu_percent,
                            'threshold': self.alert_thresholds.cpu_percent,
                            'timestamp': metrics['timestamp']
                        })
                
                # Check process memory usage
                if 'memory_percent' in proc_info:
                    memory_percent = proc_info['memory_percent']
                    if memory_percent > self.alert_thresholds.memory_percent:
                        alerts.append({
                            'type': 'process_memory_utilization',
                            'level': 'warning',
                            'message': f'Process {proc_name} (PID: {proc_info["pid"]}) memory utilization {memory_percent:.1f}% exceeds threshold {self.alert_thresholds.memory_percent:.1f}%',
                            'process': proc_name,
                            'pid': proc_info['pid'],
                            'value': memory_percent,
                            'threshold': self.alert_thresholds.memory_percent,
                            'timestamp': metrics['timestamp']
                        })
                
                # Check open files
                if 'open_files' in proc_info and proc_info['open_files'] != -1:
                    open_files = proc_info['open_files']
                    if open_files > self.alert_thresholds.open_files:
                        alerts.append({
                            'type': 'process_open_files',
                            'level': 'warning',
                            'message': f'Process {proc_name} (PID: {proc_info["pid"]}) has {open_files} open files, exceeds threshold {self.alert_thresholds.open_files}',
                            'process': proc_name,
                            'pid': proc_info['pid'],
                            'value': open_files,
                            'threshold': self.alert_thresholds.open_files,
                            'timestamp': metrics['timestamp']
                        })
        
        # Notify subscribers of any alerts
        for alert in alerts:
            for callback in self.alert_subscribers:
                try:
                    if asyncio.iscoroutinefunction(callback):
                        await callback(alert['type'], alert)
                    else:
                        callback(alert['type'], alert)
                except Exception as e:
                    logger.error(f"Error in alert subscriber: {str(e)}", exc_info=True)
    
    def get_latest_metrics(self) -> Optional[Dict[str, Any]]:
        """
        Get the most recent resource metrics.
        
        Returns:
            Dictionary of resource metrics or None if no metrics are available
        """
        if not self.metrics_history:
            return None
        
        return self.metrics_history[-1]
    
    def get_metrics_history(self, start_time: Optional[float] = None, 
                          end_time: Optional[float] = None,
                          limit: Optional[int] = None) -> List[Dict[str, Any]]:
        """
        Get historical resource metrics.
        
        Args:
            start_time: Optional start time (Unix timestamp)
            end_time: Optional end time (Unix timestamp)
            limit: Maximum number of data points to return
            
        Returns:
            List of resource metrics
        """
        filtered_metrics = []
        
        for metrics in self.metrics_history:
            # Filter by time range
            if start_time is not None and metrics['timestamp'] < start_time:
                continue
            if end_time is not None and metrics['timestamp'] > end_time:
                continue
            
            filtered_metrics.append(metrics)
        
        # Apply limit if specified
        if limit is not None and limit > 0:
            filtered_metrics = filtered_metrics[-limit:]
        
        return filtered_metrics
    
    def collect_metrics(self) -> Dict[str, Any]:
        """
        Collect current resource metrics synchronously.
        
        Returns:
            Dictionary of resource metrics
        """
        # This is a synchronous version for integration with other components
        cpu_percent = self._get_cpu_metrics()
        memory_info = self._get_memory_metrics()
        disk_info = self._get_disk_metrics()
        network_info = self._get_network_metrics()
        process_info = self._get_process_metrics()
        
        # Combine all metrics
        metrics = {
            'timestamp': time.time(),
            'component': self.component_name,
            'system': {
                'name': platform.node(),
                'platform': platform.system(),
                'release': platform.release(),
                'version': platform.version(),
                'uptime': self._get_uptime()
            },
            'cpu': cpu_percent,
            'memory': memory_info,
            'disk': disk_info,
            'network': network_info,
            'processes': process_info
        }
        
        return metrics