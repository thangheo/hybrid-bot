import logging
import json
import os
import shutil
import time
from typing import Dict, List, Any, Optional
import aiohttp
import asyncio

logger = logging.getLogger(__name__)

class GrafanaClient:
    """
    Client for interacting with Grafana API to manage dashboards and alerts.
    """
    
    def __init__(self, config=None):
        """
        Initialize the Grafana client.
        
        Args:
            config: Configuration dictionary for the Grafana client
        """
        self.config = config or {}
        self.base_url = self.config.get('url', 'http://localhost:3000')
        self.api_key = self.config.get('api_key', '')
        self.username = self.config.get('username', '')
        self.password = self.config.get('password', '')
        self.organization = self.config.get('organization', 'Main Org.')
        self.verify_ssl = self.config.get('verify_ssl', True)
        self.session = None
        
        # Configuration for dashboard provisioning
        self.dashboard_dir = self.config.get('dashboard_dir', '/etc/grafana/dashboards')
        self.datasource_dir = self.config.get('datasource_dir', '/etc/grafana/provisioning/datasources')
    
    async def start(self) -> None:
        """Initialize the client session."""
        self.session = aiohttp.ClientSession()
        
        # Test connection to Grafana
        try:
            await self.get_health()
            logger.info("Successfully connected to Grafana")
        except Exception as e:
            logger.warning(f"Could not connect to Grafana: {str(e)}")
    
    async def stop(self) -> None:
        """Close the client session."""
        if self.session:
            await self.session.close()
            self.session = None
    
    async def _request(self, method: str, path: str, data: Any = None, 
                     params: Dict[str, Any] = None) -> Dict[str, Any]:
        """
        Send a request to the Grafana API.
        
        Args:
            method: HTTP method
            path: API path
            data: Request data
            params: Query parameters
            
        Returns:
            Response data
        """
        if not self.session:
            await self.start()
        
        headers = {'Content-Type': 'application/json'}
        
        # Use API key if available, otherwise use basic auth
        if self.api_key:
            headers['Authorization'] = f"Bearer {self.api_key}"
        else:
            auth = aiohttp.BasicAuth(self.username, self.password)
        
        url = f"{self.base_url}/api/{path.lstrip('/')}"
        
        try:
            if self.api_key:
                async with self.session.request(
                    method, 
                    url, 
                    json=data, 
                    params=params, 
                    headers=headers,
                    ssl=self.verify_ssl
                ) as response:
                    if response.status >= 400:
                        text = await response.text()
                        raise Exception(f"Grafana API error ({response.status}): {text}")
                    
                    return await response.json()
            else:
                async with self.session.request(
                    method, 
                    url, 
                    json=data, 
                    params=params, 
                    headers=headers,
                    auth=auth,
                    ssl=self.verify_ssl
                ) as response:
                    if response.status >= 400:
                        text = await response.text()
                        raise Exception(f"Grafana API error ({response.status}): {text}")
                    
                    return await response.json()
        except aiohttp.ClientError as e:
            logger.error(f"HTTP error in Grafana API request: {str(e)}")
            raise
    
    async def get_health(self) -> Dict[str, Any]:
        """
        Get Grafana health status.
        
        Returns:
            Health status
        """
        return await self._request('GET', 'health')
    
    async def get_datasources(self) -> List[Dict[str, Any]]:
        """
        Get all datasources.
        
        Returns:
            List of datasources
        """
        return await self._request('GET', 'datasources')
    
    async def create_datasource(self, datasource: Dict[str, Any]) -> Dict[str, Any]:
        """
        Create a new datasource.
        
        Args:
            datasource: Datasource configuration
            
        Returns:
            Created datasource
        """
        return await self._request('POST', 'datasources', data=datasource)
    
    async def get_dashboard(self, uid: str) -> Dict[str, Any]:
        """
        Get a dashboard by UID.
        
        Args:
            uid: Dashboard UID
            
        Returns:
            Dashboard configuration
        """
        return await self._request('GET', f'dashboards/uid/{uid}')
    
    async def create_dashboard(self, dashboard: Dict[str, Any], overwrite: bool = True) -> Dict[str, Any]:
        """
        Create or update a dashboard.
        
        Args:
            dashboard: Dashboard configuration
            overwrite: Whether to overwrite an existing dashboard
            
        Returns:
            Created dashboard
        """
        payload = {
            'dashboard': dashboard,
            'overwrite': overwrite
        }
        
        return await self._request('POST', 'dashboards/db', data=payload)
    
    async def create_folder(self, title: str, uid: Optional[str] = None) -> Dict[str, Any]:
        """
        Create a folder.
        
        Args:
            title: Folder title
            uid: Optional folder UID
            
        Returns:
            Created folder
        """
        payload = {
            'title': title
        }
        
        if uid:
            payload['uid'] = uid
        
        return await self._request('POST', 'folders', data=payload)
    
    async def get_folders(self) -> List[Dict[str, Any]]:
        """
        Get all folders.
        
        Returns:
            List of folders
        """
        return await self._request('GET', 'folders')
    
    async def get_folder_by_uid(self, uid: str) -> Dict[str, Any]:
        """
        Get a folder by UID.
        
        Args:
            uid: Folder UID
            
        Returns:
            Folder configuration
        """
        return await self._request('GET', f'folders/{uid}')
    
    async def create_alert(self, alert_data: Dict[str, Any]) -> Dict[str, Any]:
        """
        Create a new alert rule.
        
        Args:
            alert_data: Alert rule configuration
            
        Returns:
            Created alert rule
        """
        return await self._request('POST', 'alerting/rule', data=alert_data)
    
    async def get_alert_rules(self) -> List[Dict[str, Any]]:
        """
        Get all alert rules.
        
        Returns:
            List of alert rules
        """
        return await self._request('GET', 'alerting/rules')
    
    async def get_alert_rule(self, uid: str) -> Dict[str, Any]:
        """
        Get an alert rule by UID.
        
        Args:
            uid: Alert rule UID
            
        Returns:
            Alert rule configuration
        """
        return await self._request('GET', f'alerting/rule/{uid}')
    
    async def update_alert_rule(self, uid: str, alert_data: Dict[str, Any]) -> Dict[str, Any]:
        """
        Update an alert rule.
        
        Args:
            uid: Alert rule UID
            alert_data: Alert rule configuration
            
        Returns:
            Updated alert rule
        """
        return await self._request('PUT', f'alerting/rule/{uid}', data=alert_data)
    
    async def delete_alert_rule(self, uid: str) -> Dict[str, Any]:
        """
        Delete an alert rule.
        
        Args:
            uid: Alert rule UID
            
        Returns:
            Response data
        """
        return await self._request('DELETE', f'alerting/rule/{uid}')
    
    async def get_alert_instances(self) -> List[Dict[str, Any]]:
        """
        Get all alert instances (currently firing alerts).
        
        Returns:
            List of alert instances
        """
        return await self._request('GET', 'alerting/instances')
    
    def provision_dashboard(self, dashboard_data: Dict[str, Any], name: str) -> bool:
        """
        Provision a dashboard by writing it to the filesystem (for Grafana provisioning).
        
        Args:
            dashboard_data: Dashboard configuration
            name: Dashboard file name (without extension)
            
        Returns:
            True if successful, False otherwise
        """
        if not os.path.isdir(self.dashboard_dir):
            try:
                os.makedirs(self.dashboard_dir, exist_ok=True)
            except Exception as e:
                logger.error(f"Error creating dashboard directory: {str(e)}")
                return False
        
        dashboard_file = os.path.join(self.dashboard_dir, f"{name}.json")
        
        try:
            with open(dashboard_file, 'w') as f:
                json.dump(dashboard_data, f, indent=2)
            
            logger.info(f"Provisioned dashboard file: {dashboard_file}")
            return True
        except Exception as e:
            logger.error(f"Error writing dashboard file: {str(e)}")
            return False
    
    def provision_datasource(self, datasource_data: Dict[str, Any], name: str) -> bool:
        """
        Provision a datasource by writing it to the filesystem (for Grafana provisioning).
        
        Args:
            datasource_data: Datasource configuration
            name: Datasource file name (without extension)
            
        Returns:
            True if successful, False otherwise
        """
        if not os.path.isdir(self.datasource_dir):
            try:
                os.makedirs(self.datasource_dir, exist_ok=True)
            except Exception as e:
                logger.error(f"Error creating datasource directory: {str(e)}")
                return False
        
        datasource_file = os.path.join(self.datasource_dir, f"{name}.yaml")
        
        try:
            # Convert to YAML format expected by Grafana
            yaml_data = {
                'apiVersion': 1,
                'datasources': [datasource_data]
            }
            
            # Simple YAML serialization (in practice, you'd use a proper YAML library)
            with open(datasource_file, 'w') as f:
                f.write("apiVersion: 1\n")
                f.write("datasources:\n")
                for key, value in datasource_data.items():
                    f.write(f"  - {key}: {json.dumps(value)}\n")
            
            logger.info(f"Provisioned datasource file: {datasource_file}")
            return True
        except Exception as e:
            logger.error(f"Error writing datasource file: {str(e)}")
            return False
    
    async def create_system_monitoring_dashboard(self, title: str = "System Monitoring", 
                                               folder_name: str = "Trading System",
                                               uid: str = "trading-system-monitoring") -> Dict[str, Any]:
        """
        Create a pre-configured system monitoring dashboard.
        
        Args:
            title: Dashboard title
            folder_name: Folder name
            uid: Dashboard UID
            
        Returns:
            Created dashboard
        """
        # Create folder if it doesn't exist
        folders = await self.get_folders()
        folder_id = None
        
        for folder in folders:
            if folder['title'] == folder_name:
                folder_id = folder['id']
                break
        
        if folder_id is None:
            folder = await self.create_folder(folder_name)
            folder_id = folder['id']
        
        # Define a basic system monitoring dashboard
        dashboard = {
            'uid': uid,
            'title': title,
            'tags': ['system', 'monitoring', 'trading'],
            'timezone': 'browser',
            'schemaVersion': 23,
            'version': 1,
            'refresh': '30s',
            'panels': [
                # CPU Utilization
                {
                    'id': 1,
                    'gridPos': {'h': 8, 'w': 12, 'x': 0, 'y': 0},
                    'type': 'graph',
                    'title': 'CPU Utilization',
                    'datasource': 'Prometheus',
                    'targets': [
                        {
                            'expr': 'monitoring_cpu_utilization_percent',
                            'legendFormat': 'CPU %',
                            'refId': 'A'
                        }
                    ],
                    'yaxes': [
                        {'format': 'percent', 'label': 'CPU %', 'min': 0, 'max': 100},
                        {'format': 'short', 'label': '', 'show': False}
                    ]
                },
                # Memory Utilization
                {
                    'id': 2,
                    'gridPos': {'h': 8, 'w': 12, 'x': 12, 'y': 0},
                    'type': 'graph',
                    'title': 'Memory Utilization',
                    'datasource': 'Prometheus',
                    'targets': [
                        {
                            'expr': 'monitoring_memory_utilization_percent',
                            'legendFormat': 'Memory %',
                            'refId': 'A'
                        }
                    ],
                    'yaxes': [
                        {'format': 'percent', 'label': 'Memory %', 'min': 0, 'max': 100},
                        {'format': 'short', 'label': '', 'show': False}
                    ]
                },
                # Disk Utilization
                {
                    'id': 3,
                    'gridPos': {'h': 8, 'w': 12, 'x': 0, 'y': 8},
                    'type': 'graph',
                    'title': 'Disk Utilization',
                    'datasource': 'Prometheus',
                    'targets': [
                        {
                            'expr': 'monitoring_disk_utilization_percent',
                            'legendFormat': '{{path}}',
                            'refId': 'A'
                        }
                    ],
                    'yaxes': [
                        {'format': 'percent', 'label': 'Disk %', 'min': 0, 'max': 100},
                        {'format': 'short', 'label': '', 'show': False}
                    ]
                },
                # Network Traffic
                {
                    'id': 4,
                    'gridPos': {'h': 8, 'w': 12, 'x': 12, 'y': 8},
                    'type': 'graph',
                    'title': 'Network Traffic',
                    'datasource': 'Prometheus',
                    'targets': [
                        {
                            'expr': 'monitoring_network_bytes_sent_per_sec',
                            'legendFormat': 'Sent {{interface}}',
                            'refId': 'A'
                        },
                        {
                            'expr': 'monitoring_network_bytes_recv_per_sec',
                            'legendFormat': 'Received {{interface}}',
                            'refId': 'B'
                        }
                    ],
                    'yaxes': [
                        {'format': 'Bps', 'label': 'Throughput'},
                        {'format': 'short', 'label': '', 'show': False}
                    ]
                },
                # Process CPU Usage
                {
                    'id': 5,
                    'gridPos': {'h': 8, 'w': 12, 'x': 0, 'y': 16},
                    'type': 'graph',
                    'title': 'Process CPU Usage',
                    'datasource': 'Prometheus',
                    'targets': [
                        {
                            'expr': 'monitoring_process_cpu_percent{process!=""}',
                            'legendFormat': '{{process}}',
                            'refId': 'A'
                        }
                    ],
                    'yaxes': [
                        {'format': 'percent', 'label': 'CPU %', 'min': 0},
                        {'format': 'short', 'label': '', 'show': False}
                    ]
                },
                # Process Memory Usage
                {
                    'id': 6,
                    'gridPos': {'h': 8, 'w': 12, 'x': 12, 'y': 16},
                    'type': 'graph',
                    'title': 'Process Memory Usage',
                    'datasource': 'Prometheus',
                    'targets': [
                        {
                            'expr': 'monitoring_process_memory_percent{process!=""}',
                            'legendFormat': '{{process}}',
                            'refId': 'A'
                        }
                    ],
                    'yaxes': [
                        {'format': 'percent', 'label': 'Memory %', 'min': 0},
                        {'format': 'short', 'label': '', 'show': False}
                    ]
                }
            ]
        }
        
        # Create the dashboard
        return await self.create_dashboard(dashboard)
    
    async def create_trading_performance_dashboard(self, title: str = "Trading Performance", 
                                                 folder_name: str = "Trading System",
                                                 uid: str = "trading-performance") -> Dict[str, Any]:
        """
        Create a pre-configured trading performance dashboard.
        
        Args:
            title: Dashboard title
            folder_name: Folder name
            uid: Dashboard UID
            
        Returns:
            Created dashboard
        """
        # Create folder if it doesn't exist
        folders = await self.get_folders()
        folder_id = None
        
        for folder in folders:
            if folder['title'] == folder_name:
                folder_id = folder['id']
                break
        
        if folder_id is None:
            folder = await self.create_folder(folder_name)
            folder_id = folder['id']
        
        # Define a trading performance dashboard
        dashboard = {
            'uid': uid,
            'title': title,
            'tags': ['trading', 'performance', 'finance'],
            'timezone': 'browser',
            'schemaVersion': 23,
            'version': 1,
            'refresh': '1m',
            'panels': [
                # P&L Chart
                {
                    'id': 1,
                    'gridPos': {'h': 8, 'w': 24, 'x': 0, 'y': 0},
                    'type': 'graph',
                    'title': 'Profit & Loss',
                    'datasource': 'Prometheus',
                    'targets': [
                        {
                            'expr': 'monitoring_trading_pnl_daily',
                            'legendFormat': 'Daily P&L',
                            'refId': 'A'
                        },
                        {
                            'expr': 'monitoring_trading_pnl_cumulative',
                            'legendFormat': 'Cumulative P&L',
                            'refId': 'B'
                        }
                    ],
                    'yaxes': [
                        {'format': 'currencyUSD', 'label': 'P&L'},
                        {'format': 'short', 'label': '', 'show': False}
                    ]
                },
                # Order Count
                {
                    'id': 2,
                    'gridPos': {'h': 8, 'w': 12, 'x': 0, 'y': 8},
                    'type': 'graph',
                    'title': 'Order Count',
                    'datasource': 'Prometheus',
                    'targets': [
                        {
                            'expr': 'sum(increase(monitoring_order_count[1h])) by (exchange, side)',
                            'legendFormat': '{{exchange}} - {{side}}',
                            'refId': 'A'
                        }
                    ],
                    'yaxes': [
                        {'format': 'short', 'label': 'Orders', 'min': 0},
                        {'format': 'short', 'label': '', 'show': False}
                    ]
                },
                # Order Latency
                {
                    'id': 3,
                    'gridPos': {'h': 8, 'w': 12, 'x': 12, 'y': 8},
                    'type': 'graph',
                    'title': 'Order Latency',
                    'datasource': 'Prometheus',
                    'targets': [
                        {
                            'expr': 'histogram_quantile(0.95, sum(rate(monitoring_order_latency_seconds_bucket[5m])) by (le, exchange))',
                            'legendFormat': '95th percentile - {{exchange}}',
                            'refId': 'A'
                        },
                        {
                            'expr': 'histogram_quantile(0.50, sum(rate(monitoring_order_latency_seconds_bucket[5m])) by (le, exchange))',
                            'legendFormat': 'Median - {{exchange}}',
                            'refId': 'B'
                        }
                    ],
                    'yaxes': [
                        {'format': 's', 'label': 'Latency'},
                        {'format': 'short', 'label': '', 'show': False}
                    ]
                },
                # Order Book Imbalance
                {
                    'id': 4,
                    'gridPos': {'h': 8, 'w': 12, 'x': 0, 'y': 16},
                    'type': 'graph',
                    'title': 'Order Book Imbalance',
                    'datasource': 'Prometheus',
                    'targets': [
                        {
                            'expr': 'monitoring_order_book_imbalance',
                            'legendFormat': '{{exchange}} - {{symbol}}',
                            'refId': 'A'
                        }
                    ],
                    'yaxes': [
                        {'format': 'percentunit', 'label': 'Imbalance', 'min': -1, 'max': 1},
                        {'format': 'short', 'label': '', 'show': False}
                    ]
                },
                # Execution Quality
                {
                    'id': 5,
                    'gridPos': {'h': 8, 'w': 12, 'x': 12, 'y': 16},
                    'type': 'graph',
                    'title': 'Execution Quality (Price Improvement)',
                    'datasource': 'Prometheus',
                    'targets': [
                        {
                            'expr': 'histogram_quantile(0.50, sum(rate(monitoring_execution_quality_price_improvement_bucket[5m])) by (le, exchange, symbol))',
                            'legendFormat': '{{exchange}} - {{symbol}}',
                            'refId': 'A'
                        }
                    ],
                    'yaxes': [
                        {'format': 'percentunit', 'label': 'Price Improvement'},
                        {'format': 'short', 'label': '', 'show': False}
                    ]
                }
            ]
        }
        
        # Create the dashboard
        return await self.create_dashboard(dashboard)