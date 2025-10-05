#!/usr/bin/env python3
"""Check feature NaNs for all CSVs in python-strategy/data
Usage: python3 tools/check_features.py
"""
import sys
from pathlib import Path
import pandas as pd

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
SRC_DIR = REPO / 'python-strategy' / 'src'
PS_ROOT = REPO / 'python-strategy'
if str(SRC_DIR) not in sys.path:
    sys.path.insert(0, str(SRC_DIR))
if str(PS_ROOT) not in sys.path:
    sys.path.insert(0, str(PS_ROOT))

from src.regime.market_regime import Regime

DATA_DIR = PS_ROOT / 'data'

models = {}

r = Regime()

bad = []
ok = []
no_features = []
no_file = []

for csv in sorted(DATA_DIR.glob('*.csv')):
    name = csv.stem
    # skip files like data_*.csv
    if name.startswith('data_'):
        continue
    try:
        df = pd.read_csv(csv, index_col=0, parse_dates=True)
    except Exception as e:
        print(f'{name}: read error: {e}')
        no_file.append(name)
        continue
    if df.empty:
        print(f'{name}: empty dataframe')
        no_file.append(name)
        continue
    try:
        features = r.compute_regime_features(df)
        if features.empty:
            features = r.compute_regime_features(df, light=True)
            if features.empty:
                print(f'{name}: no feature rows (even after light fallback)')
                no_features.append(name)
                continue
        # find model entry for this symbol (key is same as filename)
        key = name
        model_entry = models.get(key)
        # use model_entry.feature_cols if available else use features.columns
        feature_cols = model_entry.get('feature_cols') if model_entry else list(features.columns)
        available = [c for c in feature_cols if c in features.columns]
        if not available:
            print(f'{name}: model feature cols not found in computed features')
            bad.append(name)
            continue
        last_row = features[available].iloc[-1:]
        nan_cols = [c for c in available if pd.isna(last_row[c].iloc[0])]
        if nan_cols:
            print(f'{name}: NaNs in last row -> {nan_cols}')
            bad.append(name)
        else:
            ok.append(name)
    except Exception as e:
        print(f'{name}: feature computation error: {e}')
        bad.append(name)

print('\nSummary:')
print(f'  OK: {len(ok)}')
print(f'  Bad (NaN / errors): {len(bad)}')
print(f'  No features: {len(no_features)}')
print(f'  Read errors / empty: {len(no_file)}')

if bad:
    print('\nBad list:')
    for s in bad:
        print('  ', s)

if no_features:
    print('\nNo features list:')
    for s in no_features:
        print('  ', s)

if no_file:
    print('\nNo file / read errors list:')
    for s in no_file:
        print('  ', s)
