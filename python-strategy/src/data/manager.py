# src/data/manager.py - UPDATED for multi-timeframe
import asyncio
import logging
from datetime import datetime, timedelta
from typing import List, Optional, Dict
import pandas as pd
import asyncpg
import ccxt.async_support as ccxt
import os
from .rate_limiter import AdaptiveRateLimiter

logger = logging.getLogger(__name__)

class DataIngestionManager:
    """CCXT-based data ingestion with multi-timeframe support"""
    
    def __init__(self, config: dict):
        self.config = config
        self.symbols = config.get('symbols',{})
        # if symbols is empty log error and exit
        if not self.symbols:
            logger.error("No symbols provided in configuration")
            raise ValueError("No symbols provided in configuration")
        # UPDATED: Support both 4h and 1d timeframes
        self.timeframes = config.get('timeframes', ['4h', '1d'])
        # Log all symbols
        for symbol in self.symbols:
            logger.info(f"Monitoring symbol: {symbol}")
        # Database config
        db_config = config.get('database', {})
        self.db_url = db_config.get('url', 'postgresql://trading:Pass_1234@localhost:5432/trading_db')
        
        # Initialize CCXT exchange
        binance_config = config.get('binance', {})
        self.exchange = ccxt.binance({
            'apiKey': binance_config.get('api_key'),
            'secret': binance_config.get('api_secret'),
            'sandbox': binance_config.get('testnet', False),
            'enableRateLimit': True,
            'rateLimit': 50,
        })
        
        self.db_pool = None
        # Initialize rate limiter
        self.rate_limiter = AdaptiveRateLimiter(
            initial_delay=config.get('rate_limit_delay', 0.5),
            max_delay=config.get('max_rate_delay', 5.0)
        )
        self.max_concurrent = config.get('max_concurrent_symbols', 3)
    async def initialize(self):
        """Initialize exchange and database"""
        try:
            # Load exchange markets
            await self.exchange.load_markets()
            logger.info(f"Loaded {len(self.exchange.markets)} markets from Binance")
            
            # Database pool
            self.db_pool = await asyncpg.create_pool(
                self.db_url,
                min_size=2,
                max_size=5,
                command_timeout=60
            )
            await self._create_tables()
            
            logger.info("Data ingestion initialized with CCXT")
            
        except Exception as e:
            logger.error(f"Failed to initialize: {e}")
            raise
    
    async def _create_tables(self):
        """Create database tables"""
        async with self.db_pool.acquire() as conn:
            await conn.execute("""
                CREATE TABLE IF NOT EXISTS ohlcv_data (
                    symbol VARCHAR(20) NOT NULL,
                    timeframe VARCHAR(10) NOT NULL,
                    timestamp BIGINT NOT NULL,
                    open DECIMAL(30,12) NOT NULL,
                    high DECIMAL(30,12) NOT NULL,
                    low DECIMAL(30,12) NOT NULL,
                    close DECIMAL(30,12) NOT NULL,
                    volume DECIMAL(30,12) NOT NULL,
                    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                    PRIMARY KEY (symbol, timeframe, timestamp)
                )
            """)
            
            await conn.execute("""
                CREATE INDEX IF NOT EXISTS idx_ohlcv_timestamp 
                ON ohlcv_data(timestamp DESC)
            """)
            
            await conn.execute("""
                CREATE INDEX IF NOT EXISTS idx_ohlcv_symbol_timeframe 
                ON ohlcv_data(symbol, timeframe, timestamp DESC)
            """)
    
    def _symbol_to_db_format(self, symbol: str) -> str:
        """Convert CCXT symbol (BTC/USDT) to database format (BTCUSDT)"""
        return symbol.replace('/', '')
    
    def _symbol_to_ccxt_format(self, symbol: str) -> str:
        """Convert database symbol (BTCUSDT) to CCXT format (BTC/USDT)"""
        if '/' not in symbol and symbol.endswith('USDT'):
            base = symbol[:-4]
            return f"{base}/USDT"
        return symbol
    
    
    
    async def save_ohlcv_data(self, ohlcv_data: List[List], symbol: str, timeframe: str) -> int:
        """Save OHLCV data to database"""
        if not ohlcv_data:
            return 0
        
        db_symbol = self._symbol_to_db_format(symbol)
        saved_count = 0
        
        async with self.db_pool.acquire() as conn:
            for candle in ohlcv_data:
                try:
                    timestamp, open_price, high, low, close, volume = candle
                    
                    # Basic validation
                    if not all(x is not None and x >= 0 for x in [open_price, high, low, close, volume]):
                        continue
                    
                    if not (low <= open_price <= high and low <= close <= high):
                        continue
                    
                    await conn.execute("""
                        INSERT INTO ohlcv_data (symbol, timeframe, timestamp, open, high, low, close, volume)
                        VALUES ($1, $2, $3, $4, $5, $6, $7, $8)
                        ON CONFLICT (symbol, timeframe, timestamp) DO NOTHING
                    """, db_symbol, timeframe, int(timestamp), float(open_price), 
                        float(high), float(low), float(close), float(volume))
                    
                    saved_count += 1
                    
                except Exception as e:
                    logger.warning(f"Failed to save candle for {symbol}: {e}")
                    continue
        
        return saved_count
    
    async def get_latest_timestamp(self, symbol: str, timeframe: str) -> Optional[int]:
        """Get latest timestamp for symbol"""
        db_symbol = self._symbol_to_db_format(symbol)
        
        async with self.db_pool.acquire() as conn:
            result = await conn.fetchval("""
                SELECT MAX(timestamp) FROM ohlcv_data 
                WHERE symbol = $1 AND timeframe = $2
            """, db_symbol, timeframe)
            return result
    
    async def fetch_latest_data_optimized(self, symbols: List[str] = None) -> Dict[str, int]:
        """Optimized fetch for multiple symbols"""
        symbols = symbols or self.symbols
        symbols = self._prioritize_symbols(symbols)
        
        logger.info(f"Starting optimized fetch for {len(symbols)} symbols")
        
        # Collect all tasks
        fetch_tasks = []
        batch_data = {}
        
        for symbol in symbols:
            ccxt_symbol = self._symbol_to_ccxt_format(symbol) if '/' not in symbol else symbol
            batch_data[ccxt_symbol] = {}
            
            for timeframe in self.timeframes:
                # Check if update needed
                latest_ts = await self.get_latest_timestamp(ccxt_symbol, timeframe)
                since, limit = self._calculate_fetch_params(latest_ts, timeframe)
                
                if limit > 0:  # Only fetch if needed
                    task = self._create_fetch_task(ccxt_symbol, timeframe, since, limit)
                    fetch_tasks.append(task)
        
        # Execute tasks with controlled concurrency
        logger.info(f"Executing {len(fetch_tasks)} fetch tasks")
        results = await asyncio.gather(*fetch_tasks, return_exceptions=True)
        
        # Process results and build batch data
        task_index = 0
        for symbol in symbols:
            ccxt_symbol = self._symbol_to_ccxt_format(symbol) if '/' not in symbol else symbol
            for timeframe in self.timeframes:
                if task_index < len(results):
                    result = results[task_index]
                    if not isinstance(result, Exception) and result:
                        batch_data[ccxt_symbol][timeframe] = result
                    task_index += 1
        
        # Batch save all data
        if any(batch_data.values()):
            save_results = await self.save_ohlcv_batch(batch_data)
            logger.info(f"Batch operation completed: {save_results}")
            return save_results
        else:
            logger.warning("No data to save from batch operation")
            return {}

    async def _create_fetch_task(self, symbol: str, timeframe: str, since: int, limit: int):
        """Create individual fetch task"""
        return await self.fetch_symbol_data_safe(symbol, timeframe, since, limit)

    def _calculate_fetch_params(self, latest_ts: Optional[int], timeframe: str) -> tuple:
        """Calculate since and limit parameters"""
        if latest_ts:
            # Incremental update
            since = latest_ts + 1
            limit = 200  # Small limit for updates
            
            # Check staleness
            hours_old = (datetime.now().timestamp() * 1000 - latest_ts) / (1000 * 3600)
            timeframe_hours = {'4h': 4, '1d': 24}
            required_hours = timeframe_hours.get(timeframe, 24)
            
            if hours_old < required_hours:
                return since, 0  # No update needed
                
            return since, limit
        else:
            # Initial fetch - get enough for indicators
            days_back = 60  # 2 months for daily strategies
            since = int((datetime.now() - timedelta(days=days_back)).timestamp() * 1000)
            return since, 500
    async def _fetch_ohlcv_with_retry(self, symbol: str, timeframe: str, 
                                    since: int = None, limit: int = 1000) -> List[List]:
        """Fetch OHLCV with retry logic using CCXT standard methods"""
        max_retries = 3
        retry_delay = 1
        
        for attempt in range(max_retries):
            try:
                # Use standard CCXT fetch_ohlcv method
                params = {
                    'symbol': symbol,
                    'timeframe': timeframe,
                    'limit': min(limit, 1000)  # Respect exchange limits
                }
                
                if since:
                    params['since'] = since
                
                ohlcv = await self.exchange.fetch_ohlcv(**params)
                
                if ohlcv:
                    logger.debug(f"Fetched {len(ohlcv)} {timeframe} bars for {symbol}")
                    return ohlcv
                else:
                    logger.warning(f"Empty response for {symbol} {timeframe}")
                    return []
                    
            except ccxt.RateLimitExceeded as e:
                wait_time = retry_delay * (2 ** attempt)
                logger.warning(f"Rate limit hit for {symbol}, waiting {wait_time}s")
                await asyncio.sleep(wait_time)
                
            except ccxt.NetworkError as e:
                if attempt < max_retries - 1:
                    wait_time = retry_delay * (2 ** attempt)
                    logger.warning(f"Network error for {symbol}, retrying in {wait_time}s: {e}")
                    await asyncio.sleep(wait_time)
                else:
                    logger.error(f"Network error for {symbol} after {max_retries} attempts: {e}")
                    return []
                    
            except ccxt.ExchangeError as e:
                logger.error(f"Exchange error for {symbol}: {e}")
                return []
                
            except Exception as e:
                logger.error(f"Unexpected error fetching {symbol}: {e}")
                return []
        
        logger.error(f"Failed to fetch {symbol} after {max_retries} attempts")
        return []

    async def _verify_data_sufficiency(self, symbol: str, timeframe: str):
        """Verify we have sufficient data for strategy calculations"""
        try:
            df = self.get_symbol_data(symbol, days=100, timeframe=timeframe)
            record_count = len(df)
            
            # Define minimum requirements
            min_requirements = {
                '4h': 60,   # ~10 days for EMA26 + buffer
                '1d': 60,   # ~2 months for daily strategies
                '1h': 120   # ~5 days for hourly
            }
            
            min_needed = min_requirements.get(timeframe, 50)
            
            if record_count >= min_needed:
                logger.info(f"✅ {symbol} {timeframe}: {record_count} records (sufficient)")
            elif record_count >= min_needed * 0.7:  # 70% of minimum
                logger.warning(f"⚠️ {symbol} {timeframe}: {record_count} records (marginal)")
            else:
                logger.error(f"❌ {symbol} {timeframe}: {record_count} records (insufficient)")
                
                # Try to fetch more data if critically low
                if record_count < min_needed * 0.5:
                    await self._fetch_more_historical_data(symbol, timeframe, min_needed)
                    
        except Exception as e:
            logger.error(f"Failed to verify data sufficiency for {symbol}: {e}")

    async def _fetch_more_historical_data(self, symbol: str, timeframe: str, min_needed: int):
        """Fetch more historical data when insufficient"""
        try:
            logger.info(f"Fetching additional historical data for {symbol} {timeframe}")
            
            # Calculate how far back to go
            timeframe_hours = {'1h': 1, '4h': 4, '1d': 24}
            hours_per_bar = timeframe_hours.get(timeframe, 24)
            days_back = int((min_needed * hours_per_bar) / 24) + 10  # Extra buffer
            
            since = int((datetime.now() - timedelta(days=days_back)).timestamp() * 1000)
            
            # Fetch in chunks to avoid hitting limits
            chunk_size = 500
            all_data = []
            
            current_since = since
            while len(all_data) < min_needed and current_since < int(datetime.now().timestamp() * 1000):
                chunk = await self._fetch_ohlcv_with_retry(symbol, timeframe, current_since, chunk_size)
                
                if not chunk:
                    break
                    
                all_data.extend(chunk)
                
                # Update since to last timestamp + 1
                current_since = chunk[-1][0] + 1
                
                # Small delay to respect rate limits
                await asyncio.sleep(0.1)
            
            if all_data:
                saved_count = await self.save_ohlcv_data(all_data, symbol, timeframe)
                logger.info(f"Saved additional {saved_count} records for {symbol} {timeframe}")
            
        except Exception as e:
            logger.error(f"Failed to fetch additional data for {symbol}: {e}")

    # Legacy method for backward compatibility
    async def fetch_ohlcv(self, symbol: str, timeframe: str = '1d', 
                        since: int = None, limit: int = 1000) -> List[List]:
        """Legacy method - use _fetch_ohlcv_with_retry instead"""
        return await self._fetch_ohlcv_with_retry(symbol, timeframe, since, limit)
        
    def get_symbol_data(self, symbol: str, days: int = 100, 
                       timeframe: str = '1d', end_date: Optional[datetime] = None) -> pd.DataFrame:
        """Get historical data as DataFrame"""
        db_symbol = self._symbol_to_db_format(symbol)
        end_date = end_date or datetime.now()
        start_date = end_date - timedelta(days=days)
        
        start_ts = int(start_date.timestamp() * 1000)
        end_ts = int(end_date.timestamp() * 1000)
        
        try:
            import psycopg2
            conn = psycopg2.connect(self.db_url)
            
            query = """
                SELECT timestamp, open, high, low, close, volume
                FROM ohlcv_data
                WHERE symbol = %s AND timeframe = %s 
                AND timestamp >= %s AND timestamp <= %s
                ORDER BY timestamp
            """
            
            df = pd.read_sql_query(query, conn, params=(db_symbol, timeframe, start_ts, end_ts))
            conn.close()
            
            if not df.empty:
                df['timestamp'] = pd.to_datetime(df['timestamp'], unit='ms')
                df.set_index('timestamp', inplace=True)
                
                for col in ['open', 'high', 'low', 'close', 'volume']:
                    df[col] = pd.to_numeric(df[col], errors='coerce')
                
                df.dropna(inplace=True)
            
            return df
            
        except Exception as e:
            logger.error(f"Error querying data for {symbol}: {e}")
            return pd.DataFrame()
    
    # NEW METHOD: Aggregate 4h to 1d
    def aggregate_4h_to_1d(self, h4_data: pd.DataFrame) -> pd.DataFrame:
        """Aggregate 4h candles to 1d candles"""
        if h4_data.empty:
            return pd.DataFrame()
        
        try:
            # Group by date and aggregate
            daily_data = h4_data.groupby(h4_data.index.date).agg({
                'open': 'first',    # First open of the day
                'high': 'max',      # Highest high
                'low': 'min',       # Lowest low  
                'close': 'last',    # Last close of the day
                'volume': 'sum'     # Sum of volumes
            })
            
            # Convert date index back to datetime
            daily_data.index = pd.to_datetime(daily_data.index)
            
            return daily_data
            
        except Exception as e:
            logger.error(f"Error aggregating 4h to 1d: {e}")
            return pd.DataFrame()
    
    # NEW METHOD: Get both timeframes
    def get_multi_timeframe_data(self, symbol: str, days: int = 100, 
                                end_date: Optional[datetime] = None) -> Dict[str, pd.DataFrame]:
        """Get both 4h and 1d data for a symbol"""
        result = {}
        
        # Get 4h data (need more days since 6 bars per day)
        h4_data = self.get_symbol_data(symbol, days=days, timeframe='4h', end_date=end_date)
        result['4h'] = h4_data
        
        # Get 1d data
        d1_data = self.get_symbol_data(symbol, days=days, timeframe='1d', end_date=end_date)
        
        # If we have 4h data but no 1d data, aggregate 4h to 1d
        if not h4_data.empty and d1_data.empty:
            logger.info(f"No 1d data for {symbol}, aggregating from 4h")
            d1_data = self.aggregate_4h_to_1d(h4_data)
        
        # result['1d'] = d1_data
        result['1d'] = d1_data

        return result
    
    async def get_market_summary(self) -> Dict[str, Dict]:
        """Get market summary using CCXT"""
        summary = {}
        
        try:
            symbols_to_fetch = [s for s in self.symbols if s in self.exchange.markets]
            if not symbols_to_fetch:
                return summary
                
            tickers = await self.exchange.fetch_tickers(symbols_to_fetch)
            
            for symbol, ticker in tickers.items():
                if symbol in self.symbols:
                    summary[symbol] = {
                        'price': ticker['last'] or 0,
                        'change_24h': ticker['percentage'] or 0,
                        'volume': ticker['baseVolume'] or 0,
                        'high_24h': ticker['high'] or 0,
                        'low_24h': ticker['low'] or 0,
                        'last_update': ticker['datetime'] or datetime.now().isoformat()
                    }
                    
        except Exception as e:
            logger.error(f"Failed to get market summary: {e}")
        
        return summary
    
    async def get_exchange_info(self) -> Dict:
        """Get exchange information and trading rules"""
        try:
            markets = self.exchange.markets
            return {
                'total_markets': len(markets),
                'active_markets': len([m for m in markets.values() if m['active']]),
                'base_currencies': list(set(m['base'] for m in markets.values())),
                'quote_currencies': list(set(m['quote'] for m in markets.values())),
                'exchange_status': 'online' if self.exchange.has['fetchStatus'] else 'unknown'
            }
        except Exception as e:
            logger.error(f"Failed to get exchange info: {e}")
            return {}
    
    async def test_connection(self) -> bool:
        """Test exchange connection"""
        try:
            await self.exchange.fetch_status()
            return True
        except Exception as e:
            logger.error(f"Connection test failed: {e}")
            return False
    
    async def close(self):
        """Clean up resources"""
        if self.exchange:
            await self.exchange.close()
        if self.db_pool:
            await self.db_pool.close()
    # function to collect historical data
    # async def collect_historical_data(self, symbol: str, timeframe: str, since: datetime, limit: int) -> pd.DataFrame:
    #     """Collect historical data for a specific symbol and timeframe"""
    #     try:
    #         data = await self.exchange.fetch_ohlcv(symbol, timeframe, since=since, limit=limit)
    #         df = pd.DataFrame(data, columns=['timestamp', 'open', 'high', 'low', 'close', 'volume'])
    #         df['timestamp'] = pd.to_datetime(df['timestamp'], unit='ms')
    #         df.set_index('timestamp', inplace=True)
    #         return df
    #     except Exception as e:
    #         logger.error(f"Failed to collect historical data: {e}")
    #         return pd.DataFrame()

    async def collect_historical_data(self, symbols: List[str] = None, 
                                    timeframes: List[str] = None,
                                    days_back: int = 365,
                                    force_refresh: bool = False) -> Dict[str, Dict[str, int]]:
        """
        Comprehensive historical data collection for multiple symbols and timeframes
        
        Args:
            symbols: List of symbols to collect (default: self.symbols)
            timeframes: List of timeframes to collect (default: self.timeframes)
            days_back: How many days of history to collect
            force_refresh: Force re-download even if data exists
        
        Returns:
            Dict with collection results per symbol/timeframe
        """
        symbols = symbols or self.symbols
        timeframes = timeframes or self.timeframes
        
        logger.info(f"🚀 Starting historical data collection:")
        logger.info(f"   Symbols: {symbols}")
        logger.info(f"   Timeframes: {timeframes}")
        logger.info(f"   Days back: {days_back}")
        logger.info(f"   Force refresh: {force_refresh}")
        
        results = {}
        total_tasks = len(symbols) * len(timeframes)
        completed_tasks = 0
        
        for symbol in symbols:
            ccxt_symbol = self._symbol_to_ccxt_format(symbol) if '/' not in symbol else symbol
            results[ccxt_symbol] = {}
            
            for timeframe in timeframes:
                try:
                    logger.info(f"📈 Processing {ccxt_symbol} {timeframe} "
                               f"({completed_tasks + 1}/{total_tasks})")
                    
                    # Check existing data unless force refresh
                    if not force_refresh:
                        existing_count = await self._get_data_count(ccxt_symbol, timeframe)
                        if existing_count > 0:
                            logger.info(f"   ✅ Found {existing_count} existing records")
                            
                            # Check if we have enough recent data
                            latest_ts = await self.get_latest_timestamp(ccxt_symbol, timeframe)
                            if latest_ts:
                                hours_old = (datetime.now().timestamp() * 1000 - latest_ts) / (1000 * 3600)
                                
                                timeframe_hours = {'1h': 1, '4h': 4, '1d': 24}
                                max_age_hours = timeframe_hours.get(timeframe, 24) * 2
                                
                                if hours_old < max_age_hours and existing_count >= 100:
                                    logger.info(f"   ⏭️ Skipping {ccxt_symbol} {timeframe} (sufficient recent data)")
                                    results[ccxt_symbol][timeframe] = 0
                                    completed_tasks += 1
                                    continue
                    
                    # Calculate date range
                    end_date = datetime.now()
                    start_date = end_date - timedelta(days=days_back)
                    since = int(start_date.timestamp() * 1000)
                    
                    # Collect data in chunks
                    collected_count = await self._collect_historical_chunks(
                        ccxt_symbol, timeframe, since, days_back
                    )
                    
                    results[ccxt_symbol][timeframe] = collected_count
                    
                    if collected_count > 0:
                        logger.info(f"   ✅ Collected {collected_count} records for "
                                   f"{ccxt_symbol} {timeframe}")
                    else:
                        logger.warning(f"   ⚠️ No data collected for {ccxt_symbol} {timeframe}")
                    
                    # Validate data quality
                    await self._validate_data_quality(ccxt_symbol, timeframe)
                    
                    completed_tasks += 1
                    
                    # Rate limiting pause
                    await asyncio.sleep(0.1)
                    
                except Exception as e:
                    logger.error(f"❌ Failed to collect {ccxt_symbol} {timeframe}: {e}")
                    results[ccxt_symbol][timeframe] = -1
                    completed_tasks += 1
                    continue
        
        # Summary
        total_collected = sum(
            count for symbol_results in results.values() 
            for count in symbol_results.values() if count > 0
        )
        
        logger.info(f"🎉 Historical data collection complete:")
        logger.info(f"   Total records collected: {total_collected}")
        logger.info(f"   Success rate: {(completed_tasks - len([c for r in results.values() for c in r.values() if c == -1])) / completed_tasks * 100:.1f}%")
        
        return results
    
    async def _collect_historical_chunks(self, symbol: str, timeframe: str, 
                                       since: int, days_back: int) -> int:
        """Collect historical data in manageable chunks"""
        chunk_size = 1000  # Binance limit
        total_collected = 0
        current_since = since
        end_timestamp = int(datetime.now().timestamp() * 1000)
        
        # Calculate expected number of bars
        timeframe_ms = self._timeframe_to_ms(timeframe)
        expected_bars = int((end_timestamp - since) / timeframe_ms)
        
        logger.debug(f"   Expecting ~{expected_bars} bars for {symbol} {timeframe}")
        
        while current_since < end_timestamp and total_collected < expected_bars * 1.2:
            try:
                # Fetch chunk
                chunk = await self._fetch_ohlcv_with_retry(
                    symbol, timeframe, current_since, chunk_size
                )
                
                if not chunk:
                    logger.debug(f"   No more data available for {symbol} {timeframe}")
                    break
                
                # Save chunk
                saved_count = await self.save_ohlcv_data(chunk, symbol, timeframe)
                total_collected += saved_count
                
                # Update progress
                progress = min(100, (total_collected / max(expected_bars, 1)) * 100)
                if total_collected % 500 == 0:  # Log every 500 records
                    logger.debug(f"   Progress: {total_collected} records ({progress:.1f}%)")
                
                # Move to next chunk
                if len(chunk) < chunk_size:
                    # Last chunk received
                    break
                else:
                    current_since = chunk[-1][0] + self._timeframe_to_ms(timeframe)
                
                # Rate limiting
                await asyncio.sleep(0.1)
                
            except Exception as e:
                logger.warning(f"   Error in chunk collection for {symbol}: {e}")
                break
        
        return total_collected
    
    def _timeframe_to_ms(self, timeframe: str) -> int:
        """Convert timeframe to milliseconds"""
        multipliers = {
            '1m': 60 * 1000,
            '5m': 5 * 60 * 1000,
            '15m': 15 * 60 * 1000,
            '1h': 60 * 60 * 1000,
            '4h': 4 * 60 * 60 * 1000,
            '1d': 24 * 60 * 60 * 1000,
            '1w': 7 * 24 * 60 * 60 * 1000
        }
        return multipliers.get(timeframe, 24 * 60 * 60 * 1000)
    
    async def _get_data_count(self, symbol: str, timeframe: str) -> int:
        """Get count of existing records"""
        db_symbol = self._symbol_to_db_format(symbol)
        
        async with self.db_pool.acquire() as conn:
            count = await conn.fetchval("""
                SELECT COUNT(*) FROM ohlcv_data 
                WHERE symbol = $1 AND timeframe = $2
            """, db_symbol, timeframe)
            return count or 0
    
    async def _validate_data_quality(self, symbol: str, timeframe: str):
        """Validate data quality and identify gaps"""
        try:
            # Get recent data sample
            df = self.get_symbol_data(symbol, days=30, timeframe=timeframe)
            
            if df.empty:
                logger.warning(f"   ⚠️ No data for validation: {symbol} {timeframe}")
                return
            
            # Check for gaps in data
            expected_interval = self._timeframe_to_ms(timeframe) / 1000
            time_diffs = df.index.to_series().diff().dt.total_seconds().dropna()
            
            # Find gaps larger than expected
            gaps = time_diffs[time_diffs > expected_interval * 1.5]
            
            if len(gaps) > 0:
                logger.warning(f"   ⚠️ Found {len(gaps)} data gaps in {symbol} {timeframe}")
                
                # Log significant gaps
                large_gaps = gaps[gaps > expected_interval * 5]
                if len(large_gaps) > 0:
                    logger.warning(f"   ❌ {len(large_gaps)} large gaps (>5x interval)")
            
            # Check data consistency
            invalid_bars = 0
            for _, row in df.head(100).iterrows():  # Check sample
                if not (row['low'] <= row['open'] <= row['high'] and 
                       row['low'] <= row['close'] <= row['high']):
                    invalid_bars += 1
            
            if invalid_bars > 0:
                logger.error(f"   ❌ Found {invalid_bars} invalid OHLC bars in {symbol} {timeframe}")
            else:
                logger.debug(f"   ✅ Data quality OK for {symbol} {timeframe}")
            
            logger.info(f"   📊 Quality check: {len(df)} bars, "
                       f"{len(gaps)} gaps, {invalid_bars} invalid bars")
            
        except Exception as e:
            logger.error(f"Data validation failed for {symbol} {timeframe}: {e}")
    
    async def fill_data_gaps(self, symbol: str, timeframe: str, 
                            max_gap_hours: int = 48) -> int:
        """Fill gaps in historical data"""
        try:
            db_symbol = self._symbol_to_db_format(symbol)
            
            # Find gaps in data
            async with self.db_pool.acquire() as conn:
                gaps = await conn.fetch("""
                    SELECT 
                        timestamp as gap_start,
                        LEAD(timestamp) OVER (ORDER BY timestamp) as gap_end,
                        LEAD(timestamp) OVER (ORDER BY timestamp) - timestamp as gap_size
                    FROM ohlcv_data 
                    WHERE symbol = $1 AND timeframe = $2
                    ORDER BY timestamp
                """, db_symbol, timeframe)
            
            filled_count = 0
            timeframe_ms = self._timeframe_to_ms(timeframe)
            max_gap_ms = max_gap_hours * 60 * 60 * 1000
            
            for gap in gaps:
                if gap['gap_size'] and gap['gap_size'] > timeframe_ms * 1.5:  # Significant gap
                    if gap['gap_size'] <= max_gap_ms:  # Not too large
                        logger.info(f"Filling gap in {symbol} {timeframe}: "
                                   f"{datetime.fromtimestamp(gap['gap_start']/1000)} - "
                                   f"{datetime.fromtimestamp(gap['gap_end']/1000)}")
                        
                        # Fetch data for gap period
                        gap_data = await self._fetch_ohlcv_with_retry(
                            self._symbol_to_ccxt_format(symbol), timeframe, 
                            gap['gap_start'], limit=1000
                        )
                        
                        if gap_data:
                            saved = await self.save_ohlcv_data(gap_data, symbol, timeframe)
                            filled_count += saved
                            logger.info(f"   Filled {saved} bars")
            
            if filled_count > 0:
                logger.info(f"✅ Filled {filled_count} gaps in {symbol} {timeframe}")
            
            return filled_count
            
        except Exception as e:
            logger.error(f"Failed to fill gaps for {symbol} {timeframe}: {e}")
            return 0
    
    # Convenience methods for common use cases
    async def collect_all_symbols_full_history(self, days_back: int = 365):
        """Collect full history for all configured symbols"""
        return await self.collect_historical_data(
            symbols=self.symbols,
            timeframes=self.timeframes, 
            days_back=days_back,
            force_refresh=False
        )
    
    async def refresh_symbol_data(self, symbol: str, days_back: int = 30):
        """Force refresh data for a specific symbol"""
        return await self.collect_historical_data(
            symbols=[symbol],
            timeframes=self.timeframes,
            days_back=days_back, 
            force_refresh=True
        )
    
    async def get_data_summary(self) -> Dict:
        """Get summary of all stored data"""
        summary = {}
        
        for symbol in self.symbols:
            ccxt_symbol = self._symbol_to_ccxt_format(symbol) if '/' not in symbol else symbol
            summary[ccxt_symbol] = {}
            
            for timeframe in self.timeframes:
                try:
                    count = await self._get_data_count(ccxt_symbol, timeframe)
                    latest_ts = await self.get_latest_timestamp(ccxt_symbol, timeframe)
                    
                    latest_date = None
                    if latest_ts:
                        latest_date = datetime.fromtimestamp(latest_ts / 1000).isoformat()
                    
                    summary[ccxt_symbol][timeframe] = {
                        'record_count': count,
                        'latest_date': latest_date,
                        'hours_old': ((datetime.now().timestamp() * 1000 - latest_ts) / (1000 * 3600)) if latest_ts else None
                    }
                    
                except Exception as e:
                    logger.error(f"Error getting summary for {ccxt_symbol} {timeframe}: {e}")
                    summary[ccxt_symbol][timeframe] = {'error': str(e)}
        
        return summary    
    
    async def save_ohlcv_batch(self, batch_data: Dict[str, Dict[str, List]], 
                              batch_size: int = 1000) -> Dict[str, int]:
        """Save OHLCV data for multiple symbols in batches"""
        results = {}
        
        async with self.db_pool.acquire() as conn:
            async with conn.transaction():
                all_records = []
                
                # Flatten all data into single batch
                for symbol, timeframe_data in batch_data.items():
                    db_symbol = self._symbol_to_db_format(symbol)
                    for timeframe, ohlcv_list in timeframe_data.items():
                        for candle in ohlcv_list:
                            if len(candle) >= 6:
                                timestamp, open_p, high, low, close, volume = candle[:6]
                                # Validate data
                                if all(x is not None and x >= 0 for x in [open_p, high, low, close, volume]):
                                    if low <= open_p <= high and low <= close <= high:
                                        all_records.append((
                                            db_symbol, timeframe, int(timestamp),
                                            float(open_p), float(high), float(low),
                                            float(close), float(volume)
                                        ))
                
                # Insert in batches
                saved_count = 0
                for i in range(0, len(all_records), batch_size):
                    batch = all_records[i:i + batch_size]
                    try:
                        await conn.executemany("""
                            INSERT INTO ohlcv_data 
                            (symbol, timeframe, timestamp, open, high, low, close, volume)
                            VALUES ($1, $2, $3, $4, $5, $6, $7, $8)
                            ON CONFLICT (symbol, timeframe, timestamp) DO NOTHING
                        """, batch)
                        saved_count += len(batch)
                    except Exception as e:
                        logger.error(f"Batch insert failed: {e}")
                
                results['total_saved'] = saved_count
                logger.info(f"Batch saved {saved_count} records across all symbols")
                
        return results
    def _prioritize_symbols(self, symbols: List[str]) -> List[str]:
        """Prioritize symbols by importance"""
        high_priority = ['BTC/USDT', 'ETH/USDT', 'ADA/USDT']
        medium_priority = [s for s in symbols if s not in high_priority and 'USDT' in s]
        low_priority = [s for s in symbols if s not in high_priority + medium_priority]
        
        return high_priority + medium_priority + low_priority
    
    async def fetch_symbol_data_safe(self, symbol: str, timeframe: str, 
                                   since: int = None, limit: int = 1000) -> Optional[List]:
        """Fetch data for single symbol with rate limiting"""
        semaphore = getattr(self, '_semaphore', asyncio.Semaphore(self.max_concurrent))
        if not hasattr(self, '_semaphore'):
            self._semaphore = semaphore
            
        async with semaphore:
            await self.rate_limiter.wait()
            
            try:
                data = await self._fetch_ohlcv_with_retry(symbol, timeframe, since, limit)
                self.rate_limiter.on_success()
                return data
                
            except ccxt.RateLimitExceeded as e:
                self.rate_limiter.on_rate_limit()
                logger.warning(f"Rate limit for {symbol}: {e}")
                # Wait longer and retry once
                await asyncio.sleep(self.rate_limiter.delay)
                return await self._fetch_ohlcv_with_retry(symbol, timeframe, since, limit//2)
                
            except ccxt.NetworkError as e:
                self.rate_limiter.on_error()
                logger.error(f"Network error for {symbol}: {e}")
                return None
                
            except Exception as e:
                logger.error(f"Unexpected error for {symbol}: {e}")
                return None