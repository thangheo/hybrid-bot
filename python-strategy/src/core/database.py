# src/core/database.py
"""
Bio-Inspired Trading System - Database Management
Database interface with connection pooling, health monitoring, and recovery mechanisms
"""

import asyncio
import logging
from abc import ABC, abstractmethod
from typing import Dict, Any, Optional, List, AsyncContextManager, Union, Tuple
from contextlib import asynccontextmanager
from datetime import datetime, timedelta
import time
import json
from dataclasses import dataclass, field
from enum import Enum
import sqlite3
import aiosqlite

# Optional imports for PostgreSQL support
try:
    import asyncpg
    ASYNCPG_AVAILABLE = True
except ImportError:
    ASYNCPG_AVAILABLE = False
    asyncpg = None

from .interfaces import (
    DatabaseInterface, SystemHealth, PerformanceMetrics, 
    LayerHealth, LayerType, SystemState
)


logger = logging.getLogger(__name__)


class DatabaseType(Enum):
    """Supported database types"""
    SQLITE = "sqlite"
    POSTGRESQL = "postgresql"


@dataclass
class ConnectionMetrics:
    """Database connection metrics"""
    total_connections: int = 0
    active_connections: int = 0
    failed_connections: int = 0
    avg_query_time: float = 0.0
    total_queries: int = 0
    failed_queries: int = 0
    last_health_check: datetime = field(default_factory=datetime.now)
    uptime_seconds: float = 0.0
    
    def get_success_rate(self) -> float:
        """Calculate query success rate"""
        if self.total_queries == 0:
            return 1.0
        return (self.total_queries - self.failed_queries) / self.total_queries
    
    def get_connection_success_rate(self) -> float:
        """Calculate connection success rate"""
        if self.total_connections == 0:
            return 1.0
        return (self.total_connections - self.failed_connections) / self.total_connections


class ConnectionPool:
    """
    Database connection pool with adaptive sizing and health monitoring
    """
    
    def __init__(self, 
                 database_url: str,
                 min_connections: int = 2,
                 max_connections: int = 10,
                 max_idle_time: int = 300):
        self.database_url = database_url
        self.min_connections = min_connections
        self.max_connections = max_connections
        self.max_idle_time = max_idle_time
        
        # Connection management
        self.pool: Optional[Any] = None
        self.db_type = self._detect_database_type()
        self.is_initialized = False
        
        # Health monitoring
        self.metrics = ConnectionMetrics()
        self.start_time = datetime.now()
        
        # Connection tracking
        self._active_connections = set()
        self._connection_semaphore = asyncio.Semaphore(max_connections)
    
    def _detect_database_type(self) -> DatabaseType:
        """Detect database type from URL"""
        if self.database_url.startswith('postgresql://'):
            return DatabaseType.POSTGRESQL
        elif self.database_url.startswith('sqlite://') or self.database_url.endswith('.db'):
            return DatabaseType.SQLITE
        else:
            # Default to SQLite for file paths
            return DatabaseType.SQLITE
    
    async def initialize(self) -> bool:
        """Initialize connection pool"""
        try:
            logger.info(f"Initializing {self.db_type.value} connection pool...")
            
            if self.db_type == DatabaseType.POSTGRESQL:
                if not ASYNCPG_AVAILABLE:
                    raise ImportError("asyncpg package required for PostgreSQL support")
                
                self.pool = await asyncpg.create_pool(
                    self.database_url,
                    min_size=self.min_connections,
                    max_size=self.max_connections,
                    max_inactive_connection_lifetime=self.max_idle_time
                )
            
            elif self.db_type == DatabaseType.SQLITE:
                # SQLite doesn't have built-in pooling, we'll manage connections manually
                self.pool = self.database_url
                # Create database file if it doesn't exist
                await self._ensure_sqlite_database()
            
            self.is_initialized = True
            self.metrics.total_connections += 1
            logger.info("Database connection pool initialized successfully")
            return True
            
        except Exception as e:
            logger.error(f"Failed to initialize connection pool: {e}")
            self.metrics.failed_connections += 1
            return False
    
    async def close(self) -> None:
        """Close connection pool"""
        if not self.is_initialized:
            return
        
        try:
            if self.db_type == DatabaseType.POSTGRESQL and self.pool:
                await self.pool.close()
            
            self.is_initialized = False
            logger.info("Database connection pool closed")
            
        except Exception as e:
            logger.error(f"Error closing connection pool: {e}")
    
    @asynccontextmanager
    async def get_connection(self) -> AsyncContextManager:
        """Get database connection from pool"""
        async with self._connection_semaphore:
            connection_id = id(asyncio.current_task())
            
            try:
                self.metrics.active_connections += 1
                self._active_connections.add(connection_id)
                
                if self.db_type == DatabaseType.POSTGRESQL:
                    async with self.pool.acquire() as conn:
                        yield conn
                
                elif self.db_type == DatabaseType.SQLITE:
                    async with aiosqlite.connect(self.pool) as conn:
                        # Enable WAL mode for better concurrency
                        await conn.execute("PRAGMA journal_mode=WAL")
                        yield conn
                
            except Exception as e:
                logger.error(f"Database connection error: {e}")
                self.metrics.failed_connections += 1
                raise
            
            finally:
                self.metrics.active_connections -= 1
                self._active_connections.discard(connection_id)
    
    async def health_check(self) -> bool:
        """Perform health check on database connection"""
        try:
            async with self.get_connection() as conn:
                if self.db_type == DatabaseType.POSTGRESQL:
                    await conn.fetchval("SELECT 1")
                elif self.db_type == DatabaseType.SQLITE:
                    async with conn.execute("SELECT 1") as cursor:
                        await cursor.fetchone()
            
            self.metrics.last_health_check = datetime.now()
            return True
            
        except Exception as e:
            logger.error(f"Database health check failed: {e}")
            return False
    
    async def _ensure_sqlite_database(self) -> None:
        """Ensure SQLite database file exists"""
        if self.database_url.startswith('sqlite://'):
            db_path = self.database_url[9:]  # Remove 'sqlite://' prefix
        else:
            db_path = self.database_url
        
        # Create database file if it doesn't exist
        async with aiosqlite.connect(db_path) as conn:
            await conn.execute("SELECT 1")
    
    def get_metrics(self) -> Dict[str, Any]:
        """Get connection pool metrics"""
        self.metrics.uptime_seconds = (datetime.now() - self.start_time).total_seconds()
        
        return {
            "database_type": self.db_type.value,
            "is_initialized": self.is_initialized,
            "total_connections": self.metrics.total_connections,
            "active_connections": self.metrics.active_connections,
            "failed_connections": self.metrics.failed_connections,
            "connection_success_rate": self.metrics.get_connection_success_rate(),
            "total_queries": self.metrics.total_queries,
            "failed_queries": self.metrics.failed_queries,
            "query_success_rate": self.metrics.get_success_rate(),
            "avg_query_time_ms": self.metrics.avg_query_time * 1000,
            "uptime_seconds": self.metrics.uptime_seconds,
            "last_health_check": self.metrics.last_health_check.isoformat()
        }


class BioDatabaseManager(DatabaseInterface):
    """
    Bio-inspired database manager with adaptive query optimization
    and self-healing capabilities
    """
    
    def __init__(self, config: Dict[str, Any]):
        self.config = config
        self.pool = ConnectionPool(
            database_url=config['url'],
            min_connections=config.get('min_connections', 2),
            max_connections=config.get('max_connections', 10),
            max_idle_time=config.get('max_idle_time', 300)
        )
        
        # Query performance tracking
        self.query_cache: Dict[str, Any] = {}
        self.slow_query_threshold = config.get('slow_query_threshold', 1.0)
        
        # Schema management
        self.schema_version = 1
        self.is_schema_initialized = False
    
    async def connect(self) -> bool:
        """Establish database connection"""
        try:
            success = await self.pool.initialize()
            if success:
                await self._initialize_schema()
            return success
        except Exception as e:
            logger.error(f"Failed to connect to database: {e}")
            return False
    
    async def disconnect(self) -> None:
        """Close database connection"""
        await self.pool.close()
    
    async def execute_query(self, query: str, params: Optional[Dict] = None) -> Any:
        """Execute database query with performance tracking"""
        start_time = time.time()
        
        try:
            async with self.pool.get_connection() as conn:
                if self.pool.db_type == DatabaseType.POSTGRESQL:
                    if params:
                        # Convert named parameters to positional for asyncpg
                        query_parts = query.split()
                        param_values = list(params.values()) if params else []
                        result = await conn.fetch(query, *param_values)
                    else:
                        result = await conn.fetch(query)
                
                elif self.pool.db_type == DatabaseType.SQLITE:
                    if params:
                        async with conn.execute(query, params) as cursor:
                            result = await cursor.fetchall()
                    else:
                        async with conn.execute(query) as cursor:
                            result = await cursor.fetchall()
                    await conn.commit()
            
            # Update performance metrics
            execution_time = time.time() - start_time
            self._update_query_metrics(query, execution_time, True)
            
            # Log slow queries
            if execution_time > self.slow_query_threshold:
                logger.warning(f"Slow query detected ({execution_time:.3f}s): {query[:100]}...")
            
            return result
            
        except Exception as e:
            execution_time = time.time() - start_time
            self._update_query_metrics(query, execution_time, False)
            logger.error(f"Query execution failed: {e}")
            raise
    
    async def health_check(self) -> bool:
        """Check database health"""
        return await self.pool.health_check()
    
    async def store_system_health(self, health: SystemHealth) -> bool:
        """Store system health metrics"""
        try:
            query = """
            INSERT INTO system_health 
            (stability_score, risk_level, performance_score, adaptation_score, 
             state, timestamp, details, alerts)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            """
            
            params = {
                'stability_score': health.stability_score,
                'risk_level': health.risk_level,
                'performance_score': health.performance_score,
                'adaptation_score': health.adaptation_score,
                'state': health.state.value,
                'timestamp': health.timestamp.isoformat(),
                'details': json.dumps(health.details),
                'alerts': json.dumps(health.alerts)
            }
            
            await self.execute_query(query, params)
            return True
            
        except Exception as e:
            logger.error(f"Failed to store system health: {e}")
            return False
    
    async def store_performance_metrics(self, metrics: PerformanceMetrics) -> bool:
        """Store performance metrics"""
        try:
            query = """
            INSERT INTO performance_metrics 
            (returns, sharpe_ratio, max_drawdown, win_rate, total_trades,
             avg_trade_duration, volatility, alpha, beta, timestamp, 
             period_days, metadata)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """
            
            params = {
                'returns': metrics.returns,
                'sharpe_ratio': metrics.sharpe_ratio,
                'max_drawdown': metrics.max_drawdown,
                'win_rate': metrics.win_rate,
                'total_trades': metrics.total_trades,
                'avg_trade_duration': metrics.avg_trade_duration,
                'volatility': metrics.volatility,
                'alpha': metrics.alpha,
                'beta': metrics.beta,
                'timestamp': metrics.timestamp.isoformat(),
                'period_days': metrics.period_days,
                'metadata': json.dumps(metrics.metadata)
            }
            
            await self.execute_query(query, params)
            return True
            
        except Exception as e:
            logger.error(f"Failed to store performance metrics: {e}")
            return False
    
    async def store_layer_health(self, health: LayerHealth) -> bool:
        """Store layer health metrics"""
        try:
            query = """
            INSERT INTO layer_health 
            (layer_type, is_active, cpu_usage, memory_usage, response_time_ms,
             error_rate, last_update, status_message, metrics)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
            """
            
            params = {
                'layer_type': health.layer_type.value,
                'is_active': health.is_active,
                'cpu_usage': health.cpu_usage,
                'memory_usage': health.memory_usage,
                'response_time_ms': health.response_time_ms,
                'error_rate': health.error_rate,
                'last_update': health.last_update.isoformat(),
                'status_message': health.status_message,
                'metrics': json.dumps(health.metrics)
            }
            
            await self.execute_query(query, params)
            return True
            
        except Exception as e:
            logger.error(f"Failed to store layer health: {e}")
            return False
    
    async def get_historical_performance(self, days: int = 30) -> List[PerformanceMetrics]:
        """Retrieve historical performance data"""
        try:
            cutoff_date = datetime.now() - timedelta(days=days)
            
            query = """
            SELECT * FROM performance_metrics 
            WHERE timestamp >= ? 
            ORDER BY timestamp DESC
            """
            
            result = await self.execute_query(query, {'cutoff': cutoff_date.isoformat()})
            
            performance_list = []
            for row in result:
                if self.pool.db_type == DatabaseType.POSTGRESQL:
                    # asyncpg returns Record objects
                    performance = PerformanceMetrics(
                        returns=row['returns'],
                        sharpe_ratio=row['sharpe_ratio'],
                        max_drawdown=row['max_drawdown'],
                        win_rate=row['win_rate'],
                        total_trades=row['total_trades'],
                        avg_trade_duration=row['avg_trade_duration'],
                        volatility=row['volatility'],
                        alpha=row['alpha'],
                        beta=row['beta'],
                        timestamp=datetime.fromisoformat(row['timestamp']),
                        period_days=row['period_days'],
                        metadata=json.loads(row['metadata']) if row['metadata'] else {}
                    )
                else:
                    # SQLite returns tuples
                    performance = PerformanceMetrics(
                        returns=row[1],
                        sharpe_ratio=row[2],
                        max_drawdown=row[3],
                        win_rate=row[4],
                        total_trades=row[5],
                        avg_trade_duration=row[6],
                        volatility=row[7],
                        alpha=row[8],
                        beta=row[9],
                        timestamp=datetime.fromisoformat(row[10]),
                        period_days=row[11],
                        metadata=json.loads(row[12]) if row[12] else {}
                    )
                
                performance_list.append(performance)
            
            return performance_list
            
        except Exception as e:
            logger.error(f"Failed to retrieve historical performance: {e}")
            return []
    
    async def get_latest_system_health(self) -> Optional[SystemHealth]:
        """Get the most recent system health record"""
        try:
            query = """
            SELECT * FROM system_health 
            ORDER BY timestamp DESC 
            LIMIT 1
            """
            
            result = await self.execute_query(query)
            
            if not result:
                return None
            
            row = result[0]
            
            if self.pool.db_type == DatabaseType.POSTGRESQL:
                return SystemHealth(
                    stability_score=row['stability_score'],
                    risk_level=row['risk_level'],
                    performance_score=row['performance_score'],
                    adaptation_score=row['adaptation_score'],
                    state=SystemState(row['state']),
                    timestamp=datetime.fromisoformat(row['timestamp']),
                    details=json.loads(row['details']) if row['details'] else {},
                    alerts=json.loads(row['alerts']) if row['alerts'] else []
                )
            else:
                return SystemHealth(
                    stability_score=row[1],
                    risk_level=row[2],
                    performance_score=row[3],
                    adaptation_score=row[4],
                    state=SystemState(row[5]),
                    timestamp=datetime.fromisoformat(row[6]),
                    details=json.loads(row[7]) if row[7] else {},
                    alerts=json.loads(row[8]) if row[8] else []
                )
                
        except Exception as e:
            logger.error(f"Failed to get latest system health: {e}")
            return None
    
    async def get_layer_health_history(self, layer_type: LayerType, hours: int = 24) -> List[LayerHealth]:
        """Get layer health history"""
        try:
            cutoff_date = datetime.now() - timedelta(hours=hours)
            
            query = """
            SELECT * FROM layer_health 
            WHERE layer_type = ? AND last_update >= ?
            ORDER BY last_update DESC
            """
            
            params = {
                'layer_type': layer_type.value,
                'cutoff': cutoff_date.isoformat()
            }
            
            result = await self.execute_query(query, params)
            
            health_list = []
            for row in result:
                if self.pool.db_type == DatabaseType.POSTGRESQL:
                    health = LayerHealth(
                        layer_type=LayerType(row['layer_type']),
                        is_active=row['is_active'],
                        cpu_usage=row['cpu_usage'],
                        memory_usage=row['memory_usage'],
                        response_time_ms=row['response_time_ms'],
                        error_rate=row['error_rate'],
                        last_update=datetime.fromisoformat(row['last_update']),
                        status_message=row['status_message'],
                        metrics=json.loads(row['metrics']) if row['metrics'] else {}
                    )
                else:
                    health = LayerHealth(
                        layer_type=LayerType(row[1]),
                        is_active=bool(row[2]),
                        cpu_usage=row[3],
                        memory_usage=row[4],
                        response_time_ms=row[5],
                        error_rate=row[6],
                        last_update=datetime.fromisoformat(row[7]),
                        status_message=row[8],
                        metrics=json.loads(row[9]) if row[9] else {}
                    )
                
                health_list.append(health)
            
            return health_list
            
        except Exception as e:
            logger.error(f"Failed to get layer health history: {e}")
            return []
    
    async def store_trading_signal(self, signal_data: Dict[str, Any]) -> bool:
        """Store trading signal for analysis"""
        try:
            query = """
            INSERT INTO trading_signals 
            (signal_id, symbol, action, strength, confidence, position_size,
             regime, entry_price, stop_loss, take_profit, timestamp, metadata)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """
            
            params = {
                'signal_id': signal_data.get('signal_id'),
                'symbol': signal_data.get('symbol'),
                'action': signal_data.get('action'),
                'strength': signal_data.get('strength'),
                'confidence': signal_data.get('confidence'),
                'position_size': signal_data.get('position_size'),
                'regime': signal_data.get('regime'),
                'entry_price': signal_data.get('entry_price'),
                'stop_loss': signal_data.get('stop_loss'),
                'take_profit': signal_data.get('take_profit'),
                'timestamp': signal_data.get('timestamp', datetime.now().isoformat()),
                'metadata': json.dumps(signal_data.get('metadata', {}))
            }
            
            await self.execute_query(query, params)
            return True
            
        except Exception as e:
            logger.error(f"Failed to store trading signal: {e}")
            return False
    
    async def cleanup_old_data(self, retention_days: int = 90) -> bool:
        """Clean up old data to maintain database performance"""
        try:
            cutoff_date = datetime.now() - timedelta(days=retention_days)
            cutoff_str = cutoff_date.isoformat()
            
            # Clean up old system health records
            await self.execute_query(
                "DELETE FROM system_health WHERE timestamp < ?",
                {'cutoff': cutoff_str}
            )
            
            # Clean up old layer health records
            await self.execute_query(
                "DELETE FROM layer_health WHERE last_update < ?",
                {'cutoff': cutoff_str}
            )
            
            # Keep performance metrics longer (1 year)
            perf_cutoff = datetime.now() - timedelta(days=365)
            await self.execute_query(
                "DELETE FROM performance_metrics WHERE timestamp < ?",
                {'cutoff': perf_cutoff.isoformat()}
            )
            
            # Clean up old trading signals
            await self.execute_query(
                "DELETE FROM trading_signals WHERE timestamp < ?",
                {'cutoff': cutoff_str}
            )
            
            logger.info(f"Cleaned up data older than {retention_days} days")
            return True
            
        except Exception as e:
            logger.error(f"Failed to cleanup old data: {e}")
            return False
    
    def get_metrics(self) -> Dict[str, Any]:
        """Get database manager metrics"""
        pool_metrics = self.pool.get_metrics()
        
        return {
            **pool_metrics,
            "schema_version": self.schema_version,
            "schema_initialized": self.is_schema_initialized,
            "slow_query_threshold": self.slow_query_threshold,
            "query_cache_size": len(self.query_cache)
        }
    
    async def _initialize_schema(self) -> None:
        """Initialize database schema"""
        try:
            # Create system health table
            await self.execute_query("""
                CREATE TABLE IF NOT EXISTS system_health (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    stability_score REAL NOT NULL,
                    risk_level REAL NOT NULL,
                    performance_score REAL NOT NULL,
                    adaptation_score REAL NOT NULL,
                    state TEXT NOT NULL,
                    timestamp TEXT NOT NULL,
                    details TEXT,
                    alerts TEXT
                )
            """)
            
            # Create performance metrics table
            await self.execute_query("""
                CREATE TABLE IF NOT EXISTS performance_metrics (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    returns REAL NOT NULL,
                    sharpe_ratio REAL NOT NULL,
                    max_drawdown REAL NOT NULL,
                    win_rate REAL NOT NULL,
                    total_trades INTEGER NOT NULL,
                    avg_trade_duration REAL NOT NULL,
                    volatility REAL NOT NULL,
                    alpha REAL NOT NULL,
                    beta REAL NOT NULL,
                    timestamp TEXT NOT NULL,
                    period_days INTEGER NOT NULL,
                    metadata TEXT
                )
            """)
            
            # Create layer health table
            await self.execute_query("""
                CREATE TABLE IF NOT EXISTS layer_health (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    layer_type TEXT NOT NULL,
                    is_active BOOLEAN NOT NULL,
                    cpu_usage REAL NOT NULL,
                    memory_usage REAL NOT NULL,
                    response_time_ms REAL NOT NULL,
                    error_rate REAL NOT NULL,
                    last_update TEXT NOT NULL,
                    status_message TEXT,
                    metrics TEXT
                )
            """)
            
            # Create trading signals table
            await self.execute_query("""
                CREATE TABLE IF NOT EXISTS trading_signals (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    signal_id TEXT UNIQUE NOT NULL,
                    symbol TEXT NOT NULL,
                    action TEXT NOT NULL,
                    strength REAL NOT NULL,
                    confidence REAL NOT NULL,
                    position_size REAL NOT NULL,
                    regime TEXT,
                    entry_price REAL,
                    stop_loss REAL,
                    take_profit REAL,
                    timestamp TEXT NOT NULL,
                    metadata TEXT
                )
            """)
            
            # Create indexes for better performance
            await self.execute_query("CREATE INDEX IF NOT EXISTS idx_system_health_timestamp ON system_health(timestamp)")
            await self.execute_query("CREATE INDEX IF NOT EXISTS idx_performance_timestamp ON performance_metrics(timestamp)")
            await self.execute_query("CREATE INDEX IF NOT EXISTS idx_layer_health_type_time ON layer_health(layer_type, last_update)")
            await self.execute_query("CREATE INDEX IF NOT EXISTS idx_trading_signals_timestamp ON trading_signals(timestamp)")
            await self.execute_query("CREATE INDEX IF NOT EXISTS idx_trading_signals_symbol ON trading_signals(symbol)")
            
            self.is_schema_initialized = True
            logger.info("Database schema initialized successfully")
            
        except Exception as e:
            logger.error(f"Failed to initialize database schema: {e}")
            raise
    
    def _update_query_metrics(self, query: str, execution_time: float, success: bool) -> None:
        """Update query performance metrics"""
        self.pool.metrics.total_queries += 1
        
        if not success:
            self.pool.metrics.failed_queries += 1
        
        # Update average query time using exponential moving average
        alpha = 0.1  # Smoothing factor
        if self.pool.metrics.avg_query_time == 0:
            self.pool.metrics.avg_query_time = execution_time
        else:
            self.pool.metrics.avg_query_time = (
                alpha * execution_time + 
                (1 - alpha) * self.pool.metrics.avg_query_time
            )


class DatabaseFactory:
    """Factory for creating database managers"""
    
    @staticmethod
    def create_database_manager(config: Dict[str, Any]) -> BioDatabaseManager:
        """Create database manager with configuration"""
        return BioDatabaseManager(config)
    
    @staticmethod
    def create_test_database_manager() -> BioDatabaseManager:
        """Create in-memory database for testing"""
        test_config = {
            'url': ':memory:',
            'min_connections': 1,
            'max_connections': 2,
            'slow_query_threshold': 0.1
        }
        return BioDatabaseManager(test_config)


# Database schema migration utilities
class DatabaseMigration:
    """Handle database schema migrations"""
    
    def __init__(self, db_manager: BioDatabaseManager):
        self.db_manager = db_manager
    
    async def get_current_version(self) -> int:
        """Get current schema version"""
        try:
            result = await self.db_manager.execute_query(
                "SELECT version FROM schema_version ORDER BY applied_at DESC LIMIT 1"
            )
            return result[0][0] if result else 0
        except:
            # Table doesn't exist, create it
            await self.db_manager.execute_query("""
                CREATE TABLE IF NOT EXISTS schema_version (
                    version INTEGER PRIMARY KEY,
                    applied_at TEXT NOT NULL,
                    description TEXT
                )
            """)
            return 0
    
    async def apply_migration(self, version: int, description: str, migration_sql: str) -> bool:
        """Apply a database migration"""
        try:
            current_version = await self.get_current_version()
            
            if version <= current_version:
                logger.info(f"Migration {version} already applied, skipping")
                return True
            
            # Apply migration
            await self.db_manager.execute_query(migration_sql)
            
            # Record migration
            await self.db_manager.execute_query(
                "INSERT INTO schema_version (version, applied_at, description) VALUES (?, ?, ?)",
                {
                    'version': version,
                    'applied_at': datetime.now().isoformat(),
                    'description': description
                }
            )
            
            logger.info(f"Applied migration {version}: {description}")
            return True
            
        except Exception as e:
            logger.error(f"Failed to apply migration {version}: {e}")
            return False