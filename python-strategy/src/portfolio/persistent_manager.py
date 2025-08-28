# src/portfolio/persistent_manager.py
"""
Persistent Position Manager with Database Backup
Prevents portfolio state loss on bot restart
"""

import asyncio
import asyncpg
from typing import Dict, List, Optional
from datetime import datetime, date
import logging

logger = logging.getLogger(__name__)

class PersistentPositionManager:
    """Position manager with database persistence"""
    
    def __init__(self, config: dict):
        self.config = config
        self.db_url = config.get('database', {}).get('url')
        self.pool = None
        
        # In-memory cache
        self.positions_cache = {}
        self.last_sync = None
        
    async def initialize(self):
        """Initialize database connection"""
        try:
            self.pool = await asyncpg.create_pool(self.db_url, min_size=2, max_size=5)
            await self._create_tables_if_needed()
            await self._load_positions_from_db()
            logger.info("Persistent position manager initialized")
        except Exception as e:
            logger.error(f"Failed to initialize persistent position manager: {e}")
            raise
    
    async def _create_tables_if_needed(self):
        """Create database tables if they don't exist"""
        async with self.pool.acquire() as conn:
            # Create positions table
            await conn.execute("""
                CREATE TABLE IF NOT EXISTS positions (
                    symbol VARCHAR(20) PRIMARY KEY,
                    quantity DECIMAL(20,8) NOT NULL,
                    avg_price DECIMAL(20,8) NOT NULL,
                    market_value DECIMAL(20,8) NOT NULL,
                    last_updated TIMESTAMP DEFAULT CURRENT_TIMESTAMP
                )
            """)
            
            # Create trades table
            await conn.execute("""
                CREATE TABLE IF NOT EXISTS trades (
                    id SERIAL PRIMARY KEY,
                    trade_id VARCHAR(50) UNIQUE NOT NULL,
                    symbol VARCHAR(20) NOT NULL,
                    side VARCHAR(10) NOT NULL,
                    quantity DECIMAL(20,8) NOT NULL,
                    price DECIMAL(20,8) NOT NULL,
                    value DECIMAL(20,8) NOT NULL,
                    strategy_signal DECIMAL(5,2) DEFAULT 0,
                    executed_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                    exchange_order_id VARCHAR(100)
                )
            """)
    
    async def _load_positions_from_db(self):
        """Load positions from database into cache"""
        try:
            async with self.pool.acquire() as conn:
                rows = await conn.fetch("SELECT * FROM positions WHERE quantity > 0")
                
                self.positions_cache = {}
                for row in rows:
                    self.positions_cache[row['symbol']] = {
                        'quantity': float(row['quantity']),
                        'avg_price': float(row['avg_price']),
                        'market_value': float(row['market_value']),
                        'last_updated': row['last_updated']
                    }
                
                logger.info(f"Loaded {len(self.positions_cache)} positions from database")
                
        except Exception as e:
            logger.error(f"Failed to load positions from database: {e}")
    
    async def sync_with_exchange(self, exchange_balance: dict) -> Dict[str, float]:
        """Sync local positions with exchange balance"""
        try:
            discrepancies = {}
            
            # Parse exchange balance
            exchange_positions = {}
            for asset, balance_data in exchange_balance.items():
                if asset in ['info', 'timestamp', 'datetime', 'free', 'used', 'total']:
                    continue
                
                if isinstance(balance_data, dict):
                    free_amount = float(balance_data.get('free', 0))
                    if free_amount > 0:
                        exchange_positions[asset] = free_amount
            
            # Compare with database positions
            for symbol, db_position in self.positions_cache.items():
                base_asset = symbol.replace('USDT', '')
                exchange_qty = exchange_positions.get(base_asset, 0)
                db_qty = db_position['quantity']
                
                if abs(exchange_qty - db_qty) > 0.000001:  # Small tolerance for floating point
                    discrepancies[symbol] = {
                        'database': db_qty,
                        'exchange': exchange_qty,
                        'difference': exchange_qty - db_qty
                    }
            
            # Update positions from exchange if discrepancies found
            if discrepancies:
                logger.warning(f"Position discrepancies found: {discrepancies}")
                await self._update_positions_from_exchange(exchange_positions)
            
            return discrepancies
            
        except Exception as e:
            logger.error(f"Failed to sync with exchange: {e}")
            return {}
    
    async def _update_positions_from_exchange(self, exchange_positions: dict):
        """Update database positions from exchange data"""
        try:
            async with self.pool.acquire() as conn:
                # Clear existing positions
                await conn.execute("DELETE FROM positions")
                
                # Insert current exchange positions
                for asset, quantity in exchange_positions.items():
                    symbol = f"{asset}USDT"
                    
                    # Get current price (you'll need to implement this)
                    current_price = await self._get_current_price(symbol)
                    market_value = quantity * current_price
                    
                    await conn.execute("""
                        INSERT INTO positions (symbol, quantity, avg_price, market_value)
                        VALUES ($1, $2, $3, $4)
                        ON CONFLICT (symbol) DO UPDATE SET
                            quantity = EXCLUDED.quantity,
                            market_value = EXCLUDED.market_value,
                            last_updated = CURRENT_TIMESTAMP
                    """, symbol, quantity, current_price, market_value)
                
                # Reload cache
                await self._load_positions_from_db()
                
        except Exception as e:
            logger.error(f"Failed to update positions from exchange: {e}")
    
    async def record_trade(self, trade_data: dict):
        """Record trade in database and update positions"""
        try:
            async with self.pool.acquire() as conn:
                # Insert trade record
                await conn.execute("""
                    INSERT INTO trades (trade_id, symbol, side, quantity, price, value, strategy_signal, exchange_order_id)
                    VALUES ($1, $2, $3, $4, $5, $6, $7, $8)
                    ON CONFLICT (trade_id) DO NOTHING
                """, 
                    trade_data['trade_id'],
                    trade_data['symbol'],
                    trade_data['side'],
                    trade_data['quantity'],
                    trade_data['price'],
                    trade_data['value'],
                    trade_data.get('strategy_signal', 0),
                    trade_data.get('exchange_order_id')
                )
                
                # Update position
                await self._update_position_after_trade(conn, trade_data)
                
        except Exception as e:
            logger.error(f"Failed to record trade: {e}")
    
    async def _update_position_after_trade(self, conn, trade_data: dict):
        """Update position after trade execution"""
        symbol = trade_data['symbol']
        side = trade_data['side']
        quantity = trade_data['quantity']
        price = trade_data['price']
        
        # Get current position
        current_pos = await conn.fetchrow("SELECT * FROM positions WHERE symbol = $1", symbol)
        
        if side == 'BUY':
            if current_pos:
                # Update existing position
                new_qty = float(current_pos['quantity']) + quantity
                total_cost = (float(current_pos['quantity']) * float(current_pos['avg_price'])) + (quantity * price)
                new_avg_price = total_cost / new_qty if new_qty > 0 else price
                new_market_value = new_qty * price
                
                await conn.execute("""
                    UPDATE positions SET 
                        quantity = $1, 
                        avg_price = $2, 
                        market_value = $3,
                        last_updated = CURRENT_TIMESTAMP
                    WHERE symbol = $4
                """, new_qty, new_avg_price, new_market_value, symbol)
            else:
                # New position
                market_value = quantity * price
                await conn.execute("""
                    INSERT INTO positions (symbol, quantity, avg_price, market_value)
                    VALUES ($1, $2, $3, $4)
                """, symbol, quantity, price, market_value)
        
        else:  # SELL
            if current_pos:
                new_qty = float(current_pos['quantity']) - quantity
                if new_qty <= 0:
                    # Position closed
                    await conn.execute("DELETE FROM positions WHERE symbol = $1", symbol)
                else:
                    # Partial sell
                    new_market_value = new_qty * price
                    await conn.execute("""
                        UPDATE positions SET 
                            quantity = $1,
                            market_value = $2,
                            last_updated = CURRENT_TIMESTAMP
                        WHERE symbol = $3
                    """, new_qty, new_market_value, symbol)
        
        # Reload cache
        await self._load_positions_from_db()
    
    async def get_positions(self) -> dict:
        """Get current positions from cache"""
        return self.positions_cache.copy()
    
    async def has_position(self, symbol: str) -> bool:
        """Check if we have position in symbol"""
        base_asset = symbol.replace('USDT', '').replace('/', '')
        return base_asset + 'USDT' in self.positions_cache
    
    async def get_position_quantity(self, symbol: str) -> float:
        """Get position quantity for symbol"""
        base_asset = symbol.replace('USDT', '').replace('/', '')
        symbol_key = base_asset + 'USDT'
        position = self.positions_cache.get(symbol_key, {})
        return position.get('quantity', 0)
    
    async def _get_current_price(self, symbol: str) -> float:
        """Get current price for symbol (implement based on your exchange client)"""
        # This is a placeholder - you need to implement this based on your exchange client
        return 50000.0  # Placeholder price
    
    async def close(self):
        """Close database connections"""
        if self.pool:
            await self.pool.close()