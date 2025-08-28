from fastapi import APIRouter, Depends, HTTPException, Query, Path, Body, status
from typing import List, Dict, Any, Optional
from pydantic import BaseModel, Field
from datetime import datetime
from enum import Enum

from python.monitoring.alerts.alert_manager import AlertManager
from python.monitoring.alerts.escalation_policy import EscalationPolicyManager
from python.monitoring.utils.security import get_current_user, User, admin_required

router = APIRouter()
alert_manager = AlertManager()
escalation_policy_manager = EscalationPolicyManager()

class AlertSeverity(str, Enum):
    low = "low"
    medium = "medium"
    high = "high"
    critical = "critical"

class AlertStatus(str, Enum):
    firing = "firing"
    acknowledged = "acknowledged"
    resolved = "resolved"
    silenced = "silenced"

class Alert(BaseModel):
    id: str
    name: str
    description: str
    severity: AlertSeverity
    status: AlertStatus
    source: str
    created_at: datetime
    updated_at: datetime
    acknowledged_at: Optional[datetime] = None
    resolved_at: Optional[datetime] = None
    acknowledged_by: Optional[str] = None
    silenced_until: Optional[datetime] = None
    tags: Dict[str, str] = {}
    metric_value: Optional[float] = None
    details: Dict[str, Any] = {}

class AlertUpdate(BaseModel):
    status: Optional[AlertStatus] = None
    comment: Optional[str] = None
    silence_duration_minutes: Optional[int] = None

class AlertRule(BaseModel):
    id: str
    name: str
    description: str
    query: str
    threshold: float
    comparison: str  # "gt", "lt", "gte", "lte", "eq", "neq"
    severity: AlertSeverity
    enabled: bool
    evaluation_interval: str  # e.g., "1m", "5m"
    for_duration: str  # e.g., "5m", "1h"
    labels: Dict[str, str] = {}
    annotations: Dict[str, str] = {}
    notification_channels: List[str] = []

class EscalationPolicy(BaseModel):
    id: str
    name: str
    description: str
    steps: List[Dict[str, Any]]
    repeat_interval: Optional[int] = None
    auto_close: bool = False

@router.get("/", response_model=List[Alert])
async def get_alerts(
    status: Optional[List[AlertStatus]] = Query(None, description="Filter by alert status"),
    severity: Optional[List[AlertSeverity]] = Query(None, description="Filter by alert severity"),
    from_date: Optional[datetime] = Query(None, description="Filter alerts from this date"),
    to_date: Optional[datetime] = Query(None, description="Filter alerts up to this date"),
    source: Optional[str] = Query(None, description="Filter by alert source"),
    tags: Optional[Dict[str, str]] = None,
    limit: int = Query(100, ge=1, le=1000, description="Maximum number of alerts to return"),
    current_user: User = Depends(get_current_user)
):
    """
    Get a list of alerts with optional filtering.
    """
    alerts = await alert_manager.get_alerts(
        status=status,
        severity=severity,
        from_date=from_date,
        to_date=to_date,
        source=source,
        tags=tags,
        limit=limit
    )
    return alerts

@router.get("/{alert_id}", response_model=Alert)
async def get_alert(
    alert_id: str = Path(..., description="The ID of the alert to retrieve"),
    current_user: User = Depends(get_current_user)
):
    """
    Get details for a specific alert by ID.
    """
    alert = await alert_manager.get_alert(alert_id)
    if not alert:
        raise HTTPException(status_code=404, detail="Alert not found")
    return alert

@router.put("/{alert_id}", response_model=Alert)
async def update_alert(
    alert_id: str = Path(..., description="The ID of the alert to update"),
    update: AlertUpdate = Body(..., description="Alert update data"),
    current_user: User = Depends(get_current_user)
):
    """
    Update an alert (acknowledge, resolve, silence).
    """
    updated_alert = await alert_manager.update_alert(
        alert_id=alert_id,
        status=update.status,
        user_id=current_user.id,
        user_name=current_user.name,
        comment=update.comment,
        silence_duration=update.silence_duration_minutes
    )
    if not updated_alert:
        raise HTTPException(status_code=404, detail="Alert not found")
    return updated_alert

@router.get("/rules", response_model=List[AlertRule])
async def get_alert_rules(
    enabled: Optional[bool] = Query(None, description="Filter by enabled status"),
    severity: Optional[List[AlertSeverity]] = Query(None, description="Filter by severity"),
    current_user: User = Depends(get_current_user)
):
    """
    Get a list of alert rules with optional filtering.
    """
    rules = await alert_manager.get_alert_rules(enabled=enabled, severity=severity)
    return rules

@router.get("/rules/{rule_id}", response_model=AlertRule)
async def get_alert_rule(
    rule_id: str = Path(..., description="The ID of the alert rule to retrieve"),
    current_user: User = Depends(get_current_user)
):
    """
    Get details for a specific alert rule by ID.
    """
    rule = await alert_manager.get_alert_rule(rule_id)
    if not rule:
        raise HTTPException(status_code=404, detail="Alert rule not found")
    return rule

@router.post("/rules", response_model=AlertRule, status_code=status.HTTP_201_CREATED)
async def create_alert_rule(
    rule: AlertRule = Body(..., description="Alert rule to create"),
    current_user: User = Depends(admin_required)
):
    """
    Create a new alert rule.
    """
    created_rule = await alert_manager.create_alert_rule(rule)
    return created_rule

@router.put("/rules/{rule_id}", response_model=AlertRule)
async def update_alert_rule(
    rule_id: str = Path(..., description="The ID of the alert rule to update"),
    rule: AlertRule = Body(..., description="Updated alert rule"),
    current_user: User = Depends(admin_required)
):
    """
    Update an existing alert rule.
    """
    if rule_id != rule.id:
        raise HTTPException(status_code=400, detail="Alert rule ID mismatch")
    
    updated_rule = await alert_manager.update_alert_rule(rule)
    if not updated_rule:
        raise HTTPException(status_code=404, detail="Alert rule not found")
    return updated_rule

@router.delete("/rules/{rule_id}", status_code=status.HTTP_204_NO_CONTENT)
async def delete_alert_rule(
    rule_id: str = Path(..., description="The ID of the alert rule to delete"),
    current_user: User = Depends(admin_required)
):
    """
    Delete an alert rule.
    """
    success = await alert_manager.delete_alert_rule(rule_id)
    if not success:
        raise HTTPException(status_code=404, detail="Alert rule not found")

@router.get("/escalation", response_model=List[EscalationPolicy])
async def get_escalation_policies(
    current_user: User = Depends(get_current_user)
):
    """
    Get a list of escalation policies.
    """
    policies = await escalation_policy_manager.get_policies()
    return policies

@router.get("/escalation/{policy_id}", response_model=EscalationPolicy)
async def get_escalation_policy(
    policy_id: str = Path(..., description="The ID of the escalation policy to retrieve"),
    current_user: User = Depends(get_current_user)
):
    """
    Get details for a specific escalation policy by ID.
    """
    policy = await escalation_policy_manager.get_policy(policy_id)
    if not policy:
        raise HTTPException(status_code=404, detail="Escalation policy not found")
    return policy