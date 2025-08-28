import time
from datetime import datetime, timedelta
from typing import Dict, List
import logging

logger = logging.getLogger(__name__)

class DataHealthMonitor:
    def __init__(self, data_manager):
        self.data_manager = data_manager
        self.last_health_check = None
        
    async def check_data_health(self) -> Dict:
        """Comprehensive data health check"""
        health_report = {
            'overall_status': 'healthy',
            'timestamp': datetime.now().isoformat(),
            'symbols_status': {},
            'issues': []
        }
        
        for symbol in self.data_manager.symbols:
            symbol_health = await self._check_symbol_health(symbol)
            health_report['symbols_status'][symbol] = symbol_health
            
            if symbol_health['status'] != 'healthy':
                health_report['overall_status'] = 'degraded'
                health_report['issues'].extend(symbol_health.get('issues', []))
        
        self.last_health_check = datetime.now()
        return health_report
    
    async def _check_symbol_health(self, symbol: str) -> Dict:
        """Check health of individual symbol"""
        issues = []
        
        for timeframe in self.data_manager.timeframes:
            # Check data freshness
            latest_ts = await self.data_manager.get_latest_timestamp(symbol, timeframe)
            
            if not latest_ts:
                issues.append(f"No data for {symbol} {timeframe}")
                continue
                
            hours_old = (datetime.now().timestamp() * 1000 - latest_ts) / (1000 * 3600)
            max_age = {'4h': 8, '1d': 48}  # Max acceptable age
            
            if hours_old > max_age.get(timeframe, 48):
                issues.append(f"{symbol} {timeframe} data is {hours_old:.1f}h old")
        
        return {
            'status': 'healthy' if not issues else 'unhealthy',
            'issues': issues
        }