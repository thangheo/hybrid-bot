#!/usr/bin/env python3
"""
Fixed Crypto Correlation Analysis
Based on actual Alpha Vantage API format from your discovery
"""

import requests
import pandas as pd
import numpy as np
from datetime import datetime, timedelta
import time
import logging
from typing import Dict, List, Tuple, Optional
from scipy import stats
import matplotlib.pyplot as plt
import seaborn as sns

logger = logging.getLogger(__name__)

class FixedCryptoCorrelationAnalysis:
    """
    Fixed version with correct Alpha Vantage API formats
    """
    
    def __init__(self, api_key: str):
        self.api_key = api_key
        self.base_url = "https://www.alphavantage.co/query"
        self.data_cache = {}
        
    def fetch_crypto_data_fixed(self, symbol: str = "BTC", market: str = "USD") -> pd.DataFrame:
        """
        Fixed crypto data fetching with correct format handling
        """
        print(f"🔗 Fetching crypto data for {symbol}/{market}...")
        
        try:
            params = {
                'function': 'DIGITAL_CURRENCY_DAILY',
                'symbol': symbol,
                'market': market,
                'apikey': self.api_key
            }
            
            response = requests.get(self.base_url, params=params)
            data = response.json()
            
            # Check for errors first
            if 'Error Message' in data:
                print(f"❌ API Error: {data['Error Message']}")
                return pd.DataFrame()
            
            if 'Note' in data:
                print(f"⚠️ Rate limit: {data['Note']}")
                return pd.DataFrame()
            
            # Check for the correct key based on your API response
            ts_key = 'Time Series (Digital Currency Daily)'
            if ts_key not in data:
                print(f"❌ Expected key '{ts_key}' not found")
                print(f"Available keys: {list(data.keys())}")
                return pd.DataFrame()
            
            ts_data = data[ts_key]
            
            # Parse the data with correct field names
            df_data = []
            for date_str, values in ts_data.items():
                try:
                    # From your API response, we see the correct field names:
                    df_data.append({
                        'date': pd.to_datetime(date_str),
                        'open': float(values['1. open']),      # Note: no 'a' suffix for USD market
                        'high': float(values['2. high']),      # From your JSON sample
                        'low': float(values['3. low']),
                        'close': float(values['4. close']),
                        'volume': float(values['5. volume'])
                    })
                except KeyError as e:
                    # If market is not USD, try with 'a' suffix
                    try:
                        df_data.append({
                            'date': pd.to_datetime(date_str),
                            'open': float(values['1a. open (USD)']),
                            'high': float(values['2a. high (USD)']),
                            'low': float(values['3a. low (USD)']),
                            'close': float(values['4a. close (USD)']),
                            'volume': float(values['5. volume'])
                        })
                    except KeyError:
                        print(f"❌ Could not parse data for {date_str}: {e}")
                        print(f"Available fields: {list(values.keys())}")
                        continue
            
            if not df_data:
                print(f"❌ No valid data points parsed for {symbol}")
                return pd.DataFrame()
            
            # Create DataFrame
            df = pd.DataFrame(df_data)
            df = df.sort_values('date').reset_index(drop=True)
            
            # Calculate returns and technical indicators
            df['returns'] = df['close'].pct_change()
            df['log_returns'] = np.log(df['close'] / df['close'].shift(1))
            df['sma_20'] = df['close'].rolling(20).mean()
            df['volatility_20'] = df['returns'].rolling(20).std() * np.sqrt(252)
            
            # Data quality info
            latest_date = df['date'].max()
            data_age = (datetime.now() - latest_date).days
            
            print(f"✅ {symbol}: {len(df)} days of data")
            print(f"   Latest: {latest_date.strftime('%Y-%m-%d')} ({data_age} days old)")
            print(f"   Price range: ${df['close'].min():.0f} - ${df['close'].max():.0f}")
            
            return df
            
        except Exception as e:
            print(f"❌ Error fetching crypto {symbol}: {e}")
            return pd.DataFrame()

    def fetch_traditional_asset_data(self, symbol: str) -> pd.DataFrame:
        """
        Fetch traditional asset data (stocks, ETFs) - this format works fine
        """
        print(f"📊 Fetching traditional asset data for {symbol}...")
        
        try:
            params = {
                'function': 'TIME_SERIES_DAILY',
                'symbol': symbol,
                'apikey': self.api_key
            }
            
            response = requests.get(self.base_url, params=params)
            data = response.json()
            
            if 'Time Series (Daily)' not in data:
                print(f"❌ No data for {symbol}")
                return pd.DataFrame()
            
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
            
            # Technical indicators
            df['returns'] = df['close'].pct_change()
            df['log_returns'] = np.log(df['close'] / df['close'].shift(1))
            df['sma_20'] = df['close'].rolling(20).mean()
            df['volatility_20'] = df['returns'].rolling(20).std() * np.sqrt(252)
            
            latest_date = df['date'].max()
            data_age = (datetime.now() - latest_date).days
            
            print(f"✅ {symbol}: {len(df)} days, {data_age} days old")
            return df
            
        except Exception as e:
            print(f"❌ Error fetching {symbol}: {e}")
            return pd.DataFrame()

    def fetch_forex_data_fixed(self, from_symbol: str, to_symbol: str) -> pd.DataFrame:
        """
        Fetch forex data - this format also works fine
        """
        print(f"💱 Fetching forex data for {from_symbol}/{to_symbol}...")
        
        try:
            params = {
                'function': 'FX_DAILY',
                'from_symbol': from_symbol,
                'to_symbol': to_symbol,
                'apikey': self.api_key
            }
            
            response = requests.get(self.base_url, params=params)
            data = response.json()
            
            if 'Time Series FX (Daily)' not in data:
                print(f"❌ No forex data for {from_symbol}/{to_symbol}")
                return pd.DataFrame()
            
            fx_data = data['Time Series FX (Daily)']
            df_data = []
            
            for date_str, values in fx_data.items():
                df_data.append({
                    'date': pd.to_datetime(date_str),
                    'open': float(values['1. open']),
                    'high': float(values['2. high']),
                    'low': float(values['3. low']),
                    'close': float(values['4. close'])
                })
            
            df = pd.DataFrame(df_data)
            df = df.sort_values('date').reset_index(drop=True)
            df['returns'] = df['close'].pct_change()
            
            print(f"✅ {from_symbol}/{to_symbol}: {len(df)} days")
            return df
            
        except Exception as e:
            print(f"❌ Error fetching {from_symbol}/{to_symbol}: {e}")
            return pd.DataFrame()

    def run_comprehensive_correlation_analysis(self) -> Dict[str, any]:
        """
        Run comprehensive correlation analysis with fixed crypto data
        """
        print("\n🔍 COMPREHENSIVE CORRELATION ANALYSIS")
        print("=" * 60)
        
        # Step 1: Fetch crypto data (fixed format)
        print("\n1️⃣ FETCHING CRYPTO DATA")
        crypto_data = self.fetch_crypto_data_fixed("BTC", "USD")  # Use USD market
        
        if crypto_data.empty:
            print("❌ Cannot proceed without crypto data")
            return {}
        
        time.sleep(12)  # Rate limiting
        
        # Step 2: Fetch traditional market data
        print("\n2️⃣ FETCHING TRADITIONAL MARKET DATA")
        
        traditional_symbols = ['SPY', 'QQQ', 'GLD', 'TLT', 'VTI']  # Remove VIX for now
        traditional_data = {}
        
        for symbol in traditional_symbols:
            df = self.fetch_traditional_asset_data(symbol)
            if not df.empty:
                traditional_data[symbol] = df
            time.sleep(12)  # Rate limiting
        
        if not traditional_data:
            print("❌ No traditional market data collected")
            return {}
        
        # Step 3: Fetch forex data  
        print("\n3️⃣ FETCHING FOREX DATA")
        forex_pairs = [('USD', 'JPY'), ('EUR', 'USD')]
        forex_data = {}
        
        for from_curr, to_curr in forex_pairs:
            pair_name = f"{from_curr}/{to_curr}"
            df = self.fetch_forex_data_fixed(from_curr, to_curr)
            if not df.empty:
                forex_data[pair_name] = df
            time.sleep(12)
        
        # Step 4: Align dates and calculate correlations
        print("\n4️⃣ CALCULATING CORRELATIONS")
        
        all_data = {'BTC': crypto_data}
        all_data.update(traditional_data)
        all_data.update(forex_data)
        
        # Find common dates
        common_dates = None
        for symbol, df in all_data.items():
            if not df.empty:
                dates = set(df['date'].dt.date)
                if common_dates is None:
                    common_dates = dates
                else:
                    common_dates = common_dates.intersection(dates)
        
        if not common_dates:
            print("❌ No common dates found")
            return {}
        
        print(f"📅 Found {len(common_dates)} common trading days")
        
        # Create aligned returns DataFrame
        returns_data = {}
        for symbol, df in all_data.items():
            # Filter to common dates
            df_filtered = df[df['date'].dt.date.isin(common_dates)].copy()
            df_filtered = df_filtered.sort_values('date').reset_index(drop=True)
            
            if len(df_filtered) > 10:  # Need minimum data points
                returns_data[symbol] = df_filtered['returns']
        
        returns_df = pd.DataFrame(returns_data)
        returns_df = returns_df.dropna()
        
        if returns_df.empty:
            print("❌ No aligned returns data")
            return {}
        
        print(f"📊 Analyzing {len(returns_df)} observations across {len(returns_df.columns)} assets")
        
        # Calculate correlation matrix
        correlation_matrix = returns_df.corr()
        
        # Step 5: Focused analysis on BTC correlations
        if 'BTC' in correlation_matrix.columns:
            btc_correlations = correlation_matrix['BTC'].drop('BTC').sort_values(key=abs, ascending=False)
            
            print(f"\n📈 BTC CORRELATIONS (Ranked by Absolute Value):")
            for asset, corr in btc_correlations.items():
                strength = self._correlation_strength(abs(corr))
                direction = "📈" if corr > 0 else "📉"
                print(f"   {direction} {asset}: {corr:+.3f} ({strength})")
        
        # Step 6: Market regime analysis
        regime_analysis = self._analyze_market_regimes(returns_df)
        
        # Step 7: Leading/lagging analysis
        leading_analysis = self._find_leading_indicators(returns_df)
        
        # Compile results
        analysis_result = {
            'correlation_matrix': correlation_matrix,
            'returns_data': returns_df,
            'btc_correlations': btc_correlations if 'BTC' in correlation_matrix.columns else None,
            'regime_analysis': regime_analysis,
            'leading_analysis': leading_analysis,
            'data_summary': {
                'total_observations': len(returns_df),
                'date_range': f"{returns_df.index[0]} to {returns_df.index[-1]}",
                'assets_analyzed': list(returns_df.columns)
            }
        }
        
        return analysis_result
    
    def _correlation_strength(self, abs_corr: float) -> str:
        """Classify correlation strength"""
        if abs_corr > 0.7:
            return "Very Strong"
        elif abs_corr > 0.5:
            return "Strong"
        elif abs_corr > 0.3:
            return "Moderate"
        elif abs_corr > 0.1:
            return "Weak"
        else:
            return "Very Weak"
    
    def _analyze_market_regimes(self, returns_df: pd.DataFrame) -> Dict:
        """Analyze correlations in different market regimes"""
        if 'SPY' not in returns_df.columns or 'BTC' not in returns_df.columns:
            return {}
        
        spy_returns = returns_df['SPY']
        
        # Define regimes based on SPY 20-day rolling average
        spy_sma = spy_returns.rolling(20).mean()
        
        bull_mask = spy_sma > 0.001  # Positive momentum
        bear_mask = spy_sma < -0.001  # Negative momentum
        
        regime_analysis = {}
        
        if bull_mask.sum() > 30:  # Need sufficient data
            bull_correlations = returns_df[bull_mask].corr()['BTC'].drop('BTC')
            regime_analysis['bull_market'] = {
                'periods': bull_mask.sum(),
                'correlations': bull_correlations.sort_values(key=abs, ascending=False)
            }
        
        if bear_mask.sum() > 30:
            bear_correlations = returns_df[bear_mask].corr()['BTC'].drop('BTC')
            regime_analysis['bear_market'] = {
                'periods': bear_mask.sum(),
                'correlations': bear_correlations.sort_values(key=abs, ascending=False)
            }
        
        return regime_analysis
    
    def _find_leading_indicators(self, returns_df: pd.DataFrame) -> Dict:
        """Find which assets lead BTC movements"""
        if 'BTC' not in returns_df.columns:
            return {}
        
        btc_returns = returns_df['BTC']
        leading_analysis = {}
        
        for asset in returns_df.columns:
            if asset == 'BTC':
                continue
            
            asset_returns = returns_df[asset]
            
            # Test 1-day to 5-day lags
            lag_correlations = {}
            for lag in range(1, 6):
                if len(asset_returns) > lag:
                    lagged_asset = asset_returns.shift(lag)
                    correlation = btc_returns.corr(lagged_asset)
                    if not np.isnan(correlation):
                        lag_correlations[f"{lag}-day"] = correlation
            
            if lag_correlations:
                best_lag = max(lag_correlations.items(), key=lambda x: abs(x[1]))
                leading_analysis[asset] = {
                    'lag_correlations': lag_correlations,
                    'best_lag': best_lag[0],
                    'best_correlation': best_lag[1]
                }
        
        return leading_analysis
    
    def print_comprehensive_analysis(self, analysis: Dict) -> None:
        """Print detailed analysis results"""
        if not analysis:
            print("❌ No analysis results to display")
            return
        
        print("\n" + "=" * 80)
        print("📊 COMPREHENSIVE CRYPTO CORRELATION ANALYSIS RESULTS")
        print("=" * 80)
        
        # Data summary
        summary = analysis['data_summary']
        print(f"\n📈 DATA SUMMARY:")
        print(f"   • Total observations: {summary['total_observations']}")
        print(f"   • Date range: {summary['date_range']}")
        print(f"   • Assets analyzed: {', '.join(summary['assets_analyzed'])}")
        
        # BTC correlations
        btc_corrs = analysis.get('btc_correlations')
        if btc_corrs is not None:
            print(f"\n🔗 BTC CORRELATIONS (Strongest to Weakest):")
            for asset, corr in btc_corrs.items():
                strength = self._correlation_strength(abs(corr))
                direction_symbol = "📈" if corr > 0 else "📉"
                print(f"   {direction_symbol} {asset:<10}: {corr:+.3f} ({strength})")
        
        # Market regime analysis
        regime_analysis = analysis.get('regime_analysis', {})
        if regime_analysis:
            print(f"\n📊 MARKET REGIME ANALYSIS:")
            
            for regime, data in regime_analysis.items():
                print(f"\n   {regime.upper().replace('_', ' ')} ({data['periods']} observations):")
                for asset, corr in list(data['correlations'].items())[:5]:  # Top 5
                    print(f"     • {asset}: {corr:+.3f}")
        
        # Leading indicators
        leading_analysis = analysis.get('leading_analysis', {})
        if leading_analysis:
            print(f"\n🎯 LEADING INDICATORS (Assets that predict BTC):")
            
            # Sort by best absolute correlation
            sorted_leaders = sorted(
                leading_analysis.items(), 
                key=lambda x: abs(x[1]['best_correlation']), 
                reverse=True
            )
            
            for asset, data in sorted_leaders[:5]:  # Top 5
                best_lag = data['best_lag']
                best_corr = data['best_correlation']
                print(f"   • {asset}: {best_corr:+.3f} at {best_lag} lag")
        
        # Trading recommendations
        self._print_trading_recommendations(analysis)
    
    def _print_trading_recommendations(self, analysis: Dict) -> None:
        """Print actionable trading recommendations"""
        print(f"\n🚀 ACTIONABLE TRADING RECOMMENDATIONS:")
        
        btc_corrs = analysis.get('btc_correlations')
        if btc_corrs is None:
            return
        
        # High correlation assets (for confirmation signals)
        high_corr_assets = btc_corrs[abs(btc_corrs) > 0.4]
        if len(high_corr_assets) > 0:
            print(f"\n   📈 HIGH CORRELATION SIGNALS (Use for confirmation):")
            for asset, corr in high_corr_assets.items():
                direction = "same direction" if corr > 0 else "opposite direction"
                print(f"     • {asset} moves in {direction} as BTC")
        
        # Leading indicators
        leading_analysis = analysis.get('leading_analysis', {})
        if leading_analysis:
            strong_leaders = {
                asset: data for asset, data in leading_analysis.items() 
                if abs(data['best_correlation']) > 0.3
            }
            
            if strong_leaders:
                print(f"\n   🎯 PREDICTIVE SIGNALS (Use for early warning):")
                for asset, data in strong_leaders.items():
                    lag = data['best_lag']
                    corr = data['best_correlation']
                    direction = "bullish" if corr > 0 else "bearish"
                    print(f"     • Watch {asset} for {direction} BTC signals ({lag} ahead)")
        
        print(f"\n   ⚡ IMPLEMENTATION TIPS:")
        print(f"     • Use high correlation assets to confirm BTC signals")
        print(f"     • Monitor leading indicators for early position adjustment")  
        print(f"     • Consider market regime when applying correlations")
        print(f"     • Correlations change over time - update regularly")

def main():
    """
    Run the fixed correlation analysis
    """
    print("🔧 FIXED CRYPTO CORRELATION ANALYSIS")
    print("=" * 60)
    
    import os
    api_key = os.getenv('ALPHA_VANTAGE_API_KEY')
    if not api_key:
        print("⚠️ No API key found in environment variable")
        api_key = input("Enter your Alpha Vantage API key: ").strip()
    
    if not api_key:
        print("❌ No API key provided")
        return
    
    # Initialize analyzer
    analyzer = FixedCryptoCorrelationAnalysis(api_key)
    
    # Run comprehensive analysis
    analysis = analyzer.run_comprehensive_correlation_analysis()
    
    # Print results
    analyzer.print_comprehensive_analysis(analysis)
    
    # Save results if successful
    if analysis and 'correlation_matrix' in analysis:
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        
        # Save correlation matrix
        corr_matrix = analysis['correlation_matrix']
        corr_matrix.to_csv(f'btc_correlations_{timestamp}.csv')
        
        # Save returns data
        returns_data = analysis['returns_data']
        returns_data.to_csv(f'returns_data_{timestamp}.csv')
        
        print(f"\n💾 Results saved with timestamp: {timestamp}")
        print(f"   • btc_correlations_{timestamp}.csv")
        print(f"   • returns_data_{timestamp}.csv")

if __name__ == "__main__":
    main()