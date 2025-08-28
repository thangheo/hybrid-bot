#!/usr/bin/env python3
"""
Alpha Vantage Data Study Script
Khám phá các data sources khác từ Alpha Vantage cho crypto trading bot
"""

import requests
import pandas as pd
import json
from datetime import datetime, timedelta
import time
from typing import Dict, List, Optional
import os

class AlphaVantageStudy:
    def __init__(self, api_key: str):
        self.api_key = api_key
        self.base_url = "https://www.alphavantage.co/query"
        
    def test_api_connection(self) -> bool:
        """Test Alpha Vantage API connection"""
        try:
            params = {
                'function': 'TIME_SERIES_INTRADAY',
                'symbol': 'AAPL',
                'interval': '1min',
                'apikey': self.api_key
            }
            response = requests.get(self.base_url, params=params)
            data = response.json()
            
            if 'Error Message' in data:
                print(f"❌ API Error: {data['Error Message']}")
                return False
            elif 'Note' in data:
                print(f"⚠️  Rate Limit: {data['Note']}")
                return False
            else:
                print("✅ API Connection successful")
                return True
        except Exception as e:
            print(f"❌ Connection failed: {e}")
            return False

    def get_crypto_data(self, symbol: str = "BTC") -> Dict:
        """
        Get crypto data - Alpha Vantage supports digital currencies
        """
        print(f"\n=== CRYPTO DATA: {symbol} ===")
        
        # Daily crypto data
        params = {
            'function': 'DIGITAL_CURRENCY_DAILY',
            'symbol': symbol,
            'market': 'USD',
            'apikey': self.api_key
        }
        
        try:
            response = requests.get(self.base_url, params=params)
            data = response.json()
            
            if 'Error Message' in data:
                print(f"❌ Error: {data['Error Message']}")
                return {}
            
            if 'Time Series (Digital Currency Daily)' in data:
                ts_data = data['Time Series (Digital Currency Daily)']
                latest_date = list(ts_data.keys())[0]
                latest_data = ts_data[latest_date]
                
                print(f"📊 Latest {symbol} data ({latest_date}):")
                print(f"   Open: ${latest_data['1a. open (USD)']}")
                print(f"   High: ${latest_data['2a. high (USD)']}")
                print(f"   Low: ${latest_data['3a. low (USD)']}")
                print(f"   Close: ${latest_data['4a. close (USD)']}")
                print(f"   Volume: {latest_data['5. volume']}")
                
                return data
            else:
                print("❌ No crypto data found")
                return {}
                
        except Exception as e:
            print(f"❌ Error fetching crypto data: {e}")
            return {}

    def get_forex_data(self, from_symbol: str = "USD", to_symbol: str = "JPY") -> Dict:
        """
        Get forex data - có thể hữu ích cho currency exposure analysis
        """
        print(f"\n=== FOREX DATA: {from_symbol}/{to_symbol} ===")
        
        params = {
            'function': 'FX_DAILY',
            'from_symbol': from_symbol,
            'to_symbol': to_symbol,
            'apikey': self.api_key
        }
        
        try:
            response = requests.get(self.base_url, params=params)
            data = response.json()
            
            if 'Error Message' in data:
                print(f"❌ Error: {data['Error Message']}")
                return {}
            
            if 'Time Series FX (Daily)' in data:
                ts_data = data['Time Series FX (Daily)']
                latest_date = list(ts_data.keys())[0]
                latest_data = ts_data[latest_date]
                
                print(f"📈 Latest {from_symbol}/{to_symbol} rate ({latest_date}):")
                print(f"   Open: {latest_data['1. open']}")
                print(f"   High: {latest_data['2. high']}")
                print(f"   Low: {latest_data['3. low']}")
                print(f"   Close: {latest_data['4. close']}")
                
                return data
            else:
                print("❌ No forex data found")
                return {}
                
        except Exception as e:
            print(f"❌ Error fetching forex data: {e}")
            return {}

    def get_economic_indicators(self) -> Dict:
        """
        Get macroeconomic indicators - rất hữu ích cho market sentiment
        """
        print("\n=== ECONOMIC INDICATORS ===")
        
        indicators = {
            'FEDERAL_FUNDS_RATE': 'Fed Funds Rate',
            'CPI': 'Consumer Price Index (Inflation)',
            'GDP': 'Gross Domestic Product',
            'UNEMPLOYMENT': 'Unemployment Rate',
            'NONFARM_PAYROLL': 'Non-Farm Payrolls'
        }
        
        results = {}
        
        for indicator, name in indicators.items():
            print(f"\n📊 {name}:")
            params = {
                'function': 'FEDERAL_FUNDS_RATE' if indicator == 'FEDERAL_FUNDS_RATE' else indicator,
                'apikey': self.api_key
            }
            
            try:
                response = requests.get(self.base_url, params=params)
                data = response.json()
                
                if 'data' in data and len(data['data']) > 0:
                    latest = data['data'][0]
                    print(f"   Date: {latest['date']}")
                    print(f"   Value: {latest['value']}")
                    results[indicator] = data
                else:
                    print("   ❌ No data available")
                    
                time.sleep(12)  # Rate limiting
                
            except Exception as e:
                print(f"   ❌ Error: {e}")
        
        return results

    def get_market_sentiment_data(self) -> Dict:
        """
        Get market sentiment indicators
        """
        print("\n=== MARKET SENTIMENT ===")
        
        # VIX - Volatility Index (fear index)
        print("📈 VIX (Volatility Index):")
        params = {
            'function': 'TIME_SERIES_DAILY',
            'symbol': 'VIX',
            'apikey': self.api_key
        }
        
        try:
            response = requests.get(self.base_url, params=params)
            data = response.json()
            
            if 'Time Series (Daily)' in data:
                ts_data = data['Time Series (Daily)']
                latest_date = list(ts_data.keys())[0]
                latest_data = ts_data[latest_date]
                
                print(f"   Latest VIX ({latest_date}): {latest_data['4. close']}")
                print(f"   High: {latest_data['2. high']} | Low: {latest_data['3. low']}")
                
                vix_value = float(latest_data['4. close'])
                if vix_value < 20:
                    sentiment = "Low Fear (Complacent)"
                elif vix_value < 30:
                    sentiment = "Moderate Fear"
                else:
                    sentiment = "High Fear (Panic)"
                    
                print(f"   Market Sentiment: {sentiment}")
                return data
            else:
                print("   ❌ No VIX data found")
                return {}
                
        except Exception as e:
            print(f"   ❌ Error fetching VIX: {e}")
            return {}

    def get_sector_performance(self) -> Dict:
        """
        Get sector performance data
        """
        print("\n=== SECTOR PERFORMANCE ===")
        
        params = {
            'function': 'SECTOR',
            'apikey': self.api_key
        }
        
        try:
            response = requests.get(self.base_url, params=params)
            data = response.json()
            
            if 'Rank A: Real-Time Performance' in data:
                realtime = data['Rank A: Real-Time Performance']
                print("📊 Real-time Sector Performance:")
                for sector, performance in realtime.items():
                    print(f"   {sector}: {performance}")
                
                return data
            else:
                print("❌ No sector data found")
                return {}
                
        except Exception as e:
            print(f"❌ Error fetching sector data: {e}")
            return {}

    def get_commodities_data(self) -> Dict:
        """
        Get commodities data - useful for inflation/market correlation
        """
        print("\n=== COMMODITIES DATA ===")
        
        commodities = ['GLD', 'SLV', 'USO', 'UNG']  # Gold, Silver, Oil, Natural Gas ETFs
        results = {}
        
        for commodity in commodities:
            print(f"\n📊 {commodity}:")
            params = {
                'function': 'TIME_SERIES_DAILY',
                'symbol': commodity,
                'apikey': self.api_key
            }
            
            try:
                response = requests.get(self.base_url, params=params)
                data = response.json()
                
                if 'Time Series (Daily)' in data:
                    ts_data = data['Time Series (Daily)']
                    latest_date = list(ts_data.keys())[0]
                    latest_data = ts_data[latest_date]
                    
                    print(f"   Latest ({latest_date}): ${latest_data['4. close']}")
                    print(f"   Change: ${float(latest_data['4. close']) - float(latest_data['1. open']):.2f}")
                    results[commodity] = data
                else:
                    print("   ❌ No data found")
                    
                time.sleep(12)  # Rate limiting
                
            except Exception as e:
                print(f"   ❌ Error: {e}")
        
        return results

    def analyze_data_freshness(self, symbol: str = "AAPL") -> None:
        """
        Analyze how fresh the data is
        """
        print(f"\n=== DATA FRESHNESS ANALYSIS: {symbol} ===")
        
        params = {
            'function': 'TIME_SERIES_DAILY',
            'symbol': symbol,
            'apikey': self.api_key
        }
        
        try:
            response = requests.get(self.base_url, params=params)
            data = response.json()
            
            if 'Time Series (Daily)' in data:
                ts_data = data['Time Series (Daily)']
                dates = list(ts_data.keys())
                
                latest_date = datetime.strptime(dates[0], '%Y-%m-%d')
                now = datetime.now()
                
                print(f"📅 Latest data: {latest_date.strftime('%Y-%m-%d')}")
                print(f"📅 Current time: {now.strftime('%Y-%m-%d %H:%M')}")
                
                days_behind = (now - latest_date).days
                print(f"⏰ Data is {days_behind} days behind")
                
                if days_behind <= 1:
                    print("✅ Data is fresh (within 1 day)")
                elif days_behind <= 3:
                    print("⚠️  Data is slightly stale (2-3 days)")
                else:
                    print("❌ Data is stale (>3 days)")
                
                # Check update frequency
                print(f"\n📊 Recent dates available:")
                for i, date in enumerate(dates[:5]):
                    print(f"   {i+1}. {date}")
                    
        except Exception as e:
            print(f"❌ Error analyzing freshness: {e}")

    def suggest_useful_data_for_crypto_trading(self) -> None:
        """
        Suggest useful data sources for crypto trading
        """
        print("\n" + "="*60)
        print("🎯 RECOMMENDED DATA SOURCES FOR CRYPTO TRADING")
        print("="*60)
        
        recommendations = {
            "Market Sentiment Indicators": {
                "VIX": "Fear/Greed index - crypto often correlates inversely",
                "USD/JPY": "Risk-on/risk-off sentiment",
                "Gold (GLD)": "Safe haven demand vs risk assets"
            },
            "Macro Economic Data": {
                "Federal Funds Rate": "Interest rate changes affect crypto flows",
                "CPI (Inflation)": "Inflation data drives crypto as inflation hedge narrative",
                "DXY (Dollar Index)": "USD strength typically inverse to crypto"
            },
            "Traditional Market Correlations": {
                "NASDAQ (QQQ)": "Tech stocks correlation with crypto",
                "S&P 500 (SPY)": "Overall market sentiment",
                "Russell 2000 (IWM)": "Risk appetite indicator"
            },
            "Alternative Data": {
                "Sector Performance": "Tech sector performance vs crypto",
                "Commodities": "Inflation hedges comparison",
                "Currency Pairs": "Global liquidity flows"
            }
        }
        
        for category, items in recommendations.items():
            print(f"\n📋 {category}:")
            for item, description in items.items():
                print(f"   • {item}: {description}")
        
        print("\n🔄 UPDATE FREQUENCIES:")
        print("   • Daily OHLCV: End of trading day (~4PM ET)")
        print("   • Intraday: Real-time during market hours")
        print("   • Economic indicators: Monthly/Quarterly releases")
        print("   • Forex: 24/7 except weekends")
        print("   • Crypto: 24/7 (but Alpha Vantage updates daily)")

def main():
    """
    Main function để run study
    """
    print("🔍 ALPHA VANTAGE DATA STUDY")
    print("="*50)
    
    # Get API key from environment or input
    api_key = os.getenv('ALPHAVANTAGE_API_KEY') 
    if not api_key:
        api_key = input("Enter your Alpha Vantage API key: ").strip()
    
    if not api_key:
        print("❌ No API key provided")
        return
    
    study = AlphaVantageStudy(api_key)
    
    # Test connection first
    if not study.test_api_connection():
        print("❌ Cannot proceed without valid API connection")
        return
    
    print("\n🚀 Starting comprehensive data study...")
    
    # 1. Test crypto data
    study.get_crypto_data("BTC")
    time.sleep(12)
    
    # 2. Test forex data (useful for global sentiment)
    study.get_forex_data("USD", "JPY")
    time.sleep(12)
    
    # 3. Market sentiment
    study.get_market_sentiment_data()
    time.sleep(12)
    
    # 4. Sector performance
    study.get_sector_performance()
    time.sleep(12)
    
    # 5. Commodities
    study.get_commodities_data()
    
    # 6. Data freshness analysis
    study.analyze_data_freshness("SPY")
    
    # 7. Economic indicators (commented out due to rate limits)
    # study.get_economic_indicators()
    
    # 8. Recommendations
    study.suggest_useful_data_for_crypto_trading()
    
    print("\n✅ Study completed!")
    print("\n💡 Next steps:")
    print("   1. Choose the most relevant indicators for your strategy")
    print("   2. Implement data collection for selected sources")
    print("   3. Create correlation analysis between crypto and traditional markets")
    print("   4. Build composite sentiment indicators")

if __name__ == "__main__":
    main()