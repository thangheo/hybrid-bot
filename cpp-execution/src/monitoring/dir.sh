#!/bin/bash

# Script to create the file structure for the Monitoring & Management component
# of the trading system.

# Exit on any error
set -e

echo "Creating directory structure for Monitoring & Management component..."

# Base directories
mkdir -p include/monitoring
mkdir -p src/monitoring/frontend/components
mkdir -p src/monitoring/db/schema
mkdir -p python/monitoring
mkdir -p config/monitoring
mkdir -p docs/api/monitoring
mkdir -p docker/monitoring
mkdir -p docker/kubernetes
mkdir -p tests/monitoring

# System Monitoring - Backend Components (C++)
echo "Creating System Monitoring C++ files..."

# Health Monitor
cat > include/monitoring/health_monitor.h << 'EOF'
/**
 * @file health_monitor.h
 * @brief Header defining the ComponentHealthMonitor class
 * 
 * This file contains interfaces for component status checking, health score calculation,
 * and component registration/deregistration methods.
 */

#pragma once

// TODO: Implement the ComponentHealthMonitor class definition
EOF

cat > src/monitoring/health_monitor.cpp << 'EOF'
/**
 * @file health_monitor.cpp
 * @brief Implementation of component health monitoring
 * 
 * This file implements health status collection from system components,
 * aggregation of health metrics into overall system health score.
 */

#include "monitoring/health_monitor.h"

// TODO: Implement the ComponentHealthMonitor class implementation
EOF

# Latency Tracker
cat > include/monitoring/latency_tracker.h << 'EOF'
/**
 * @file latency_tracker.h
 * @brief Header defining LatencyTracker class and latency measurement utilities
 * 
 * This file contains interfaces for start/end time recording across components,
 * sampling and statistical analysis methods.
 */

#pragma once

// TODO: Implement the LatencyTracker class definition
EOF

cat > src/monitoring/latency_tracker.cpp << 'EOF'
/**
 * @file latency_tracker.cpp
 * @brief Implementation of end-to-end and component-level latency tracking
 * 
 * This file implements support for high-precision timing using std::chrono,
 * calculation of percentiles and statistical metrics.
 */

#include "monitoring/latency_tracker.h"

// TODO: Implement the LatencyTracker class implementation
EOF

# Resource Monitor
cat > include/monitoring/resource_monitor.h << 'EOF'
/**
 * @file resource_monitor.h
 * @brief Header defining ResourceMonitor class
 *
 * This file contains interfaces for CPU, memory, network, and disk utilization monitoring,
 * threshold configuration and sampling interval settings.
 */

#pragma once

// TODO: Implement the ResourceMonitor class definition
EOF

cat > src/monitoring/resource_monitor.cpp << 'EOF'
/**
 * @file resource_monitor.cpp
 * @brief Implementation of system resource monitoring
 * 
 * This file implements integration with Linux /proc filesystem for resource metrics,
 * resource usage trend analysis and prediction.
 */

#include "monitoring/resource_monitor.h"

// TODO: Implement the ResourceMonitor class implementation
EOF

# Log Collector
cat > include/monitoring/log_collector.h << 'EOF'
/**
 * @file log_collector.h
 * @brief Header defining LogCollector class
 * 
 * This file contains interfaces for log aggregation and forwarding,
 * log level filtering and routing configurations.
 */

#pragma once

// TODO: Implement the LogCollector class definition
EOF

cat > src/monitoring/log_collector.cpp << 'EOF'
/**
 * @file log_collector.cpp
 * @brief Implementation of centralized logging collection
 * 
 * This file implements integration with ELK stack or similar logging infrastructure,
 * log rotation and archiving functionality.
 */

#include "monitoring/log_collector.h"

// TODO: Implement the LogCollector class implementation
EOF

# System Monitoring - Backend Components (Python)
echo "Creating System Monitoring Python files..."

cat > python/monitoring/health_metrics_processor.py << 'EOF'
"""
Health Metrics Processor

This module handles processing of health metrics for analysis and visualization,
historical health data storage and trend analysis, and integration with 
time-series database for metrics storage.
"""

# TODO: Implement health metrics processing functionality
EOF

cat > python/monitoring/latency_analyzer.py << 'EOF'
"""
Latency Analyzer

This module provides advanced analysis of latency data, detection of latency 
anomalies and patterns, and correlation of latency with system events and market conditions.
"""

# TODO: Implement latency analysis functionality
EOF

cat > python/monitoring/resource_analyzer.py << 'EOF'
"""
Resource Analyzer

This module provides advanced analysis of resource utilization patterns,
anomaly detection for resource usage, and resource optimization recommendations.
"""

# TODO: Implement resource analysis functionality
EOF

cat > python/monitoring/log_analyzer.py << 'EOF'
"""
Log Analyzer

This module provides log parsing and analysis, error pattern recognition,
and automated issue identification from logs.
"""

# TODO: Implement log analysis functionality
EOF

# System Monitoring - Frontend Components (Vue.js)
echo "Creating System Monitoring Vue.js files..."

cat > src/monitoring/frontend/components/HealthDashboard.vue << 'EOF'
<template>
  <!-- Component for displaying real-time system health status -->
  <!-- Visual indicators for component status (green/yellow/red) -->
  <!-- Interactive drill-down for component-specific health metrics -->
</template>

<script>
export default {
  name: 'HealthDashboard',
  // TODO: Implement component logic
}
</script>

<style scoped>
/* TODO: Implement component styling */
</style>
EOF

cat > src/monitoring/frontend/components/LatencyDashboard.vue << 'EOF'
<template>
  <!-- Real-time and historical latency visualization -->
  <!-- Latency breakdown by system component -->
  <!-- Configurable thresholds and alerts for latency spikes -->
</template>

<script>
export default {
  name: 'LatencyDashboard',
  // TODO: Implement component logic
}
</script>

<style scoped>
/* TODO: Implement component styling */
</style>
EOF

cat > src/monitoring/frontend/components/ResourceMonitorDashboard.vue << 'EOF'
<template>
  <!-- CPU, memory, network, and disk usage visualization -->
  <!-- Historical resource usage trends -->
  <!-- Resource bottleneck identification and alerts -->
</template>

<script>
export default {
  name: 'ResourceMonitorDashboard',
  // TODO: Implement component logic
}
</script>

<style scoped>
/* TODO: Implement component styling */
</style>
EOF

cat > src/monitoring/frontend/components/LogExplorer.vue << 'EOF'
<template>
  <!-- Interface for searching and filtering logs -->
  <!-- Log visualization with color-coding by severity -->
  <!-- Pattern highlighting and context display -->
</template>

<script>
export default {
  name: 'LogExplorer',
  // TODO: Implement component logic
}
</script>

<style scoped>
/* TODO: Implement component styling */
</style>
EOF

# Trading Performance Metrics - Backend Components (C++)
echo "Creating Trading Performance Metrics C++ files..."

# PnL Calculator
cat > include/monitoring/pnl_calculator.h << 'EOF'
/**
 * @file pnl_calculator.h
 * @brief Header defining PnLCalculator class
 * 
 * This file contains interfaces for real-time and historical P&L tracking,
 * methods for realized and unrealized P&L calculation.
 */

#pragma once

// TODO: Implement the PnLCalculator class definition
EOF

cat > src/monitoring/pnl_calculator.cpp << 'EOF'
/**
 * @file pnl_calculator.cpp
 * @brief Implementation of profit/loss calculations
 * 
 * This file implements integration with position tracking and market data,
 * currency conversion for multi-asset P&L aggregation.
 */

#include "monitoring/pnl_calculator.h"

// TODO: Implement the PnLCalculator class implementation
EOF

# Execution Analyzer
cat > include/monitoring/execution_analyzer.h << 'EOF'
/**
 * @file execution_analyzer.h
 * @brief Header defining ExecutionQualityAnalyzer class
 * 
 * This file contains interfaces for slippage calculation, fill rate tracking,
 * methods for execution time and cost analysis.
 */

#pragma once

// TODO: Implement the ExecutionQualityAnalyzer class definition
EOF

cat > src/monitoring/execution_analyzer.cpp << 'EOF'
/**
 * @file execution_analyzer.cpp
 * @brief Implementation of execution quality metrics
 * 
 * This file implements comparison of expected vs. actual execution prices,
 * analysis of execution speed and market impact.
 */

#include "monitoring/execution_analyzer.h"

// TODO: Implement the ExecutionQualityAnalyzer class implementation
EOF

# Strategy Performance
cat > include/monitoring/strategy_performance.h << 'EOF'
/**
 * @file strategy_performance.h
 * @brief Header defining StrategyPerformanceTracker class
 * 
 * This file contains interfaces for tracking returns, drawdowns, and risk-adjusted metrics,
 * methods for strategy comparison and attribution analysis.
 */

#pragma once

// TODO: Implement the StrategyPerformanceTracker class definition
EOF

cat > src/monitoring/strategy_performance.cpp << 'EOF'
/**
 * @file strategy_performance.cpp
 * @brief Implementation of strategy performance metrics
 * 
 * This file implements calculation of Sharpe ratio, Sortino ratio, and other performance metrics,
 * strategy-level P&L attribution and analysis.
 */

#include "monitoring/strategy_performance.h"

// TODO: Implement the StrategyPerformanceTracker class implementation
EOF

# Risk Metrics
cat > include/monitoring/risk_metrics.h << 'EOF'
/**
 * @file risk_metrics.h
 * @brief Header defining RiskMetricsCalculator class
 * 
 * This file contains interfaces for VaR, expected shortfall, and correlation analysis,
 * methods for risk factor decomposition.
 */

#pragma once

// TODO: Implement the RiskMetricsCalculator class definition
EOF

cat > src/monitoring/risk_metrics.cpp << 'EOF'
/**
 * @file risk_metrics.cpp
 * @brief Implementation of risk metric calculations
 * 
 * This file implements market stress test simulations,
 * portfolio risk analysis and decomposition.
 */

#include "monitoring/risk_metrics.h"

// TODO: Implement the RiskMetricsCalculator class implementation
EOF

# Trading Performance Metrics - Backend Components (Python)
echo "Creating Trading Performance Metrics Python files..."

cat > python/monitoring/pnl_analysis.py << 'EOF'
"""
P&L Analysis

This module provides advanced P&L analysis and visualization,
performance attribution by various factors, and historical P&L pattern analysis.
"""

# TODO: Implement P&L analysis functionality
EOF

cat > python/monitoring/execution_quality_analysis.py << 'EOF'
"""
Execution Quality Analysis

This module provides detailed analysis of execution quality metrics,
market impact model calibration, and execution venue comparison and optimization.
"""

# TODO: Implement execution quality analysis functionality
EOF

cat > python/monitoring/strategy_analyzer.py << 'EOF'
"""
Strategy Analyzer

This module provides strategy performance analysis and comparisons,
correlation of strategy returns with market factors, and strategy improvement recommendations.
"""

# TODO: Implement strategy analysis functionality
EOF

cat > python/monitoring/risk_analyzer.py << 'EOF'
"""
Risk Analyzer

This module provides advanced risk analysis and scenario testing,
risk factor sensitivity analysis, and risk model calibration and backtesting.
"""

# TODO: Implement risk analysis functionality
EOF

# Trading Performance Metrics - Frontend Components (Vue.js)
echo "Creating Trading Performance Metrics Vue.js files..."

cat > src/monitoring/frontend/components/PnLDashboard.vue << 'EOF'
<template>
  <!-- Real-time and historical P&L visualization -->
  <!-- P&L breakdown by strategy, asset, and time period -->
  <!-- Interactive P&L exploration and drill-down -->
</template>

<script>
export default {
  name: 'PnLDashboard',
  // TODO: Implement component logic
}
</script>

<style scoped>
/* TODO: Implement component styling */
</style>
EOF

cat > src/monitoring/frontend/components/ExecutionQualityDashboard.vue << 'EOF'
<template>
  <!-- Visualization of execution quality metrics -->
  <!-- Slippage analysis by exchange, asset, and time -->
  <!-- Fill rate and execution time monitoring -->
</template>

<script>
export default {
  name: 'ExecutionQualityDashboard',
  // TODO: Implement component logic
}
</script>

<style scoped>
/* TODO: Implement component styling */
</style>
EOF

cat > src/monitoring/frontend/components/StrategyPerformanceDashboard.vue << 'EOF'
<template>
  <!-- Strategy returns and risk-adjusted metrics display -->
  <!-- Comparative strategy performance visualization -->
  <!-- Drawdown visualization and analysis -->
</template>

<script>
export default {
  name: 'StrategyPerformanceDashboard',
  // TODO: Implement component logic
}
</script>

<style scoped>
/* TODO: Implement component styling */
</style>
EOF

cat > src/monitoring/frontend/components/RiskMetricsDashboard.vue << 'EOF'
<template>
  <!-- VaR and expected shortfall visualization -->
  <!-- Risk exposure breakdown by factors -->
  <!-- Correlation matrix visualization and analysis -->
</template>

<script>
export default {
  name: 'RiskMetricsDashboard',
  // TODO: Implement component logic
}
</script>

<style scoped>
/* TODO: Implement component styling */
</style>
EOF

# Alerting System - Backend Components (C++)
echo "Creating Alerting System C++ files..."

# Alert Manager
cat > include/monitoring/alert_manager.h << 'EOF'
/**
 * @file alert_manager.h
 * @brief Header defining AlertManager class
 * 
 * This file contains interfaces for alert configuration, triggering, and notification,
 * alert severity levels and categorization.
 */

#pragma once

// TODO: Implement the AlertManager class definition
EOF

cat > src/monitoring/alert_manager.cpp << 'EOF'
/**
 * @file alert_manager.cpp
 * @brief Implementation of alert detection and notification
 * 
 * This file implements integration with various notification channels (email, SMS, etc.),
 * alert throttling and deduplication logic.
 */

#include "monitoring/alert_manager.h"

// TODO: Implement the AlertManager class implementation
EOF

# Escalation Policy
cat > include/monitoring/escalation_policy.h << 'EOF'
/**
 * @file escalation_policy.h
 * @brief Header defining EscalationPolicy class
 * 
 * This file contains interfaces for defining tiered notification policies,
 * methods for escalation path configuration.
 */

#pragma once

// TODO: Implement the EscalationPolicy class definition
EOF

cat > src/monitoring/escalation_policy.cpp << 'EOF'
/**
 * @file escalation_policy.cpp
 * @brief Implementation of tiered notification escalation
 * 
 * This file implements time-based escalation rules,
 * acknowledgment and resolution tracking.
 */

#include "monitoring/escalation_policy.h"

// TODO: Implement the EscalationPolicy class implementation
EOF

# OnCall Manager
cat > include/monitoring/oncall_manager.h << 'EOF'
/**
 * @file oncall_manager.h
 * @brief Header defining OnCallManager class
 * 
 * This file contains interfaces for on-call schedule management,
 * methods for determining current on-call personnel.
 */

#pragma once

// TODO: Implement the OnCallManager class definition
EOF

cat > src/monitoring/oncall_manager.cpp << 'EOF'
/**
 * @file oncall_manager.cpp
 * @brief Implementation of on-call rotation management
 * 
 * This file implements integration with external scheduling systems,
 * availability and backup personnel tracking.
 */

#include "monitoring/oncall_manager.h"

// TODO: Implement the OnCallManager class implementation
EOF

# Incident Manager
cat > include/monitoring/incident_manager.h << 'EOF'
/**
 * @file incident_manager.h
 * @brief Header defining IncidentManager class
 * 
 * This file contains interfaces for incident tracking and resolution,
 * methods for incident prioritization and assignment.
 */

#pragma once

// TODO: Implement the IncidentManager class definition
EOF

cat > src/monitoring/incident_manager.cpp << 'EOF'
/**
 * @file incident_manager.cpp
 * @brief Implementation of incident response coordination
 * 
 * This file implements integration with incident response workflows,
 * post-incident analysis and reporting.
 */

#include "monitoring/incident_manager.h"

// TODO: Implement the IncidentManager class implementation
EOF

# Alerting System - Backend Components (Python)
echo "Creating Alerting System Python files..."

cat > python/monitoring/alert_analyzer.py << 'EOF'
"""
Alert Analyzer

This module provides analysis of alert patterns and frequencies,
alert noise reduction algorithms, and correlation of alerts with system conditions.
"""

# TODO: Implement alert analysis functionality
EOF

cat > python/monitoring/escalation_analyzer.py << 'EOF'
"""
Escalation Analyzer

This module provides analysis of escalation effectiveness,
response time tracking and optimization, and escalation path optimization recommendations.
"""

# TODO: Implement escalation analysis functionality
EOF

cat > python/monitoring/oncall_optimizer.py << 'EOF'
"""
On-Call Optimizer

This module provides on-call schedule optimization,
workload balancing and fairness analysis, and on-call burden analysis and recommendations.
"""

# TODO: Implement on-call optimization functionality
EOF

cat > python/monitoring/incident_analyzer.py << 'EOF'
"""
Incident Analyzer

This module provides incident pattern analysis,
mean time to resolution analysis, and incident impact assessment.
"""

# TODO: Implement incident analysis functionality
EOF

# Alerting System - Frontend Components (Vue.js)
echo "Creating Alerting System Vue.js files..."

cat > src/monitoring/frontend/components/AlertDashboard.vue << 'EOF'
<template>
  <!-- Real-time alert visualization and management -->
  <!-- Alert filtering and sorting -->
  <!-- Alert acknowledgment and resolution interface -->
</template>

<script>
export default {
  name: 'AlertDashboard',
  // TODO: Implement component logic
}
</script>

<style scoped>
/* TODO: Implement component styling */
</style>
EOF

cat > src/monitoring/frontend/components/EscalationPolicyEditor.vue << 'EOF'
<template>
  <!-- Interface for configuring escalation policies -->
  <!-- Visual escalation path editor -->
  <!-- Testing tools for escalation policies -->
</template>

<script>
export default {
  name: 'EscalationPolicyEditor',
  // TODO: Implement component logic
}
</script>

<style scoped>
/* TODO: Implement component styling */
</style>
EOF

cat > src/monitoring/frontend/components/OnCallScheduler.vue << 'EOF'
<template>
  <!-- On-call schedule visualization and management -->
  <!-- Shift swapping and override interface -->
  <!-- Availability and coverage visualization -->
</template>

<script>
export default {
  name: 'OnCallScheduler',
  // TODO: Implement component logic
}
</script>

<style scoped>
/* TODO: Implement component styling */
</style>
EOF

cat > src/monitoring/frontend/components/IncidentDashboard.vue << 'EOF'
<template>
  <!-- Active incident tracking and management -->
  <!-- Incident history and resolution metrics -->
  <!-- Post-incident review documentation -->
</template>

<script>
export default {
  name: 'IncidentDashboard',
  // TODO: Implement component logic
}
</script>

<style scoped>
/* TODO: Implement component styling */
</style>
EOF

# Configuration Files
echo "Creating Configuration files..."

cat > config/monitoring/system_monitoring.yaml << 'EOF'
# System Monitoring Configuration

# Component health monitoring configuration
component_health:
  check_interval_sec: 60
  health_score_threshold: 0.7
  components:
    # TODO: Add component-specific health check configurations

# Latency tracking configuration
latency_tracking:
  enabled: true
  sample_rate: 0.1
  percentiles:
    - 50
    - 90
    - 99
    - 99.9
  # TODO: Add component-specific latency tracking configurations

# Resource utilization thresholds and monitoring intervals
resource_monitoring:
  enabled: true
  interval_sec: 30
  cpu_threshold_percent: 80
  memory_threshold_percent: 85
  disk_threshold_percent: 90
  network_threshold_mbps: 800
  # TODO: Add additional resource monitoring configurations

# Log collection and forwarding settings
log_collection:
  enabled: true
  log_levels:
    - ERROR
    - WARNING
    - INFO
  retention_days: 30
  # TODO: Add log collection endpoints and routing configurations
EOF

cat > config/monitoring/trading_metrics.yaml << 'EOF'
# Trading Metrics Configuration

# P&L calculation settings
pnl_calculation:
  update_interval_sec: 5
  historical_storage_days: 90
  realized_pnl_calculation_method: FIFO
  # TODO: Add additional P&L calculation settings

# Execution quality metrics configuration
execution_quality:
  metrics:
    - slippage
    - fill_rate
    - execution_time
    - market_impact
  comparison_baseline: midPrice
  # TODO: Add additional execution quality settings

# Strategy performance monitoring parameters
strategy_performance:
  metrics:
    - returns
    - sharpe_ratio
    - sortino_ratio
    - maxDrawdown
    - win_loss_ratio
  performance_window_days: 30
  # TODO: Add additional strategy performance settings

# Risk metric calculation parameters
risk_metrics:
  var_confidence_level: 0.95
  var_time_horizon_days: 1
  stress_test_scenarios:
    - market_crash
    - liquidity_crisis
    - correlation_breakdown
  # TODO: Add additional risk metric settings
EOF

cat > config/monitoring/alerts.yaml << 'EOF'
# Alerting Configuration

# Alert definitions and thresholds
alert_definitions:
  system_health:
    - name: component_down
      description: "System component is down or unresponsive"
      severity: critical
      conditions:
        - metric: component.health
          operator: "=="
          value: 0
      # TODO: Add more system health alerts
      
  performance:
    - name: high_latency
      description: "System latency exceeds threshold"
      severity: warning
      conditions:
        - metric: latency.p99
          operator: ">"
          value: 1000
      # TODO: Add more performance alerts
  
  trading:
    - name: large_drawdown
      description: "Strategy experiencing significant drawdown"
      severity: warning
      conditions:
        - metric: strategy.drawdown
          operator: ">"
          value: 0.1
      # TODO: Add more trading alerts
      
  resources:
    - name: high_cpu_usage
      description: "CPU usage above threshold"
      severity: warning
      conditions:
        - metric: system.cpu_usage
          operator: ">"
          value: 0.85
      # TODO: Add more resource alerts

# Notification channel configuration
notification_channels:
  email:
    enabled: true
    smtp_server: "smtp.example.com"
    smtp_port: 587
    # TODO: Complete email configuration
    
  slack:
    enabled: true
    webhook_url: "https://hooks.slack.com/services/XXX/YYY/ZZZ"
    # TODO: Complete Slack configuration
    
  sms:
    enabled: true
    provider: "twilio"
    # TODO: Complete SMS configuration

# Alert grouping and deduplication settings
alert_grouping:
  enabled: true
  group_by:
    - source
    - severity
  deduplication_window_sec: 300
  # TODO: Add more grouping settings

# Alert severity levels and categories
severity_levels:
  critical:
    color: "#FF0000"
    notification_channels:
      - email
      - slack
      - sms
  warning:
    color: "#FFCC00"
    notification_channels:
      - slack
  info:
    color: "#0066CC"
    notification_channels:
      - slack
EOF

cat > config/monitoring/escalation.yaml << 'EOF'
# Escalation Configuration

# Escalation policy definitions
escalation_policies:
  - name: critical_system_issues
    description: "Escalation policy for critical system issues"
    steps:
      - level: 1
        wait_minutes: 5
        notify:
          - primary_oncall
      - level: 2
        wait_minutes: 15
        notify:
          - secondary_oncall
      - level: 3
        wait_minutes: 30
        notify:
          - team_manager
          - lead_developer
    # TODO: Add more escalation policies

# On-call rotation schedules
oncall_schedules:
  - name: primary_oncall
    rotation_days: 7
    start_day: Monday
    handover_time: "09:00"
    time_zone: "UTC"
    members:
      # TODO: Add team members
  - name: secondary_oncall
    rotation_days: 7
    start_day: Monday
    handover_time: "09:00"
    time_zone: "UTC"
    members:
      # TODO: Add team members

# Contact information for team members
team_members:
  # TODO: Add team member contact information

# Incident response procedures
incident_procedures:
  - type: system_outage
    steps:
      - "Verify the outage scope and affected components"
      - "Notify all stakeholders"
      - "Begin recovery procedures according to playbook"
      # TODO: Add more procedural steps
  - type: security_breach
    steps:
      - "Isolate affected systems"
      - "Preserve evidence"
      - "Notify security team"
      # TODO: Add more procedural steps
  # TODO: Add more incident types and procedures
EOF

# API Documentation
echo "Creating API Documentation files..."

cat > docs/api/monitoring/health_api.md << 'EOF'
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
EOF

cat > docs/api/monitoring/metrics_api.md << 'EOF'
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
- `tradingPair` (optional): Filter by trading pair

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