# src/homeostasis/portfolio_health_monitor.py
import numpy as np
import pandas as pd
from typing import Dict, List, Optional, Tuple
from dataclasses import dataclass
from datetime import datetime, timedelta
import logging

@dataclass
class HealthMetrics:
    overall_score: float  # 0-100
    components: Dict[str, float]
    warnings: List[str]
    critical_issues: List[str]
    recommendations: List[str]
    timestamp: datetime

@dataclass
class VitalSigns:
    """Portfolio vital signs similar to biological systems"""
    stability_score: float      # 0-100 (like blood pressure)
    adaptation_rate: float      # How quickly system adapts to changes
    stress_level: float         # 0-100 (like cortisol levels)
    energy_level: float         # Available capital for new opportunities
    immune_strength: float      # Resistance to market shocks
    recovery_rate: float        # How fast portfolio recovers from drawdowns

class PortfolioHealthMonitor:
    """
    Bio-inspired portfolio health monitoring system
    Monitors portfolio like a living organism's vital signs
    """
    
    def __init__(self, config: dict):
        self.config = config
        self.historical_health = []
        self.stress_threshold = config.get('stress_threshold', 70)
        self.critical_threshold = config.get('critical_threshold', 30)
        
        # Health component weights
        self.component_weights = {
            'performance': 0.25,      # Recent performance trend
            'risk_control': 0.20,     # Risk metrics adherence  
            'diversification': 0.15,  # Portfolio diversification
            'drawdown_health': 0.15,  # Drawdown control
            'volatility_control': 0.10, # Volatility management
            'liquidity': 0.10,        # Portfolio liquidity
            'correlation_risk': 0.05  # Correlation concentration
        }
        
        # Historical performance tracking
        self.performance_history = []
        self.drawdown_history = []
        self.volatility_history = []
        
        logging.basicConfig(level=logging.INFO)
        self.logger = logging.getLogger(__name__)
    
    def assess_portfolio_health(self, portfolio_data: Dict, 
                               market_data: Dict,
                               performance_metrics: Dict) -> HealthMetrics:
        """
        Comprehensive portfolio health assessment
        """
        components = {}
        warnings = []
        critical_issues = []
        recommendations = []
        
        # 1. Performance Health (25%)
        performance_score = self._assess_performance_health(performance_metrics)
        components['performance'] = performance_score
        
        if performance_score < 40:
            critical_issues.append("Poor recent performance trend")
            recommendations.append("Review and adjust strategy allocations")
        elif performance_score < 60:
            warnings.append("Performance below target")
        
        # 2. Risk Control Health (20%)
        risk_score = self._assess_risk_control(portfolio_data, performance_metrics)
        components['risk_control'] = risk_score
        
        if risk_score < 30:
            critical_issues.append("Risk limits being breached")
            recommendations.append("Reduce position sizes immediately")
        elif risk_score < 50:
            warnings.append("Risk controls under stress")
        
        # 3. Diversification Health (15%)
        diversification_score = self._assess_diversification(portfolio_data)
        components['diversification'] = diversification_score
        
        if diversification_score < 40:
            warnings.append("Portfolio concentration risk high")
            recommendations.append("Increase diversification across assets/strategies")
        
        # 4. Drawdown Health (15%)
        drawdown_score = self._assess_drawdown_health(performance_metrics)
        components['drawdown_health'] = drawdown_score
        
        if drawdown_score < 30:
            critical_issues.append("Excessive drawdown detected")
            recommendations.append("Implement emergency drawdown protection")
        
        # 5. Volatility Control (10%)
        volatility_score = self._assess_volatility_control(performance_metrics)
        components['volatility_control'] = volatility_score
        
        # 6. Liquidity Health (10%)
        liquidity_score = self._assess_liquidity_health(portfolio_data, market_data)
        components['liquidity'] = liquidity_score
        
        # 7. Correlation Risk (5%)
        correlation_score = self._assess_correlation_risk(portfolio_data, market_data)
        components['correlation_risk'] = correlation_score
        
        # Calculate overall health score
        overall_score = sum(
            components[component] * self.component_weights[component]
            for component in components
        )
        
        # Add overall recommendations based on score
        if overall_score < self.critical_threshold:
            critical_issues.append("Portfolio health critical - immediate intervention required")
            recommendations.extend([
                "Reduce all position sizes by 50%",
                "Activate defensive strategies only",
                "Consider temporary trading halt"
            ])
        elif overall_score < self.stress_threshold:
            warnings.append("Portfolio under stress - careful monitoring required")
            recommendations.extend([
                "Reduce position sizes by 25%", 
                "Increase allocation to defensive strategies",
                "Monitor regime changes closely"
            ])
        
        health_metrics = HealthMetrics(
            overall_score=overall_score,
            components=components,
            warnings=warnings,
            critical_issues=critical_issues,
            recommendations=recommendations,
            timestamp=datetime.now()
        )
        
        # Store historical health
        self.historical_health.append(health_metrics)
        self._maintain_health_history()
        
        return health_metrics
    
    def get_vital_signs(self, portfolio_data: Dict, 
                       performance_metrics: Dict) -> VitalSigns:
        """
        Get current portfolio vital signs
        """
        # Stability Score - how stable the portfolio returns are
        stability_score = self._calculate_stability_score(performance_metrics)
        
        # Adaptation Rate - how quickly portfolio adapts to regime changes
        adaptation_rate = self._calculate_adaptation_rate()
        
        # Stress Level - portfolio stress based on drawdowns and volatility
        stress_level = self._calculate_stress_level(performance_metrics)
        
        # Energy Level - available capital/capacity for new trades
        energy_level = self._calculate_energy_level(portfolio_data)
        
        # Immune Strength - resistance to market shocks
        immune_strength = self._calculate_immune_strength(portfolio_data)
        
        # Recovery Rate - how fast portfolio recovers from drawdowns
        recovery_rate = self._calculate_recovery_rate(performance_metrics)
        
        return VitalSigns(
            stability_score=stability_score,
            adaptation_rate=adaptation_rate,
            stress_level=stress_level,
            energy_level=energy_level,
            immune_strength=immune_strength,
            recovery_rate=recovery_rate
        )
    
    def _assess_performance_health(self, performance_metrics: Dict) -> float:
        """Assess recent performance health (0-100)"""
        score = 50  # Base score
        
        # Recent returns
        recent_return = performance_metrics.get('recent_return', 0)
        if recent_return > 0.02:  # >2% recent return
            score += 30
        elif recent_return > 0:
            score += 15
        elif recent_return > -0.02:
            score -= 10
        else:
            score -= 30
        
        # Sharpe ratio
        sharpe = performance_metrics.get('sharpe_ratio', 0)
        if sharpe > 2.0:
            score += 20
        elif sharpe > 1.0:
            score += 10
        elif sharpe < 0:
            score -= 20
        
        # Win rate
        win_rate = performance_metrics.get('win_rate', 0.5)
        if win_rate > 0.6:
            score += 10
        elif win_rate < 0.4:
            score -= 15
        
        return max(0, min(100, score))
    
    def _assess_risk_control(self, portfolio_data: Dict, 
                           performance_metrics: Dict) -> float:
        """Assess risk control effectiveness (0-100)"""
        score = 70  # Base score assuming good risk control
        
        # Position size adherence
        max_position = max(portfolio_data.get('positions', {}).values(), default=0)
        max_allowed = self.config.get('max_position_size', 0.1)
        
        if max_position > max_allowed * 1.5:
            score -= 40
        elif max_position > max_allowed * 1.2:
            score -= 20
        
        # VaR adherence
        current_var = performance_metrics.get('portfolio_var', 0)
        max_var = self.config.get('max_portfolio_risk', 0.02)
        
        if current_var > max_var * 2:
            score -= 30
        elif current_var > max_var * 1.5:
            score -= 15
        
        # Drawdown control
        current_dd = abs(performance_metrics.get('current_drawdown', 0))
        max_dd = self.config.get('max_drawdown_limit', 0.15)
        
        if current_dd > max_dd:
            score -= 40
        elif current_dd > max_dd * 0.8:
            score -= 20
        
        return max(0, min(100, score))
    
    def _assess_diversification(self, portfolio_data: Dict) -> float:
        """Assess portfolio diversification (0-100)"""
        positions = portfolio_data.get('positions', {})
        
        if len(positions) < 2:
            return 20  # Very poor diversification
        
        # Calculate concentration (Herfindahl Index)
        weights = list(positions.values())
        total_weight = sum(abs(w) for w in weights)
        
        if total_weight == 0:
            return 50
        
        normalized_weights = [abs(w) / total_weight for w in weights]
        herfindahl_index = sum(w**2 for w in normalized_weights)
        
        # Convert to diversification score (lower HHI = better diversification)
        diversification_score = max(0, (1 - herfindahl_index) * 100)
        
        # Bonus for having multiple strategies/assets
        if len(positions) >= 5:
            diversification_score += 10
        elif len(positions) >= 3:
            diversification_score += 5
        
        return min(100, diversification_score)
    
    def _assess_drawdown_health(self, performance_metrics: Dict) -> float:
        """Assess drawdown health (0-100)"""
        current_dd = abs(performance_metrics.get('current_drawdown', 0))
        max_dd = performance_metrics.get('max_drawdown', 0.05)
        max_allowed_dd = self.config.get('max_drawdown_limit', 0.15)
        
        score = 80  # Base score
        
        # Current drawdown assessment
        if current_dd > max_allowed_dd:
            score -= 50
        elif current_dd > max_allowed_dd * 0.7:
            score -= 30
        elif current_dd > max_allowed_dd * 0.5:
            score -= 15
        
        # Historical max drawdown
        if max_dd > max_allowed_dd:
            score -= 20
        elif max_dd > max_allowed_dd * 0.8:
            score -= 10
        
        # Recovery trend
        if len(self.drawdown_history) > 5:
            recent_dd_trend = np.polyfit(range(5), self.drawdown_history[-5:], 1)[0]
            if recent_dd_trend < 0:  # Improving (drawdown reducing)
                score += 10
            elif recent_dd_trend > 0.001:  # Worsening
                score -= 15
        
        return max(0, min(100, score))
    
    def _assess_volatility_control(self, performance_metrics: Dict) -> float:
        """Assess volatility control (0-100)"""
        current_vol = performance_metrics.get('volatility', 0.2)
        target_vol = self.config.get('target_volatility', 0.15)
        max_vol = self.config.get('max_volatility', 0.3)
        
        score = 70  # Base score
        
        if current_vol > max_vol:
            score -= 40
        elif current_vol > target_vol * 1.5:
            score -= 20
        elif current_vol < target_vol * 0.8:
            score += 10  # Bonus for low volatility
        
        # Volatility stability
        if len(self.volatility_history) > 10:
            vol_stability = np.std(self.volatility_history[-10:])
            if vol_stability < 0.05:
                score += 15
            elif vol_stability > 0.1:
                score -= 10
        
        return max(0, min(100, score))
    
    def _assess_liquidity_health(self, portfolio_data: Dict, market_data: Dict) -> float:
        """Assess portfolio liquidity (0-100)"""
        score = 80  # Base score assuming good liquidity
        
        positions = portfolio_data.get('positions', {})
        
        for symbol, position_size in positions.items():
            # Get market data for this symbol
            symbol_data = market_data.get(symbol, {})
            avg_volume = symbol_data.get('avg_volume', 1000000)  # Default reasonable volume
            
            # Estimate position value (simplified)
            price = symbol_data.get('price', 100)
            position_value = abs(position_size) * price
            
            # Calculate days to liquidate (assuming we can trade 10% of daily volume)
            daily_tradeable_value = avg_volume * price * 0.1
            days_to_liquidate = position_value / max(daily_tradeable_value, 1)
            
            if days_to_liquidate > 5:  # More than 5 days to liquidate
                score -= 20
            elif days_to_liquidate > 2:
                score -= 10
        
        return max(0, min(100, score))
    
    def _assess_correlation_risk(self, portfolio_data: Dict, market_data: Dict) -> float:
        """Assess correlation concentration risk (0-100)"""
        # This would require correlation matrix calculation
        # For now, simplified assessment based on asset classes
        
        positions = portfolio_data.get('positions', {})
        if len(positions) < 2:
            return 50
        
        # Simplified: assume crypto assets are highly correlated
        # In reality, would calculate correlation matrix
        score = 70
        
        # If all positions are crypto, high correlation risk
        crypto_symbols = ['BTC', 'ETH', 'ADA', 'LINK', 'DOT', 'SOL']
        crypto_positions = sum(1 for symbol in positions if any(crypto in symbol for crypto in crypto_symbols))
        
        if crypto_positions == len(positions) and len(positions) > 1:
            correlation_risk = min(crypto_positions * 10, 40)
            score -= correlation_risk
        
        return max(0, min(100, score))
    
    def _calculate_stability_score(self, performance_metrics: Dict) -> float:
        """Calculate portfolio stability score (0-100)"""
        if len(self.performance_history) < 10:
            return 50  # Not enough data
        
        # Recent return volatility
        recent_returns = self.performance_history[-20:] if len(self.performance_history) >= 20 else self.performance_history
        return_std = np.std(recent_returns)
        
        # Stability score (lower volatility = higher stability)
        stability = max(0, 100 - (return_std * 1000))  # Scale appropriately
        return min(100, stability)
    
    def _calculate_adaptation_rate(self) -> float:
        """Calculate how quickly portfolio adapts to changes (0-100)"""
        if len(self.historical_health) < 5:
            return 50
        
        # Look at health score changes over time
        recent_health_scores = [h.overall_score for h in self.historical_health[-10:]]
        
        if len(recent_health_scores) < 3:
            return 50
        
        # Calculate how responsive health score is to changes
        health_changes = np.diff(recent_health_scores)
        responsiveness = np.std(health_changes)
        
        # Moderate responsiveness is good (not too sluggish, not too jumpy)
        if 2 < responsiveness < 8:
            return 80
        elif responsiveness < 1:
            return 40  # Too sluggish
        elif responsiveness > 15:
            return 30  # Too jumpy
        else:
            return 60
    
    def _calculate_stress_level(self, performance_metrics: Dict) -> float:
        """Calculate portfolio stress level (0-100, higher = more stressed)"""
        stress = 20  # Base stress level
        
        # Drawdown stress
        current_dd = abs(performance_metrics.get('current_drawdown', 0))
        stress += min(current_dd * 400, 40)  # Up to 40 points for drawdown
        
        # Volatility stress
        current_vol = performance_metrics.get('volatility', 0.15)
        target_vol = self.config.get('target_volatility', 0.15)
        if current_vol > target_vol:
            stress += min((current_vol - target_vol) * 200, 30)
        
        # Performance stress
        recent_return = performance_metrics.get('recent_return', 0)
        if recent_return < -0.05:  # Losing more than 5%
            stress += 20
        elif recent_return < 0:
            stress += 10
        
        return min(100, stress)
    
    def _calculate_energy_level(self, portfolio_data: Dict) -> float:
        """Calculate available energy for new opportunities (0-100)"""
        total_capital = portfolio_data.get('total_capital', 100000)
        used_capital = sum(abs(pos) for pos in portfolio_data.get('positions', {}).values())
        
        # Calculate utilization
        utilization = used_capital / max(total_capital, 1)
        
        # Energy decreases as utilization increases
        energy = max(0, (1 - utilization) * 100)
        
        # Bonus energy if portfolio is performing well
        if len(self.performance_history) > 0:
            recent_performance = np.mean(self.performance_history[-5:]) if len(self.performance_history) >= 5 else self.performance_history[-1]
            if recent_performance > 0.01:  # Good recent performance
                energy = min(100, energy + 10)
        
        return energy
    
    def _calculate_immune_strength(self, portfolio_data: Dict) -> float:
        """Calculate resistance to market shocks (0-100)"""
        immune_strength = 50  # Base immunity
        
        # Diversification boosts immunity
        positions = portfolio_data.get('positions', {})
        if len(positions) >= 3:
            immune_strength += 20
        elif len(positions) >= 5:
            immune_strength += 30
        
        # Cash reserves boost immunity
        cash_ratio = portfolio_data.get('cash_ratio', 0.1)
        immune_strength += min(cash_ratio * 100, 20)  # Up to 20 points for cash
        
        # Historical drawdown recovery boosts immunity
        if len(self.drawdown_history) > 10:
            avg_recovery = np.mean([max(0, -dd) for dd in self.drawdown_history[-10:]])
            if avg_recovery < 0.05:  # Low average drawdowns
                immune_strength += 15
        
        return min(100, immune_strength)
    
    def _calculate_recovery_rate(self, performance_metrics: Dict) -> float:
        """Calculate portfolio recovery rate from drawdowns (0-100)"""
        if len(self.drawdown_history) < 10:
            return 50  # Not enough data
        
        # Look for drawdown recovery patterns
        recovery_periods = []
        in_drawdown = False
        drawdown_start = 0
        
        for i, dd in enumerate(self.drawdown_history):
            if dd < -0.02 and not in_drawdown:  # Enter drawdown
                in_drawdown = True
                drawdown_start = i
            elif dd > -0.01 and in_drawdown:  # Exit drawdown
                in_drawdown = False
                recovery_period = i - drawdown_start
                recovery_periods.append(recovery_period)
        
        if not recovery_periods:
            return 70  # No significant drawdowns = good recovery
        
        # Faster recovery = higher score
        avg_recovery_period = np.mean(recovery_periods)
        recovery_rate = max(0, 100 - (avg_recovery_period * 5))  # 5 points per period
        
        return min(100, recovery_rate)
    
    def _maintain_health_history(self, max_history: int = 1000):
        """Maintain historical health records"""
        if len(self.historical_health) > max_history:
            self.historical_health = self.historical_health[-max_history:]
    
    def get_health_trend(self, lookback_periods: int = 20) -> Dict[str, float]:
        """Get health trend analysis"""
        if len(self.historical_health) < lookback_periods:
            return {'trend': 0, 'confidence': 0}
        
        recent_scores = [h.overall_score for h in self.historical_health[-lookback_periods:]]
        
        # Calculate trend
        x = np.arange(len(recent_scores))
        trend_slope = np.polyfit(x, recent_scores, 1)[0]
        
        # Calculate trend confidence based on R-squared
        correlation = np.corrcoef(x, recent_scores)[0, 1]
        confidence = correlation ** 2 if not np.isnan(correlation) else 0
        
        return {
            'trend': trend_slope,  # Positive = improving, negative = deteriorating
            'confidence': confidence,  # 0-1, how confident we are in the trend
            'current_score': recent_scores[-1],
            'score_change_30d': recent_scores[-1] - recent_scores[0] if len(recent_scores) >= lookback_periods else 0
        }
    
    def get_health_alerts(self, health_metrics: HealthMetrics) -> List[Dict[str, str]]:
        """Generate health-based alerts"""
        alerts = []
        
        # Critical health alerts
        if health_metrics.overall_score < self.critical_threshold:
            alerts.append({
                'level': 'CRITICAL',
                'message': f'Portfolio health critical: {health_metrics.overall_score:.1f}/100',
                'action': 'Immediate intervention required'
            })
        
        # Component-specific alerts
        for component, score in health_metrics.components.items():
            if score < 30:
                alerts.append({
                    'level': 'WARNING',
                    'message': f'{component.replace("_", " ").title()} health poor: {score:.1f}/100',
                    'action': f'Review {component} management'
                })
        
        # Trend-based alerts
        trend_data = self.get_health_trend()
        if trend_data['trend'] < -2 and trend_data['confidence'] > 0.7:
            alerts.append({
                'level': 'WARNING',
                'message': 'Portfolio health declining trend detected',
                'action': 'Review strategy performance and market conditions'
            })
        
        return alerts
    
    def prescribe_health_actions(self, health_metrics: HealthMetrics) -> List[str]:
        """Prescribe specific actions based on health assessment"""
        actions = []
        
        # Overall health actions
        if health_metrics.overall_score < 40:
            actions.extend([
                "EMERGENCY: Reduce all position sizes by 50%",
                "Activate only defensive strategies",
                "Increase monitoring frequency to every 15 minutes",
                "Consider temporary trading halt until health improves"
            ])
        elif health_metrics.overall_score < 60:
            actions.extend([
                "Reduce position sizes by 25%",
                "Increase allocation to low-risk strategies",
                "Review and tighten risk parameters"
            ])
        
        # Component-specific actions
        components = health_metrics.components
        
        if components.get('performance', 100) < 40:
            actions.extend([
                "Review underperforming strategies",
                "Consider strategy parameter optimization",
                "Analyze recent market regime changes"
            ])
        
        if components.get('risk_control', 100) < 40:
            actions.extend([
                "Immediately review all position sizes",
                "Tighten stop-loss parameters",
                "Reduce maximum position limits"
            ])
        
        if components.get('diversification', 100) < 40:
            actions.extend([
                "Increase portfolio diversification",
                "Reduce concentration in top positions",
                "Add uncorrelated strategies or assets"
            ])
        
        if components.get('drawdown_health', 100) < 40:
            actions.extend([
                "Implement emergency drawdown protection",
                "Review and adjust stop-loss levels",
                "Consider reducing overall exposure"
            ])
        
        return actions
    
    def update_performance_history(self, new_performance: float):
        """Update performance history for health calculations"""
        self.performance_history.append(new_performance)
        if len(self.performance_history) > 1000:  # Keep last 1000 periods
            self.performance_history = self.performance_history[-1000:]
    
    def update_drawdown_history(self, new_drawdown: float):
        """Update drawdown history"""
        self.drawdown_history.append(new_drawdown)
        if len(self.drawdown_history) > 1000:
            self.drawdown_history = self.drawdown_history[-1000:]
    
    def update_volatility_history(self, new_volatility: float):
        """Update volatility history"""
        self.volatility_history.append(new_volatility)
        if len(self.volatility_history) > 1000:
            self.volatility_history = self.volatility_history[-1000:]


# Usage example and integration
class HealthBasedRiskAdjuster:
    """
    Adjusts risk parameters based on portfolio health
    """
    
    def __init__(self, config: dict):
        self.config = config
        self.health_monitor = PortfolioHealthMonitor(config)
        
    def adjust_risk_parameters(self, portfolio_data: Dict, 
                             market_data: Dict,
                             performance_metrics: Dict) -> Dict[str, float]:
        """
        Adjust risk parameters based on current health
        """
        # Get current health assessment
        health_metrics = self.health_monitor.assess_portfolio_health(
            portfolio_data, market_data, performance_metrics
        )
        
        # Base risk parameters
        base_params = {
            'max_position_size': self.config.get('max_position_size', 0.1),
            'stop_loss_pct': self.config.get('initial_stop_pct', 0.05),
            'take_profit_ratio': self.config.get('take_profit_ratio', 2.0),
            'max_portfolio_risk': self.config.get('max_portfolio_risk', 0.02)
        }
        
        # Health-based adjustments
        health_score = health_metrics.overall_score
        
        if health_score < 30:  # Critical health
            adjustment_factor = 0.3  # Very conservative
        elif health_score < 50:  # Poor health
            adjustment_factor = 0.6  # Conservative
        elif health_score < 70:  # Moderate health
            adjustment_factor = 0.8  # Slightly conservative
        elif health_score > 85:  # Excellent health
            adjustment_factor = 1.2  # Slightly aggressive
        else:  # Good health
            adjustment_factor = 1.0  # Normal
        
        # Apply adjustments
        adjusted_params = {}
        for param, value in base_params.items():
            if param in ['max_position_size', 'max_portfolio_risk']:
                # Reduce size limits when health is poor
                adjusted_params[param] = value * adjustment_factor
            elif param == 'stop_loss_pct':
                # Tighter stops when health is poor
                adjusted_params[param] = value / adjustment_factor
            else:
                adjusted_params[param] = value
        
        return adjusted_params


if __name__ == "__main__":
    # Example usage
    config = {
        'max_position_size': 0.1,
        'max_drawdown_limit': 0.15,
        'target_volatility': 0.15,
        'stress_threshold': 70,
        'critical_threshold': 30
    }
    
    # Mock data
    portfolio_data = {
        'positions': {'BTCUSDT': 0.05, 'ETHUSDT': 0.03, 'ADAUSDT': 0.02},
        'total_capital': 100000,
        'cash_ratio': 0.1
    }
    
    market_data = {
        'BTCUSDT': {'price': 45000, 'avg_volume': 1000000},
        'ETHUSDT': {'price': 3000, 'avg_volume': 800000},
        'ADAUSDT': {'price': 1.2, 'avg_volume': 500000}
    }
    
    performance_metrics = {
        'recent_return': 0.03,
        'sharpe_ratio': 1.5,
        'win_rate': 0.65,
        'current_drawdown': -0.02,
        'max_drawdown': -0.05,
        'volatility': 0.18
    }
    
    monitor = PortfolioHealthMonitor(config)
    health = monitor.assess_portfolio_health(portfolio_data, market_data, performance_metrics)
    
    print(f"Overall Health Score: {health.overall_score:.1f}/100")
    print(f"Components: {health.components}")
    print(f"Warnings: {health.warnings}")
    print(f"Critical Issues: {health.critical_issues}")
    print(f"Recommendations: {health.recommendations}")
    
    # Get vital signs
    vital_signs = monitor.get_vital_signs(portfolio_data, performance_metrics)
    print(f"\nVital Signs:")
    print(f"Stability: {vital_signs.stability_score:.1f}")
    print(f"Stress Level: {vital_signs.stress_level:.1f}")
    print(f"Energy Level: {vital_signs.energy_level:.1f}")
    print(f"Immune Strength: {vital_signs.immune_strength:.1f}")