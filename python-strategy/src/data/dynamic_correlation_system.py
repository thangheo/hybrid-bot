# ================================================================
# DYNAMIC CORRELATION SYSTEM - Auto-adjust khi correlation thay đổi
# ================================================================

import pandas as pd
import numpy as np
from datetime import datetime, timedelta
from typing import Dict, List, Tuple, Optional
import logging
from scipy import stats
from dataclasses import dataclass
from enum import Enum

@dataclass
class CorrelationAlert:
    asset_pair: str
    old_correlation: float
    new_correlation: float
    change_magnitude: float
    alert_level: str
    timestamp: datetime
    recommendation: str

class CorrelationStrength(Enum):
    VERY_STRONG = "very_strong"    # > 0.7
    STRONG = "strong"              # 0.5 - 0.7
    MODERATE = "moderate"          # 0.3 - 0.5
    WEAK = "weak"                  # 0.1 - 0.3
    NEGLIGIBLE = "negligible"      # < 0.1

class DynamicCorrelationMonitor:
    """
    🔄 Dynamic correlation monitoring system
    Auto-detects correlation breakdown và adjusts strategy
    """
    
    def __init__(self, config: dict):
        self.config = config
        self.correlation_history = {}
        self.current_correlations = {}
        self.alert_thresholds = {
            'major_breakdown': 0.3,    # Correlation drop > 0.3
            'regime_shift': 0.2,       # Correlation change > 0.2
            'warning': 0.15            # Correlation change > 0.15
        }
        
        # Correlation tracking settings
        self.lookback_periods = [30, 60, 90, 180]  # Multiple timeframes
        self.min_observations = 20
        self.update_frequency_hours = 4
        self.last_update = {}
        
        # Auto-adjustment settings
        self.auto_adjust_enabled = config.get('auto_correlation_adjust', True)
        self.min_correlation_threshold = 0.2  # Below this = disable filter
        self.correlation_confidence_threshold = 0.05  # p-value threshold
        
        logging.info("🔄 Dynamic correlation monitor initialized")
    
    async def update_correlations(self, btc_data: pd.DataFrame, 
                                spy_data: pd.DataFrame) -> Dict[str, float]:
        """
        📊 Update correlations across multiple timeframes
        Returns current correlation estimates
        """
        
        correlations = {}
        
        try:
            # Align data by dates
            aligned_data = self._align_data(btc_data, spy_data)
            
            if len(aligned_data) < self.min_observations:
                logging.warning(f"⚠️ Insufficient data for correlation: {len(aligned_data)} days")
                return self.current_correlations
            
            # Calculate correlations for different periods
            for period in self.lookback_periods:
                if len(aligned_data) >= period:
                    recent_data = aligned_data.tail(period)
                    corr = recent_data['btc_returns'].corr(recent_data['spy_returns'])
                    
                    # Statistical significance test
                    p_value = self._correlation_significance_test(
                        recent_data['btc_returns'], 
                        recent_data['spy_returns']
                    )
                    
                    correlations[f'btc_spy_{period}d'] = {
                        'correlation': corr,
                        'p_value': p_value,
                        'significant': p_value < self.correlation_confidence_threshold,
                        'strength': self._classify_correlation_strength(abs(corr)),
                        'sample_size': len(recent_data),
                        'timestamp': datetime.now()
                    }
            
            # Update history and detect changes
            alerts = self._detect_correlation_changes(correlations)
            
            # Store current correlations
            self.current_correlations = correlations
            self.last_update['btc_spy'] = datetime.now()
            
            # Log correlation status
            self._log_correlation_status(correlations, alerts)
            
            return correlations
            
        except Exception as e:
            logging.error(f"❌ Correlation update failed: {e}")
            return self.current_correlations
    
    def _align_data(self, btc_data: pd.DataFrame, spy_data: pd.DataFrame) -> pd.DataFrame:
        """Align BTC and SPY data by dates"""
        
        # Ensure we have date columns
        if 'date' not in btc_data.columns:
            btc_data = btc_data.reset_index()
            btc_data.rename(columns={'index': 'date'}, inplace=True)
            
        if 'date' not in spy_data.columns:
            spy_data = spy_data.reset_index()
            spy_data.rename(columns={'index': 'date'}, inplace=True)
        
        # Calculate returns
        btc_data['btc_returns'] = btc_data['close'].pct_change()
        spy_data['spy_returns'] = spy_data['close'].pct_change()
        
        # Merge on dates
        merged = pd.merge(
            btc_data[['date', 'btc_returns']], 
            spy_data[['date', 'spy_returns']], 
            on='date', 
            how='inner'
        )
        
        # Remove NaN values
        merged = merged.dropna()
        
        return merged.sort_values('date').reset_index(drop=True)
    
    def _correlation_significance_test(self, series1: pd.Series, 
                                     series2: pd.Series) -> float:
        """Test statistical significance of correlation"""
        try:
            corr, p_value = stats.pearsonr(series1, series2)
            return p_value
        except:
            return 1.0  # Not significant
    
    def _classify_correlation_strength(self, abs_corr: float) -> CorrelationStrength:
        """Classify correlation strength"""
        if abs_corr > 0.7:
            return CorrelationStrength.VERY_STRONG
        elif abs_corr > 0.5:
            return CorrelationStrength.STRONG
        elif abs_corr > 0.3:
            return CorrelationStrength.MODERATE
        elif abs_corr > 0.1:
            return CorrelationStrength.WEAK
        else:
            return CorrelationStrength.NEGLIGIBLE
    
    def _detect_correlation_changes(self, new_correlations: Dict) -> List[CorrelationAlert]:
        """🚨 Detect significant correlation changes"""
        
        alerts = []
        
        for period_key, new_corr_data in new_correlations.items():
            # Check if we have historical data
            if period_key in self.correlation_history:
                old_corr_data = self.correlation_history[period_key]
                old_corr = old_corr_data['correlation']
                new_corr = new_corr_data['correlation']
                
                change = abs(new_corr - old_corr)
                
                # Determine alert level
                alert_level = None
                recommendation = ""
                
                if change > self.alert_thresholds['major_breakdown']:
                    alert_level = "CRITICAL"
                    recommendation = "Consider disabling SPY filter - major correlation breakdown"
                elif change > self.alert_thresholds['regime_shift']:
                    alert_level = "WARNING"
                    recommendation = "Monitor closely - potential regime shift"
                elif change > self.alert_thresholds['warning']:
                    alert_level = "INFO"
                    recommendation = "Correlation weakening - review strategy"
                
                if alert_level:
                    alert = CorrelationAlert(
                        asset_pair=f"BTC/SPY ({period_key})",
                        old_correlation=old_corr,
                        new_correlation=new_corr,
                        change_magnitude=change,
                        alert_level=alert_level,
                        timestamp=datetime.now(),
                        recommendation=recommendation
                    )
                    alerts.append(alert)
        
        # Update history
        self.correlation_history.update(new_correlations)
        
        return alerts
    
    def get_current_filter_adjustment(self) -> Tuple[bool, float, str]:
        """
        🎛️ Get current filter adjustment recommendation
        Returns: (should_use_filter, adjustment_factor, reason)
        """
        
        if not self.current_correlations:
            return False, 0.0, "No correlation data available"
        
        # Use 60-day correlation as primary (balance of responsiveness vs stability)
        primary_key = 'btc_spy_60d'
        if primary_key not in self.current_correlations:
            # Fallback to available correlation
            primary_key = next(iter(self.current_correlations.keys()))
        
        corr_data = self.current_correlations[primary_key]
        correlation = corr_data['correlation']
        is_significant = corr_data['significant']
        strength = corr_data['strength']
        
        # Decision logic
        if not is_significant:
            return False, 0.0, f"Correlation not statistically significant (p={corr_data['p_value']:.3f})"
        
        if abs(correlation) < self.min_correlation_threshold:
            return False, 0.0, f"Correlation too weak ({correlation:.3f} < {self.min_correlation_threshold})"
        
        # Calculate adjustment factor based on correlation strength
        adjustment_factor = self._calculate_adjustment_factor(correlation, strength)
        
        reason = f"Active filter: corr={correlation:.3f} ({strength.value}), factor={adjustment_factor:.2f}"
        
        return True, adjustment_factor, reason
    
    def _calculate_adjustment_factor(self, correlation: float, 
                                   strength: CorrelationStrength) -> float:
        """Calculate adjustment factor based on correlation strength"""
        
        # Base adjustment factors
        base_factors = {
            CorrelationStrength.VERY_STRONG: 1.0,
            CorrelationStrength.STRONG: 0.8,
            CorrelationStrength.MODERATE: 0.6,
            CorrelationStrength.WEAK: 0.4,
            CorrelationStrength.NEGLIGIBLE: 0.1
        }
        
        base_factor = base_factors[strength]
        
        # Scale by actual correlation magnitude
        scaled_factor = base_factor * abs(correlation)
        
        # Ensure reasonable bounds
        return np.clip(scaled_factor, 0.1, 1.0)
    
    def _log_correlation_status(self, correlations: Dict, alerts: List[CorrelationAlert]):
        """Log correlation status and alerts"""
        
        # Log current correlations
        logging.info("📊 Correlation Status Update:")
        for period_key, data in correlations.items():
            corr = data['correlation']
            strength = data['strength'].value
            significant = "✅" if data['significant'] else "❌"
            logging.info(f"   {period_key}: {corr:+.3f} ({strength}) {significant}")
        
        # Log alerts
        for alert in alerts:
            level_emoji = {"CRITICAL": "🚨", "WARNING": "⚠️", "INFO": "ℹ️"}
            emoji = level_emoji.get(alert.alert_level, "📊")
            
            logging.warning(f"{emoji} {alert.alert_level}: {alert.asset_pair}")
            logging.warning(f"   {alert.old_correlation:.3f} → {alert.new_correlation:.3f} (Δ{alert.change_magnitude:.3f})")
            logging.warning(f"   Recommendation: {alert.recommendation}")
    
    def get_correlation_health_report(self) -> Dict:
        """📋 Get comprehensive correlation health report"""
        
        if not self.current_correlations:
            return {"error": "No correlation data available"}
        
        # Summary statistics
        correlations = [data['correlation'] for data in self.current_correlations.values()]
        significance_rates = [data['significant'] for data in self.current_correlations.values()]
        
        # Current filter recommendation
        should_use, adjustment_factor, reason = self.get_current_filter_adjustment()
        
        # Historical trend (if available)
        trend_analysis = self._analyze_correlation_trend()
        
        return {
            'timestamp': datetime.now().isoformat(),
            'current_correlations': self.current_correlations,
            'summary': {
                'mean_correlation': np.mean(correlations),
                'correlation_range': [min(correlations), max(correlations)],
                'significance_rate': sum(significance_rates) / len(significance_rates),
                'sample_sizes': [data['sample_size'] for data in self.current_correlations.values()]
            },
            'filter_recommendation': {
                'should_use_filter': should_use,
                'adjustment_factor': adjustment_factor,
                'reason': reason
            },
            'trend_analysis': trend_analysis,
            'health_status': self._assess_correlation_health()
        }
    
    def _analyze_correlation_trend(self) -> Dict:
        """Analyze correlation trend over time"""
        
        if len(self.correlation_history) < 3:
            return {"trend": "insufficient_data"}
        
        # Get 60-day correlations over time
        key = 'btc_spy_60d'
        if key not in self.correlation_history:
            return {"trend": "no_historical_data"}
        
        # This is simplified - in practice you'd store time series of correlations
        recent_corr = self.current_correlations.get(key, {}).get('correlation', 0)
        historical_corr = self.correlation_history.get(key, {}).get('correlation', 0)
        
        if abs(recent_corr - historical_corr) > 0.2:
            trend = "breaking_down" if recent_corr < historical_corr else "strengthening"
        else:
            trend = "stable"
        
        return {
            'trend': trend,
            'recent_correlation': recent_corr,
            'historical_correlation': historical_corr,
            'change': recent_corr - historical_corr
        }
    
    def _assess_correlation_health(self) -> str:
        """Overall health assessment"""
        
        primary_data = self.current_correlations.get('btc_spy_60d', {})
        
        if not primary_data:
            return "unknown"
        
        correlation = abs(primary_data.get('correlation', 0))
        is_significant = primary_data.get('significant', False)
        
        if not is_significant:
            return "poor"
        elif correlation > 0.5:
            return "excellent"
        elif correlation > 0.3:
            return "good"
        elif correlation > 0.2:
            return "fair"
        else:
            return "poor"

# ================================================================
# ENHANCED SPY Market Filter với Dynamic Correlation
# ================================================================

class EnhancedSPYMarketFilter:
    """🎛️ Enhanced SPY filter với dynamic correlation adjustment"""
    
    def __init__(self, config: dict):
        self.config = config
        self.correlation_monitor = DynamicCorrelationMonitor(config)
        self.static_correlation = config.get('spy_market_filter', {}).get('correlation_strength', 0.50)
        
        # Enhanced adjustment factors
        self.base_adjustments = {
            'bull_regime_amplify': 1.3,
            'bear_regime_reduce': 0.6,
            'volatile_regime_reduce': 0.8
        }
        
        logging.info("🎛️ Enhanced SPY filter with dynamic correlation initialized")
    
    async def update_dynamic_correlations(self, btc_data: pd.DataFrame, 
                                        spy_data: pd.DataFrame):
        """📊 Update dynamic correlations"""
        await self.correlation_monitor.update_correlations(btc_data, spy_data)
    
    def get_enhanced_signal_adjustment(self, spy_analysis: Dict, btc_signal_strength: float,
                                     btc_data: pd.DataFrame = None) -> Tuple[float, str]:
        """
        🎯 Enhanced signal adjustment với dynamic correlation
        """
        
        # Get current filter recommendation
        should_use_filter, correlation_adjustment, correlation_reason = \
            self.correlation_monitor.get_current_filter_adjustment()
        
        if not should_use_filter:
            # Correlation breakdown - minimal or no adjustment
            logging.warning(f"🚫 SPY filter disabled: {correlation_reason}")
            return btc_signal_strength * 0.95, f"Filter disabled: {correlation_reason}"
        
        # Apply correlation-adjusted SPY filter
        regime = spy_analysis.get('regime')
        if not regime:
            return btc_signal_strength, "No SPY regime data"
        
        # Base regime adjustment
        adjustment = 1.0
        reasons = []
        
        if hasattr(regime, 'value'):
            regime_name = regime.value
        else:
            regime_name = str(regime)
        
        # Apply regime adjustments scaled by correlation strength
        if regime_name == 'bull':
            if btc_signal_strength > 0:
                base_amplify = self.base_adjustments['bull_regime_amplify']
                scaled_amplify = 1.0 + (base_amplify - 1.0) * correlation_adjustment
                adjustment *= scaled_amplify
                reasons.append(f"Bull regime (+{(scaled_amplify-1)*100:.0f}%)")
            
        elif regime_name == 'bear':
            if btc_signal_strength > 0:
                base_reduce = self.base_adjustments['bear_regime_reduce']
                scaled_reduce = 1.0 - (1.0 - base_reduce) * correlation_adjustment
                adjustment *= scaled_reduce
                reasons.append(f"Bear regime ({(scaled_reduce-1)*100:+.0f}%)")
        
        elif regime_name == 'volatile':
            base_reduce = self.base_adjustments['volatile_regime_reduce']
            scaled_reduce = 1.0 - (1.0 - base_reduce) * correlation_adjustment
            adjustment *= scaled_reduce
            reasons.append(f"Volatile regime ({(scaled_reduce-1)*100:+.0f}%)")
        
        # Apply adjustment
        adjusted_strength = btc_signal_strength * adjustment
        
        # Add correlation info to reason
        reasons.append(f"Corr factor: {correlation_adjustment:.2f}")
        reason_text = "; ".join(reasons) if reasons else "No adjustment"
        
        return adjusted_strength, reason_text
    
    def get_filter_status(self) -> Dict:
        """📋 Get comprehensive filter status"""
        return self.correlation_monitor.get_correlation_health_report()

# ================================================================
# MODIFICATION: Integration vào main trading bot
# ================================================================

# THÊM VÀO main.py trong class EnhancedTradingBot:

async def _initialize_enhanced_spy_filter(self):
    """🔄 Initialize enhanced SPY filter với dynamic correlation"""
    
    alpha_vantage_key = self.config.get('alpha_vantage', {}).get('api_key')
    if alpha_vantage_key:
        from src.data.spy_data_manager import SPYDataManager
        from src.strategy.enhanced_spy_filter import EnhancedSPYMarketFilter
        
        self.spy_data_manager = SPYDataManager(alpha_vantage_key)
        self.spy_market_filter = EnhancedSPYMarketFilter(self.config)
        self.spy_filter_enabled = True
        
        self.logger.info("🎛️ Enhanced SPY filter with dynamic correlation enabled")
    else:
        self.spy_filter_enabled = False
        self.logger.warning("⚠️ SPY filter disabled - no API key")

async def _update_correlation_monitoring(self):
    """📊 Scheduled correlation monitoring update"""
    
    if not self.spy_filter_enabled:
        return
    
    try:
        # Get recent BTC and SPY data
        symbols = self.config.get('data_ingestion', {}).get('symbols', [])
        btc_symbol = next((s for s in symbols if 'BTC' in s), None)
        
        if not btc_symbol:
            return
        
        # Get BTC data
        btc_data = self.data_manager.get_symbol_data(btc_symbol, days=200)
        
        # Get SPY data
        spy_data = await self.spy_data_manager.get_spy_data(200)
        
        if btc_data.empty or spy_data is None:
            self.logger.warning("⚠️ Insufficient data for correlation update")
            return
        
        # Update dynamic correlations
        await self.spy_market_filter.update_dynamic_correlations(btc_data, spy_data)
        
        # Get filter status
        filter_status = self.spy_market_filter.get_filter_status()
        health = filter_status.get('health_status', 'unknown')
        
        # Log correlation health
        self.logger.info(f"🔄 Correlation health: {health}")
        
        # Send alert if correlation health is poor
        if health in ['poor', 'unknown']:
            await self.alert_manager.send_alert(
                'WARNING',
                f'🚨 BTC-SPY Correlation Health: {health}',
                {
                    'health_status': health,
                    'filter_status': filter_status,
                    'recommendation': 'Consider reviewing SPY filter settings'
                }
            )
        
    except Exception as e:
        self.logger.error(f"❌ Correlation monitoring update failed: {e}")

# THÊM VÀO _schedule_enhanced_tasks():
def _schedule_enhanced_tasks(self):
    # ... existing tasks ...
    
    # 🔄 Correlation monitoring (every 6 hours)
    if self.spy_filter_enabled:
        self.scheduler.add_job(
            self._update_correlation_monitoring,
            trigger=IntervalTrigger(hours=6),
            id='correlation_monitoring',
            max_instances=1
        )
        
        self.logger.info("🔄 Dynamic correlation monitoring scheduled")

# ================================================================
# CONFIG ENHANCEMENT
# ================================================================

# THÊM VÀO config/trading_bot.yaml:
"""
spy_market_filter:
  enabled: true
  
  # Dynamic correlation monitoring
  auto_correlation_adjust: true
  min_correlation_threshold: 0.2    # Below this = disable filter
  correlation_update_hours: 6       # Update correlation every 6h
  
  # Alert thresholds for correlation changes
  correlation_alerts:
    major_breakdown: 0.3            # Alert if correlation drops >0.3
    regime_shift: 0.2               # Alert if correlation changes >0.2
    warning: 0.15                   # Info alert if changes >0.15
  
  # Lookback periods for correlation calculation
  correlation_lookbacks: [30, 60, 90, 180]  # Multiple timeframes
  primary_lookback: 60              # Primary period for decisions
"""

# ================================================================
# TEST: Correlation breakdown simulation
# ================================================================

async def test_correlation_breakdown():
    """🧪 Test correlation breakdown detection"""
    
    print("🧪 TESTING CORRELATION BREAKDOWN DETECTION")
    print("=" * 60)
    
    config = {
        'auto_correlation_adjust': True,
        'correlation_alerts': {
            'major_breakdown': 0.3,
            'regime_shift': 0.2,
            'warning': 0.15
        }
    }
    
    monitor = DynamicCorrelationMonitor(config)
    
    # Simulate correlation breakdown over time
    correlations_over_time = [
        0.52,  # Original strong correlation
        0.48,  # Slight weakening
        0.35,  # Moderate weakening
        0.15,  # Major breakdown!
        0.08,  # Almost no correlation
        -0.05  # Negative correlation
    ]
    
    print("📊 Simulating correlation changes over time:")
    
    for i, new_corr in enumerate(correlations_over_time):
        print(f"\nPeriod {i+1}: Correlation = {new_corr:+.3f}")
        
        # Simulate correlation data
        fake_corr_data = {
            'btc_spy_60d': {
                'correlation': new_corr,
                'p_value': 0.01,
                'significant': True,
                'strength': monitor._classify_correlation_strength(abs(new_corr)),
                'sample_size': 60,
                'timestamp': datetime.now()
            }
        }
        
        # Detect changes
        alerts = monitor._detect_correlation_changes(fake_corr_data)
        
        # Get filter recommendation
        should_use, adj_factor, reason = monitor.get_current_filter_adjustment()
        
        print(f"   Filter active: {should_use}, Factor: {adj_factor:.2f}")
        print(f"   Reason: {reason}")
        
        if alerts:
            for alert in alerts:
                print(f"   🚨 {alert.alert_level}: {alert.recommendation}")
        
        # Update for next iteration
        monitor.current_correlations = fake_corr_data
    
    print(f"\n🎯 Key Insights:")
    print(f"   • Strong correlation (0.5+) = Full SPY filter active")
    print(f"   • Moderate correlation (0.2-0.5) = Scaled SPY filter")
    print(f"   • Weak correlation (<0.2) = SPY filter disabled")
    print(f"   • System auto-detects breakdowns and adjusts")

if __name__ == "__main__":
    import asyncio
    asyncio.run(test_correlation_breakdown())