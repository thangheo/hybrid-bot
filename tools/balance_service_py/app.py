from fastapi import FastAPI, HTTPException
from pydantic import BaseModel, Field
from typing import Dict, Optional
from decimal import Decimal
from datetime import datetime

app = FastAPI(title="Balance Service Prototype")

# Simple in-memory store for prototype
# Structure: balances[exchange][asset] = {free: Decimal, locked: Decimal, ts: datetime}
balances: Dict[str, Dict[str, Dict]] = {}


class BalanceRequest(BaseModel):
    exchange: str
    asset: str


class ValidateOrderRequest(BaseModel):
    exchange: str
    side: str  # BUY/SELL
    base_asset: str
    quote_asset: str
    quantity: Decimal = Field(..., gt=0)
    price: Optional[Decimal] = None
    buffer_pct: Optional[Decimal] = Decimal('0.0')


class BalanceResponse(BaseModel):
    exchange: str
    asset: str
    free: Decimal
    locked: Decimal
    last_update: datetime


class ValidateOrderResponse(BaseModel):
    is_valid: bool
    required: Decimal
    available: Decimal
    suggested_quantity: Optional[Decimal]
    reason: Optional[str]
    last_update: Optional[datetime]


@app.post("/v1/balance/get", response_model=BalanceResponse)
async def get_balance(req: BalanceRequest):
    exch = req.exchange
    asset = req.asset.upper()
    if exch not in balances or asset not in balances[exch]:
        raise HTTPException(status_code=404, detail="Balance not found")
    entry = balances[exch][asset]
    return BalanceResponse(exchange=exch, asset=asset, free=entry['free'], locked=entry['locked'], last_update=entry['ts'])


@app.post("/v1/balance/update")
async def update_balance(req: BalanceResponse):
    exch = req.exchange
    asset = req.asset.upper()
    if exch not in balances:
        balances[exch] = {}
    balances[exch][asset] = {'free': req.free, 'locked': req.locked, 'ts': req.last_update}
    return {"ok": True}


@app.post("/v1/balance/all")
async def get_all_balances(req: BalanceRequest):
    """Return all balances for an exchange as a mapping asset -> {free, locked, last_update}
    The prototype returns Decimal values as strings to avoid float precision issues.
    """
    exch = req.exchange
    if exch not in balances:
        raise HTTPException(status_code=404, detail="Exchange not found")

    # Build a serializable dict
    out = {}
    for asset, entry in balances[exch].items():
        out[asset] = {
            'free': str(entry['free']),
            'locked': str(entry['locked']),
            'last_update': entry['ts'].isoformat()
        }
    return {"exchange": exch, "balances": out}


@app.post("/v1/validate_order", response_model=ValidateOrderResponse)
async def validate_order(req: ValidateOrderRequest):
    exch = req.exchange
    side = req.side.upper()
    base = req.base_asset.upper()
    quote = req.quote_asset.upper()
    qty = req.quantity
    price = req.price
    buffer_pct = req.buffer_pct or Decimal('0.0')

    # For BUY: need quote asset balance >= qty*price * (1 + buffer)
    # For SELL: need base asset balance >= qty * (1 + buffer)
    now = datetime.utcnow()
    if side == 'BUY':
        if price is None:
            raise HTTPException(status_code=400, detail='Price required for BUY validation')
        required = (qty * price) * (1 + buffer_pct)
        # get quote asset balance
        try:
            bal = balances[exch][quote]
            avail = bal['free']
            last = bal['ts']
        except Exception:
            return ValidateOrderResponse(is_valid=False, required=required, available=Decimal('0'), suggested_quantity=None, reason='Balance not found', last_update=None)
        if avail >= required:
            return ValidateOrderResponse(is_valid=True, required=required, available=avail, suggested_quantity=qty, reason=None, last_update=last)
        else:
            # suggest smaller qty
            if price > 0:
                suggested_qty = (avail / price) / (1 + buffer_pct)
                return ValidateOrderResponse(is_valid=False, required=required, available=avail, suggested_quantity=suggested_qty, reason='Insufficient quote balance', last_update=last)
            else:
                return ValidateOrderResponse(is_valid=False, required=required, available=avail, suggested_quantity=None, reason='Invalid price', last_update=last)
    elif side == 'SELL':
        required = qty * (1 + buffer_pct)
        try:
            bal = balances[exch][base]
            avail = bal['free']
            last = bal['ts']
        except Exception:
            return ValidateOrderResponse(is_valid=False, required=required, available=Decimal('0'), suggested_quantity=None, reason='Balance not found', last_update=None)
        if avail >= required:
            return ValidateOrderResponse(is_valid=True, required=required, available=avail, suggested_quantity=qty, reason=None, last_update=last)
        else:
            suggested_qty = (avail) / (1 + buffer_pct)
            return ValidateOrderResponse(is_valid=False, required=required, available=avail, suggested_quantity=suggested_qty, reason='Insufficient base balance', last_update=last)
    else:
        raise HTTPException(status_code=400, detail='Unknown side')


# Add a small health endpoint
@app.get('/health')
async def health():
    return {"status": "ok", "balances_count": sum(len(v) for v in balances.values())}
