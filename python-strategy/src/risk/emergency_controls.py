# src/risk/emergency_controls.py
"""
Emergency Risk Control Mechanisms
Uses standard libraries for system monitoring and emergency procedures
"""

import asyncio
from typing import Dict, List
from datetime import datetime, timedelta
import logging

logger = logging.getLogger(__name__)

class EmergencyRiskControls:
    """Emergency risk control mechanisms and circuit breakers"""
    
    def __init__(self, config: dict):
        self.config = config
        self.emergency_stops = {
            'max_daily_loss': config.get('max_daily_loss', 0.05),
            'flash_crash_threshold': config.get('flash_crash_threshold', 0.15),
            'correlation_spike': config.get('correlation_spike', 0.9)
        }
        
        # Circuit breaker state
        self.trading_halted = False
        self.halt_reason = ""
        self.halt_timestamp = None
        
        # Emergency state tracking
        self.daily_start_value = None
        self.emergency_log = []
        
    async def check_emergency_conditions(self, portfolio: dict, 
                                       market_data: dict) -> Dict[str, bool]:
        """Check for emergency stop conditions"""
        
        emergency_flags = {
            'daily_loss_limit': self._check_daily_loss(portfolio),
            'flash_crash': self._check_flash_crash(market_data),
            'correlation_spike': self._check_correlation_spike(market_data),
            'system_failure': await self._check_system_health()
        }
        
        # If any emergency condition is triggered
        if any(emergency_flags.values()):
            triggered_conditions = [k for k, v in emergency_flags.items() if v]
            await self.execute_emergency_stop(f"Emergency conditions: {triggered_conditions}")
            
        return emergency_flags
    
    async def execute_emergency_stop(self, reason: str) -> None:
        """Execute emergency portfolio liquidation"""
        
        logger.critical(f"EMERGENCY STOP TRIGGERED: {reason}")
        
        self.trading_halted = True
        self.halt_reason = reason
        self.halt_timestamp = datetime.now()
        
        # Log emergency event
        emergency_event = {
            'timestamp': self.halt_timestamp,
            'reason': reason,
            'type': 'EMERGENCY_STOP'
        }
        self.emergency_log.append(emergency_event)
        
        # Stop all new trades
        await self._halt_trading()
        
        # Close all positions (implement gradually to avoid slippage)
        await self._emergency_liquidation()
        
        # Send alerts
        await self._send_emergency_alerts(reason)
    
    def _check_daily_loss(self, portfolio: dict) -> bool:
        """Check if daily loss limit exceeded"""
        
        if not portfolio:
            return False
            
        current_value = sum(portfolio.values())
        
        # Initialize daily start value if not set
        if self.daily_start_value is None:
            self.daily_start_value = current_value
            return False
            
        if self.daily_start_value == 0:
            return False
            
        daily_loss = (self.daily_start_value - current_value) / self.daily_start_value
        
        if daily_loss > self.emergency_stops['max_daily_loss']:
            logger.warning(f"Daily loss limit exceeded: {daily_loss:.2%}")
            return True
            
        return False
    
    def _check_flash_crash(self, market_data: dict) -> bool:
        """Check for flash crash conditions"""
        
        if not market_data:
            return False
            
        # Check for rapid price drops across multiple symbols
        crash_count = 0
        total_symbols = 0
        
        for symbol, data in market_data.items():
            total_symbols += 1
            price_change = data.get('price_change_percent', 0)
            
            # Convert percentage string to float if needed
            if isinstance(price_change, str):
                price_change = float(price_change.rstrip('%')) / 100
                
            if price_change < -self.emergency_stops['flash_crash_threshold']:
                crash_count += 1
                
        # If more than 50% of symbols are crashing
        if total_symbols > 0 and crash_count / total_symbols > 0.5:
            logger.warning(f"Flash crash detected: {crash_count}/{total_symbols} symbols down >15%")
            return True
            
        return False
    
    def _check_correlation_spike(self, market_data: dict) -> bool:
        """Check for unusual correlation spikes"""
        
        # Simplified correlation spike detection
        # In production, would use proper correlation calculations
        
        if not market_data or len(market_data) < 2:
            return False
            
        # Check if all major symbols are moving in same direction with high magnitude
        movements = []
        for symbol, data in market_data.items():
            price_change = data.get('price_change_percent', 0)
            if isinstance(price_change, str):
                price_change = float(price_change.rstrip('%')) / 100
            movements.append(price_change)
            
        # If all movements are in same direction and significant
        if len(movements) > 1:
            avg_movement = sum(movements) / len(movements)
            if abs(avg_movement) > 0.1:  # 10% average movement
                same_direction = all(m * avg_movement > 0 for m in movements)
                if same_direction:
                    logger.warning(f"Correlation spike detected: avg movement {avg_movement:.2%}")
                    return True
                    
        return False
    
    async def _check_system_health(self) -> bool:
        """Check system health indicators"""
        
        try:
            # Check basic system resources using psutil if available
            try:
                import psutil
                
                # Check memory usage
                memory = psutil.virtual_memory()
                if memory.percent > 70: # is this a reasonable threshold?
                    logger.warning(f"High memory usage: {memory.percent}%")
                    return True
                    
                # Check CPU usage
                cpu_percent = psutil.cpu_percent(interval=1)
                if cpu_percent > 70:
                    logger.warning(f"High CPU usage: {cpu_percent}%")
                    return True
                    
            except ImportError:
                # psutil not available, skip resource checks
                pass
                
            return False
            
        except Exception as e:
            logger.error(f"System health check failed: {e}")
            return True  # Assume system failure if check fails
    
    async def _halt_trading(self) -> None:
        """Stop all new trading activities"""
        
        logger.info("Halting all trading activities")
        # Set global trading halt flag
        # In production, this would notify all trading components
        
    async def _emergency_liquidation(self) -> None:
        """Emergency portfolio liquidation procedure"""
        
        logger.info("Initiating emergency liquidation")
        # In production, this would:
        # 1. Get current positions
        # 2. Create market orders to close all positions
        # 3. Monitor execution
        # 4. Handle partial fills
        
    async def _send_emergency_alerts(self, reason: str) -> None:
        """Send emergency alerts through all channels"""
        
        alert_message = f"🚨 EMERGENCY STOP ACTIVATED 🚨\nReason: {reason}\nTime: {datetime.now()}"
        
        logger.critical(alert_message)
        
        # In production, send through:
        # - Email
        # - Slack
        # - SMS
        # - Discord
        # - Telegram
    
    def reset_daily_tracking(self) -> None:
        """Reset daily tracking values (call at start of each day)"""
        
        self.daily_start_value = None
        logger.info("Daily risk tracking reset")
    
    def resume_trading(self, authorized_by: str) -> bool:
        """Resume trading after emergency stop (requires authorization)"""
        
        if not self.trading_halted:
            return True
            
        # In production, add proper authorization checks
        self.trading_halted = False
        self.halt_reason = ""
        self.halt_timestamp = None
        
        resume_event = {
            'timestamp': datetime.now(),
            'authorized_by': authorized_by,
            'type': 'TRADING_RESUMED'
        }
        self.emergency_log.append(resume_event)
        
        logger.info(f"Trading resumed by {authorized_by}")
        return True
    
    def get_emergency_status(self) -> Dict:
        """Get current emergency control status"""
        
        return {
            'trading_halted': self.trading_halted,
            'halt_reason': self.halt_reason,
            'halt_timestamp': self.halt_timestamp,
            'daily_start_value': self.daily_start_value,
            'recent_events': self.emergency_log[-10:] if self.emergency_log else []
        }