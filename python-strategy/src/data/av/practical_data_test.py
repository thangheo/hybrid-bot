#!/usr/bin/env python3
"""
Practical Test của các Alpha Vantage data sources
Test các endpoints khác ngoài ETF để tìm data hữu ích cho crypto trading
"""

import requests
import pandas as pd
import json
from datetime import datetime, timedelta
import time
import os

def test_alpha_vantage_data_sources():
    """
    Test comprehensive Alpha Vantage data sources
    """
    # Get API key
    api_key = os.getenv('ALPHAVANTAGE_API_KEY') 
    if not api_key:
        print("⚠️  Set ALPHAVANTAGE_API_KEY environment variable or enter manually:")
        api_key = input("API Key: ").strip()
    
    if not api_key:
        print("❌ No API key provided")
        return
    
    base_url = "https://www.alphavantage.co/query"
    
    print("🔍 TESTING ALPHA VANTAGE DATA SOURCES")
    print("=" * 50)
    
    # Test 1: Market Sentiment Indicators
    print("\n1️⃣ MARKET SENTIMENT INDICATORS")
    print("-" * 30)
    
    # VIX - Volatility Index (Fear/Greed)
    print("📊 Testing VIX (Fear Index)...")
    try:
        vix_params = {
            'function': 'TIME_SERIES_DAILY',
            'symbol': 'VIX',
            'apikey': api_key
        }
        response = requests.get(base_url, params=vix_params)
        vix_data = response.json()
        
        if 'Time Series (Daily)' in vix_data:
            latest_date = list(vix_data['Time Series (Daily)'].keys())[0]
            latest_vix = vix_data['Time Series (Daily)'][latest_date]['4. close']
            print(f"✅ VIX: {latest_vix} (Date: {latest_date})")
            
            vix_val = float(latest_vix)
            if vix_val < 20:
                sentiment = "Low Fear - Bullish for risk assets"
            elif vix_val < 30:
                sentiment = "Moderate Fear - Neutral"
            else:
                sentiment = "High Fear - Risk-off environment"
            print(f"   Market Sentiment: {sentiment}")
        else:
            print("❌ VIX data not available")
    except Exception as e:
        print(f"❌ VIX Error: {e}")
    
    time.sleep(12)  # Rate limiting
    
    # Test 2: Major Market Indices
    print("\n2️⃣ MAJOR MARKET INDICES")
    print("-" * 30)
    
    indices = {
        'SPY': 'S&P 500 ETF',
        'QQQ': 'NASDAQ-100 ETF', 
        'IWM': 'Russell 2000 Small Cap'
    }
    
    for symbol, name in indices.items():
        print(f"📈 Testing {symbol} ({name})...")
        try:
            params = {
                'function': 'TIME_SERIES_DAILY',
                'symbol': symbol,
                'apikey': api_key
            }
            response = requests.get(base_url, params=params)
            data = response.json()
            
            if 'Time Series (Daily)' in data:
                ts = data['Time Series (Daily)']
                latest_date = list(ts.keys())[0]
                latest_data = ts[latest_date]
                
                close = float(latest_data['4. close'])
                open_price = float(latest_data['1. open'])
                change = ((close - open_price) / open_price) * 100
                
                print(f"✅ {symbol}: ${close:.2f} ({change:+.2f}%) - {latest_date}")
            else:
                print(f"❌ {symbol}: No data available")
        except Exception as e:
            print(f"❌ {symbol} Error: {e}")
        
        time.sleep(12)
    
    # Test 3: Commodities & Safe Havens
    print("\n3️⃣ COMMODITIES & SAFE HAVENS")
    print("-" * 30)
    
    commodities = {
        'GLD': 'Gold ETF',
        'SLV': 'Silver ETF',
        'USO': 'Oil ETF',
        'TLT': '20+ Year Treasury Bonds'
    }
    
    for symbol, name in commodities.items():
        print(f"🥇 Testing {symbol} ({name})...")
        try:
            params = {
                'function': 'TIME_SERIES_DAILY',
                'symbol': symbol,
                'apikey': api_key
            }
            response = requests.get(base_url, params=params)
            data = response.json()
            
            if 'Time Series (Daily)' in data:
                ts = data['Time Series (Daily)']
                latest_date = list(ts.keys())[0]
                latest_data = ts[latest_date]
                
                close = float(latest_data['4. close'])
                print(f"✅ {symbol}: ${close:.2f} - {latest_date}")
            else:
                print(f"❌ {symbol}: No data available")
        except Exception as e:
            print(f"❌ {symbol} Error: {e}")
        
        time.sleep(12)
    
    # Test 4: Sector Performance
    print("\n4️⃣ SECTOR PERFORMANCE")
    print("-" * 30)
    
    try:
        print("📊 Testing Sector Performance...")
        sector_params = {
            'function': 'SECTOR',
            'apikey': api_key
        }
        response = requests.get(base_url, params=sector_params)
        sector_data = response.json()
        
        if 'Rank A: Real-Time Performance' in sector_data:
            print("✅ Sector Performance Data Available:")
            sectors = sector_data['Rank A: Real-Time Performance']
            
            # Show top/bottom performing sectors
            sector_items = list(sectors.items())
            print("   Top Performers:")
            for sector, perf in sector_items[:3]:
                print(f"     • {sector}: {perf}")
            
            print("   Bottom Performers:")
            for sector, perf in sector_items[-3:]:
                print(f"     • {sector}: {perf}")
        else:
            print("❌ Sector data not available")
    except Exception as e:
        print(f"❌ Sector Error: {e}")
    
    time.sleep(12)
    
    # Test 5: Forex Data (Global Sentiment)
    print("\n5️⃣ FOREX DATA (GLOBAL SENTIMENT)")
    print("-" * 30)
    
    forex_pairs = [
        ('USD', 'JPY', 'Risk Sentiment Indicator'),
        ('EUR', 'USD', 'Dollar Strength'),
        ('GBP', 'USD', 'Risk Appetite')
    ]
    
    for from_curr, to_curr, description in forex_pairs:
        print(f"💱 Testing {from_curr}/{to_curr} ({description})...")
        try:
            params = {
                'function': 'FX_DAILY',
                'from_symbol': from_curr,
                'to_symbol': to_curr,
                'apikey': api_key
            }
            response = requests.get(base_url, params=params)
            data = response.json()
            
            if 'Time Series FX (Daily)' in data:
                fx_ts = data['Time Series FX (Daily)']
                latest_date = list(fx_ts.keys())[0]
                latest_rate = fx_ts[latest_date]['4. close']
                
                print(f"✅ {from_curr}/{to_curr}: {latest_rate} - {latest_date}")
            else:
                print(f"❌ {from_curr}/{to_curr}: No data available")
        except Exception as e:
            print(f"❌ {from_curr}/{to_curr} Error: {e}")
        
        time.sleep(12)
    
    # Test 6: Economic Indicators (if available)
    print("\n6️⃣ ECONOMIC INDICATORS")
    print("-" * 30)
    
    # Federal Funds Rate
    print("🏛️ Testing Federal Funds Rate...")
    try:
        fed_params = {
            'function': 'FEDERAL_FUNDS_RATE',
            'apikey': api_key
        }
        response = requests.get(base_url, params=fed_params)
        fed_data = response.json()
        
        if 'data' in fed_data and len(fed_data['data']) > 0:
            latest_fed = fed_data['data'][0]
            print(f"✅ Fed Funds Rate: {latest_fed['value']}% (Date: {latest_fed['date']})")
        else:
            print("❌ Fed Funds Rate: No data available")
    except Exception as e:
        print(f"❌ Fed Funds Rate Error: {e}")
    
    time.sleep(12)
    
    # Test 7: Direct Crypto Data
    print("\n7️⃣ CRYPTO DATA FROM ALPHA VANTAGE")
    print("-" * 30)
    
    crypto_symbols = ['BTC', 'ETH', 'ADA']
    
    for crypto in crypto_symbols:
        print(f"₿ Testing {crypto}...")
        try:
            params = {
                'function': 'DIGITAL_CURRENCY_DAILY',
                'symbol': crypto,
                'market': 'USD',
                'apikey': api_key
            }
            response = requests.get(base_url, params=params)
            data = response.json()
            
            if 'Time Series (Digital Currency Daily)' in data:
                crypto_ts = data['Time Series (Digital Currency Daily)']
                latest_date = list(crypto_ts.keys())[0]
                latest_data = crypto_ts[latest_date]
                
                close_usd = latest_data['4a. close (USD)']
                volume = latest_data['5. volume']
                
                print(f"✅ {crypto}: ${float(close_usd):,.2f} USD")
                print(f"   Volume: {float(volume):,.0f}")
                print(f"   Date: {latest_date}")
                
                # Check data freshness
                latest_dt = datetime.strptime(latest_date, '%Y-%m-%d')
                days_old = (datetime.now() - latest_dt).days
                if days_old <= 1:
                    freshness = "Fresh"
                elif days_old <= 3:
                    freshness = "Acceptable"
                else:
                    freshness = "Stale"
                print(f"   Freshness: {freshness} ({days_old} days old)")
            else:
                print(f"❌ {crypto}: No data available")
        except Exception as e:
            print(f"❌ {crypto} Error: {e}")
        
        time.sleep(12)
    
    # Summary and Recommendations
    print("\n" + "=" * 60)
    print("📋 SUMMARY & RECOMMENDATIONS")
    print("=" * 60)
    
    print("""
🎯 MOST USEFUL DATA SOURCES FOR CRYPTO TRADING:

1. MARKET SENTIMENT (High Priority):
   • VIX: Strong correlation with crypto during risk-off periods
   • SPY/QQQ: General risk appetite indicators
   • USD/JPY: Classic risk-on/risk-off pair

2. TRADITIONAL MARKET PROXIES (Medium Priority):
   • Gold (GLD): Inflation hedge comparison
   • Treasury Bonds (TLT): Flight-to-safety indicator  
   • Technology Sector (XLK): Overlapping investor base

3. MACRO ECONOMIC (Lower Priority but Important):
   • Federal Funds Rate: Affects crypto indirectly through liquidity
   • Sector Performance: Tech sector correlation
   • DXY (if available): Dollar strength affects crypto

4. UPDATE FREQUENCY ANALYSIS:
   ✅ Traditional Markets: Daily updates, reliable
   ⚠️  Crypto from Alpha Vantage: Daily updates but sometimes delayed
   ❌ ETFs: Can be stale, especially niche ones

🚀 RECOMMENDATIONS:

1. IMPLEMENT FIRST (High correlation, reliable updates):
   • VIX for market sentiment
   • SPY for overall market direction  
   • Gold (GLD) for safe-haven comparison

2. ADD LATER (Supporting indicators):
   • USD/JPY for global risk sentiment
   • QQQ for tech sector exposure
   • TLT for bond market sentiment

3. CONSIDER ALTERNATIVES:
   • Use Binance API for real-time crypto data
   • Alpha Vantage mainly for traditional market context
   • Combine multiple sources for robust signals

4. DATA UPDATE STRATEGY:
   • Traditional markets: Once daily after market close
   • Economic indicators: When released (monthly/quarterly)
   • Crypto: Real-time from dedicated crypto APIs
   """)
    
    print("✅ Data source testing completed!")

if __name__ == "__main__":
    test_alpha_vantage_data_sources()