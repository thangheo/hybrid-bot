# Health Monitoring API

This document describes the API endpoints for health monitoring in the trading system.

## Authentication

All API requests require authentication using JWT tokens.

## Endpoints

### Get System Health Status

Retrieves the current health status of the system.

**URL:** `/api/v1/monitoring/health`

**Method:** `GET`

**Query Parameters:**
- `component` (optional): Filter by component name
- `detailed` (optional): Boolean flag to include detailed metrics (default: false)

**Response:**
```json
{
  "system_health": 0.95,
  "components": [
    {
      "name": "data_ingestion",
      "health": 1.0,
      "status": "healthy",
      "last_updated": "2025-04-29T10:15:30Z"
    },
    {
      "name": "strategy_engine",
      "health": 0.85,
      "status": "degraded",
      "last_updated": "2025-04-29T10:15:28Z",
      "issues": [
        {
          "description": "High latency detected",
          "severity": "warning"
        }
      ]
    }
  ]
}
```

### Get Component Health Details

Retrieves detailed health information for a specific component.

**URL:** `/api/v1/monitoring/health/{component}`

**Method:** `GET`

**Response:**
```json
{
  "name": "strategy_engine",
  "health": 0.85,
  "status": "degraded",
  "last_updated": "2025-04-29T10:15:28Z",
  "metrics": {
    "cpu_usage": 0.75,
    "memory_usage": 0.68,
    "error_rate": 0.02,
    "response_time_ms": 125
  },
  "issues": [
    {
      "description": "High latency detected",
      "severity": "warning",
      "detected_at": "2025-04-29T10:10:15Z"
    }
  ],
  "dependencies": [
    {
      "name": "data_ingestion",
      "status": "healthy"
    },
    {
      "name": "execution_engine",
      "status": "healthy"
    }
  ]
}
```

### Update Component Health Status

Updates the health status of a component (for internal use).

**URL:** `/api/v1/monitoring/health/{component}`

**Method:** `POST`

**Request Body:**
```json
{
  "health": 0.9,
  "metrics": {
    "cpu_usage": 0.65,
    "memory_usage": 0.72,
    "error_rate": 0.01,
    "response_time_ms": 110
  },
  "issues": [
    {
      "description": "Minor latency increase",
      "severity": "info"
    }
  ]
}
```

**Response:**
```json
{
  "success": true,
  "updated_at": "2025-04-29T10:20:15Z"
}
```

## Rate Limiting

Health API endpoints are rate limited to 60 requests per minute per client.
