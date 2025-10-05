#!/usr/bin/env python3
"""Train market regime HMMs for symbols listed in config/trading_bot.yaml.

This script will:
- locate symbols in the repo config
- ensure a local data cache at ./data/
- download missing 1m OHLCV data using Regime.download_crypto_data
- compute features with Regime.compute_regime_features
- train a GaussianHMM per-symbol (n_states configurable)
- save trained model with joblib and save a matplotlib plot to data/
"""

from pathlib import Path
import sys
import yaml
import logging
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from sklearn.preprocessing import StandardScaler
from hmmlearn.hmm import GaussianHMM
import joblib
# zmq
import zmq
from src.regime.market_regime import Regime
import time
from datetime import datetime, timedelta
import argparse
import requests
import json
import multiprocessing
import os
import threading


# Ensure src is importable (where market_regime.py lives)
HERE = Path(__file__).resolve().parent
SRC_DIR = HERE / "src"
if str(SRC_DIR) not in sys.path:
    sys.path.insert(0, str(SRC_DIR))


def load_config(config_path: str = None) -> dict:
    # Look for config in repo root (parent of python-strategy)
    if config_path:
        p = Path(config_path)
        if p.exists():
            with open(p, 'r') as f:
                return yaml.safe_load(f)
        else:
            raise FileNotFoundError(f"Config file {config_path} not found")

    repo_root = HERE.parent
    candidates = [repo_root / 'config' / 'trading_bot.yaml', repo_root / 'trading_bot.yaml', HERE / 'config' / 'trading_bot.yaml']
    for p in candidates:
        if p.exists():
            with open(p, 'r') as f:
                return yaml.safe_load(f)
    raise FileNotFoundError("Could not find trading_bot.yaml in expected locations: " + ",".join(str(x) for x in candidates))


def symbol_filename(symbol: str) -> str:
    return symbol.replace('/', '_').replace('-', '_')


def _main():
    # Single clean entrypoint. Default mode is 'publish' to avoid accidental training.
    p = argparse.ArgumentParser(description='Regime script: choose one mode')
    p.add_argument('--mode', choices=['train', 'publish'], default='publish', help="'train' = download+train models; 'publish' = monitor and publish using existing models")
    p.add_argument('--n-states', type=int, default=3)
    p.add_argument('--days', type=int, default=60, help='Days of history to download when training')
    p.add_argument('--no-plots', dest='plots', action='store_false', help='Do not save regime plots during training')
    p.add_argument('--data-source', dest='data_source', type=str, default='binance', choices=['binance', 'coinbase'], help='Data source for training')
    p.add_argument('--config', type=str, default=None, help='Path to trading_bot.yaml (default: search in repo root and current dir)')
    args = p.parse_args()

    try:
        cfg = load_config(config_path=args.config)
    except FileNotFoundError:
        cfg = {}

    symbols = cfg.get('data_ingestion', {}).get('symbols', [])
    data_dir = ensure_data_dir()

    if args.mode == 'train':
        if not symbols:
            raise RuntimeError('No symbols found in config/trading_bot.yaml under "data_ingestion.symbols"')
        print(f"Starting training for {len(symbols)} symbols (data_source={args.data_source})")
        download_and_train_all(symbols, data_dir, n_states=args.n_states, days=args.days, data_source=args.data_source, save_plots=not args.plots)
        print('Training finished; exiting')
        return

    # publish mode (default)
    print('Starting in publish mode — will monitor data and publish regimes using existing models')
    context = zmq.Context()
    socket = context.socket(zmq.PUB)
    try:
        socket.bind('tcp://*:5555')
    except Exception as e:
        print(f'Failed to bind publisher socket tcp://*:5555: {e}')
        context.term()
        return

    try:
        monitor_and_publish_regimes(symbols, data_dir, socket, interval=60, poll_days=1)
    except KeyboardInterrupt:
        print('Publish loop interrupted by user')
    except Exception as e:
        print(f'Publish error: {e}')
    finally:
        try:
            socket.close()
        except Exception:
            pass
        context.term()
        print('Exiting')


if __name__ == '__main__':
    _main()


def load_saved_models(data_dir: Path):
    """Load saved joblib models from data_dir. Returns dict keyed by symbol_key (filename part).
    symbol_key is the same string used in filenames (slashes replaced by underscores).
    """
    models = {}
    for p in data_dir.glob('regime_model_*.pkl'):
        try:
            key = p.stem.replace('regime_model_', '')
            models[key] = joblib.load(p)
        except Exception as e:
            print(f"Failed to load model {p}: {e}")
    return models


def predict_regime_for_symbol(regime_obj: Regime, model_entry: dict, df: pd.DataFrame, light_fallback=True):
    """Given a Regime instance, a loaded model_entry (dict with model, scaler, feature_cols), and a dataframe
    return (regime, probs) for the latest available row. If not enough rows, optionally try light features.
    """
    model = model_entry.get('model')
    scaler = model_entry.get('scaler')
    feature_cols = model_entry.get('feature_cols')

    if model is None or scaler is None or not feature_cols:
        raise RuntimeError('Invalid model_entry contents')

    # compute features
    features = regime_obj.compute_regime_features(df)

    # if not enough rows, try light mode if requested
    if features.empty and light_fallback:
        features = regime_obj.compute_regime_features(df, light=True)

    if features.empty:
        raise RuntimeError('No feature rows available to predict')

    # pick the last available row
    # ensure feature_cols exist
    cols = [c for c in feature_cols if c in features.columns]
    if not cols:
        raise RuntimeError('Feature columns not found in computed features')

    X_current = features[cols].iloc[-1:].values
    X_scaled = scaler.transform(X_current)

    pred = model.predict(X_scaled)[0]
    probs = model.predict_proba(X_scaled)[0]
    return int(pred), probs


def poll_and_update_regimes(symbols: list, data_dir: Path, interval: int = 60):
    """Continuously poll for new data every `interval` seconds and update regimes for all symbols.
    Returns when interrupted (KeyboardInterrupt). Prints updates to stdout and keeps an in-memory map of current regimes.
    """
    models = load_saved_models(data_dir)
    if not models:
        print('No saved models found in', data_dir)
        return

    regime_obj = Regime()  # used for fetching and feature computation
    current = {}  # map symbol_key -> (regime, probs, timestamp)

    try:
        while True:
            for symbol in symbols:
                key = symbol_filename(symbol)
                model_entry = models.get(key)
                if not model_entry:
                    print(f'No trained model for {symbol} (key={key})')
                    continue

                try:
                    # get recent data (1 day suffices for prediction)
                    df_recent = download_or_load(regime_obj, symbol, data_dir, days=1)
                    pred, probs = predict_regime_for_symbol(regime_obj, model_entry, df_recent)
                    current[key] = {'symbol': symbol, 'regime': int(pred), 'probs': probs.tolist(), 'ts': datetime.utcnow().isoformat()}
                    print(f"{datetime.utcnow().isoformat()}  {symbol}: regime={pred}, probs={np.round(probs,3)}")
                except Exception as e:
                    print(f"Failed to update {symbol}: {e}")

            # sleep until next poll
            time.sleep(interval)
    except KeyboardInterrupt:
        print('Polling interrupted by user')
        return current


def get_current_regime(symbol: str, data_dir: Path = None):
    """Convenience function: load model for symbol and return current regime and probabilities.
    If model isn't found in data_dir, raises.
    """
    if data_dir is None:
        data_dir = ensure_data_dir()

    key = symbol_filename(symbol)
    models = load_saved_models(data_dir)
    model_entry = models.get(key)
    if not model_entry:
        raise RuntimeError(f'No model found for symbol {symbol} (key={key}) in {data_dir}')

    regime_obj = Regime()
    df_recent = download_or_load(regime_obj, symbol, data_dir, days=1)

    try:
        pred, probs = predict_regime_for_symbol(regime_obj, model_entry, df_recent)
        return {'symbol': symbol, 'regime': int(pred), 'probs': probs.tolist(), 'ts': datetime.utcnow().isoformat()}
    except Exception as e:
        raise RuntimeError(f'Prediction failed for {symbol}: {e}')


def download_coinbase_1m_data(product_id='BTC-USD', days=60):
    """
    Download REAL 1m data from Coinbase Exchange (public endpoint, no API key required).
    product_id examples: 'BTC-USD', 'ETH-USD', 'ADA-USD', etc.
    days: number of days to fetch going backwards from now.
    Returns: pandas DataFrame indexed by timestamp with columns ['open','high','low','close','volume'].
    """
    print(f"Downloading REAL {product_id} 1m data for {days} days from Coinbase...")
    base = "https://api.exchange.coinbase.com/products/{}/candles".format(product_id)

    end_dt = datetime.utcnow()
    start_dt = end_dt - timedelta(days=days)

    all_rows = []
    # Coinbase returns max 300 candles per request, so step by 300 minutes
    step = timedelta(seconds=60*300)  # 300 minutes
    current_start = start_dt

    while current_start < end_dt:
        current_end = min(current_start + step, end_dt)
        params = {
            'start': current_start.isoformat() + 'Z',
            'end': current_end.isoformat() + 'Z',
            'granularity': 60
        }
        try:
            resp = requests.get(base, params=params, timeout=30)
            if resp.status_code != 200:
                print(f"\nHTTP {resp.status_code}: {resp.text}")
                break
            candles = resp.json()
            if not candles:
                # no data in this window
                current_start = current_end + timedelta(seconds=60)
                time.sleep(0.1)
                continue

            # Coinbase returns list of [time, low, high, open, close, volume]
            for c in candles:
                ts = datetime.utcfromtimestamp(c[0])
                low, high, open_, close, vol = c[1], c[2], c[3], c[4], c[5]
                all_rows.append([ts, open_, high, low, close, vol])

            last_time = candles[0][0] if candles else int(current_end.timestamp())
            # Move forward: because Coinbase returns in descending order sometimes, advance by step
            current_start = current_end + timedelta(seconds=60)
            print(f"Fetched up to {current_end} ({len(all_rows):,} candles)", end='\r')
            time.sleep(0.12)  # small throttle
        except Exception as e:
            print(f"\nError: {e}")
            break

    if not all_rows:
        print("\n✗ No data downloaded!")
        return pd.DataFrame()

    df = pd.DataFrame(all_rows, columns=['timestamp','open','high','low','close','volume'])
    df['timestamp'] = pd.to_datetime(df['timestamp'])
    for c in ['open','high','low','close','volume']:
        df[c] = df[c].astype(float)
    df.set_index('timestamp', inplace=True)
    df = df.sort_index()
    df = df[~df.index.duplicated(keep='first')]

    print(f"\nDownloaded {len(df):,} candles from {df.index[0]} to {df.index[-1]}")
    return df


def publisher(context, socket,topic,msg):
    
    topic = "updates"
    counter = 0

    print("Publisher started, broadcasting every 60 seconds...")
    while True:
        counter += 1
        socket.send_string(f"{topic} {msg}")
        print("Sent:", msg)
        time.sleep(60)  # wait for 1 minute


def monitor_and_publish_regimes(symbols: list, data_dir: Path, socket, interval: int = 10, poll_days: int = 1):
    """Monitor latest candle for each symbol. When any symbol has a new timestamp,
    recompute regimes for all symbols and publish a single JSON message via ZMQ socket.

    Message format (JSON):
    {
      "ts": "2025-...",
      "source": "regime_publisher",
      "regimes": {
         "BTC/USDT": {"regime": 1, "probs": [0.1,0.7,0.2], "last_ts": "..."},
         ...
      }
    }
    """
    topic = 'regimes_update'
    models = load_saved_models(data_dir)
    if not models:
        print('No saved models found, nothing to publish')
        return

    regime_obj = Regime()
    last_ts = {symbol: None for symbol in symbols}

    print('Starting monitor loop — will check every', interval, 'seconds')
    try:
        while True:
            any_new = False
            # store latest timestamps found this iteration
            latest_seen = {}
            for symbol in symbols:
                try:
                    df_recent = download_or_load(regime_obj, symbol, data_dir, days=poll_days)
                    if df_recent is None or df_recent.empty:
                        latest_seen[symbol] = None
                        continue
                    # latest timestamp as ISO string
                    ts = df_recent.index[-1]
                    ts_s = pd.to_datetime(ts).isoformat()
                    latest_seen[symbol] = ts_s
                    if last_ts.get(symbol) != ts_s:
                        any_new = True
                except Exception as e:
                    print(f'Error fetching recent data for {symbol}: {e}')
                    latest_seen[symbol] = None

            if any_new:
                # (re)load models in case they were updated
                models = load_saved_models(data_dir)
                payload = {'ts': datetime.utcnow().isoformat(), 'source': 'regime_publisher', 'regimes': {}}
                for symbol in symbols:
                    key = symbol_filename(symbol)
                    model_entry = models.get(key)
                    entry = {'regime': None, 'probs': None, 'last_ts': latest_seen.get(symbol)}
                    if model_entry and latest_seen.get(symbol):
                        try:
                            # predict using the most recent data for that symbol
                            df_recent = download_or_load(regime_obj, symbol, data_dir, days=poll_days)
                            pred, probs = predict_regime_for_symbol(regime_obj, model_entry, df_recent)
                            entry['regime'] = int(pred)
                            entry['probs'] = [float(x) for x in probs]
                        except Exception as e:
                            entry['error'] = str(e)
                    else:
                        if not model_entry:
                            entry['error'] = 'no_model'
                    payload['regimes'][symbol] = entry

                # send single message
                try:
                    socket.send_string(f"{topic} {json.dumps(payload)}")
                    print('Published regimes update:', payload['ts'])
                except Exception as e:
                    print('Failed to publish regimes:', e)

                # update last_ts map
                last_ts.update(latest_seen)

            time.sleep(interval)
    except KeyboardInterrupt:
        print('Monitor stopped by user')
        return

def to_coinbase_product(symbol: str) -> str:
    """Convert a symbol like 'BTC/USDT' or 'BTC-USD' to Coinbase product id like 'BTC-USD'.
    If symbol already looks like Coinbase (contains '-') return as-is.
    """
    if '-' in symbol:
        return symbol
    # replace slash with dash
    p = symbol.replace('/', '-')
    # convert USDT to USD if present (approximation)
    p = p.replace('USDT', 'USD')
    return p


def generate_regimes_snapshot(symbols: list, data_dir: Path) -> dict:
    """Generate a snapshot dict for all symbols based on saved models and latest data.
    This doesn't publish, it only builds the payload and saves it to data_dir/regimes_snapshot.json
    """
    models = load_saved_models(data_dir)
    regime_obj = Regime()
    payload = {'ts': datetime.utcnow().isoformat(), 'source': 'regime_trainer', 'regimes': {}}
    for symbol in symbols:
        key = symbol_filename(symbol)
        model_entry = models.get(key)
        entry = {'regime': None, 'probs': None, 'last_ts': None}
        try:
            df_recent = download_or_load(regime_obj, symbol, data_dir, days=1)
            if df_recent is None or df_recent.empty:
                entry['error'] = 'no_recent_data'
                payload['regimes'][symbol] = entry
                continue
            entry['last_ts'] = pd.to_datetime(df_recent.index[-1]).isoformat()
            if model_entry:
                pred, probs = predict_regime_for_symbol(regime_obj, model_entry, df_recent)
                entry['regime'] = int(pred)
                entry['probs'] = [float(x) for x in probs]
            else:
                entry['error'] = 'no_model'
        except Exception as e:
            entry['error'] = str(e)
        payload['regimes'][symbol] = entry

    # save snapshot
    out = data_dir / 'regimes_snapshot.json'
    try:
        with open(out, 'w') as f:
            json.dump(payload, f)
    except Exception as e:
        print(f"Failed to write snapshot file: {e}")
    return payload


def download_and_train_all(symbols: list, data_dir: Path, n_states: int = 3, days: int = 60, data_source: str = 'binance', save_plots: bool = True):
    """Download data and train models for all symbols. This is intended to run in a separate process.
    Writes model pickles into data_dir and a regimes_snapshot.json when done.
    """
    # reduce priority to avoid interfering with other processes
    try:
        os.nice(10)
    except Exception:
        pass

    regime_obj = Regime(n_states=n_states)

    for symbol in symbols:
        print(f"[trainer] Processing {symbol} using {data_source}...")
        try:
            if data_source == 'coinbase':
                product = to_coinbase_product(symbol)
                df = download_coinbase_1m_data(product_id=product, days=days)
                if df is None or df.empty:
                    print(f"[trainer] No data for {symbol} (coinbase product {product})")
                    continue
                # normalize columns and index
                df = df.sort_index()
                df.index.name = 'timestamp'
                # save
                df.to_csv(data_dir / f"{symbol_filename(symbol)}.csv", index=True)
            else:
                # binance via Regime
                df = regime_obj.download_crypto_data(symbol, days=days)
                df.to_csv(data_dir / f"{symbol_filename(symbol)}.csv", index=True)

            # train
            try:
                model, scaler, features, feature_cols = train_hmm_for_symbol(regime_obj, df, n_states=n_states, symbol=symbol)
                joblib.dump({'model': model, 'scaler': scaler, 'feature_cols': feature_cols}, data_dir / f"regime_model_{symbol_filename(symbol)}.pkl")
                print(f"[trainer] Saved model for {symbol}")
                if save_plots:
                    plot_regimes(symbol, df, features, data_dir / f"regimes_{symbol_filename(symbol)}.png")
            except Exception as e:
                print(f"[trainer] Training failed for {symbol}: {e}")
                continue
        except Exception as e:
            print(f"[trainer] Error processing {symbol}: {e}")
            continue

    # write snapshot for publisher to pick up
    try:
        payload = generate_regimes_snapshot(symbols, data_dir)
        print('[trainer] Generated snapshot')
    except Exception as e:
        print(f'[trainer] Failed to generate snapshot: {e}')


def training_worker_process(symbols: list, data_dir: Path, n_states: int, days: int, interval_days: int, data_source: str, save_plots: bool):
    """Worker function that loops forever, running training every interval_days. Intended to be started in a Process."""
    while True:
        try:
            print(f"[trainer] Starting training run at {datetime.utcnow().isoformat()}")
            download_and_train_all(symbols, data_dir, n_states=n_states, days=days, data_source=data_source, save_plots=save_plots)
            print(f"[trainer] Training run complete; sleeping for {interval_days} days")
            time.sleep(interval_days * 86400)
        except Exception as e:
            print(f"[trainer] Uncaught error in worker: {e}")
            time.sleep(60)


def start_scheduled_training(symbols: list, data_dir: Path, n_states: int = 3, days: int = 60, interval_days: int = 7, data_source: str = 'coinbase', save_plots: bool = True):
    """Start a background process to perform scheduled training. Returns the multiprocessing.Process object."""
    p = multiprocessing.Process(target=training_worker_process, args=(symbols, data_dir, n_states, days, interval_days, data_source, save_plots), daemon=False)
    p.start()
    print(f"Started training worker PID={p.pid} (interval_days={interval_days}, data_source={data_source})")
    return p


def watch_and_publish_snapshot(snapshot_path: Path, socket, topic: str = 'regimes_update', poll_interval: int = 5):
    """Watch snapshot_path for modifications and publish its JSON content via ZMQ socket when changed.

    This uses simple polling (stat) so it's portable. The message format sent is:
    '{topic} {json_payload}'
    """
    last_mtime = None
    print(f"Watching snapshot file: {snapshot_path} (poll every {poll_interval}s)")

    # If a snapshot already exists when the watcher starts, publish it once so
    # subscribers can immediately receive the current state. Previously the
    # watcher only set last_mtime and waited for a change which made
    # --watch-snapshot appear silent.
    if snapshot_path.exists():
        try:
            mtime = snapshot_path.stat().st_mtime
            last_mtime = mtime
            with open(snapshot_path, 'r') as f:
                payload = json.load(f)
            try:
                msg = json.dumps(payload)
                socket.send_string(f"{topic} {msg}")
                print(f"Published initial snapshot ({snapshot_path}) at {datetime.utcnow().isoformat()}")
            except Exception as e:
                print(f"Failed to publish initial snapshot: {e}")
        except Exception as e:
            print(f"Watcher startup error reading snapshot {snapshot_path}: {e}")

    while True:
        try:
            if snapshot_path.exists():
                mtime = snapshot_path.stat().st_mtime
                if last_mtime is None:
                    last_mtime = mtime
                elif mtime != last_mtime:
                    last_mtime = mtime
                    try:
                        with open(snapshot_path, 'r') as f:
                            payload = json.load(f)
                    except Exception as e:
                        print(f"Failed to read snapshot file {snapshot_path}: {e}")
                        payload = {'error': str(e)}

                    try:
                        msg = json.dumps(payload)
                        socket.send_string(f"{topic} {msg}")
                        print(f"Published snapshot ({snapshot_path}) at {datetime.utcnow().isoformat()}")
                    except Exception as e:
                        print(f"Failed to publish snapshot: {e}")
        except Exception as e:
            print(f"Watcher error: {e}")

        time.sleep(poll_interval)


def main(n_states: int = 3, days: int = 60, save_plots: bool = True):
    cfg = load_config(config_path=None)
    # data_ingestion/symbols
    symbols = cfg.get('data_ingestion', {}).get('symbols', [])
    if not symbols:
        raise RuntimeError('No symbols found in config/trading_bot.yaml under "symbols"')

    data_dir = ensure_data_dir()

    for symbol in symbols:
        print(f"Processing {symbol}...")
        regime = Regime(n_states=n_states)

        try:
            df = download_or_load(regime, symbol, data_dir, days=days)
            # save df to data_dir
            df.to_csv(data_dir / f"data_{symbol_filename(symbol)}.csv")
        except Exception as e:
            print(f"Failed to get data for {symbol}: {e}")
            continue

        try:
            model, scaler, features, feature_cols = train_hmm_for_symbol(regime, df, n_states=n_states, symbol=symbol)
        except Exception as e:
            print(f"Training failed for {symbol}: {e}")
            continue

        # Save model + scaler + feature cols
        model_fname = data_dir / f"regime_model_{symbol_filename(symbol)}.pkl"
        joblib.dump({'model': model, 'scaler': scaler, 'feature_cols': feature_cols}, model_fname)
        print(f"Saved model to {model_fname}")

        if save_plots:
            plot_path = data_dir / f"regimes_{symbol_filename(symbol)}.png"
            try:
                plot_regimes(symbol, df, features, plot_path)
                print(f"Saved plot to {plot_path}")
            except Exception as e:
                print(f"Failed to plot {symbol}: {e}")

    # After training, start monitoring and publishing regimes
    context = zmq.Context()
    socket = context.socket(zmq.PUB)
    socket.bind("tcp://*:5555")  # listen on port 5555

    try:
        monitor_and_publish_regimes(symbols, data_dir, socket, interval=60, poll_days=1)
    except Exception as e:
        print(f"Error in monitor_and_publish_regimes: {e}")
    finally:
        socket.close()
        context.term()

   
if __name__ == '__main__':
    # print how to use:
    # python3 regime.py --start-trainer --watch-snapshot --trainer-interval-days 7 --data-source coinbase
    print("Usage examples:")
    print("  python3 regime.py --start-trainer --watch-snapshot --trainer-interval-days 777 --data-source coinbase")
    # small CLI-ish behavior
    p = argparse.ArgumentParser(description='Train regime HMMs for configured symbols')
    p.add_argument('--n-states', type=int, default=3)
    p.add_argument('--days', type=int, default=60)
    p.add_argument('--no-plots', dest='plots', action='store_false')
    p.add_argument('--start-trainer', dest='start_trainer', action='store_true', help='Start scheduled training worker')
    p.add_argument('--trainer-interval-days', dest='trainer_interval_days', type=int, default=7, help='Days between scheduled training runs')
    p.add_argument('--data-source', dest='data_source', type=str, default='binance', choices=['binance','coinbase'], help='Which data source to use for scheduled training')
    # allow forcing startup training even if config disables it
    p.add_argument('--train-now', dest='train_now', action='store_true', help='Run training on startup immediately (overrides config)')
    # config path must be added before parsing args
    p.add_argument('--config', type=str, default=None, help='Path to trading_bot.yaml (default: search in repo root and current dir)')
    args = p.parse_args()
    # load config early so we can consult regime.train_on_startup
    try:
        cfg = load_config(config_path=args.config)
    except FileNotFoundError:
        cfg = {}
    train_on_startup = cfg.get('regime', {}).get('train_on_startup', True)
    # start training worker if requested
    if args.start_trainer:
        symbols = cfg.get('data_ingestion', {}).get('symbols', [])
        if not symbols:
            raise RuntimeError('No symbols found in config for scheduled training')
        data_dir = ensure_data_dir()
        start_scheduled_training(symbols, data_dir, n_states=args.n_states, days=args.days, interval_days=args.trainer_interval_days, data_source=args.data_source, save_plots=not args.plots)

    # bind publisher socket
    context = zmq.Context()
    socket = context.socket(zmq.PUB)
    socket.bind("tcp://*:5555")  # listen on port 5555

    # fuck u stupid code