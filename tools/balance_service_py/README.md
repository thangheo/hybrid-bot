Prototype Balance Service

This is a small prototype of a Balance Service to validate orders.

Endpoints:
- POST /v1/balance/get {exchange, asset}
- POST /v1/balance/update {exchange, asset, free, locked, last_update}
- POST /v1/validate_order {exchange, side, base_asset, quote_asset, quantity, price?, buffer_pct?}

Run locally (recommended inside a venv):

# create venv
python -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt

# run server
uvicorn app:app --reload --port 8081

Tests

pytest -q
