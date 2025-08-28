# ================================================================
# src/strategy/base_strategy.py - FIXED VERSION
# Reduced minimum periods requirement for daily trading
# ================================================================

from abc import ABC, abstractmethod
from typing import Dict, List, Optional
import pandas as pd

class BaseStrategy(ABC):
    """Abstract base class for all trading strategies"""
    
    def __init__(self, config: dict):
        self.config = config
        self.indicators = {}
        self.signals = {}
        self.name = self.__class__.__name__
        
    @abstractmethod
    def calculate_indicators(self, data: pd.DataFrame) -> Dict[str, pd.Series]:
        """Calculate all required indicators for the strategy"""
        pass
    
    @abstractmethod
    def generate_signals(self, data: pd.DataFrame, indicators: Dict[str, pd.Series]) -> pd.DataFrame:
        """Generate buy/sell/hold signals based on indicators"""
        pass
    
    def validate_data(self, data: pd.DataFrame) -> bool:
        """Validate input data quality - IMPROVED with detailed error messages"""
        if data is None or data.empty:
            print(f"❌ Data validation failed: Empty data")
            return False
        
        # Make column names case-insensitive
        available_columns = [col.lower() for col in data.columns]
        required_columns = ['open', 'high', 'low', 'close', 'volume']
        
        if not all(col in available_columns for col in required_columns):
            missing = [col for col in required_columns if col not in available_columns]
            print(f"❌ Data validation failed: Missing columns: {missing}")
            print(f"   Available columns: {list(data.columns)}")
            return False
        
        # IMPROVED: More flexible minimum periods for daily data
        # Most EMAs need 20-30 periods, but we can work with less
        min_periods = self.config.get('min_periods', 20)  # Reduced from 30 to 20
        
        if len(data) < min_periods:
            print(f"❌ Data validation failed: Insufficient data: {len(data)} records < {min_periods} minimum")
            print(f"   Data date range: {data.index.min()} to {data.index.max()}")
            print(f"   Last 5 timestamps: {list(data.index[-5:])}")
            return False
        
        # Check for data quality - find close column case-insensitively
        close_column = None
        for col in data.columns:
            if col.lower() == 'close':
                close_column = col
                break
        
        if close_column and data[close_column].isna().sum() > len(data) * 0.1:
            nan_pct = data[close_column].isna().sum() / len(data)
            print(f"❌ Data validation failed: Too many NaN values in close: {nan_pct:.1%}")
            return False
        
        # ADDED: Check data freshness (last bar should be recent)
        if not data.index.empty:
            last_timestamp = data.index.max()
            hours_old = (pd.Timestamp.now() - last_timestamp).total_seconds() / 3600
            
            # For daily data, should not be older than 48 hours
            # For 4h data, should not be older than 12 hours
            max_age_hours = 48  # Can be made configurable
            
            if hours_old > max_age_hours:
                print(f"⚠️  Data validation warning: Data is {hours_old:.1f} hours old (max: {max_age_hours}h)")
                print(f"   Last timestamp: {last_timestamp}")
                # Don't fail, just warn - old data is better than no data for backtesting
        
        # SUCCESS
        print(f"✅ Data validation passed: {len(data)} records, latest: {data.index.max()}")
        return True
    
    def get_strategy_info(self) -> Dict:
        """Return strategy metadata"""
        return {
            'name': self.name,
            'config': self.config,
            'required_periods': self.config.get('min_periods', 30)  # Updated default
        }