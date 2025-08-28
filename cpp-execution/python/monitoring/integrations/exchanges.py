import logging
import asyncio
import time
import json
from typing import Dict, List, Any, Optional, Callable
import aiohttp
import hmac
import hashlib
import base64
import urllib.parse
from datetime import datetime

logger = logging.getLogger(__name__)

class ExchangeMonitor:
    """
    Monitors exchange status and API endpoints for availability and performance.
    """
    
    def __init__(self, config=None):
        """
        Initialize the exchange monitor.
        
        Args:
            config: Configuration dictionary for the exchange monitor
        """
        self.config = config or {}
        self.exchanges = {}  # Maps exchange names to configurations
        self.status_cache = {}  # Caches exchange status information
        self.latency_history = {}  # Keeps track of API endpoint latency
        self.check_interval = self.config.get('check_interval', 60)  # seconds
        self.history_size = self.config.get('history_size', 1000)  # Number of data points to keep
        self.status_subscribers = []  # Callbacks for status changes
        self.running = False
        self._monitor_task = None
        self.session = None  # aiohttp session
    
    def register_exchange(self, name: str, config: Dict[str, Any]) -> None:
        """
        Register an exchange to monitor.
        
        Args:
            name: Exchange name
            config: Exchange configuration (endpoints, credentials, etc.)
        """
        self.exchanges[name] = config
        self.status_cache[name] = {
            'status': 'unknown',
            'last_check': 0,
            'message': 'Not checked yet',
            'api_status': {}
        }
        self.latency_history[name] = {}
        
        # Initialize latency history for each API endpoint
        for endpoint_name, endpoint_config in config.get('endpoints', {}).items():
            self.latency_history[name][endpoint_name] = []
        
        logger.info(f"Registered exchange for monitoring: {name}")
    
    def subscribe_status(self, callback: Callable[[str, Dict[str, Any], Dict[str, Any]], None]) -> None:
        """
        Subscribe to exchange status changes.
        
        Args:
            callback: Function called with (exchange_name, old_status, new_status)
        """
        self.status_subscribers.append(callback)
    
    async def start(self) -> None:
        """Start exchange monitoring."""
        if self.running:
            logger.warning("Exchange monitor already running")
            return
        
        self.running = True
        self.session = aiohttp.ClientSession()
        self._monitor_task = asyncio.create_task(self._monitoring_loop())
        logger.info("Exchange monitor started")
    
    async def stop(self) -> None:
        """Stop exchange monitoring."""
        if not self.running:
            logger.warning("Exchange monitor not running")
            return
        
        self.running = False
        
        if self._monitor_task:
            await self._monitor_task
            self._monitor_task = None
        
        if self.session:
            await self.session.close()
            self.session = None
        
        logger.info("Exchange monitor stopped")
    
    async def _monitoring_loop(self) -> None:
        """Main monitoring loop that checks exchange status at regular intervals."""
        while self.running:
            try:
                for exchange_name in self.exchanges:
                    await self._check_exchange(exchange_name)
                
                await asyncio.sleep(self.check_interval)
            except Exception as e:
                logger.error(f"Error in exchange monitoring loop: {str(e)}", exc_info=True)
                await asyncio.sleep(1)  # Short delay before retrying after error
    
    async def _check_exchange(self, exchange_name: str) -> None:
        """
        Check the status of an exchange.
        
        Args:
            exchange_name: Name of the exchange to check
        """
        if exchange_name not in self.exchanges:
            logger.warning(f"Unknown exchange: {exchange_name}")
            return
        
        exchange_config = self.exchanges[exchange_name]
        old_status = self.status_cache[exchange_name].copy()
        new_status = {
            'status': 'unknown',
            'last_check': time.time(),
            'message': '',
            'api_status': {}
        }
        
        # Check each API endpoint
        api_statuses = []
        
        for endpoint_name, endpoint_config in exchange_config.get('endpoints', {}).items():
            endpoint_status = await self._check_endpoint(exchange_name, endpoint_name, endpoint_config)
            new_status['api_status'][endpoint_name] = endpoint_status
            api_statuses.append(endpoint_status['status'])
        
        # Determine overall exchange status
        if 'outage' in api_statuses:
            new_status['status'] = 'outage'
            new_status['message'] = 'One or more critical API endpoints are down'
        elif 'degraded' in api_statuses:
            new_status['status'] = 'degraded'
            new_status['message'] = 'One or more API endpoints have high latency'
        elif 'operational' in api_statuses:
            new_status['status'] = 'operational'
            new_status['message'] = 'All API endpoints are operational'
        else:
            new_status['status'] = 'unknown'
            new_status['message'] = 'No API endpoints checked'
        
        # Update status cache
        self.status_cache[exchange_name] = new_status
        
        # Notify subscribers if status changed
        if old_status['status'] != new_status['status']:
            logger.info(f"Exchange {exchange_name} status changed from {old_status['status']} to {new_status['status']}")
            
            for callback in self.status_subscribers:
                try:
                    if asyncio.iscoroutinefunction(callback):
                        await callback(exchange_name, old_status, new_status)
                    else:
                        callback(exchange_name, old_status, new_status)
                except Exception as e:
                    logger.error(f"Error in exchange status subscriber: {str(e)}", exc_info=True)
    
    async def _check_endpoint(self, exchange_name: str, endpoint_name: str, 
                            endpoint_config: Dict[str, Any]) -> Dict[str, Any]:
        """
        Check the status of an API endpoint.
        
        Args:
            exchange_name: Name of the exchange
            endpoint_name: Name of the API endpoint
            endpoint_config: Configuration for the API endpoint
            
        Returns:
            Dictionary with endpoint status information
        """
        url = endpoint_config.get('url')
        method = endpoint_config.get('method', 'GET')
        headers = endpoint_config.get('headers', {})
        params = endpoint_config.get('params', {})
        data = endpoint_config.get('data')
        timeout = endpoint_config.get('timeout', 10)  # seconds
        expected_status = endpoint_config.get('expected_status', 200)
        response_check = endpoint_config.get('response_check')
        critical = endpoint_config.get('critical', True)
        
        if not url:
            logger.warning(f"No URL configured for endpoint {endpoint_name} of exchange {exchange_name}")
            return {
                'status': 'unknown',
                'message': 'No URL configured',
                'latency': 0,
                'timestamp': time.time()
            }
        
        # Sign request if authentication is required
        if endpoint_config.get('auth_required', False):
            auth_type = endpoint_config.get('auth_type', 'hmac')
            
            if auth_type == 'hmac':
                await self._sign_hmac_request(exchange_name, endpoint_config, method, url, params, headers)
            elif auth_type == 'jwt':
                await self._sign_jwt_request(exchange_name, endpoint_config, headers)
            elif auth_type == 'api_key':
                await self._sign_api_key_request(exchange_name, endpoint_config, headers)
            else:
                logger.warning(f"Unknown auth type: {auth_type} for endpoint {endpoint_name} of exchange {exchange_name}")
        
        try:
            start_time = time.time()
            
            async with self.session.request(
                method, 
                url, 
                headers=headers, 
                params=params, 
                data=data, 
                timeout=timeout
            ) as response:
                latency = time.time() - start_time
                
                # Record latency
                self._record_latency(exchange_name, endpoint_name, latency)
                
                # Check response status code
                status_ok = response.status == expected_status
                
                # Check response content if configured
                content_ok = True
                response_text = None
                
                if response_check:
                    response_text = await response.text()
                    try:
                        # Try to parse as JSON
                        json_data = json.loads(response_text)
                        
                        if isinstance(response_check, dict):
                            # Check exact key/value pairs
                            for key, value in response_check.items():
                                if key not in json_data or json_data[key] != value:
                                    content_ok = False
                                    break
                        elif callable(response_check):
                            # Use custom check function
                            content_ok = response_check(json_data)
                    except json.JSONDecodeError:
                        # If not JSON, check if the response contains expected text
                        if isinstance(response_check, str):
                            content_ok = response_check in response_text
                        else:
                            content_ok = False
                
                # Determine endpoint status
                if status_ok and content_ok:
                    avg_latency = self._calculate_average_latency(exchange_name, endpoint_name)
                    max_latency = endpoint_config.get('max_latency', 1.0)  # seconds
                    
                    if latency > max_latency * 2:
                        status = 'degraded'
                        message = f'High latency: {latency:.2f}s (threshold: {max_latency:.2f}s)'
                    elif avg_latency > max_latency:
                        status = 'degraded'
                        message = f'High average latency: {avg_latency:.2f}s (threshold: {max_latency:.2f}s)'
                    else:
                        status = 'operational'
                        message = 'Endpoint is operational'
                else:
                    if critical:
                        status = 'outage'
                    else:
                        status = 'degraded'
                    
                    if not status_ok:
                        message = f'Unexpected status code: {response.status} (expected: {expected_status})'
                    elif not content_ok:
                        message = 'Response content check failed'
                    else:
                        message = 'Unknown error'
                
                return {
                    'status': status,
                    'message': message,
                    'latency': latency,
                    'timestamp': time.time()
                }
        
        except asyncio.TimeoutError:
            latency = time.time() - start_time
            
            # Record latency
            self._record_latency(exchange_name, endpoint_name, timeout)
            
            if critical:
                status = 'outage'
            else:
                status = 'degraded'
            
            return {
                'status': status,
                'message': f'Request timed out after {timeout}s',
                'latency': timeout,
                'timestamp': time.time()
            }
        
        except Exception as e:
            latency = time.time() - start_time
            
            # Record latency (if error occurred after some time)
            self._record_latency(exchange_name, endpoint_name, latency)
            
            if critical:
                status = 'outage'
            else:
                status = 'degraded'
            
            return {
                'status': status,
                'message': f'Error: {str(e)}',
                'latency': latency,
                'timestamp': time.time()
            }
    
    async def _sign_hmac_request(self, exchange_name: str, endpoint_config: Dict[str, Any],
                               method: str, url: str, params: Dict[str, Any],
                               headers: Dict[str, str]) -> None:
        """
        Sign a request using HMAC authentication.
        
        Args:
            exchange_name: Name of the exchange
            endpoint_config: Configuration for the API endpoint
            method: HTTP method
            url: Request URL
            params: Request parameters
            headers: Request headers
        """
        exchange_config = self.exchanges[exchange_name]
        api_key = exchange_config.get('api_key', '')
        api_secret = exchange_config.get('api_secret', '')
        
        if not api_key or not api_secret:
            logger.warning(f"Missing API credentials for exchange {exchange_name}")
            return
        
        # Add API key to headers or params
        key_in = endpoint_config.get('key_in', 'header')
        key_name = endpoint_config.get('key_name', 'API-Key')
        
        if key_in == 'header':
            headers[key_name] = api_key
        elif key_in == 'params':
            params[key_name] = api_key
        
        # Create signature
        timestamp = str(int(time.time() * 1000))  # milliseconds
        nonce = timestamp  # Use timestamp as nonce
        
        # Add timestamp and nonce if required
        if endpoint_config.get('timestamp_required', False):
            timestamp_name = endpoint_config.get('timestamp_name', 'timestamp')
            params[timestamp_name] = timestamp
        
        if endpoint_config.get('nonce_required', False):
            nonce_name = endpoint_config.get('nonce_name', 'nonce')
            params[nonce_name] = nonce
        
        # Generate signature string
        sig_type = endpoint_config.get('sig_type', 'query_string')
        signature = ''
        
        if sig_type == 'query_string':
            # Sort params alphabetically
            sorted_params = sorted(params.items(), key=lambda x: x[0])
            query_string = urllib.parse.urlencode(sorted_params)
            
            # Create string to sign
            string_to_sign = f"{method.upper()}{url}{query_string}"
            
            # Generate signature
            signature = hmac.new(
                api_secret.encode(),
                string_to_sign.encode(),
                hashlib.sha256
            ).hexdigest()
        
        elif sig_type == 'payload':
            # Create string to sign
            string_to_sign = f"{timestamp}{nonce}{method.upper()}{url}"
            
            if params:
                query_string = urllib.parse.urlencode(params)
                string_to_sign += query_string
            
            # Generate signature
            signature = hmac.new(
                api_secret.encode(),
                string_to_sign.encode(),
                hashlib.sha256
            ).hexdigest()
        
        else:
            logger.warning(f"Unknown signature type: {sig_type}")
            return
        
        # Add signature to headers or params
        sig_in = endpoint_config.get('sig_in', 'header')
        sig_name = endpoint_config.get('sig_name', 'API-Sign')
        
        if sig_in == 'header':
            headers[sig_name] = signature
        elif sig_in == 'params':
            params[sig_name] = signature
    
    async def _sign_jwt_request(self, exchange_name: str, endpoint_config: Dict[str, Any],
                              headers: Dict[str, str]) -> None:
        """
        Sign a request using JWT authentication.
        
        Args:
            exchange_name: Name of the exchange
            endpoint_config: Configuration for the API endpoint
            headers: Request headers
        """
        # Note: JWT signing requires additional libraries like PyJWT
        # This is a simplified implementation
        exchange_config = self.exchanges[exchange_name]
        api_key = exchange_config.get('api_key', '')
        api_secret = exchange_config.get('api_secret', '')
        
        if not api_key or not api_secret:
            logger.warning(f"Missing API credentials for exchange {exchange_name}")
            return
        
        # Create a simple JWT-like token (not a real JWT implementation)
        timestamp = str(int(time.time()))
        payload = {
            'api_key': api_key,
            'timestamp': timestamp,
            'exp': int(timestamp) + 300  # 5 minutes expiry
        }
        
        payload_str = json.dumps(payload)
        payload_base64 = base64.b64encode(payload_str.encode()).decode()
        
        # Create signature
        signature = hmac.new(
            api_secret.encode(),
            payload_base64.encode(),
            hashlib.sha256
        ).hexdigest()
        
        # Create token
        token = f"{payload_base64}.{signature}"
        
        # Add token to headers
        token_name = endpoint_config.get('token_name', 'Authorization')
        token_prefix = endpoint_config.get('token_prefix', 'Bearer')
        
        headers[token_name] = f"{token_prefix} {token}"
    
    async def _sign_api_key_request(self, exchange_name: str, endpoint_config: Dict[str, Any],
                                  headers: Dict[str, str]) -> None:
        """
        Sign a request using simple API key authentication.
        
        Args:
            exchange_name: Name of the exchange
            endpoint_config: Configuration for the API endpoint
            headers: Request headers
        """
        exchange_config = self.exchanges[exchange_name]
        api_key = exchange_config.get('api_key', '')
        
        if not api_key:
            logger.warning(f"Missing API key for exchange {exchange_name}")
            return
        
        # Add API key to headers
        key_name = endpoint_config.get('key_name', 'API-Key')
        headers[key_name] = api_key
    
    def _record_latency(self, exchange_name: str, endpoint_name: str, latency: float) -> None:
        """
        Record latency for an API endpoint.
        
        Args:
            exchange_name: Name of the exchange
            endpoint_name: Name of the API endpoint
            latency: Measured latency in seconds
        """
        if exchange_name not in self.latency_history or endpoint_name not in self.latency_history[exchange_name]:
            return
        
        # Add latency measurement
        self.latency_history[exchange_name][endpoint_name].append({
            'timestamp': time.time(),
            'latency': latency
        })
        
        # Trim history if it exceeds the maximum size
        while len(self.latency_history[exchange_name][endpoint_name]) > self.history_size:
            self.latency_history[exchange_name][endpoint_name].pop(0)
    
    def _calculate_average_latency(self, exchange_name: str, endpoint_name: str, 
                                window: int = 10) -> float:
        """
        Calculate the average latency for an API endpoint.
        
        Args:
            exchange_name: Name of the exchange
            endpoint_name: Name of the API endpoint
            window: Number of recent measurements to include
            
        Returns:
            Average latency in seconds
        """
        if (exchange_name not in self.latency_history or 
            endpoint_name not in self.latency_history[exchange_name] or
            not self.latency_history[exchange_name][endpoint_name]):
            return 0.0
        
        # Get the most recent measurements
        history = self.latency_history[exchange_name][endpoint_name]
        recent = history[-min(window, len(history)):]
        
        if not recent:
            return 0.0
        
        # Calculate average latency
        return sum(item['latency'] for item in recent) / len(recent)
    
    def get_exchange_status(self, exchange_name: Optional[str] = None) -> Dict[str, Any]:
        """
        Get the current status of exchanges.
        
        Args:
            exchange_name: Optional name of a specific exchange
            
        Returns:
            Dictionary with exchange status information
        """
        if exchange_name:
            return self.status_cache.get(exchange_name, {})
        else:
            return self.status_cache
    
    def get_latency_stats(self, exchange_name: str, endpoint_name: Optional[str] = None) -> Dict[str, Any]:
        """
        Get latency statistics for an exchange or endpoint.
        
        Args:
            exchange_name: Name of the exchange
            endpoint_name: Optional name of a specific API endpoint
            
        Returns:
            Dictionary with latency statistics
        """
        if exchange_name not in self.latency_history:
            return {}
        
        if endpoint_name:
            if endpoint_name not in self.latency_history[exchange_name]:
                return {}
            
            history = self.latency_history[exchange_name][endpoint_name]
            if not history:
                return {}
            
            latencies = [item['latency'] for item in history]
            
            return {
                'count': len(latencies),
                'min': min(latencies),
                'max': max(latencies),
                'avg': sum(latencies) / len(latencies),
                'recent': latencies[-10:] if len(latencies) >= 10 else latencies
            }
        else:
            stats = {}
            
            for ep_name, history in self.latency_history[exchange_name].items():
                if not history:
                    stats[ep_name] = {}
                    continue
                
                latencies = [item['latency'] for item in history]
                
                stats[ep_name] = {
                    'count': len(latencies),
                    'min': min(latencies),
                    'max': max(latencies),
                    'avg': sum(latencies) / len(latencies)
                }
            
            return stats
    
    def collect_metrics(self) -> Dict[str, Any]:
        """
        Collect metrics about exchange status and latency.
        
        Returns:
            Dictionary of exchange metrics
        """
        metrics = {
            'timestamp': time.time(),
            'exchanges': {}
        }
        
        for exchange_name, status in self.status_cache.items():
            exchange_metrics = {
                'status': status['status'],
                'last_check': status['last_check'],
                'message': status['message'],
                'endpoints': {}
            }
            
            for endpoint_name, endpoint_status in status.get('api_status', {}).items():
                exchange_metrics['endpoints'][endpoint_name] = {
                    'status': endpoint_status.get('status', 'unknown'),
                    'latency': endpoint_status.get('latency', 0),
                    'avg_latency': self._calculate_average_latency(exchange_name, endpoint_name)
                }
            
            metrics['exchanges'][exchange_name] = exchange_metrics
        
        return metrics