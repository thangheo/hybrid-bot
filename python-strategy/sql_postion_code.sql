-- ================================================================
-- DATABASE SCHEMA: Position & Trade Tracking
-- ================================================================

-- 1. Positions table - current holdings
CREATE TABLE positions (
    id SERIAL PRIMARY KEY,
    symbol VARCHAR(20) NOT NULL,
    quantity DECIMAL(20,8) NOT NULL,
    avg_price DECIMAL(20,8) NOT NULL,
    market_value DECIMAL(20,8) NOT NULL,
    unrealized_pnl DECIMAL(20,8) DEFAULT 0,
    last_updated TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(symbol)
);

-- 2. Trades table - all trade history
CREATE TABLE trades (
    id SERIAL PRIMARY KEY,
    trade_id VARCHAR(50) UNIQUE NOT NULL,
    symbol VARCHAR(20) NOT NULL,
    side VARCHAR(10) NOT NULL, -- BUY/SELL
    quantity DECIMAL(20,8) NOT NULL,
    price DECIMAL(20,8) NOT NULL,
    value DECIMAL(20,8) NOT NULL,
    commission DECIMAL(20,8) DEFAULT 0,
    strategy_signal DECIMAL(5,2) DEFAULT 0,
    executed_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    exchange_order_id VARCHAR(100),
    status VARCHAR(20) DEFAULT 'FILLED'
);

-- 3. Portfolio snapshots - daily portfolio values
CREATE TABLE portfolio_snapshots (
    id SERIAL PRIMARY KEY,
    snapshot_date DATE NOT NULL,
    total_value DECIMAL(20,8) NOT NULL,
    cash_balance DECIMAL(20,8) NOT NULL,
    positions_value DECIMAL(20,8) NOT NULL,
    daily_pnl DECIMAL(20,8) DEFAULT 0,
    num_positions INTEGER DEFAULT 0,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(snapshot_date)
);

-- Indexes for performance
CREATE INDEX idx_positions_symbol ON positions(symbol);
CREATE INDEX idx_trades_symbol ON trades(symbol);
CREATE INDEX idx_trades_executed_at ON trades(executed_at);
CREATE INDEX idx_portfolio_snapshots_date ON portfolio_snapshots(snapshot_date);
