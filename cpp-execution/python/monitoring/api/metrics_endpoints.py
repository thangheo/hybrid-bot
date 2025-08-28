from fastapi import APIRouter, Depends, HTTPException, Query
from typing import List, Dict, Any, Optional
from pydantic import BaseModel
from datetime import datetime, timedelta
import time

from python.monitoring.system.metrics_processor import MetricsProcessor
from python.monitoring.utils.security import get_current_user, User
from python.monitoring.database.timeseries_db import TimeseriesDB

router = APIRouter()
metrics_processor = MetricsProcessor()
timeseries_db = TimeseriesDB()

class MetricValue(BaseModel):
    timestamp: datetime
    value: float
    tags: Dict[str, str] = {}

class MetricSeries(BaseModel):
    name: str
    data: List[MetricValue]
    unit: Optional[str] = None
    description: Optional[str] = None

class MetricInfo(BaseModel):
    name: str
    description: str
    unit: Optional[str] = None
    tags: List[str] = []

@router.get("/", response_model=List[MetricInfo])
async def get_available_metrics(
    type: Optional[str] = Query(None, description="Filter by metric type (system, trading, execution, risk)"),
    current_user: User = Depends(get_current_user)
):
    """
    Get a list of available metrics that can be queried.
    """
    metrics = await metrics_processor.get_available_metrics(type)
    return metrics

@router.get("/query", response_model=List[MetricSeries])
async def query_metrics(
    metrics: List[str] = Query(..., description="List of metric names to query"),
    start: Optional[datetime] = Query(None, description="Start time (defaults to 1 hour ago)"),
    end: Optional[datetime] = Query(None, description="End time (defaults to now)"),
    interval: Optional[str] = Query("1m", description="Aggregation interval (e.g., 1m, 5m, 1h)"),
    aggregation: Optional[str] = Query("avg", description="Aggregation function (avg, min, max, sum)"),
    tags: Optional[Dict[str, str]] = None,
    current_user: User = Depends(get_current_user)
):
    """
    Query time-series metrics with optional filtering and aggregation.
    """
    # Set default time range if not provided
    if start is None:
        start = datetime.now() - timedelta(hours=1)
    if end is None:
        end = datetime.now()
        
    result = await timeseries_db.query_metrics(
        metrics=metrics,
        start=start,
        end=end,
        interval=interval,
        aggregation=aggregation,
        tags=tags or {}
    )
    
    return result

@router.get("/system", response_model=Dict[str, Any])
async def get_system_metrics(
    current_user: User = Depends(get_current_user)
):
    """
    Get current system resource metrics (CPU, memory, disk, network).
    """
    metrics = await metrics_processor.get_system_metrics()
    return metrics

@router.get("/latency", response_model=Dict[str, Any])
async def get_latency_metrics(
    component: Optional[str] = Query(None, description="Filter by component"),
    percentile: List[float] = Query([50, 95, 99], description="Percentiles to calculate"),
    current_user: User = Depends(get_current_user)
):
    """
    Get latency metrics for different system components.
    """
    metrics = await metrics_processor.get_latency_metrics(component, percentile)
    return metrics

@router.get("/prometheus", response_model=str)
async def prometheus_metrics():
    """
    Endpoint for Prometheus to scrape metrics in its format.
    No authentication required as this is typically accessed by the Prometheus server.
    """
    metrics = await metrics_processor.get_prometheus_metrics()
    return metrics