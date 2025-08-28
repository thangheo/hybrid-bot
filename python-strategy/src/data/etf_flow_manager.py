# ================================================================
# ETF Flow Data Manager - AlphaVantage Integration
# File: src/data/etf_flow_manager.py
# ================================================================

import asyncio
import aiohttp
import pandas as pd
import numpy as np
from typing import Dict, List, Optional, Tuple
from datetime import datetime, timedelta
import json
import logging
from dataclasses import dataclass
import os
from collections import defaultdict
# loadenv
from dotenv import load_dotenv

load_dotenv()

logger = logging.getLogger(__name__)

@dataclass
class ETFFlowData:
    symbol: str
    price: float
    volume: int
    flow_proxy: float  # Volume * price_change as flow proxy
    change_5d: float
    timestamp: datetime
    confidence: float  # Data quality score 0-1

@dataclass
class CompositeETFFlow:
    total_flow: float           # Combined flow from all ETFs
    flow_strength: float        # Normalized flow strength [-1, 1]
    institutional_sentiment: str  # 'bullish', 'bearish', 'neutral'
    volume_weighted_flow: float  # Volume-weighted flow
    dominant_etf: str           # ETF with highest impact
    confidence: float           # Overall data confidence
    timestamp: datetime

class ETFFlowManager:
    """Manage Bitcoin ETF flow data from AlphaVantage API"""
    
    # def __init__(self, config: dict):
    #     self.config = config
    #     self.api_key = config.get('alphavantage_api_key', os.getenv('ALPHAVANTAGE_API_KEY'))
    #     self.etf_symbols = config.get('etf_symbols', ['IBIT', 'FBTC', 'BITB', 'ARKB', 'BTCO'])
    #     # Rate limiting (AlphaVantage: 50000 calls/day)
    #     self.daily_call_limit = 50000
    #     self.calls_made_today = 0
    #     self.last_reset_date = datetime.now().date()
        
    #     # Cache settings
    #     self.cache_duration_minutes = config.get('etf_cache_duration', 360)  # 6 hours cache
    #     self.etf_cache = {}
    #     self.composite_cache = None
    #     self.cache_timestamp = None
        
    #     # Fallback data for when API fails
    #     self.fallback_enabled = config.get('etf_fallback_enabled', True)
    #     self.last_known_flows = {}
        
    #     logger.info(f"ETF Flow Manager initialized for {len(self.etf_symbols)} ETFs")
    #     logger.info(f"Daily API limit: {self.daily_call_limit}, Cache duration: {self.cache_duration_minutes}min")
    def __init__(self, config: dict):
        self.config = config
        self.api_key = config.get('alphavantage_api_key', os.getenv('ALPHAVANTAGE_API_KEY'))
        self.etf_symbols = config.get('etf_symbols', ['IBIT', 'FBTC', 'BITB', 'ARKB', 'BTCO'])
        
        # 🚀 PREMIUM API Rate Limiting (FIXED)
        self.daily_call_limit = config.get('daily_api_limit', 100000)    # Premium: practically unlimited
        self.minute_call_limit = config.get('minute_api_limit', 75)      # Premium: 75/minute  
        self.calls_made_today = 0
        self.calls_this_minute = 0
        self.last_reset_date = datetime.now().date()
        self.last_minute_reset = datetime.now().replace(second=0, microsecond=0)
        self.last_known_flows = {}
        self.fallback_enabled = config.get('etf_fallback_enabled', True)
        self.composite_cache = None
        self.cache_timestamp = None

        # Cache settings - reduce for more frequent fresh data
        self.cache_duration_minutes = config.get('etf_cache_duration', 30)  # 30min cache for premium
        
        # Rest of existing code...
        logger.info(f"🚀 PREMIUM ETF Flow Manager: {len(self.etf_symbols)} ETFs")
        logger.info(f"   Limits: {self.daily_call_limit} daily, {self.minute_call_limit}/min")
        logger.info(f"   Cache: {self.cache_duration_minutes}min duration")
    def _reset_counters_if_needed(self):
        """Reset API call counters for premium limits"""
        today = datetime.now().date()
        current_minute = datetime.now().replace(second=0, microsecond=0)
        
        # Reset daily counter
        if today > self.last_reset_date:
            self.calls_made_today = 0
            self.last_reset_date = today
            logger.info(f"🔄 Reset daily API counter for {today}")
        
        # Reset minute counter for premium rate limiting
        if current_minute > self.last_minute_reset:
            if self.calls_this_minute > 0:  # Only log if there were calls
                logger.debug(f"🔄 Minute reset: made {self.calls_this_minute}/{self.minute_call_limit} calls")
            self.calls_this_minute = 0
            self.last_minute_reset = current_minute

    def _check_premium_rate_limits(self) -> bool:
        """Check premium API rate limits"""
        self._reset_counters_if_needed()
        
        # Check daily limit (should never hit with premium)
        if self.calls_made_today >= self.daily_call_limit:
            logger.warning(f"⚠ Daily API limit reached ({self.daily_call_limit})")
            return False
        
        # Check minute limit (premium: 75/minute)
        if self.calls_this_minute >= self.minute_call_limit:
            logger.warning(f"⚠ Minute API limit reached ({self.minute_call_limit}/min)")
            return False
        
        return True
    def _reset_daily_counter_if_needed(self):
        """Reset API call counter if new day"""
        today = datetime.now().date()
        if today > self.last_reset_date:
            self.calls_made_today = 0
            self.last_reset_date = today
            logger.info(f"🔄 Reset daily API counter for {today}")
    
    def _is_cache_valid(self) -> bool:
        """Check if cached data is still valid"""
        if self.cache_timestamp is None:
            return False
        
        cache_age = datetime.now() - self.cache_timestamp
        return cache_age.total_seconds() < (self.cache_duration_minutes * 60)
    
    async def _fetch_etf_quote(self, session: aiohttp.ClientSession, symbol: str) -> Optional[ETFFlowData]:
        """Fetch single ETF quote from AlphaVantage - PRODUCTION VERSION"""
        
        # Check premium rate limits
        if not self._check_premium_rate_limits():
            return self._get_fallback_data(symbol)
        
        url = "https://www.alphavantage.co/query"
        params = {
            'function': 'GLOBAL_QUOTE',
            'symbol': symbol,
            'apikey': self.api_key
        }
        
        try:
            async with session.get(url, params=params, timeout=30) as response:  # Reduced timeout
                # Update premium counters
                self.calls_made_today += 1
                self.calls_this_minute += 1
                
                # Minimal logging for production
                logger.debug(f"📈 API call {symbol} (today: {self.calls_made_today}, "
                            f"minute: {self.calls_this_minute}/{self.minute_call_limit})")
                
                if response.status != 200:
                    logger.error(f"❌ API error for {symbol}: HTTP {response.status}")
                    return self._get_fallback_data(symbol)
                
                data = await response.json()
                
                # Check for API errors (no debug prints)
                if 'Error Message' in data:
                    logger.error(f"❌ API Error for {symbol}: {data['Error Message']}")
                    return self._get_fallback_data(symbol)
                
                if 'Note' in data or 'Information' in data:
                    logger.warning(f"⚠ API limit/info for {symbol}")
                    return self._get_fallback_data(symbol)
                
                # Parse successful response
                quote = data.get('Global Quote', {})
                if not quote:
                    logger.error(f"❌ No Global Quote for {symbol}")
                    return self._get_fallback_data(symbol)
    
                # Extract data (same as before)
                price = float(quote.get('05. price', 0))
                volume = int(float(quote.get('06. volume', 0)))
                change_percent = float(quote.get('10. change percent', '0%').replace('%', ''))
                
                # Calculate flow proxy
                flow_proxy = volume * (change_percent / 100) * price
                
                etf_data = ETFFlowData(
                    symbol=symbol,
                    price=price,
                    volume=volume,
                    flow_proxy=flow_proxy,
                    change_5d=change_percent,
                    timestamp=datetime.now(),
                    confidence=1.0
                )
                
                # Cache successful fetch
                self.last_known_flows[symbol] = etf_data
                
                logger.debug(f"✅ {symbol}: ${price:.2f} vol={volume:,} flow={flow_proxy:,.0f}")
                return etf_data
                    
        except Exception as e:
            logger.error(f"❌ Failed to fetch {symbol}: {e}")
            return self._get_fallback_data(symbol)
    def _get_fallback_data(self, symbol: str) -> Optional[ETFFlowData]:
        """Get fallback data when API fails"""
        if not self.fallback_enabled:
            return None
        
        # Use last known data with reduced confidence
        if symbol in self.last_known_flows:
            fallback = self.last_known_flows[symbol]
            fallback.confidence = 0.3  # Low confidence for stale data
            fallback.timestamp = datetime.now()
            
            logger.warning(f"🔄 Using fallback data for {symbol} (confidence: 30%)")
            return fallback
        
        # Default fallback if no historical data
        logger.warning(f"🔄 Using default fallback for {symbol}")
        return ETFFlowData(
            symbol=symbol,
            price=50.0,  # Default price
            volume=1_000_000,  # Default volume
            flow_proxy=0.0,  # Neutral flow
            change_5d=0.0,
            timestamp=datetime.now(),
            confidence=0.1  # Very low confidence
        )
    
    async def fetch_all_etf_data(self) -> Dict[str, ETFFlowData]:
        """Fetch data for all ETFs concurrently"""
        
        # Return cached data if valid
        if self._is_cache_valid() and self.etf_cache:
            logger.debug("📦 Using cached ETF data")
            return self.etf_cache
        
        logger.info(f"🔄 Fetching fresh ETF data for {len(self.etf_symbols)} symbols...")
        
        async with aiohttp.ClientSession() as session:
            tasks = [
                self._fetch_etf_quote(session, symbol) 
                for symbol in self.etf_symbols
            ]
            
            results = await asyncio.gather(*tasks, return_exceptions=True)
        
        # Process results
        etf_data = {}
        successful_fetches = 0
        
        for i, result in enumerate(results):
            symbol = self.etf_symbols[i]
            
            if isinstance(result, Exception):
                logger.error(f"❌ Exception fetching {symbol}: {result}")
                fallback = self._get_fallback_data(symbol)
                if fallback:
                    etf_data[symbol] = fallback
            elif result is not None:
                etf_data[symbol] = result
                if result.confidence > 0.5:  # Count as successful if high confidence
                    successful_fetches += 1
        
        # Update cache
        self.etf_cache = etf_data
        self.cache_timestamp = datetime.now()
        
        success_rate = successful_fetches / len(self.etf_symbols) * 100
        logger.info(f"✅ ETF data fetch complete: {successful_fetches}/{len(self.etf_symbols)} successful ({success_rate:.1f}%)")
        
        return etf_data
    
    def calculate_composite_flow(self, etf_data: Dict[str, ETFFlowData]) -> CompositeETFFlow:
        """Calculate composite institutional flow from all ETFs"""
        
        if not etf_data:
            logger.warning("⚠️ No ETF data available for composite flow calculation")
            return self._get_neutral_composite_flow()
        
        # Extract flows and volumes
        flows = []
        volumes = []
        symbols = []
        confidences = []
        
        for symbol, data in etf_data.items():
            flows.append(data.flow_proxy)
            volumes.append(data.volume)
            symbols.append(symbol)
            confidences.append(data.confidence)
        
        flows = np.array(flows)
        volumes = np.array(volumes)
        confidences = np.array(confidences)
        
        # Calculate total flow
        total_flow = np.sum(flows)
        
        # Calculate volume-weighted flow
        total_volume = np.sum(volumes)
        if total_volume > 0:
            volume_weighted_flow = np.sum(flows * volumes) / total_volume
        else:
            volume_weighted_flow = 0.0
        
        # Normalize flow strength to [-1, 1]
        # Use adaptive normalization based on typical ETF flow ranges
        max_expected_flow = 100_000_000  # $100M as reference
        flow_strength = np.tanh(total_flow / max_expected_flow)
        
        # Determine institutional sentiment
        if flow_strength > 0.2:
            sentiment = 'bullish'
        elif flow_strength < -0.3:
            sentiment = 'bearish'
        else:
            sentiment = 'neutral'
        
        # Find dominant ETF (highest absolute flow)
        dominant_idx = np.argmax(np.abs(flows))
        dominant_etf = symbols[dominant_idx]
        
        # Calculate overall confidence (weighted by volume)
        if total_volume > 0:
            overall_confidence = np.sum(confidences * volumes) / total_volume
        else:
            overall_confidence = np.mean(confidences)
        
        composite = CompositeETFFlow(
            total_flow=total_flow,
            flow_strength=flow_strength,
            institutional_sentiment=sentiment,
            volume_weighted_flow=volume_weighted_flow,
            dominant_etf=dominant_etf,
            confidence=overall_confidence,
            timestamp=datetime.now()
        )
        
        logger.debug(f"💰 Composite flow: {total_flow:,.0f} (strength: {flow_strength:.3f}, sentiment: {sentiment})")
        
        return composite
    
    def _get_neutral_composite_flow(self) -> CompositeETFFlow:
        """Return neutral composite flow when no data available"""
        return CompositeETFFlow(
            total_flow=0.0,
            flow_strength=0.0,
            institutional_sentiment='neutral',
            volume_weighted_flow=0.0,
            dominant_etf='IBIT',  # Default to largest ETF
            confidence=0.1,
            timestamp=datetime.now()
        )
    
    async def get_composite_etf_flow(self) -> CompositeETFFlow:
        """Get current composite ETF flow (main public method)"""
        
        # Return cached composite if valid
        if (self.composite_cache is not None and 
            self._is_cache_valid()):
            return self.composite_cache
        
        # Fetch fresh data
        etf_data = await self.fetch_all_etf_data()
        composite = self.calculate_composite_flow(etf_data)
        
        # Cache the result
        self.composite_cache = composite
        
        return composite
    
    def get_etf_flow_for_e8_mapping(self, composite_flow: CompositeETFFlow) -> float:
        """Convert composite flow to value suitable for E8 mapping"""
        
        # Use flow_strength as it's already normalized to [-1, 1]
        # Apply confidence weighting
        etf_flow_value = composite_flow.flow_strength * composite_flow.confidence
        
        # Ensure bounded to [-1, 1] for E8 mapping
        return np.clip(etf_flow_value, -1.0, 1.0)
    
    def get_flow_regime_weights(self, composite_flow: CompositeETFFlow) -> Dict[str, float]:
        """Get strategy weights based on ETF flow patterns"""
        
        flow_strength = abs(composite_flow.flow_strength)
        confidence = composite_flow.confidence
        sentiment = composite_flow.institutional_sentiment
        
        # Base weights
        weights = {
            'mean_reversion_bonus': 1.0,
            'trend_following_bonus': 1.0,
            'breakout_bonus': 1.0,
            'risk_off_sensitivity': 1.0
        }
        
        # Adjust based on institutional flow
        if sentiment == 'bullish' and confidence > 0.6:
            # Institutions accumulating - favor trend following
            weights['trend_following_bonus'] = 1.0 + flow_strength
            weights['breakout_bonus'] = 1.0 + (flow_strength * 0.5)
            weights['mean_reversion_bonus'] = 1.0 - (flow_strength * 0.3)
            
        elif sentiment == 'bearish' and confidence > 0.6:
            # Institutions distributing - favor mean reversion
            weights['mean_reversion_bonus'] = 1.0 + flow_strength
            weights['risk_off_sensitivity'] = 1.0 + (flow_strength * 0.5)
            weights['trend_following_bonus'] = 1.0 - (flow_strength * 0.4)
            
        else:
            # Neutral or low confidence - balanced approach
            adjustment = flow_strength * confidence * 0.2
            weights['mean_reversion_bonus'] = 1.0 + adjustment
            weights['trend_following_bonus'] = 1.0 + adjustment
        
        logger.debug(f"🎯 Flow regime weights: {sentiment} (strength: {flow_strength:.2f}) -> {weights}")
        
        return weights
    
    def get_diagnostic_info(self) -> Dict:
        """Get diagnostic information about ETF flow system"""
        return {
            'api_calls_today': self.calls_made_today,
            'api_limit': self.daily_call_limit,
            'cache_valid': self._is_cache_valid(),
            'cache_age_minutes': (
                (datetime.now() - self.cache_timestamp).total_seconds() / 60 
                if self.cache_timestamp else None
            ),
            'etf_symbols': self.etf_symbols,
            'fallback_enabled': self.fallback_enabled,
            'last_composite_flow': (
                {
                    'flow_strength': self.composite_cache.flow_strength,
                    'sentiment': self.composite_cache.institutional_sentiment,
                    'confidence': self.composite_cache.confidence
                } if self.composite_cache else None
            )
        }

# ================================================================
# Usage Example
# ================================================================

async def test_etf_flow_manager():
    """Test the ETF flow manager"""
    
    config = {
        'alphavantage_api_key': 'your_api_key_here',
        'etf_symbols': ['IBIT', 'FBTC', 'BITB', 'ARKB', 'BTCO'],
        'etf_cache_duration': 15,
        'etf_fallback_enabled': True
    }
    
    manager = ETFFlowManager(config)
    
    # Get composite flow
    composite = await manager.get_composite_etf_flow()
    
    print(f"Total Flow: ${composite.total_flow:,.0f}")
    print(f"Flow Strength: {composite.flow_strength:.3f}")
    print(f"Sentiment: {composite.institutional_sentiment}")
    print(f"Confidence: {composite.confidence:.2f}")
    print(f"Dominant ETF: {composite.dominant_etf}")
    
    # Get E8 mapping value
    etf_value = manager.get_etf_flow_for_e8_mapping(composite)
    print(f"E8 ETF Value: {etf_value:.3f}")
    
    # Get strategy weights
    weights = manager.get_flow_regime_weights(composite)
    print(f"Strategy Weights: {weights}")


if __name__ == "__main__":
    asyncio.run(test_etf_flow_manager())