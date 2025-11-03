#!/usr/bin/env python3
"""
Quantum Regime Publisher - Real-time quantum regime detection and publishing via ZeroMQ
Merges quantum_regimes.py functionality into regime_publisher.py

Run: python3 regime_publisher_quantum.py
Or: python3 regime_publisher_quantum.py --interval 30
"""

import sys
import time
import json
import zmq
import joblib
import pandas as pd
import numpy as np
from pathlib import Path
from datetime import datetime, timezone
import argparse
import yaml
import requests
import threading
import os
import tempfile
from typing import Any, Optional, Dict, Tuple
import math

# Import paths setup
HERE = Path(__file__).resolve().parent
SRC_DIR = HERE / "src"
if str(SRC_DIR) not in sys.path:
    sys.path.insert(0, str(SRC_DIR))
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

# Import the quantum regime model
sys.path.insert(0, str(HERE / "research"))
try:
    from research.DependentOriginationRegimeModel import DependentOriginationRegimeModel
except ImportError:
    print("Warning: Could not import DependentOriginationRegimeModel. Make sure it's in research/ folder")
    DependentOriginationRegimeModel = None

from src.regime.market_regime import Regime


class QuantumRegimeProcessor:
    """Handles quantum regime detection and signal generation"""
    
    def __init__(self, model_path: Optional[Path] = None):
        self.model = None
        self.model_cache = {}  # symbol -> model
        self.regime_names = ['Uptrend', 'Downtrend', 'Chaos', 'Stagnation', 'Emptiness']
        
        if model_path and model_path.exists():
            self.load_model(model_path)
    
    def load_model(self, model_path: Path) -> bool:
        """Load a pre-trained quantum regime model"""
        try:
            self.model = joblib.load(model_path)
            print(f"✓ Loaded quantum model: {model_path}")
            return True
        except Exception as e:
            print(f"✗ Failed to load quantum model: {e}")
            return False
    
    def load_or_train_model(self, symbol: str, X_market: np.ndarray, model_dir: Path) -> DependentOriginationRegimeModel:
        """Load existing model or train new one for a symbol"""
        model_file = model_dir / f"quantum_model_{symbol.replace('/', '_')}.pkl"
        
        if model_file.exists():
            try:
                model = joblib.load(model_file)
                print(f"✓ Loaded quantum model for {symbol}")
                return model
            except Exception as e:
                print(f"Warning: Could not load model for {symbol}: {e}")
        
        # Train new model if not exists or failed to load
        if DependentOriginationRegimeModel is None:
            print(f"Cannot train new model - DependentOriginationRegimeModel not available")
            return None
            
        print(f"Training new quantum model for {symbol}...")
        model = DependentOriginationRegimeModel(
            n_regimes=5,
            n_factors=5
        )
        
        try:
            model.fit(X_market, n_iterations=30)  # Reduced iterations for faster training
            # Save the trained model
            joblib.dump(model, model_file)
            print(f"✓ Trained and saved quantum model for {symbol}")
            return model
        except Exception as e:
            print(f"✗ Failed to train model for {symbol}: {e}")
            return None
    
    def prepare_features(self, df: pd.DataFrame) -> np.ndarray:
        """Convert OHLCV dataframe to quantum model features"""
        try:
            # Calculate returns
            df = df.copy()
            df['returns'] = df['close'].pct_change()
            
            # Calculate rolling volatility
            df['volatility'] = df['returns'].rolling(20).std()
            
            # Handle volume
            if 'volume' not in df.columns:
                df['volume'] = 1.0
            
            # Drop NaN rows
            df = df.dropna()
            
            if len(df) < 50:  # Need minimum data for meaningful analysis
                return None
            
            # Create feature matrix
            X_market = df[['returns', 'volume', 'volatility']].values
            
            return X_market
        except Exception as e:
            print(f"Error preparing features: {e}")
            return None
    
    def detect_regime(self, model: DependentOriginationRegimeModel, X_market: np.ndarray) -> Dict:
        """Run quantum regime detection on market data"""
        try:
            results = model.predict(X_market)
            return results
        except Exception as e:
            print(f"Error detecting regime: {e}")
            return None
    
    def generate_trading_signals(self, results: Dict) -> Dict:
        """Convert regime probabilities into trading signals"""
        if results is None:
            return None
            
        regime_probs = results['regime_probs']
        most_likely = results['most_likely']
        entropy = results['entropy']
        emptiness = results.get('emptiness_periods', np.zeros(len(regime_probs)))
        
        T = len(regime_probs)
        signals = {
            'position': np.zeros(T),  # 1=long, -1=short, 0=neutral
            'confidence': np.zeros(T),
            'risk_adjustment': np.ones(T)
        }
        
        for t in range(T):
            regime = most_likely[t]
            max_prob = regime_probs[t, regime]
            
            # Emptiness = reduce risk
            if emptiness[t]:
                signals['position'][t] = 0
                signals['confidence'][t] = 0
                signals['risk_adjustment'][t] = 0.25
                continue
            
            # High entropy = reduce risk
            if entropy[t] > 1.2:
                signals['risk_adjustment'][t] = 0.5
            
            # Generate position based on regime
            if regime == 0:  # Uptrend
                signals['position'][t] = 1
                signals['confidence'][t] = max_prob
            elif regime == 1:  # Downtrend
                signals['position'][t] = -1
                signals['confidence'][t] = max_prob
            elif regime == 2:  # Chaos
                signals['position'][t] = 0
                signals['confidence'][t] = max_prob
                signals['risk_adjustment'][t] = 0.5
            elif regime == 3:  # Stagnation
                signals['position'][t] = 0
                signals['confidence'][t] = max_prob
                signals['risk_adjustment'][t] = 0.75
        
        return signals
    
    def calculate_metrics(self, results: Dict, signals: Dict) -> Dict:
        """Calculate quantum regime metrics for publishing"""
        if results is None or signals is None:
            return {
                'long_signal_pct': 0.0,
                'short_signal_pct': 0.0,
                'neutral_pct': 100.0,
                'avg_confidence': 0.0,
                'avg_risk_adjustment': 1.0,
                'reduced_risk_pct': 0.0,
                'current_regime': 'unknown',
                'regime_probs': [0.0] * 5,
                'recommended_action': 'none',
                'p_trend_up': None,
                'short_alpha': None,
                'stay_prob': None
            }
        
        # Calculate percentages
        long_pct = (signals['position'] == 1).mean() * 100
        short_pct = (signals['position'] == -1).mean() * 100
        neutral_pct = (signals['position'] == 0).mean() * 100
        
        # Current (last) values
        last_idx = -1
        current_regime_idx = results['most_likely'][last_idx]
        current_regime = self.regime_names[current_regime_idx]
        current_probs = results['regime_probs'][last_idx].tolist()
        
        # Calculate p_trend_up (probability of uptrend)
        p_trend_up = float(current_probs[0])  # Uptrend probability
        
        # Calculate short_alpha (downtrend - uptrend probability)
        short_alpha = float(current_probs[1] - current_probs[0])
        
        # Calculate stay_prob based on regime stability
        # Use entropy as inverse indicator - lower entropy = higher stay probability
        current_entropy = results['entropy'][last_idx]
        stay_prob = max(0.1, min(0.95, 1.0 - current_entropy / 2.0))
        
        # Determine recommended action based on quantum signals
        current_position = signals['position'][last_idx]
        current_confidence = signals['confidence'][last_idx]
        current_risk_adj = signals['risk_adjustment'][last_idx]
        
        if current_position == 1 and current_confidence > 0.6:
            recommended_action = 'long'
        elif current_position == -1 and current_confidence > 0.6:
            recommended_action = 'short'
        elif current_position == 0 and abs(short_alpha) <= 0.2:
            # Neutral zone - check if we should extend position
            if stay_prob >= 0.7 and p_trend_up >= 0.5:
                recommended_action = 'extend'
            else:
                recommended_action = 'recommend_exit'
        else:
            recommended_action = 'none'
        
        return {
            'long_signal_pct': float(long_pct),
            'short_signal_pct': float(short_pct),
            'neutral_pct': float(neutral_pct),
            'avg_confidence': float(signals['confidence'].mean()),
            'avg_risk_adjustment': float(signals['risk_adjustment'].mean()),
            'reduced_risk_pct': float((signals['risk_adjustment'] < 1.0).mean() * 100),
            'current_regime': current_regime,
            'regime_probs': [round(p, 4) for p in current_probs],
            'recommended_action': recommended_action,
            'p_trend_up': float(p_trend_up),
            'short_alpha': float(short_alpha),
            'stay_prob': float(stay_prob),
            'current_position': int(current_position),
            'current_confidence': float(current_confidence),
            'current_risk_adj': float(current_risk_adj)
        }


def format_probs(probs, decimals: int = 4, n_states: int | None = None):
    """Round probabilities to `decimals` and pad/truncate to `n_states` if provided."""
    lst = [round(float(p), decimals) for p in probs]
    if n_states is not None:
        if len(lst) < n_states:
            lst += [0.0] * (n_states - len(lst))
        elif len(lst) > n_states:
            lst = lst[:n_states]
    return lst


def load_config(config_path=None):
    """Load config file"""
    if config_path:
        p = Path(config_path)
        if p.exists():
            with open(p, 'r') as f:
                return yaml.safe_load(f)
    
    repo_root = HERE.parent
    candidates = [
        repo_root / 'config' / 'trading_bot.yaml',
        repo_root / 'trading_bot.yaml',
        HERE / 'config' / 'trading_bot.yaml'
    ]
    for p in candidates:
        if p.exists():
            with open(p, 'r') as f:
                return yaml.safe_load(f)
    
    # Return default config if not found
    return {
        'data_ingestion': {'symbols': []},
        'pstay_by_regime': [0.8, 0.8, 0.6, 0.7, 0.5],
        'default_pstay': 0.8,
        'tau_stay': 0.7,
        'tau_trend': 0.5,
        'spread_cap': 0.01,
        'min_volume': 1000,
        'neutral_threshold': 0.2,
        'k_half_life': 2.0,
        'half_life_min_clamp': 0.5
    }


def symbol_filename(symbol):
    """Convert symbol to filename"""
    return symbol.replace('/', '_').replace('-', '_')


class DataCache:
    """In-memory cache of symbol DataFrames with background updates"""
    def __init__(self, data_dir: Path, regime_obj, per_symbol_delay: float = 0.2, retention_days: int = 7):
        self.data_dir = Path(data_dir)
        self.regime_obj = regime_obj
        self.lock = threading.Lock()
        self.dfs = {}  # symbol_key -> DataFrame
        self.running = False
        self.thread = None
        self.per_symbol_delay = per_symbol_delay
        self.retention_days = retention_days

    @staticmethod
    def _binance_symbol(symbol: str) -> str:
        return symbol.replace('/', '').replace('-', '')

    @staticmethod
    def _klines_to_df(klines):
        rows = []
        for k in klines:
            ts = datetime.utcfromtimestamp(k[0] / 1000)
            rows.append([ts, float(k[1]), float(k[2]), float(k[3]), float(k[4]), float(k[5])])
        df = pd.DataFrame(rows, columns=['timestamp', 'open', 'high', 'low', 'close', 'volume'])
        df['timestamp'] = pd.to_datetime(df['timestamp'])
        df.set_index('timestamp', inplace=True)
        return df

    def load_symbol_once(self, symbol: str, days: int = 7):
        """Load symbol data"""
        key = symbol_filename(symbol)
        try:
            print(f"Downloading history for {symbol} (days={days})...")
            df = self.regime_obj.download_crypto_data(symbol, days=days)
            if df is None or df.empty:
                return pd.DataFrame()
            df.index.name = 'timestamp'
            df = df[~df.index.duplicated(keep='first')].sort_index()
            
            if self.retention_days is not None and not df.empty:
                cutoff_time = df.index[-1] - pd.Timedelta(days=self.retention_days)
                df = df[df.index >= cutoff_time]
            
            with self.lock:
                self.dfs[key] = df
            return df
        except Exception as e:
            print(f"Error downloading history for {symbol}: {e}")
            return pd.DataFrame()

    def fetch_recent(self, symbol: str, limit: int = 5):
        """Fetch recent klines from Binance"""
        bsym = self._binance_symbol(symbol)
        url = 'https://api.binance.com/api/v3/klines'
        params = {'symbol': bsym, 'interval': '1m', 'limit': limit}
        try:
            r = requests.get(url, params=params, timeout=10)
            r.raise_for_status()
            klines = r.json()
            return self._klines_to_df(klines)
        except Exception as e:
            print(f"fetch_recent failed for {symbol}: {e}")
            return pd.DataFrame()

    def append_new_candles(self, symbol: str):
        """Append new candles to existing data"""
        key = symbol_filename(symbol)
        with self.lock:
            df = self.dfs.get(key)

        recent = self.fetch_recent(symbol, limit=10)
        if recent.empty:
            return

        if df is None or df.empty:
            with self.lock:
                self.dfs[key] = recent
            return

        try:
            combined = pd.concat([df, recent])
            combined = combined[~combined.index.duplicated(keep='last')].sort_index()
            
            if self.retention_days is not None:
                cutoff = combined.index[-1] - pd.Timedelta(days=self.retention_days)
                combined = combined[combined.index >= cutoff]
            
            with self.lock:
                self.dfs[key] = combined
        except Exception as e:
            print(f"append_new_candles error for {symbol}: {e}")

    def get_dataframe(self, symbol: str) -> pd.DataFrame:
        """Get current DataFrame for symbol"""
        key = symbol_filename(symbol)
        with self.lock:
            return self.dfs.get(key, pd.DataFrame())

    def update_all_symbols(self, symbols: list):
        """Update all symbols"""
        for symbol in symbols:
            self.append_new_candles(symbol)
            time.sleep(self.per_symbol_delay)

    def start(self, symbols: list):
        """Start background updater thread"""
        if self.running:
            return
        self.running = True
        
        def updater():
            while self.running:
                try:
                    self.update_all_symbols(symbols)
                    time.sleep(60)  # Update every minute
                except Exception as e:
                    print(f"Updater error: {e}")
                    time.sleep(5)
        
        self.thread = threading.Thread(target=updater, daemon=True)
        self.thread.start()

    def stop(self):
        """Stop background updater"""
        self.running = False
        if self.thread:
            self.thread.join(timeout=5)


def publish_quantum_regimes(symbols, data_dir, socket, interval=60, probs_decimals=4):
    """Main publishing loop with quantum regime detection"""
    
    # Initialize components
    regime_obj = Regime()
    cache = DataCache(data_dir, regime_obj, per_symbol_delay=0.2, retention_days=7)
    quantum_processor = QuantumRegimeProcessor()
    
    # Load configuration
    cfg = load_config()
    
    # Topic for ZMQ
    topic = "REGIME"
    
    # Initial data load for all symbols
    print("\n" + "="*70)
    print("LOADING INITIAL DATA")
    print("="*70)
    
    for symbol in symbols:
        df = cache.load_symbol_once(symbol, days=7)
        if df.empty:
            print(f"✗ No data for {symbol}")
        else:
            print(f"✓ Loaded {len(df)} candles for {symbol}")
    
    # Start background updater
    cache.start(symbols)
    print("\n✓ Background data updater started")
    
    # Model storage directory
    model_dir = data_dir / 'quantum_models'
    model_dir.mkdir(exist_ok=True)
    
    # Pre-load or train models for all symbols
    print("\n" + "="*70)
    print("PREPARING QUANTUM MODELS")
    print("="*70)
    
    symbol_models = {}
    for symbol in symbols:
        df = cache.get_dataframe(symbol)
        if df.empty:
            print(f"✗ Skipping {symbol} - no data")
            continue
        
        # Prepare features
        X_market = quantum_processor.prepare_features(df)
        if X_market is None or len(X_market) < 100:
            print(f"✗ Insufficient data for {symbol}")
            continue
        
        # Load or train model
        model = quantum_processor.load_or_train_model(symbol, X_market, model_dir)
        if model is not None:
            symbol_models[symbol] = model
    
    print(f"\n✓ Models ready for {len(symbol_models)}/{len(symbols)} symbols")
    
    # Main publishing loop
    print("\n" + "="*70)
    print("STARTING QUANTUM REGIME PUBLISHER")
    print("="*70)
    print(f"Publishing to ZMQ on port 5555")
    print(f"Interval: {interval} seconds")
    print("="*70 + "\n")
    
    try:
        while True:
            start_time = time.time()
            
            # Build payload
            now = datetime.now(timezone.utc)
            payload = {
                'ts': now.isoformat(),
                'regimes': {}
            }
            
            for symbol in symbols:
                entry = {'symbol': symbol}
                
                try:
                    # Get latest data
                    df = cache.get_dataframe(symbol)
                    if df.empty or len(df) < 100:
                        entry['error'] = 'insufficient_data'
                        entry['regime'] = -1
                        entry['probs'] = [0.0] * 5
                        payload['regimes'][symbol] = entry
                        continue
                    
                    # Check if we have a model
                    if symbol not in symbol_models:
                        # Try to train one now
                        X_market = quantum_processor.prepare_features(df)
                        if X_market is not None and len(X_market) >= 100:
                            model = quantum_processor.load_or_train_model(symbol, X_market, model_dir)
                            if model is not None:
                                symbol_models[symbol] = model
                    
                    if symbol not in symbol_models:
                        entry['error'] = 'no_model'
                        entry['regime'] = -1
                        entry['probs'] = [0.0] * 5
                        payload['regimes'][symbol] = entry
                        continue
                    
                    # Prepare features for quantum detection
                    X_market = quantum_processor.prepare_features(df)
                    if X_market is None:
                        entry['error'] = 'feature_preparation_failed'
                        entry['regime'] = -1
                        entry['probs'] = [0.0] * 5
                        payload['regimes'][symbol] = entry
                        continue
                    
                    # Run quantum regime detection
                    model = symbol_models[symbol]
                    results = quantum_processor.detect_regime(model, X_market)
                    signals = quantum_processor.generate_trading_signals(results)
                    metrics = quantum_processor.calculate_metrics(results, signals)
                    
                    # Fill entry with quantum metrics
                    entry['regime'] = int(results['most_likely'][-1]) if results else -1
                    entry['probs'] = format_probs(metrics['regime_probs'], probs_decimals, 5)
                    entry['regime_name'] = metrics['current_regime']
                    
                    # Quantum-specific metrics
                    entry['long_signal_pct'] = round(metrics['long_signal_pct'], 2)
                    entry['short_signal_pct'] = round(metrics['short_signal_pct'], 2)
                    entry['neutral_pct'] = round(metrics['neutral_pct'], 2)
                    entry['avg_confidence'] = round(metrics['avg_confidence'], 3)
                    entry['avg_risk_adjustment'] = round(metrics['avg_risk_adjustment'], 2)
                    entry['reduced_risk_pct'] = round(metrics['reduced_risk_pct'], 1)
                    
                    # Compatible with original publisher format
                    entry['p_trend_up'] = round(metrics['p_trend_up'], 3)
                    entry['short_alpha'] = round(metrics['short_alpha'], 3)
                    entry['stay_prob'] = round(metrics['stay_prob'], 3)
                    entry['recommended_action'] = metrics['recommended_action']
                    
                    # Additional quantum info
                    entry['current_position'] = metrics['current_position']
                    entry['current_confidence'] = round(metrics['current_confidence'], 3)
                    entry['current_risk_adj'] = round(metrics['current_risk_adj'], 2)
                    
                    # Determine should_extend flag
                    entry['should_extend'] = (
                        metrics['recommended_action'] == 'extend' or
                        (metrics['recommended_action'] == 'long' and metrics['current_confidence'] > 0.7)
                    )
                    
                    # Add microstructure check
                    try:
                        last_high = float(df['high'].iloc[-1])
                        last_low = float(df['low'].iloc[-1])
                        last_close = float(df['close'].iloc[-1])
                        spread_frac = (last_high - last_low) / (last_close + 1e-12)
                        avg_volume = float(df['volume'].iloc[-60:].mean()) if len(df) >= 60 else float(df['volume'].mean())
                        
                        spread_cap = cfg.get('spread_cap', 0.01)
                        min_volume = cfg.get('min_volume', 1000)
                        micro_ok = (spread_frac <= spread_cap) and (avg_volume >= min_volume)
                        entry['micro_ok'] = bool(micro_ok)
                    except:
                        entry['micro_ok'] = True
                    
                    # Create summary reason
                    reason_parts = []
                    if metrics['long_signal_pct'] > 10:
                        reason_parts.append(f"long:{metrics['long_signal_pct']:.1f}%")
                    if metrics['short_signal_pct'] > 10:
                        reason_parts.append(f"short:{metrics['short_signal_pct']:.1f}%")
                    if metrics['neutral_pct'] > 50:
                        reason_parts.append(f"neutral:{metrics['neutral_pct']:.1f}%")
                    reason_parts.append(f"conf:{metrics['avg_confidence']:.2f}")
                    reason_parts.append(f"risk:{metrics['avg_risk_adjustment']:.2f}x")
                    entry['reason'] = " ".join(reason_parts)
                    
                    # Print summary
                    print(f"{symbol:12} regime={entry['regime_name']:10} "
                          f"long={entry['long_signal_pct']:5.1f}% "
                          f"short={entry['short_signal_pct']:5.1f}% "
                          f"neutral={entry['neutral_pct']:5.1f}% "
                          f"conf={entry['avg_confidence']:.3f} "
                          f"risk={entry['avg_risk_adjustment']:.2f}x "
                          f"action={entry['recommended_action']}")
                    
                except Exception as e:
                    entry['error'] = str(e)
                    entry['regime'] = -1
                    entry['probs'] = [0.0] * 5
                    print(f"{symbol:12} ERROR: {e}")
                
                payload['regimes'][symbol] = entry
            
            # Publish via ZeroMQ
            try:
                msg = json.dumps(payload)
                socket.send_string(f"{topic} {msg}")
                print(f"\n✓ Published at {payload['ts']}")
                
                # Print quantum summary
                print("\n" + "="*50)
                print("QUANTUM REGIME SUMMARY")
                print("="*50)
                
                total_long = sum(e.get('long_signal_pct', 0) for e in payload['regimes'].values()) / len(symbols)
                total_short = sum(e.get('short_signal_pct', 0) for e in payload['regimes'].values()) / len(symbols)
                total_neutral = sum(e.get('neutral_pct', 0) for e in payload['regimes'].values()) / len(symbols)
                avg_conf = sum(e.get('avg_confidence', 0) for e in payload['regimes'].values()) / len(symbols)
                avg_risk = sum(e.get('avg_risk_adjustment', 0) for e in payload['regimes'].values()) / len(symbols)
                
                print(f"Market Average:")
                print(f"  Long signals:    {total_long:5.1f}%")
                print(f"  Short signals:   {total_short:5.1f}%")
                print(f"  Neutral/wait:    {total_neutral:5.1f}%")
                print(f"  Avg confidence:  {avg_conf:.3f}")
                print(f"  Avg risk adj:    {avg_risk:.2f}x")
                print("="*50 + "\n")
                
            except Exception as e:
                print(f"✗ Publish failed: {e}")
            
            # Sleep until next interval
            elapsed = time.time() - start_time
            sleep_time = max(0, interval - elapsed)
            if sleep_time > 0:
                time.sleep(sleep_time)
            
    except KeyboardInterrupt:
        print("\n\nStopped by user")
    finally:
        cache.stop()


def main():
    parser = argparse.ArgumentParser(description='Quantum Regime Publisher - Real-time quantum regime detection')
    parser.add_argument('--interval', type=int, default=60, help='Interval between publishes (seconds)')
    parser.add_argument('--probs-decimals', type=int, default=4, help='Decimal places for probabilities')
    parser.add_argument('--config', type=str, default=None, help='Path to trading_bot.yaml')
    parser.add_argument('--symbols', nargs='+', help='List of symbols to process (e.g., BTC/USDT ETH/USDT)')
    args = parser.parse_args()
    
    # Load config
    try:
        cfg = load_config(args.config)
        symbols = cfg.get('data_ingestion', {}).get('symbols', [])
    except Exception as e:
        print(f"✗ Error loading config: {e}")
        symbols = []
    
    # Override with command-line symbols if provided
    if args.symbols:
        symbols = args.symbols
    
    # If no symbols, try auto-discovery
    if not symbols:
        data_dir = HERE / 'data'
        symbols = []
        
        # Look for existing models
        for p in sorted(data_dir.glob('quantum_model_*.pkl')):
            symbols.append(p.stem.replace('quantum_model_', '').replace('_', '/'))
        
        # Or CSV files
        if not symbols:
            for p in sorted(data_dir.glob('*.csv')):
                stem = p.stem
                if not (stem.startswith('data_') or stem.startswith('__')):
                    # Convert filename back to symbol format
                    symbol = stem.replace('_', '/')
                    if 'USDT' in symbol:
                        symbols.append(symbol)
        
        # Default symbols if still empty
        if not symbols:
            symbols = ['BTC/USDT', 'ETH/USDT', 'BNB/USDT']
            print(f"No symbols found, using defaults: {symbols}")
    
    print(f"Will process symbols: {symbols}")
    
    # Data directory
    data_dir = HERE / 'data'
    if not data_dir.exists():
        data_dir.mkdir(parents=True)
        print(f"Created data directory: {data_dir}")
    
    # Setup ZeroMQ
    context = zmq.Context()
    socket = context.socket(zmq.PUB)
    socket.bind("tcp://*:5555")
    print(f"ZeroMQ publisher bound to port 5555")
    
    try:
        publish_quantum_regimes(symbols, data_dir, socket, 
                              interval=args.interval, 
                              probs_decimals=args.probs_decimals)
    finally:
        socket.close()
        context.term()


if __name__ == '__main__':
    main()
