#!/usr/bin/env python3
"""
Enhanced Data Manager 
Integrate multiple Alpha Vantage data sources beyond ETFs for better trading signals
"""

import asyncio
import aiohttp
import pandas as pd
import numpy as np
from datetime import datetime, timedelta
from typing import Dict, List, Optional, Tuple
import logging
from dataclasses import dataclass
from enum import Enum

logger = logging.getLogger(__name__)

class DataSourceType(Enum):
    CRYPTO = "crypto"
    EQUITY = "equity" 
    FOREX = "forex"
    COMMODITY = "commodity"
    ECONOMIC = "economic"
    SENTIMENT = "sentiment"

@dataclass
class MarketSentimentScore:
    timestamp: datetime
    vix_level: float
    vix_sentiment: str  # "fear", "greed", "neutral"
    spy_momentum: float
    tech_strength: float
    safe_haven_demand: float
    overall_score: float  # -1 to 1, negative = bearish, positive = bullish
    confidence: float     # 0 to 1

class EnhancedDataManager:
    """
    Enhanced data manager với multiple Alpha Vantage sources
    Focus on market sentiment và correlation analysis
    """
    
    def __init__(self, config: Dict):
        self.config = config
        self.api_key = config.get('alpha_vantage_api_key')
        self.base_url = "https://www.alphavantage.co/query"
        
        # Data cache
        self.data_cache = {}
        self.last_update = {}
        
        # Market sentiment indicators
        self.sentiment_sources = {
            'VIX': {'type': DataSourceType.SENTIMENT, 'weight': 0.3},
            'SPY': {'type': DataSourceType.EQUITY, 'weight': 0.25},
            'QQQ': {'type': DataSourceType.EQUITY, 'weight': 0.2}, 
            'GLD': {'type': DataSourceType.COMMODITY, 'weight': 0.15},
            'TLT': {'type': DataSourceType.COMMODITY, 'weight': 0.1}
        }
        
        # Economic indicators
        self.economic_indicators = [
            'FEDERAL_FUNDS_RATE',
            'CPI',
            'UNEMPLOYMENT'
        ]
        
        # Forex pairs for global sentiment
        self.forex_pairs = [
            ('USD', 'JPY'),  # Risk sentiment
            ('EUR', 'USD'),  # Dollar strength
        ]

    async def fetch_data(self, session: aiohttp.ClientSession, params: Dict) -> Dict:
        """
        Async fetch data from Alpha Vantage
        """
        try:
            async with session.get(self.base_url, params=params) as response:
                data = await response.json()
                
                # Check for errors
                if 'Error Message' in data:
                    logger.error(f"Alpha Vantage error: {data['Error Message']}")
                    return {}
                elif 'Note' in data:
                    logger.warning(f"Rate limit hit: {data['Note']}")
                    return {}
                
                return data
                
        except Exception as e:
            logger.error(f"Error fetching data: {e}")
            return {}

    async def get_daily_equity_data(self, session: aiohttp.ClientSession, symbol: str) -> pd.DataFrame:
        """
        Get daily equity data (SPY, QQQ, etc.)
        """
        params = {
            'function': 'TIME_SERIES_DAILY',
            'symbol': symbol,
            'apikey': self.api_key
        }
        
        data = await self.fetch_data(session, params)
        
        if 'Time Series (Daily)' not in data:
            logger.warning(f"No daily data for {symbol}")
            return pd.DataFrame()
        
        # Convert to DataFrame
        ts_data = data['Time Series (Daily)']
        df_data = []
        
        for date, values in ts_data.items():
            df_data.append({
                'date': pd.to_datetime(date),
                'symbol': symbol,
                'open': float(values['1. open']),
                'high': float(values['2. high']),
                'low': float(values['3. low']),
                'close': float(values['4. close']),
                'volume': int(values['5. volume'])
            })
        
        df = pd.DataFrame(df_data)
        df = df.sort_values('date').reset_index(drop=True)
        
        # Calculate technical indicators
        df['returns'] = df['close'].pct_change()
        df['sma_20'] = df['close'].rolling(20).mean()
        df['volatility_20'] = df['returns'].rolling(20).std() * np.sqrt(252)
        df['rsi'] = self.calculate_rsi(df['close'])
        
        return df

    async def get_forex_data(self, session: aiohttp.ClientSession, 
                           from_symbol: str, to_symbol: str) -> pd.DataFrame:
        """
        Get forex data for sentiment analysis
        """
        params = {
            'function': 'FX_DAILY',
            'from_symbol': from_symbol,
            'to_symbol': to_symbol,
            'apikey': self.api_key
        }
        
        data = await self.fetch_data(session, params)
        
        if 'Time Series FX (Daily)' not in data:
            logger.warning(f"No forex data for {from_symbol}/{to_symbol}")
            return pd.DataFrame()
        
        # Convert to DataFrame
        fx_data = data['Time Series FX (Daily)']
        df_data = []
        
        for date, values in fx_data.items():
            df_data.append({
                'date': pd.to_datetime(date),
                'pair': f"{from_symbol}/{to_symbol}",
                'open': float(values['1. open']),
                'high': float(values['2. high']),
                'low': float(values['3. low']),
                'close': float(values['4. close'])
            })
        
        df = pd.DataFrame(df_data)
        df = df.sort_values('date').reset_index(drop=True)
        df['returns'] = df['close'].pct_change()
        
        return df

    async def get_economic_indicator(self, session: aiohttp.ClientSession, 
                                   indicator: str) -> pd.DataFrame:
        """
        Get economic indicator data
        """
        params = {
            'function': indicator,
            'apikey': self.api_key
        }
        
        data = await self.fetch_data(session, params)
        
        if 'data' not in data:
            logger.warning(f"No data for economic indicator {indicator}")
            return pd.DataFrame()
        
        # Convert to DataFrame
        indicator_data = data['data']
        df_data = []
        
        for item in indicator_data:
            if item['value'] != '.':  # Skip missing values
                df_data.append({
                    'date': pd.to_datetime(item['date']),
                    'indicator': indicator,
                    'value': float(item['value'])
                })
        
        df = pd.DataFrame(df_data)
        df = df.sort_values('date').reset_index(drop=True)
        
        return df

    async def collect_all_market_data(self) -> Dict[str, pd.DataFrame]:
        """
        Collect all market data sources asynchronously
        """
        logger.info("Collecting market data from multiple sources...")
        
        async with aiohttp.ClientSession() as session:
            tasks = []
            
            # Equity data
            for symbol in self.sentiment_sources.keys():
                if symbol not in ['FEDERAL_FUNDS_RATE']:  # Skip economic indicators
                    tasks.append(self.get_daily_equity_data(session, symbol))
                    await asyncio.sleep(12)  # Rate limiting
            
            # Forex data
            for from_curr, to_curr in self.forex_pairs:
                tasks.append(self.get_forex_data(session, from_curr, to_curr))
                await asyncio.sleep(12)
            
            # Economic indicators
            for indicator in self.economic_indicators[:1]:  # Limit to avoid rate limits
                tasks.append(self.get_economic_indicator(session, indicator))
                await asyncio.sleep(12)
            
            # Execute all tasks
            results = await asyncio.gather(*tasks, return_exceptions=True)
            
            # Process results
            market_data = {}
            result_index = 0
            
            # Map equity results
            for symbol in self.sentiment_sources.keys():
                if symbol not in ['FEDERAL_FUNDS_RATE']:
                    if result_index < len(results) and not isinstance(results[result_index], Exception):
                        market_data[symbol] = results[result_index]
                    result_index += 1
            
            # Map forex results
            for from_curr, to_curr in self.forex_pairs:
                pair_key = f"{from_curr}/{to_curr}"
                if result_index < len(results) and not isinstance(results[result_index], Exception):
                    market_data[pair_key] = results[result_index]
                result_index += 1
            
            # Map economic results
            for indicator in self.economic_indicators[:1]:
                if result_index < len(results) and not isinstance(results[result_index], Exception):
                    market_data[indicator] = results[result_index]
                result_index += 1
        
        logger.info(f"Collected data for {len(market_data)} sources")
        return market_data

    def calculate_market_sentiment_score(self, market_data: Dict[str, pd.DataFrame]) -> MarketSentimentScore:
        """
        Calculate comprehensive market sentiment score
        """
        logger.info("Calculating market sentiment score...")
        
        sentiment_components = {}
        latest_date = None
        
        # VIX Analysis (Fear/Greed)
        if 'VIX' in market_data and not market_data['VIX'].empty:
            vix_df = market_data['VIX']
            latest_vix = vix_df.iloc[-1]
            vix_value = latest_vix['close']
            latest_date = latest_vix['date']
            
            # VIX sentiment scoring
            if vix_value < 20:
                vix_sentiment = "greed"
                vix_score = 0.5  # Bullish
            elif vix_value < 30:
                vix_sentiment = "neutral" 
                vix_score = 0.0  # Neutral
            else:
                vix_sentiment = "fear"
                vix_score = -0.5  # Bearish
                
            sentiment_components['vix'] = {
                'value': vix_value,
                'sentiment': vix_sentiment,
                'score': vix_score
            }
        
        # SPY Momentum Analysis
        if 'SPY' in market_data and not market_data['SPY'].empty:
            spy_df = market_data['SPY']
            latest_spy = spy_df.iloc[-1]
            
            # Calculate momentum (price vs SMA)
            price_vs_sma = (latest_spy['close'] - latest_spy['sma_20']) / latest_spy['sma_20']
            
            # RSI analysis
            rsi_score = (latest_spy['rsi'] - 50) / 50  # Normalize to -1 to 1
            
            spy_momentum = (price_vs_sma + rsi_score) / 2
            sentiment_components['spy_momentum'] = spy_momentum
        
        # Tech Sector Strength (QQQ)
        if 'QQQ' in market_data and not market_data['QQQ'].empty:
            qqq_df = market_data['QQQ']
            latest_qqq = qqq_df.iloc[-1]
            
            # Tech sector relative strength
            tech_momentum = (latest_qqq['close'] - latest_qqq['sma_20']) / latest_qqq['sma_20']
            sentiment_components['tech_strength'] = tech_momentum
        
        # Safe Haven Demand (Gold vs Bonds)
        safe_haven_score = 0
        if 'GLD' in market_data and not market_data['GLD'].empty:
            gld_df = market_data['GLD']
            latest_gld = gld_df.iloc[-1]
            gold_momentum = (latest_gld['close'] - latest_gld['sma_20']) / latest_gld['sma_20']
            safe_haven_score += gold_momentum * 0.6
        
        if 'TLT' in market_data and not market_data['TLT'].empty:
            tlt_df = market_data['TLT']
            latest_tlt = tlt_df.iloc[-1]
            bond_momentum = (latest_tlt['close'] - latest_tlt['sma_20']) / latest_tlt['sma_20']
            safe_haven_score += bond_momentum * 0.4
        
        sentiment_components['safe_haven'] = safe_haven_score
        
        # USD/JPY Risk Sentiment
        if 'USD/JPY' in market_data and not market_data['USD/JPY'].empty:
            usdjpy_df = market_data['USD/JPY']
            latest_usdjpy = usdjpy_df.iloc[-1]
            
            # Rising USD/JPY = risk-on, falling = risk-off
            usdjpy_change = usdjpy_df['returns'].iloc[-5:].mean()  # 5-day average
            sentiment_components['risk_appetite'] = usdjpy_change * 10  # Scale up
        
        # Calculate Overall Sentiment Score
        overall_score = 0
        total_weight = 0
        confidence = 0
        
        # Weight the components
        if 'vix' in sentiment_components:
            weight = self.sentiment_sources['VIX']['weight']
            overall_score += sentiment_components['vix']['score'] * weight
            total_weight += weight
            confidence += weight
        
        if 'spy_momentum' in sentiment_components:
            weight = self.sentiment_sources['SPY']['weight']
            overall_score += sentiment_components['spy_momentum'] * weight
            total_weight += weight
            confidence += weight
        
        if 'tech_strength' in sentiment_components:
            weight = self.sentiment_sources['QQQ']['weight']
            overall_score += sentiment_components['tech_strength'] * weight
            total_weight += weight
            confidence += weight
        
        if 'safe_haven' in sentiment_components:
            weight = (self.sentiment_sources['GLD']['weight'] + 
                     self.sentiment_sources['TLT']['weight'])
            overall_score += sentiment_components['safe_haven'] * weight
            total_weight += weight
            confidence += weight
        
        if 'risk_appetite' in sentiment_components:
            weight = 0.1  # Additional weight for forex
            overall_score += sentiment_components['risk_appetite'] * weight
            total_weight += weight
            confidence += weight
        
        # Normalize
        if total_weight > 0:
            overall_score = overall_score / total_weight
            confidence = confidence / len(self.sentiment_sources)
        
        # Clamp scores
        overall_score = max(-1, min(1, overall_score))
        confidence = max(0, min(1, confidence))
        
        # Create sentiment object
        return MarketSentimentScore(
            timestamp=latest_date or datetime.now(),
            vix_level=sentiment_components.get('vix', {}).get('value', 0),
            vix_sentiment=sentiment_components.get('vix', {}).get('sentiment', 'neutral'),
            spy_momentum=sentiment_components.get('spy_momentum', 0),
            tech_strength=sentiment_components.get('tech_strength', 0),
            safe_haven_demand=sentiment_components.get('safe_haven', 0),
            overall_score=overall_score,
            confidence=confidence
        )

    def calculate_rsi(self, prices: pd.Series, period: int = 14) -> pd.Series:
        """
        Calculate RSI indicator
        """
        delta = prices.diff()
        gain = (delta.where(delta > 0, 0)).rolling(window=period).mean()
        loss = (-delta.where(delta < 0, 0)).rolling(window=period).mean()
        rs = gain / loss
        rsi = 100 - (100 / (1 + rs))
        return rsi

    def analyze_crypto_correlation(self, market_data: Dict[str, pd.DataFrame], 
                                 crypto_returns: pd.Series) -> Dict[str, float]:
        """
        Analyze correlation between crypto and traditional markets
        """
        correlations = {}
        
        for symbol, df in market_data.items():
            if not df.empty and 'returns' in df.columns:
                # Align dates
                crypto_aligned = crypto_returns.dropna()
                market_aligned = df.set_index('date')['returns'].dropna()
                
                # Find common dates
                common_dates = crypto_aligned.index.intersection(market_aligned.index)
                
                if len(common_dates) > 30:  # Need sufficient data
                    crypto_common = crypto_aligned[common_dates]
                    market_common = market_aligned[common_dates]
                    
                    correlation = crypto_common.corr(market_common)
                    correlations[symbol] = correlation
        
        return correlations

    def get_trading_signals(self, sentiment_score: MarketSentimentScore,
                          correlations: Dict[str, float]) -> Dict[str, any]:
        """
        Generate trading signals based on market sentiment and correlations
        """
        signals = {
            'sentiment_signal': 0,
            'confidence': sentiment_score.confidence,
            'reasoning': [],
            'market_regime': 'neutral'
        }
        
        # Primary sentiment signal
        if sentiment_score.overall_score > 0.3:
            signals['sentiment_signal'] = 1
            signals['market_regime'] = 'risk_on'
            signals['reasoning'].append("Strong risk-on sentiment favors crypto")
        elif sentiment_score.overall_score < -0.3:
            signals['sentiment_signal'] = -1
            signals['market_regime'] = 'risk_off'
            signals['reasoning'].append("Risk-off sentiment negative for crypto")
        
        # VIX-specific signals
        if sentiment_score.vix_level > 30:
            signals['reasoning'].append(f"High VIX ({sentiment_score.vix_level:.1f}) indicates fear")
        elif sentiment_score.vix_level < 15:
            signals['reasoning'].append(f"Low VIX ({sentiment_score.vix_level:.1f}) indicates complacency")
        
        # Tech sector correlation
        if 'QQQ' in correlations and correlations['QQQ'] > 0.5:
            if sentiment_score.tech_strength > 0.1:
                signals['reasoning'].append("Strong tech sector supports crypto")
            elif sentiment_score.tech_strength < -0.1:
                signals['reasoning'].append("Weak tech sector drags crypto")
        
        # Safe haven competition
        if sentiment_score.safe_haven_demand > 0.1:
            signals['reasoning'].append("Strong safe haven demand may compete with crypto")
        
        return signals

    async def run_comprehensive_analysis(self) -> Dict[str, any]:
        """
        Run comprehensive market analysis
        """
        logger.info("Starting comprehensive market analysis...")
        
        # Collect all market data
        market_data = await self.collect_all_market_data()
        
        if not market_data:
            logger.error("No market data collected")
            return {}
        
        # Calculate market sentiment
        sentiment_score = self.calculate_market_sentiment_score(market_data)
        
        # For demo purposes, create sample crypto returns
        # In real implementation, this would come from crypto data source
        sample_crypto_returns = pd.Series(
            np.random.normal(0, 0.03, 100),  # Sample crypto returns
            index=pd.date_range('2024-01-01', periods=100, freq='D')
        )
        
        # Analyze correlations
        correlations = self.analyze_crypto_correlation(market_data, sample_crypto_returns)
        
        # Generate trading signals
        signals = self.get_trading_signals(sentiment_score, correlations)
        
        # Compile results
        analysis_result = {
            'timestamp': datetime.now(),
            'sentiment_score': sentiment_score,
            'correlations': correlations,
            'signals': signals,
            'data_sources': list(market_data.keys()),
            'data_quality': {
                source: len(df) for source, df in market_data.items()
            }
        }
        
        logger.info("Analysis completed successfully")
        return analysis_result

    def print_analysis_summary(self, analysis: Dict[str, any]) -> None:
        """
        Print human-readable analysis summary
        """
        if not analysis:
            print("❌ No analysis data available")
            return
        
        print("\n" + "="*60)
        print("📊 COMPREHENSIVE MARKET ANALYSIS SUMMARY")
        print("="*60)
        
        sentiment = analysis['sentiment_score']
        signals = analysis['signals']
        correlations = analysis['correlations']
        
        print(f"\n🎯 MARKET SENTIMENT SCORE")
        print(f"   Overall Score: {sentiment.overall_score:.3f} (-1 to 1)")
        print(f"   Confidence: {sentiment.confidence:.3f}")
        print(f"   Market Regime: {signals['market_regime']}")
        
        print(f"\n📈 SENTIMENT COMPONENTS")
        print(f"   VIX Level: {sentiment.vix_level:.1f} ({sentiment.vix_sentiment})")
        print(f"   SPY Momentum: {sentiment.spy_momentum:.3f}")
        print(f"   Tech Strength: {sentiment.tech_strength:.3f}")
        print(f"   Safe Haven Demand: {sentiment.safe_haven_demand:.3f}")
        
        print(f"\n🔗 CRYPTO CORRELATIONS")
        for asset, corr in correlations.items():
            strength = "Strong" if abs(corr) > 0.5 else "Moderate" if abs(corr) > 0.3 else "Weak"
            direction = "Positive" if corr > 0 else "Negative"
            print(f"   {asset}: {corr:.3f} ({strength} {direction})")
        
        print(f"\n🚦 TRADING SIGNALS")
        signal_direction = "BULLISH" if signals['sentiment_signal'] > 0 else "BEARISH" if signals['sentiment_signal'] < 0 else "NEUTRAL"
        print(f"   Primary Signal: {signal_direction}")
        print(f"   Confidence: {signals['confidence']:.3f}")
        
        print(f"\n💡 REASONING")
        for reason in signals['reasoning']:
            print(f"   • {reason}")
        
        print(f"\n📊 DATA QUALITY")
        for source, count in analysis['data_quality'].items():
            print(f"   {source}: {count} data points")

# Usage Example
async def main():
    """
    Example usage of enhanced data manager
    """
    config = {
        'alpha_vantage_api_key': 'your_api_key_here'  # Replace with actual API key
    }
    
    # Initialize manager
    manager = EnhancedDataManager(config)
    
    # Run analysis
    analysis = await manager.run_comprehensive_analysis()
    
    # Print results
    manager.print_analysis_summary(analysis)

if __name__ == "__main__":
    # Set your API key as environment variable or replace directly
    import os
    if not os.getenv('ALPHAVANTAGE_API_KEY'):
        print("⚠️  Set ALPHAVANTAGE_API_KEY environment variable")
    
    asyncio.run(main())