-- Merged schema.sql
-- Create extensions
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";

-- Create schemas
CREATE SCHEMA IF NOT EXISTS trading;
CREATE SCHEMA IF NOT EXISTS config;
CREATE SCHEMA IF NOT EXISTS metrics;
CREATE SCHEMA IF NOT EXISTS monitoring;

-- Create tables for strategy parameters
CREATE TABLE IF NOT EXISTS config.strategy_parameters (
    id SERIAL PRIMARY KEY,
    strategy_id VARCHAR(100) NOT NULL,
    parameter_group VARCHAR(100) NOT NULL,
    version VARCHAR(50) NOT NULL,
    parameters JSONB NOT NULL,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    active BOOLEAN DEFAULT TRUE,
    UNIQUE(strategy_id, parameter_group, version)
);

-- Create table for risk parameters
CREATE TABLE IF NOT EXISTS config.risk_parameters (
    id SERIAL PRIMARY KEY,
    exchange_id VARCHAR(50) NOT NULL,
    trading_pair VARCHAR(30) NOT NULL,
    strategy_id VARCHAR(100) NOT NULL,
    max_position_size NUMERIC NOT NULL,
    max_order_size NUMERIC NOT NULL,
    max_drawdown NUMERIC NOT NULL,
    max_exposure NUMERIC NOT NULL,
    additional_parameters JSONB,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    UNIQUE(exchange_id, trading_pair, strategy_id)
);

-- Configuration table for system settings
CREATE TABLE IF NOT EXISTS config.configuration (
    key TEXT PRIMARY KEY,
    value JSONB NOT NULL,
    description TEXT,
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    updated_by TEXT
);

-- Create tables for position tracking
CREATE TABLE IF NOT EXISTS trading.positions (
    id SERIAL PRIMARY KEY,
    exchange_id VARCHAR(50) NOT NULL,
    trading_pair VARCHAR(30) NOT NULL,
    strategy_id VARCHAR(100) NOT NULL,
    asset VARCHAR(20) NOT NULL,
    quantity NUMERIC NOT NULL,
    average_entry_price NUMERIC NOT NULL,
    current_price NUMERIC,
    unrealized_pnl NUMERIC,
    realized_pnl NUMERIC DEFAULT 0,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    UNIQUE(exchange_id, trading_pair, strategy_id, asset)
);

-- Create tables for order tracking
CREATE TABLE IF NOT EXISTS trading.orders (
    id SERIAL PRIMARY KEY,
    client_order_id VARCHAR(100) NOT NULL UNIQUE,
    exchange_order_id VARCHAR(100),
    exchange_id VARCHAR(50) NOT NULL,
    trading_pair VARCHAR(30) NOT NULL,
    strategy_id VARCHAR(100) NOT NULL,
    signal_id VARCHAR(100),
    side VARCHAR(10) NOT NULL,
    order_type VARCHAR(20) NOT NULL,
    quantity NUMERIC NOT NULL,
    price NUMERIC,
    filled_quantity NUMERIC DEFAULT 0,
    average_fill_price NUMERIC,
    status VARCHAR(20) NOT NULL,
    time_in_force VARCHAR(10),
    created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT NOW()
);

-- Create table for order fills
CREATE TABLE IF NOT EXISTS trading.fills (
    id SERIAL PRIMARY KEY,
    order_id INTEGER REFERENCES trading.orders(id),
    fill_id VARCHAR(100) NOT NULL,
    quantity NUMERIC NOT NULL,
    price NUMERIC NOT NULL,
    fee NUMERIC,
    fee_currency VARCHAR(20),
    timestamp TIMESTAMP WITH TIME ZONE NOT NULL,
    UNIQUE(order_id, fill_id)
);

-- Alerts table for monitoring
CREATE TABLE IF NOT EXISTS monitoring.alerts (
    alert_id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    severity TEXT NOT NULL,
    source TEXT NOT NULL,
    description TEXT NOT NULL,
    state TEXT NOT NULL,
    created_at TIMESTAMP WITH TIME ZONE NOT NULL,
    updated_at TIMESTAMP WITH TIME ZONE NOT NULL,
    acknowledged_at TIMESTAMP WITH TIME ZONE,
    resolved_at TIMESTAMP WITH TIME ZONE,
    assigned_to TEXT,
    tags JSONB DEFAULT '{}',
    metrics JSONB DEFAULT '{}'
);

-- Incidents table for tracking issues
CREATE TABLE IF NOT EXISTS monitoring.incidents (
    incident_id TEXT PRIMARY KEY,
    title TEXT NOT NULL,
    description TEXT NOT NULL,
    state TEXT NOT NULL,
    created_at TIMESTAMP WITH TIME ZONE NOT NULL,
    updated_at TIMESTAMP WITH TIME ZONE NOT NULL,
    resolved_at TIMESTAMP WITH TIME ZONE,
    closed_at TIMESTAMP WITH TIME ZONE,
    severity TEXT NOT NULL,
    owner TEXT,
    alerts JSONB DEFAULT '[]',
    tags JSONB DEFAULT '{}',
    timeline JSONB DEFAULT '[]'
);

-- Notification settings table
CREATE TABLE IF NOT EXISTS monitoring.notification_settings (
    id SERIAL PRIMARY KEY,
    user_id TEXT NOT NULL,
    channel TEXT NOT NULL,
    enabled BOOLEAN DEFAULT TRUE,
    config JSONB DEFAULT '{}',
    UNIQUE(user_id, channel)
);

-- On-call schedule table
CREATE TABLE IF NOT EXISTS monitoring.oncall_schedule (
    id SERIAL PRIMARY KEY,
    role TEXT NOT NULL,
    user_id TEXT NOT NULL,
    start_time TIMESTAMP WITH TIME ZONE NOT NULL,
    end_time TIMESTAMP WITH TIME ZONE NOT NULL
);

-- Create indexes for common queries
-- Trading indexes
CREATE INDEX idx_orders_strategy ON trading.orders(strategy_id, exchange_id, trading_pair);
CREATE INDEX idx_orders_status ON trading.orders(status);
CREATE INDEX idx_orders_timestamps ON trading.orders(created_at, updated_at);

-- Monitoring indexes
CREATE INDEX idx_alerts_state ON monitoring.alerts(state);
CREATE INDEX idx_alerts_severity ON monitoring.alerts(severity);
CREATE INDEX idx_alerts_created_at ON monitoring.alerts(created_at);

CREATE INDEX idx_incidents_state ON monitoring.incidents(state);
CREATE INDEX idx_incidents_severity ON monitoring.incidents(severity);
CREATE INDEX idx_incidents_created_at ON monitoring.incidents(created_at);

CREATE INDEX idx_oncall_schedule_active ON monitoring.oncall_schedule(role, start_time, end_time);

-- Create a view for active on-call assignments
CREATE OR REPLACE VIEW monitoring.active_oncall AS
SELECT role, user_id, start_time, end_time
FROM monitoring.oncall_schedule
WHERE start_time <= NOW() AND end_time > NOW();

-- Add sample configuration data
INSERT INTO config.configuration (key, value, description)
VALUES 
    ('system.thresholds', '{"cpu": 90, "memory": 85, "disk": 90}', 'System resource thresholds'),
    ('alerts.notification_channels', '{"email": {"enabled": true}, "slack": {"enabled": true}}', 'Alert notification channels')
ON CONFLICT (key) DO NOTHING;

-- Create roles and permissions
CREATE ROLE trading_read WITH LOGIN PASSWORD 'read_password';
CREATE ROLE trading_write WITH LOGIN PASSWORD 'write_password';
CREATE ROLE monitoring_read WITH LOGIN PASSWORD 'monitor_read_password';
CREATE ROLE monitoring_write WITH LOGIN PASSWORD 'monitor_write_password';

-- Grant permissions to trading roles
GRANT USAGE ON SCHEMA trading, config, metrics TO trading_read;
GRANT SELECT ON ALL TABLES IN SCHEMA trading, config, metrics TO trading_read;

GRANT USAGE ON SCHEMA trading, config, metrics TO trading_write;
GRANT ALL PRIVILEGES ON ALL TABLES IN SCHEMA trading, config, metrics TO trading_write;
GRANT USAGE ON ALL SEQUENCES IN SCHEMA trading, config, metrics TO trading_write;

-- Grant permissions to monitoring roles
GRANT USAGE ON SCHEMA monitoring, metrics TO monitoring_read;
GRANT SELECT ON ALL TABLES IN SCHEMA monitoring, metrics TO monitoring_read;

GRANT USAGE ON SCHEMA monitoring, metrics TO monitoring_write;
GRANT ALL PRIVILEGES ON ALL TABLES IN SCHEMA monitoring, metrics TO monitoring_write;
GRANT USAGE ON ALL SEQUENCES IN SCHEMA monitoring, metrics TO monitoring_write;