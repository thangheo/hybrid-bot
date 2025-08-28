# src/monitoring/bot_monitor.py
"""
Trading Bot Health Monitor
Monitors bot performance and trading activities
"""
import asyncio
from datetime import datetime, timedelta
from typing import Dict, Any, Optional
from dataclasses import dataclass
import logging

from .system_monitor import SystemMonitor, SystemMetrics
from .alert_manager import AlertManager

logger = logging.getLogger(__name__)

@dataclass
class TradingBotMetrics:
    """Trading bot specific metrics"""
    timestamp: datetime
    uptime_seconds: int
    active_positions: int
    pending_orders: int
    daily_pnl: float
    portfolio_value: float
    last_signal_time: Optional[datetime]
    last_trade_time: Optional[datetime]
    api_success_rate: float
    error_count_24h: int

class TradingBotMonitor:
    """Monitor trading bot health and performance"""
    
    def __init__(self, config: dict):
        self.config = config
        self.system_monitor = SystemMonitor(config)
        self.alert_manager = AlertManager(config.get('alerts', {}))
        
        # Bot-specific tracking
        self.start_time = datetime.now()
        self.last_heartbeat = datetime.now()
        self.error_count = 0
        self.api_calls_success = 0
        self.api_calls_total = 0
        self.trading_metrics_history = []
        
        # Health check settings
        self.heartbeat_timeout = config.get('heartbeat_timeout', 300)  # 5 minutes
        self.max_memory_mb = config.get('max_memory_mb', 1000)
        self.max_error_rate = config.get('max_error_rate', 0.05)  # 5%
        
        self.monitoring_active = False
    
    async def start_monitoring(self):
        """Start continuous monitoring"""
        self.monitoring_active = True
        logger.info("Trading bot monitoring started")
        
        # Send startup alert
        await self.alert_manager.send_alert(
            'INFO',
            'Trading bot monitoring started',
            {'start_time': self.start_time.isoformat()}
        )
        
        # Start monitoring loop
        asyncio.create_task(self._monitoring_loop())
    
    async def stop_monitoring(self):
        """Stop monitoring"""
        self.monitoring_active = False
        uptime = (datetime.now() - self.start_time).total_seconds()
        
        await self.alert_manager.send_alert(
            'INFO',
            'Trading bot monitoring stopped',
            {
                'uptime_seconds': int(uptime),
                'uptime_hours': round(uptime / 3600, 2)
            }
        )
    
    async def _monitoring_loop(self):
        """Main monitoring loop"""
        while self.monitoring_active:
            try:
                await self._perform_health_checks()
                await asyncio.sleep(60)  # Check every minute
                
            except Exception as e:
                logger.error(f"Monitoring loop error: {e}")
                await self.alert_manager.send_alert(
                    'ERROR',
                    'Monitoring system error',
                    {'error': str(e)}
                )
                await asyncio.sleep(30)  # Shorter retry on error
    
    async def _perform_health_checks(self):
        """Perform comprehensive health checks"""
        # System metrics
        system_metrics = self.system_monitor.collect_metrics()
        system_health = self.system_monitor.check_health(system_metrics)
        
        # Trading bot metrics
        bot_metrics = self._collect_bot_metrics()
        bot_health = self._check_bot_health(bot_metrics, system_metrics)
        
        # Check for issues and alert
        await self._check_system_alerts(system_metrics, system_health)
        await self._check_bot_alerts(bot_metrics, bot_health)
    
    def _collect_bot_metrics(self) -> TradingBotMetrics:
        """Collect trading bot specific metrics"""
        uptime = (datetime.now() - self.start_time).total_seconds()
        api_success_rate = (self.api_calls_success / max(self.api_calls_total, 1)) if self.api_calls_total > 0 else 1.0
        
        # These would be updated by the main trading bot
        metrics = TradingBotMetrics(
            timestamp=datetime.now(),
            uptime_seconds=int(uptime),
            active_positions=getattr(self, '_active_positions', 0),
            pending_orders=getattr(self, '_pending_orders', 0),
            daily_pnl=getattr(self, '_daily_pnl', 0.0),
            portfolio_value=getattr(self, '_portfolio_value', 0.0),
            last_signal_time=getattr(self, '_last_signal_time', None),
            last_trade_time=getattr(self, '_last_trade_time', None),
            api_success_rate=api_success_rate,
            error_count_24h=self.error_count
        )
        
        # Store metrics
        self.trading_metrics_history.append(metrics)
        
        # Keep only last 24 hours
        cutoff_time = datetime.now() - timedelta(hours=24)
        self.trading_metrics_history = [
            m for m in self.trading_metrics_history 
            if m.timestamp > cutoff_time
        ]
        
        return metrics
    
    def _check_bot_health(self, bot_metrics: TradingBotMetrics, system_metrics: SystemMetrics) -> Dict[str, bool]:
        """Check trading bot health"""
        now = datetime.now()
        
        return {
            'heartbeat_ok': (now - self.last_heartbeat).total_seconds() < self.heartbeat_timeout,
            'memory_ok': system_metrics.process_memory < (self.max_memory_mb * 1024 * 1024),
            'api_ok': bot_metrics.api_success_rate > (1 - self.max_error_rate),
            'signals_ok': (
                bot_metrics.last_signal_time is None or 
                (now - bot_metrics.last_signal_time).total_seconds() < 7200  # 2 hours
            )
        }
    
    async def _check_system_alerts(self, metrics: SystemMetrics, health: Dict[str, bool]):
        """Check and send system alerts"""
        if not health['memory_ok']:
            await self.alert_manager.send_alert(
                'WARNING',
                f'High system memory usage: {metrics.memory_percent:.1f}%',
                {
                    'memory_percent': metrics.memory_percent,
                    'memory_available_gb': round(metrics.memory_available / 1e9, 2),
                    'threshold': self.system_monitor.memory_threshold
                }
            )
        
        if not health['cpu_ok']:
            await self.alert_manager.send_alert(
                'WARNING',
                f'High CPU usage: {metrics.cpu_percent:.1f}%',
                {
                    'cpu_percent': metrics.cpu_percent,
                    'threshold': self.system_monitor.cpu_threshold
                }
            )
        
        if not health['disk_ok']:
            await self.alert_manager.send_alert(
                'ERROR',
                f'Low disk space: {metrics.disk_percent:.1f}% used',
                {
                    'disk_percent': metrics.disk_percent,
                    'disk_free_gb': round(metrics.disk_free / 1e9, 2),
                    'threshold': self.system_monitor.disk_threshold
                }
            )
    
    async def _check_bot_alerts(self, bot_metrics: TradingBotMetrics, health: Dict[str, bool]):
        """Check and send bot-specific alerts"""
        if not health['heartbeat_ok']:
            await self.alert_manager.send_alert(
                'CRITICAL',
                'Trading bot heartbeat timeout',
                {
                    'last_heartbeat': self.last_heartbeat.isoformat(),
                    'timeout_minutes': self.heartbeat_timeout / 60,
                    'action': 'Check if bot is running and responsive'
                }
            )
        
        if not health['memory_ok']:
            await self.alert_manager.send_alert(
                'WARNING',
                f'Trading bot using too much memory: {bot_metrics.timestamp}',
                {
                    'process_memory_mb': round(getattr(self.system_monitor.collect_metrics(), 'process_memory', 0) / 1024 / 1024, 1),
                    'limit_mb': self.max_memory_mb
                }
            )
        
        if not health['api_ok']:
            await self.alert_manager.send_alert(
                'ERROR',
                f'Low API success rate: {bot_metrics.api_success_rate:.1%}',
                {
                    'success_rate': f"{bot_metrics.api_success_rate:.1%}",
                    'total_calls': self.api_calls_total,
                    'successful_calls': self.api_calls_success,
                    'threshold': f"{(1-self.max_error_rate):.1%}"
                }
            )
        
        # Daily P&L alerts
        if abs(bot_metrics.daily_pnl) > 1000:  # Alert on large P&L changes
            level = 'WARNING' if bot_metrics.daily_pnl < 0 else 'INFO'
            await self.alert_manager.send_alert(
                level,
                f'Significant daily P&L: ${bot_metrics.daily_pnl:.2f}',
                {
                    'daily_pnl': bot_metrics.daily_pnl,
                    'portfolio_value': bot_metrics.portfolio_value,
                    'active_positions': bot_metrics.active_positions
                }
            )
    
    # Methods to be called by main trading bot
    def heartbeat(self):
        """Update heartbeat - call this regularly from main bot"""
        self.last_heartbeat = datetime.now()
    
    def record_api_call(self, success: bool):
        """Record API call result"""
        self.api_calls_total += 1
        if success:
            self.api_calls_success += 1
    
    def record_error(self, error: Exception, context: str = ""):
        """Record error occurrence"""
        self.error_count += 1
        logger.error(f"Bot error in {context}: {error}")
    
    def update_trading_state(self, 
                           active_positions: int = None,
                           pending_orders: int = None,
                           daily_pnl: float = None,
                           portfolio_value: float = None):
        """Update trading state for monitoring"""
        if active_positions is not None:
            self._active_positions = active_positions
        if pending_orders is not None:
            self._pending_orders = pending_orders
        if daily_pnl is not None:
            self._daily_pnl = daily_pnl
        if portfolio_value is not None:
            self._portfolio_value = portfolio_value
    
    def record_signal_generated(self):
        """Record when trading signal was generated"""
        self._last_signal_time = datetime.now()
    
    def record_trade_executed(self):
        """Record when trade was executed"""
        self._last_trade_time = datetime.now()
    
    async def send_custom_alert(self, level: str, message: str, details: Dict = None):
        """Send custom alert from trading bot"""
        await self.alert_manager.send_alert(level, message, details)
    
    def get_status_summary(self) -> Dict:
        """Get comprehensive status summary"""
        system_metrics = self.system_monitor.collect_metrics()
        bot_metrics = self._collect_bot_metrics()
        system_health = self.system_monitor.check_health(system_metrics)
        bot_health = self._check_bot_health(bot_metrics, system_metrics)
        
        return {
            'timestamp': datetime.now().isoformat(),
            'uptime_hours': round(bot_metrics.uptime_seconds / 3600, 2),
            'system_health': system_health,
            'bot_health': bot_health,
            'system_metrics': {
                'cpu_percent': system_metrics.cpu_percent,
                'memory_percent': system_metrics.memory_percent,
                'disk_percent': system_metrics.disk_percent
            },
            'bot_metrics': {
                'active_positions': bot_metrics.active_positions,
                'pending_orders': bot_metrics.pending_orders,
                'daily_pnl': bot_metrics.daily_pnl,
                'api_success_rate': f"{bot_metrics.api_success_rate:.1%}"
            }
        }


# Usage example in main trading bot
"""
# In your main trading bot file
from src.monitoring.bot_monitor import TradingBotMonitor

class CryptoTradingBot:
    def __init__(self, config_path: str):
        # ... other initialization
        
        # Initialize monitoring
        monitor_config = self.config.get('monitoring', {})
        self.monitor = TradingBotMonitor(monitor_config)
    
    async def start(self):
        # Start monitoring
        await self.monitor.start_monitoring()
        
        # Main trading loop
        while self.running:
            try:
                # Update heartbeat
                self.monitor.heartbeat()
                
                # Your trading logic here
                await self._execute_trading_cycle()
                
                # Update trading state
                self.monitor.update_trading_state(
                    active_positions=len(self.portfolio.positions),
                    pending_orders=len(self.order_manager.active_orders),
                    daily_pnl=self.portfolio.daily_pnl,
                    portfolio_value=self.portfolio.total_value
                )
                
            except Exception as e:
                # Record error
                self.monitor.record_error(e, "main_loop")
                await self.monitor.send_custom_alert(
                    'ERROR', 
                    f'Trading loop error: {str(e)}'
                )
    
    async def _execute_api_call(self, func, *args, **kwargs):
        try:
            result = await func(*args, **kwargs)
            self.monitor.record_api_call(True)
            return result
        except Exception as e:
            self.monitor.record_api_call(False)
            raise
"""