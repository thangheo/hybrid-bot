# src/risk/portfolio_monitor.py
"""
Portfolio Risk Monitoring
Uses NumPy/SciPy for statistical calculations and portfolio risk metrics
"""

import numpy as np
from scipy import stats
from typing import Dict, List, Optional 
import logging

logger = logging.getLogger(__name__)

# Import from local module
try:
    from .risk_models import RiskMetrics, RiskLevel
except ImportError:
    # Fallback for direct execution
    from risk_models import RiskMetrics, RiskLevel

class PortfolioRiskMonitor:
    """Monitor portfolio-level risk metrics using established statistical libraries"""
    
    def __init__(self, config: dict):
        self.config = config
        self.var_confidence = config.get('var_confidence', 0.05)
        self.correlation_threshold = config.get('correlation_threshold', 0.7)
    # Fix for src/risk/portfolio_monitor.py
    """
    Updated portfolio risk calculation to handle empty portfolios correctly
    """

    def calculate_portfolio_risk(self, portfolio: dict, 
                            new_symbol: str = None, 
                            new_position: float = 0) -> float:
        """Calculate portfolio Value at Risk (VaR) using SciPy - FIXED VERSION"""
        
        positions = self._get_position_weights(portfolio, new_symbol, new_position)
        if not positions:
            return 0
            
        # FIX: Handle single position case (empty portfolio + new position)
        if len(positions) == 1 and new_symbol and new_position > 0:
            # For single position, use individual asset risk instead of portfolio correlation
            asset_volatility = self._get_asset_volatility(new_symbol)
            # Scale by position size (2% position = 2% of asset volatility)
            single_position_risk = asset_volatility * new_position
            return single_position_risk
        
        # Get correlation matrix
        correlation_matrix = self._get_correlation_matrix(list(positions.keys()))
        
        # Calculate portfolio variance using NumPy linear algebra
        weights = np.array(list(positions.values()))
        volatilities = np.array([self._get_asset_volatility(symbol) 
                            for symbol in positions.keys()])
        
        # Portfolio variance = w^T * Σ * w
        covariance_matrix = correlation_matrix * np.outer(volatilities, volatilities)
        portfolio_variance = np.dot(weights, np.dot(covariance_matrix, weights))
        portfolio_volatility = np.sqrt(portfolio_variance)
        
        # VaR calculation using SciPy normal distribution
        var = stats.norm.ppf(self.var_confidence) * portfolio_volatility
        
        return abs(var)
    
    def check_concentration_risk(self, portfolio: dict) -> Dict[str, float]:
        """Check for concentration risk using NumPy"""
        
        positions = self._get_position_weights(portfolio)
        
        if not positions:
            return {}
            
        position_values = np.array(list(positions.values()))
        
        concentration_metrics = {
            'max_position': np.max(position_values),
            'top_3_concentration': np.sum(np.sort(position_values)[-3:]),
            'herfindahl_index': np.sum(position_values ** 2),
            'effective_positions': 1 / np.sum(position_values ** 2) if np.sum(position_values ** 2) > 0 else 0
        }
        
        return concentration_metrics
    
    def check_sector_exposure(self, portfolio: dict) -> Dict[str, float]:
        """Check exposure by sector/category"""
        
        # Crypto sector mapping (simplified)
        sectors = {
            'BTCUSDT': 'digital_gold',
            'ETHUSDT': 'smart_contracts',
            'ADAUSDT': 'smart_contracts',
            'LINKUSDT': 'oracles',
            'DOTUSDT': 'interoperability',
            'SOLUSDT': 'smart_contracts'
        }
        
        sector_exposure = {}
        positions = self._get_position_weights(portfolio)
        
        for symbol, weight in positions.items():
            sector = sectors.get(symbol, 'other')
            sector_exposure[sector] = sector_exposure.get(sector, 0) + weight
            
        return sector_exposure
    
    def get_risk_metrics(self, portfolio: dict) -> RiskMetrics:
        """Get comprehensive risk metrics"""
        
        portfolio_var = self.calculate_portfolio_risk(portfolio)
        concentration = self.check_concentration_risk(portfolio)
        
        # Determine risk level
        risk_level = self._determine_risk_level(portfolio_var, concentration)
        
        return RiskMetrics(
            portfolio_var=portfolio_var,
            max_position_size=concentration.get('max_position', 0),
            current_exposure=sum(self._get_position_weights(portfolio).values()),
            drawdown_level=self.calculate_current_drawdown(portfolio),
            risk_level=risk_level
        )
    
    def check_correlation_exposure(self, symbol: str, portfolio: dict) -> bool:
        """Check if adding symbol would exceed correlation limits"""
        
        # Simplified correlation check - in production use real correlation data
        correlated_symbols = self._get_correlated_symbols(symbol)
        
        current_correlated_exposure = 0
        positions = self._get_position_weights(portfolio)
        
        for corr_symbol in correlated_symbols:
            current_correlated_exposure += positions.get(corr_symbol, 0)
            
        return current_correlated_exposure < self.correlation_threshold
    
    def calculate_current_drawdown(self, portfolio: dict) -> float:
        """Calculate current portfolio drawdown"""
        
        # Extract numeric values from portfolio
        portfolio_positions = self._extract_portfolio_positions(portfolio)
        
        # Simplified drawdown calculation
        # In production, track portfolio value over time
        current_value = sum(portfolio_positions.values()) if portfolio_positions else 0
        peak_value = current_value * 1.1  # Assume 10% higher peak for demo
        
        if peak_value == 0:
            return 0
            
        drawdown = (peak_value - current_value) / peak_value
        return max(0, drawdown)
    
    def _extract_portfolio_positions(self, portfolio: dict) -> Dict[str, float]:
        """Extract numeric position values from portfolio data"""
        positions = {}
        
        # Handle different portfolio formats
        if isinstance(portfolio, dict):
            for key, value in portfolio.items():
                # Skip non-position data (metadata, timestamps, etc.)
                if key in ['timestamp', 'total_value', 'cash_balance', 'positions_value', 
                          'total_unrealized_pnl', 'total_realized_pnl', 'daily_pnl',
                          'total_commission_paid', 'num_positions', 'return_pct']:
                    continue
                
                # Handle numeric values (position sizes)
                if isinstance(value, (int, float)) and not isinstance(value, bool):
                    if value != 0:  # Only include non-zero positions
                        positions[key] = float(value)
                # Handle nested position data
                elif isinstance(value, dict) and 'market_value' in value:
                    if value['market_value'] != 0:
                        positions[key] = float(value['market_value'])
        
        return positions
    
    def _get_position_weights(self, portfolio: dict, 
                            new_symbol: str = None, 
                            new_position: float = 0) -> Dict[str, float]:
        """Get position weights as percentages"""
        
        # Extract numeric positions from portfolio
        positions = self._extract_portfolio_positions(portfolio)
        
        if new_symbol and new_position != 0:
            positions[new_symbol] = positions.get(new_symbol, 0) + new_position
            
        if not positions:
            return {}
            
        # Calculate total absolute value for weights
        total_value = sum(abs(pos) for pos in positions.values())
        
        if total_value == 0:
            return {}
            
        return {symbol: abs(position) / total_value 
                for symbol, position in positions.items() if position != 0}
    
    def _get_correlation_matrix(self, symbols: List[str]) -> np.ndarray:
        """Get correlation matrix for symbols (simplified)"""
        
        n = len(symbols)
        if n == 0:
            return np.array([[]])
            
        # Simplified correlation matrix - in production use real market data
        correlation_matrix = np.eye(n)
        
        # Add some correlation between crypto pairs
        crypto_correlation = 0.6
        for i in range(n):
            for j in range(i + 1, n):
                correlation_matrix[i, j] = crypto_correlation
                correlation_matrix[j, i] = crypto_correlation
                
        return correlation_matrix
    
    def _get_asset_volatility(self, symbol: str) -> float:
        """Get asset volatility (simplified)"""
        
        # Simplified volatility mapping - in production use real market data
        volatility_map = {
            'BTCUSDT': 0.04,  # 4% daily volatility
            'ETHUSDT': 0.05,  # 5% daily volatility
            'ADAUSDT': 0.06,  # 6% daily volatility
            'LINKUSDT': 0.07, # 7% daily volatility
            'DOTUSDT': 0.06,  # 6% daily volatility
            'XRPUSDT': 0.05,  # 5% daily volatility
            'ARBUSDT': 0.05,  # 5% daily volatility
            'SUIUSDT': 0.05,  # 5% daily volatility
            'SOLUSDT': 0.08   # 8% daily volatility

        }
        
        return volatility_map.get(symbol, 0.05)  # 5% default
    
    def _get_correlated_symbols(self, symbol: str) -> List[str]:
        """Get symbols highly correlated with given symbol"""
        
        # Simplified correlation mapping
        correlation_groups = {
            'BTCUSDT': ['ETHUSDT'],
            'ETHUSDT': ['BTCUSDT', 'ADAUSDT', 'SOLUSDT'],
            'ADAUSDT': ['ETHUSDT', 'DOTUSDT'],
            'LINKUSDT': ['DOTUSDT'],
            
            'DOTUSDT': ['ADAUSDT', 'LINKUSDT'],
            'SOLUSDT': ['ETHUSDT']
        }
        
        return correlation_groups.get(symbol, [])
    
    def _determine_risk_level(self, portfolio_var: float, 
                            concentration: Dict[str, float]) -> RiskLevel:
        """Determine overall risk level"""
        
        if portfolio_var > 0.05 or concentration.get('max_position', 0) > 0.3:
            return RiskLevel.CRITICAL
        elif portfolio_var > 0.03 or concentration.get('max_position', 0) > 0.2:
            return RiskLevel.HIGH
        elif portfolio_var > 0.02 or concentration.get('max_position', 0) > 0.15:
            return RiskLevel.MEDIUM
        else:
            return RiskLevel.LOW