import numpy as np
import pandas as pd
from hmmlearn.hmm import GaussianHMM
import ccxt
from sklearn.preprocessing import StandardScaler
from datetime import datetime, timedelta
# Ensure python-strategy root is importable so `data.download` module can be found
import sys
from pathlib import Path
HERE = Path(__file__).resolve().parents[2]
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))
try:
    from data.download import download_binance_1m_data
except ModuleNotFoundError:
    # Fallback: load the file directly from the python-strategy/data path
    import importlib.util
    import sys as _sys
    DATA_FILE = HERE / 'data' / 'download.py'
    if DATA_FILE.exists():
        spec = importlib.util.spec_from_file_location('data.download', str(DATA_FILE))
        mod = importlib.util.module_from_spec(spec)
        _sys.modules['data.download'] = mod
        spec.loader.exec_module(mod)
        download_binance_1m_data = mod.download_binance_1m_data
    else:
        raise
class Regime:
    def __init__(self, n_states=3):
        self.n_states = n_states
        self.model = None
        self.scaler = None
        self.feature_cols = None

    # === 1. Download 1m data ===
    def download_crypto_data(self, symbol, days=60):
        """
        Download 1m candles - sử dụng Binance API trực tiếp
        """
        # Convert symbol to Binance format if needed
        if '/' in symbol:
            symbol = symbol.replace('/', '')
        
        df = download_binance_1m_data(symbol=symbol, days=days)
        return df
    def download_crypto_data_demo(self, symbol, days=30):
        """
        Download 1m candles - sử dụng CCXT cho crypto
        """
        exchange = ccxt.binance({
            'enableRateLimit': True,
            'options': {'defaultType': 'future'}  # Can change to 'spot' if needed
        })
        
        # Calculate since timestamp
        since = exchange.milliseconds() - days * 24 * 60 * 60 * 1000
        
        all_ohlcv = []
        limit = 1500  # Binance limit per request
        
        print(f"Downloading {symbol} 1m data for {days} days...")
        
        while since < exchange.milliseconds():
            try:
                ohlcv = exchange.fetch_ohlcv(symbol, '1m', since=since, limit=limit)
                if len(ohlcv) == 0:
                    break
                
                all_ohlcv.extend(ohlcv)
                since = ohlcv[-1][0] + 60000  # Move to next minute
                
                print(f"Downloaded {len(all_ohlcv)} candles...", end='\r')
                
            except Exception as e:
                print(f"\nError: {e}")
                break
        
        print(f"\nTotal downloaded: {len(all_ohlcv)} candles")
        
        df = pd.DataFrame(all_ohlcv, columns=['timestamp', 'open', 'high', 'low', 'close', 'volume'])
        df['timestamp'] = pd.to_datetime(df['timestamp'], unit='ms')
        df.set_index('timestamp', inplace=True)
        
        # Remove duplicates
        df = df[~df.index.duplicated(keep='first')]
        
        return df
    
    def compute_rsi(self, prices, period=14):
        """Calculate RSI indicator"""
        delta = prices.diff()
        gain = (delta.where(delta > 0, 0)).rolling(period).mean()
        loss = (-delta.where(delta < 0, 0)).rolling(period).mean()
        rs = gain / loss
        return 100 - (100 / (1 + rs))

    def compute_atr(self, df, period=14):
        """Calculate ATR indicator"""
        high_low = df['high'] - df['low']
        high_close = np.abs(df['high'] - df['close'].shift())
        low_close = np.abs(df['low'] - df['close'].shift())
        ranges = pd.concat([high_low, high_close, low_close], axis=1)
        true_range = np.max(ranges, axis=1)
        return true_range.rolling(period).mean()
    
    # === 2. Feature engineering cho crypto scalping ===
    def compute_regime_features(self, df, window_short=15, window_long=60, vol_percentile_window=1440, trend_long_window=240, atr_period=60, light: bool = False):
        """
        Compute features for regime detection.

        Parameters:
        - df: DataFrame with open/high/low/close/volume indexed by timestamp
        - window_short, window_long: integers (in bars) used for rolling windows
        - vol_percentile_window: window for volatility percentile calculation
        - trend_long_window: long window used in trend_strength calculations
        - atr_period: period for ATR
        - light: if True, use much shorter windows to support short series
        """
        # If light mode requested, reduce windows to make features computable on short series
        if light:
            window_short = max(1, int(window_short // 3))
            window_long = max(1, int(window_long // 5))
            vol_percentile_window = max(10, int(vol_percentile_window // 20))
            trend_long_window = max(10, int(trend_long_window // 5))
            atr_period = max(3, int(atr_period // 5))

        features = pd.DataFrame(index=df.index)

        # Returns multi-timeframe
        features['return_1m'] = df['close'].pct_change()
        # use provided windows for multi-frame returns
        features['return_15m'] = df['close'].pct_change(window_short)
        features['return_1h'] = df['close'].pct_change(window_long)

        # Volatility multi-timeframe (use min_periods=1 to avoid NaNs on short series)
        features['vol_15m'] = features['return_1m'].rolling(window_short, min_periods=1).std()
        features['vol_1h'] = features['return_1m'].rolling(window_long, min_periods=1).std()
        features['vol_4h'] = features['return_1m'].rolling(max(1, window_long * 4), min_periods=1).std()

        # Trend strength (use trend_long_window)
        ma_short = df['close'].rolling(window_long, min_periods=1).mean()
        ma_long = df['close'].rolling(trend_long_window, min_periods=1).mean()
        ma_long_std = df['close'].rolling(trend_long_window, min_periods=1).std().replace(0, np.nan)
        features['trend_strength'] = (ma_short - ma_long) / ma_long_std

        # Volume patterns
        features['volume_ma'] = df['volume'].rolling(window_long, min_periods=1).mean()
        features['volume_spike'] = df['volume'] / features['volume_ma'].replace(0, np.nan)
        vol_ma_long = df['volume'].rolling(trend_long_window, min_periods=1).mean()
        features['volume_trend'] = features['volume_ma'] / vol_ma_long.replace(0, np.nan)

        # Price momentum
        rsi_period = max(3, int(window_long / max(1, window_short))) if light else 60
        features['rsi_60'] = self.compute_rsi(df['close'], rsi_period)
        features['price_distance_ma60'] = (df['close'] - df['close'].rolling(window_long, min_periods=1).mean()) / df['close']

        # Volatility percentile - compute on a rolling window but allow small windows
        def _vol_percentile(x):
            s = pd.Series(x).dropna()
            if len(s) == 0:
                return np.nan
            return s.rank(pct=True).iloc[-1]

        features['vol_percentile'] = features['vol_1h'].rolling(vol_percentile_window, min_periods=1).apply(_vol_percentile)

        # ATR and directional movement
        features['atr'] = self.compute_atr(df, period=atr_period)
        features['directional_movement'] = np.abs(df['close'] - df['close'].shift(window_long)) / features['atr'].replace(0, np.nan)

        # Replace infinite and extremely large/small values
        features = features.replace([np.inf, -np.inf], np.nan)

        # Try to preserve as many rows as possible for short series: forward/backfill then drop rows that are all-NaN
        features = features.ffill().bfill()
        return features.dropna(how='all')

    # === 3. Train HMM với proper features ===
    def train_regime_hmm(self, symbol, n_states=3):
        """Train HMM model for regime detection"""
        # Download data
        df = self.download_crypto_data(symbol, days=60)  # 2 tháng data
        
        # Compute features
        features = self.compute_regime_features(df)
        
        # Select key features (không dùng hết, tránh overfitting)
        feature_cols = [
            'vol_1h', 'vol_4h', 'vol_percentile',
            'trend_strength', 'directional_movement',
            'volume_spike', 'rsi_60'
        ]
        
        X = features[feature_cols].values
        
        # Standardize (CRITICAL cho HMM)
        scaler = StandardScaler()
        X_scaled = scaler.fit_transform(X)
        
        print(f"\nTraining HMM with {len(X_scaled)} samples...")
        
        # Train HMM
        model = GaussianHMM(
            n_components=n_states,
            covariance_type="full",
            n_iter=500,
            random_state=42,
            verbose=False
        )
        
        model.fit(X_scaled)
        
        # Get regimes
        states = model.predict(X_scaled)
        probs = model.predict_proba(X_scaled)
        
        # Add to dataframe
        features['regime'] = states
        for i in range(n_states):
            features[f'regime_prob_{i}'] = probs[:, i]
        
        # Store in object
        self.model = model
        self.scaler = scaler
        self.feature_cols = feature_cols
        
        return model, scaler, features, feature_cols

    # === 4. Analyze regimes ===
    def analyze_regimes(self, features, df):
        """
        Hiểu từng regime là gì
        """
        # Align dataframes
        features_aligned = features.reindex(df.index)
        
        # If labels were computed, use them
        has_labels = 'regime_label' in features.columns

        for state in sorted(features['regime'].unique()):
            mask = features['regime'] == state
            
            label = None
            if has_labels:
                # pick the most common label for this state
                try:
                    label = features.loc[mask, 'regime_label'].mode().iloc[0]
                except Exception:
                    label = None

            print(f"\n=== REGIME {state} " + (f"({label})" if label is not None else "") + " ===")
            print(f"Frequency: {mask.sum() / len(features) * 100:.1f}%")
            print(f"Avg 1h Vol: {features.loc[mask, 'vol_1h'].mean():.6f}")
            print(f"Avg Trend Strength: {features.loc[mask, 'trend_strength'].mean():.4f}")
            print(f"Avg Volume Spike: {features.loc[mask, 'volume_spike'].mean():.2f}")
            
            # Forward returns (holding vài tiếng)
            for horizon in [60, 120, 240]:  # 1h, 2h, 4h
                fwd_ret = df['close'].pct_change(horizon).shift(-horizon)
                # Align with features
                fwd_ret_aligned = fwd_ret.reindex(features.index)
                fwd_ret_regime = fwd_ret_aligned[mask].mean()
                print(f"Avg {horizon}m forward return: {fwd_ret_regime*100:.3f}%")

    def label_regimes(self, features, df, forward_horizons=(60, 120, 240), up_thresh=0.002, down_thresh=-0.002):
        """
        Assign human-readable labels ('bull', 'bear', 'sideways') to each regime/state.

        Strategy:
        - For each state, compute mean forward returns at supplied horizons and mean trend_strength.
        - If mean forward returns across horizons are consistently > up_thresh => 'bull'
        - If consistently < down_thresh => 'bear'
        - Else => 'sideways'

        Parameters:
        - features: DataFrame returned from training containing 'regime'
        - df: original price DataFrame (used to compute forward returns)
        - forward_horizons: iterable of integer minute horizons to evaluate
        - up_thresh / down_thresh: thresholds for forward returns to classify

        Returns:
        - features with a new column 'regime_label' (in-place and returned)
        """
        if 'regime' not in features.columns:
            raise ValueError("features must contain a 'regime' column. Run the HMM first.")

        # compute forward returns for each horizon and attach
        fwd_returns = pd.DataFrame(index=features.index)
        for h in forward_horizons:
            fwd = df['close'].pct_change(h).shift(-h)
            fwd_returns[f'fwd_ret_{h}'] = fwd.reindex(features.index)

        labels = pd.Series(index=features.index, dtype=object)

        mapping = {}
        for state in sorted(features['regime'].unique()):
            mask = features['regime'] == state
            # mean forward returns (ignore NaNs)
            mean_fwd = fwd_returns.loc[mask].mean()
            mean_all = mean_fwd.mean()
            mean_trend = features.loc[mask, 'trend_strength'].mean()

            # simple logic: use forward returns primarily, trend as tie-breaker
            if pd.isna(mean_all):
                assigned = 'unknown'
            elif (mean_fwd > up_thresh).all():
                assigned = 'bull'
            elif (mean_fwd < down_thresh).all():
                assigned = 'bear'
            else:
                # use trend_strength: positive => bull-like, negative => bear-like, else sideways
                if mean_trend > 0.1:
                    assigned = 'bull'
                elif mean_trend < -0.1:
                    assigned = 'bear'
                else:
                    assigned = 'sideways'

            mapping[state] = assigned

        # apply mapping
        labels = features['regime'].map(mapping)
        features['regime_label'] = labels
        # store mapping on the object for easy lookup
        self.regime_label_mapping = mapping
        # keep backward-compatible: return features only
        return features

    def predict_current_regime(self, symbol):
        """Predict current regime for a symbol"""
        if self.model is None:
            raise ValueError("Model not trained. Call train_regime_hmm first.")
        if self.scaler is None or self.feature_cols is None:
            raise ValueError("Scaler or feature columns missing. Ensure train_regime_hmm was called and completed successfully.")
        
        # Download recent data
        df = self.download_crypto_data(symbol, days=7)
        
        # Compute features
        features = self.compute_regime_features(df)
        if features.empty:
            raise ValueError("No features computed from recent data. Check the input data or use a shorter 'days' parameter.")
        
        # Get last row
        X_current = features[self.feature_cols].iloc[-1:].values
        # ensure numeric array
        X_current = np.asarray(X_current, dtype=float)
        X_scaled = self.scaler.transform(X_current)
        
        # Predict
        regime = self.model.predict(X_scaled)[0]
        probs = self.model.predict_proba(X_scaled)[0]
        # map to human label if available and store on self for downstream consumers
        if hasattr(self, 'regime_label_mapping') and isinstance(self.regime_label_mapping, dict):
            self.last_predicted_label = self.regime_label_mapping.get(regime)
        else:
            self.last_predicted_label = None

        # return (regime, probs) to preserve existing external interface
        return regime, probs

    # function to visualize regimes (optional)
    def plot_regimes(self, features, df):
        import matplotlib.pyplot as plt
        import seaborn as sns

        # Align dataframes
        features_aligned = features.reindex(df.index)

        plt.figure(figsize=(15, 8))
        plt.plot(df.index, df['close'], label='Close Price', color='black')

        # Plot regimes
        for state in sorted(features['regime'].unique()):
            mask = features_aligned['regime'] == state
            plt.scatter(features_aligned.index[mask], df['close'][mask], label=f'Regime {state}', s=10)
        # save plot

        plt.title('Market Regimes')
        plt.xlabel('Time')
        plt.ylabel('Price')
        plt.legend()
        plt.savefig('market_regimes' + datetime.now().strftime('%Y%m%d_%H%M%S') + '.png')
        plt.show()

    def get_last_regime_label(self):
        """Return the last predicted regime label (str) or None if not available.

        This is a small accessor intended for external publishers (ZMQ/IPC) to
        read the human-readable label without changing the predict_current_regime
        return signature.
        """
        return getattr(self, 'last_predicted_label', None)

if __name__ == "__main__":
    try:
        symbol = 'BTC/USDT'
        print(f"Starting regime detection for {symbol}")
        
        regime = Regime(n_states=3)
        model, scaler, features, feature_cols = regime.train_regime_hmm(symbol, n_states=4)
        
        print("\n" + "="*50)
        print("REGIME ANALYSIS")
        print("="*50)
        
        df = regime.download_crypto_data(symbol, days=10)
        regime.analyze_regimes(features, df)
        
        print("\n" + "="*50)
        print("CURRENT REGIME PREDICTION")
        print("="*50)
        
        current_regime, probs = regime.predict_current_regime(symbol)
        print(f"\nCurrent Regime: {current_regime}")
        if hasattr(regime, 'last_predicted_label') and regime.last_predicted_label is not None:
            print(f"Label: {regime.last_predicted_label}")
        print("Probabilities:")
        for i, prob in enumerate(probs):
            print(f"  Regime {i}: {prob*100:.2f}%")
        
        print("\n✓ Success!")
        regime.plot_regimes(features, df)
        
    except Exception as e:
        print(f"\n✗ Error: {str(e)}")
        import traceback
        traceback.print_exc()