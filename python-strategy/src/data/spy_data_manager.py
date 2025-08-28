# 1. FILE: src/data/spy_data_manager.py
import requests
import pandas as pd
import numpy as np
from datetime import datetime, timedelta
from typing import Dict, Optional
import logging

class SPYDataManager:
    """Quản lý SPY data cho market filter"""
    
    def __init__(self, api_key: str):
        self.api_key = api_key
        self.base_url = "https://www.alphavantage.co/query"
        self.spy_cache = {}
        self.last_update = None
        
    async def get_spy_data(self, days: int = 30) -> Optional[pd.DataFrame]:
        """Lấy SPY data với cache"""
        try:
            # Check cache (update mỗi 4 hours)
            if (self.last_update and 
                datetime.now() - self.last_update < timedelta(hours=4) and
                'SPY' in self.spy_cache):
                return self.spy_cache['SPY']
            
            # Fetch fresh data
            params = {
                'function': 'TIME_SERIES_DAILY',
                'symbol': 'SPY',
                'apikey': self.api_key
            }
            
            response = requests.get(self.base_url, params=params)
            data = response.json()
            
            if 'Time Series (Daily)' not in data:
                logging.error("❌ Failed to fetch SPY data")
                return None
            
            ts_data = data['Time Series (Daily)']
            df_data = []
            
            for date_str, values in ts_data.items():
                df_data.append({
                    'date': pd.to_datetime(date_str),
                    'open': float(values['1. open']),
                    'high': float(values['2. high']),
                    'low': float(values['3. low']),
                    'close': float(values['4. close']),
                    'volume': int(values['5. volume'])
                })
            
            df = pd.DataFrame(df_data)
            df = df.sort_values('date').reset_index(drop=True)
            df = df.tail(days)  # Keep recent data
            
            # Calculate technical indicators
            df['returns'] = df['close'].pct_change()
            df['sma_5'] = df['close'].rolling(5).mean()
            df['sma_10'] = df['close'].rolling(10).mean()
            df['sma_20'] = df['close'].rolling(20).mean()
            df['volatility'] = df['returns'].rolling(10).std()
            df['momentum'] = df['close'].pct_change(5)  # 5-day momentum
            
            # Cache data
            self.spy_cache['SPY'] = df
            self.last_update = datetime.now()
            
            logging.info(f"📊 SPY data updated: {len(df)} days")
            return df
            
        except Exception as e:
            logging.error(f"❌ SPY data fetch failed: {e}")
            return None

