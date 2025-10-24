from fastapi.testclient import TestClient
from app import app
from decimal import Decimal
from datetime import datetime

client = TestClient(app)

def test_validate_buy_and_sell():
    now = datetime.utcnow().isoformat()
    # seed balances
    resp = client.post('/v1/balance/update', json={
        'exchange': 'BINANCE',
        'asset': 'USDT',
        'free': '1000.0',
        'locked': '0.0',
        'last_update': now
    })
    assert resp.status_code == 200

    resp = client.post('/v1/balance/update', json={
        'exchange': 'BINANCE',
        'asset': 'AAVE',
        'free': '2.5',
        'locked': '0.0',
        'last_update': now
    })
    assert resp.status_code == 200

    # BUY validation (price * qty = 200, available 1000)
    resp = client.post('/v1/validate_order', json={
        'exchange': 'BINANCE',
        'side': 'BUY',
        'base_asset': 'AAVE',
        'quote_asset': 'USDT',
        'quantity': '1.0',
        'price': '200.0',
        'buffer_pct': '0.01'
    })
    assert resp.status_code == 200
    body = resp.json()
    assert body['is_valid'] == True

    # SELL validation (have 2.5 AAVE, trying sell 3.0)
    resp = client.post('/v1/validate_order', json={
        'exchange': 'BINANCE',
        'side': 'SELL',
        'base_asset': 'AAVE',
        'quote_asset': 'USDT',
        'quantity': '3.0',
        'price': '210.0',
        'buffer_pct': '0.0'
    })
    assert resp.status_code == 200
    body = resp.json()
    assert body['is_valid'] == False
    assert float(body['available']) == 2.5
    assert float(body['suggested_quantity']) <= 2.5
