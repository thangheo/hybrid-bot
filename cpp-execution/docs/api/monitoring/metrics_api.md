# Performance Metrics API

This document describes the API endpoints for accessing performance metrics in the trading system.

## Authentication

All API requests require authentication using JWT tokens.

## Endpoints

### Get Performance Metrics

Retrieves performance metrics based on specified parameters.

**URL:** `/api/v1/monitoring/metrics`

**Method:** `GET`

**Query Parameters:**
- `metric_type`: Type of metrics to retrieve (system, trading, execution)
- `start_time`: Start time for the metrics query (ISO 8601 format)
- `end_time`: End time for the metrics query (ISO 8601 format)
- `interval`: Time interval for data aggregation (e.g., 1m, 5m, 1h, 1d)
- `component` (optional): Filter by component name
- `strategy` (optional): Filter by strategy name
- `trading_pair` (optional): Filter by trading pair

**Response:**
```json
{
  "metric_type": "trading",
  "start_time": "2025-04-28T00:00:00Z",
  "end_time": "2025-04-29T00:00:00Z",
  "interval": "1h",
  "data": [
    {
      "timestamp": "2025-04-28T00:00:00Z",
      "metrics": {
        "pnl": 1250.75,
        "trading_volume": 45000.00,
        "trade_count": 28,
        "win_rate": 0.68
      }
    },
    {
      "timestamp": "2025-04-28T01:00:00Z",
      "metrics": {
        "pnl": 980.25,
        "trading_volume": 38500.00,
        "trade_count": 24,
        "win_rate": 0.62
      }
    }
  ]
}
```

### Get Specific Metric History

Retrieves historical data for a specific metric.

**URL:** `/api/v1/monitoring/metrics/{metric_name}`

**Method:** `GET`

**Query Parameters:**
- `start_time`: Start time for the metrics query (ISO 8601 format)
