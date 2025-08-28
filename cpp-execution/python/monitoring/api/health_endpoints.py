from fastapi import APIRouter, Depends, HTTPException, Query
from typing import List, Dict, Any, Optional
from pydantic import BaseModel
from datetime import datetime

from python.monitoring.system.health_monitor import HealthMonitor
from python.monitoring.utils.security import get_current_user, User

router = APIRouter()
health_monitor = HealthMonitor()

class HealthStatus(BaseModel):
    status: str  # "healthy", "degraded", "unhealthy"
    timestamp: datetime
    components: Dict[str, Dict[str, Any]]
    message: Optional[str] = None

class ComponentHealth(BaseModel):
    name: str
    status: str
    details: Dict[str, Any]
    last_checked: datetime

@router.get("/status", response_model=HealthStatus)
async def get_system_health(current_user: User = Depends(get_current_user)):
    """
    Get the overall health status of the trading system.
    """
    health_data = await health_monitor.get_system_health()
    return health_data

@router.get("/components", response_model=List[ComponentHealth])
async def get_component_health(
    component: Optional[str] = Query(None, description="Filter by component name"),
    current_user: User = Depends(get_current_user)
):
    """
    Get health status of individual system components.
    Optionally filter by component name.
    """
    components = await health_monitor.get_component_health(component)
    return components

@router.get("/issues", response_model=List[Dict[str, Any]])
async def get_health_issues(
    severity: Optional[str] = Query(None, description="Filter by severity (low, medium, high, critical)"),
    component: Optional[str] = Query(None, description="Filter by component name"),
    limit: int = Query(10, ge=1, le=100, description="Maximum number of issues to return"),
    current_user: User = Depends(get_current_user)
):
    """
    Get current health issues detected in the system.
    """
    issues = await health_monitor.get_health_issues(severity, component, limit)
    return issues

@router.post("/check", response_model=HealthStatus)
async def trigger_health_check(current_user: User = Depends(get_current_user)):
    """
    Trigger an immediate health check of all system components.
    """
    health_data = await health_monitor.run_health_check()
    return health_data