
import numpy as np
import pandas as pd
from sklearn.preprocessing import StandardScaler
from sklearn.decomposition import PCA
from hmmlearn.hmm import GaussianHMM
import matplotlib.pyplot as plt
import pickle
import os

class RegimeHMMDetector:
    def __init__(self):
        self.model = None
        self.scaler = None
        self.pca = None
        self.feature_cols = None
        self.pca_components = None

    # ---------------- basic indicators ----------------
    def compute_rsi(self, series, period=14):
        delta = series.diff()
        gain = delta.clip(lower=0)
        loss = -delta.clip(upper=0)
        # Wilder smoothing (approx)
        avg_gain = gain.rolling(window=period, min_periods=1).mean()
        avg_loss = loss.rolling(window=period, min_periods=1).mean()
        rs = avg_gain / (avg_loss + 1e-9)
        rsi = 100 - (100 / (1 + rs))
        return rsi

    def compute_atr(self, df, period=14):
        high_low = df['high'] - df['low']
        high_close = (df['high'] - df['close'].shift()).abs()
        low_close = (df['low'] - df['close'].shift()).abs()
        tr = pd.concat([high_low, high_close, low_close], axis=1).max(axis=1)
        atr = tr.rolling(period, min_periods=1).mean()
        return atr

    # ---------------- features for DAILY timeframe ----------------
    def compute_regime_features(self,
                                df,
                                window_short=5,          # ~1 week
                                window_long=20,         # ~1 month
                                vol_percentile_window=60, # ~3 months
                                trend_long_window=120,   # ~6 months
                                atr_period=14):
        """
        df: DataFrame indexed by datetime with columns ['open','high','low','close','volume']
        returns: features DataFrame
        """

        df = df.copy().sort_index()

        features = pd.DataFrame(index=df.index)

        # Returns
        features['return_1d'] = df['close'].pct_change()
        features['return_5d'] = df['close'].pct_change(window_short)
        features['return_20d'] = df['close'].pct_change(window_long)

        # Volatility
        features['vol_5d'] = features['return_1d'].rolling(window_short, min_periods=1).std()
        features['vol_20d'] = features['return_1d'].rolling(window_long, min_periods=1).std()
        features['vol_60d'] = features['return_1d'].rolling(vol_percentile_window, min_periods=1).std()

        # trend strength: normalized distance between short MA and long MA
        ma_short = df['close'].rolling(window_long, min_periods=1).mean()
        ma_long = df['close'].rolling(trend_long_window, min_periods=1).mean()
        ma_long_std = df['close'].rolling(trend_long_window, min_periods=1).std().replace(0, np.nan)
        features['trend_strength'] = (ma_short - ma_long) / (ma_long_std + 1e-9)

        # RSI
        features['rsi_14'] = self.compute_rsi(df['close'], period=14)

        # price distance from MA
        features['price_distance_ma20'] = (df['close'] - ma_short) / ma_short.replace(0, np.nan)

        # Volume features
        vol_ma = df['volume'].rolling(window_long, min_periods=1).mean()
        features['volume_ratio'] = df['volume'] / vol_ma.replace(0, np.nan)
        features['volume_spike'] = df['volume'] / (df['volume'].rolling(5, min_periods=1).mean().replace(0, np.nan))

        # liquidity stress (variability of volume)
        features['liquidity_stress'] = np.log1p(df['volume'].rolling(5, min_periods=1).std() / (vol_ma + 1e-9))

        # distributional features
        ret = features['return_1d']
        features['return_skew_30'] = ret.rolling(30).skew()
        features['return_kurt_30'] = ret.rolling(30).kurt()

        # vol percentile (where current vol_20d stands within past vol_60d window)
        def _vol_percentile(x):
            s = pd.Series(x).dropna()
            if len(s) == 0:
                return np.nan
            return s.rank(pct=True).iloc[-1]

        features['vol_percentile'] = features['vol_20d'].rolling(vol_percentile_window, min_periods=1).apply(_vol_percentile)

        # ATR & directional
        features['atr'] = self.compute_atr(df, period=atr_period)
        features['directional_movement'] = np.abs(df['close'] - df['close'].shift(window_long)) / (features['atr'].replace(0, np.nan) + 1e-9)

        # volatility regime indicators
        features['vol_ratio'] = features['vol_5d'] / (features['vol_60d'] + 1e-9)
        features['vol_zscore'] = (features['vol_20d'] - features['vol_20d'].rolling(60).mean()) / (features['vol_20d'].rolling(60).std() + 1e-9)

        # cleanup
        features = features.replace([np.inf, -np.inf], np.nan)
        features = features.ffill().bfill()
        return features

    # ---------------- training HMM ----------------
    def train_regime_hmm(self,
                        df,
                        n_states=3,
                        feature_cols=None,
                        use_pca=False,
                        pca_components=4,
                        random_state=42,
                        n_iter=500):
        """
        df: OHLCV daily dataframe
        feature_cols: list of features to use. If None, default recommended set used.
        use_pca: whether to run PCA before HMM (recommended for >4 correlated features)
        """

        # compute features first
        features = self.compute_regime_features(df)

        # default feature set for daily HMM (compact, less collinearity)
        default_features = [
            'vol_percentile', 'trend_strength', 'volume_ratio', 'rsi_14'
        ]
        if feature_cols is None:
            feature_cols = default_features

        # verify columns exist
        missing = [c for c in feature_cols if c not in features.columns]
        if missing:
            raise ValueError(f"Missing feature columns computed: {missing}")

        X = features[feature_cols].values

        # scale
        scaler = StandardScaler()
        X_scaled = scaler.fit_transform(X)

        # PCA optional
        pca = None
        X_in = X_scaled
        if use_pca:
            pca = PCA(n_components=min(pca_components, X_scaled.shape[1]))
            X_in = pca.fit_transform(X_scaled)
            self.pca_components = pca.n_components_

        # fit HMM
        model = GaussianHMM(n_components=n_states,
                            covariance_type='full',
                            n_iter=n_iter,
                            random_state=random_state,
                            verbose=False)
        print(f"Training HMM on {X_in.shape[0]} samples, features shape {X_in.shape} ...")
        model.fit(X_in)

        # predict
        states = model.predict(X_in)
        probs = model.predict_proba(X_in)

        # attach to features
        features = features.copy()
        features['regime'] = states
        for i in range(n_states):
            features[f'regime_prob_{i}'] = probs[:, i]

        # store
        self.model = model
        self.scaler = scaler
        self.pca = pca
        self.feature_cols = feature_cols

        return model, scaler, features

    # ---------------- analyze and label ----------------
    def analyze_regimes(self, features, df, fwd_horizons=(5, 10, 20)):
        """
        Print and return a summary per regime; compute forward returns for given horizons (days).
        """
        features = features.reindex(df.index)
        summary = {}
        print("\nREGIME SUMMARY")
        for state in sorted(features['regime'].unique()):
            mask = features['regime'] == state
            row = {}
            row['frequency_pct'] = mask.sum() / len(features) * 100
            # safe fetch of columns
            row['avg_vol_20d'] = features.loc[mask, 'vol_20d'].mean() if 'vol_20d' in features else np.nan
            row['avg_vol_percentile'] = features.loc[mask, 'vol_percentile'].mean()
            row['avg_trend_strength'] = features.loc[mask, 'trend_strength'].mean()
            row['avg_volume_ratio'] = features.loc[mask, 'volume_ratio'].mean()
            row['avg_rsi_14'] = features.loc[mask, 'rsi_14'].mean()

            # forward returns
            for h in fwd_horizons:
                fwd_ret = df['close'].pct_change(h).shift(-h)
                fwd_ret_aligned = fwd_ret.reindex(features.index)
                row[f'forward_{h}d_mean'] = fwd_ret_aligned[mask].mean()

            summary[state] = row

            # print
            print(f"\n--- REGIME {state} ---")
            print(f"Freq: {row['frequency_pct']:.2f}% | Avg vol_pct: {row['avg_vol_percentile']:.3f} | Avg trend: {row['avg_trend_strength']:.4f}")
            for h in fwd_horizons:
                val = row[f'forward_{h}d_mean']
                print(f"Avg forward {h}d return: {val*100 if not pd.isna(val) else np.nan:.3f}%")
        return pd.DataFrame.from_dict(summary, orient='index')

    def auto_label_regimes(self, features, rules=None):
        """
        Basic automatic labeling of regimes based on simple rules or clustering of summary stats.
        rules: optional dict mapping label -> lambda(row_summary) boolean
        Returns a dict {regime_index: label}
        """
        summary = {}
        for state in sorted(features['regime'].unique()):
            mask = features['regime'] == state
            summary[state] = {
                'vol_percentile': features.loc[mask, 'vol_percentile'].mean(),
                'trend_strength': features.loc[mask, 'trend_strength'].mean(),
                'volume_ratio': features.loc[mask, 'volume_ratio'].mean(),
                'rsi_14': features.loc[mask, 'rsi_14'].mean()
            }
        summary_df = pd.DataFrame.from_dict(summary, orient='index')

        # default rules if not provided
        if rules is None:
            # heuristics:
            # - High vol (vol_percentile > 0.7)
            # - Strong trend (abs(trend_strength) > thresh)
            lab = {}
            for idx, row in summary_df.iterrows():
                if row['vol_percentile'] > 0.75:
                    lab[idx] = 'HighVol/Stress'
                elif abs(row['trend_strength']) > 0.8 and row['trend_strength'] > 0:
                    lab[idx] = 'Uptrend'
                elif abs(row['trend_strength']) > 0.8 and row['trend_strength'] < 0:
                    lab[idx] = 'Downtrend'
                else:
                    lab[idx] = 'Sideways/LowVol'
            return lab
        else:
            lab = {}
            for idx, row in summary_df.iterrows():
                assigned = None
                for label, rule in rules.items():
                    try:
                        if rule(row):
                            assigned = label
                            break
                    except Exception:
                        continue
                lab[idx] = assigned or f"regime_{idx}"
            return lab

    # ---------------- visualization ----------------
    def plot_regimes(self, df, features, show_probs=False, cmap='tab10', figsize=(14,6)):
        """
        Plot price with regime colors (background) and optionally regime probabilities.
        """
        df = df.copy().sort_index()
        features = features.reindex(df.index)
        regimes = features['regime']

        # price plot
        fig, ax = plt.subplots(figsize=figsize)
        ax.plot(df.index, df['close'], label='close', linewidth=1.2)

        # shade regimes
        unique_states = sorted(features['regime'].dropna().unique())
        colors = plt.get_cmap(cmap)(np.linspace(0,1,len(unique_states)))
        # colors = {0: '#a6cee3', 1: '#fb9a99', 2: '#b2df8a'}  # xanh nhạt, đỏ nhạt, xanh lá
        state_to_color = {s: colors[i] for i,s in enumerate(unique_states)}

        current_state = None
        start_idx = None
        for i, (idx, st) in enumerate(regimes.items()):
            if pd.isna(st):
                continue
            if current_state is None:
                current_state = st
                start_idx = idx
            elif st != current_state:
                if start_idx is None:
                    continue
                ax.axvspan(start_idx, idx, color=state_to_color[current_state], alpha=0.08)
                current_state = st
                start_idx = idx
        # last span
        if start_idx is not None and current_state is not None:
            ax.axvspan(start_idx, df.index[-1], color=state_to_color[current_state], alpha=0.08)

        ax.set_title('Price with regime shading')
        ax.set_ylabel('Price')
        ax.legend()

        if show_probs:
            # plot regime probabilities stacked
            fig2, ax2 = plt.subplots(figsize=(figsize[0], 2.5))
            probs_cols = [c for c in features.columns if c.startswith('regime_prob_')]
            if probs_cols:
                ax2.stackplot(df.index, [features[c].values for c in probs_cols], labels=probs_cols)
                ax2.set_title('Regime probabilities (stacked)')
        plt.show()

    def plot_transition_matrix(self):
        if self.model is None:
            raise RuntimeError("Model not trained")
        trans = self.model.transmat_
        fig, ax = plt.subplots(figsize=(5,4))
        im = ax.imshow(trans, cmap='Blues')
        for i in range(trans.shape[0]):
            for j in range(trans.shape[1]):
                ax.text(j, i, f"{trans[i,j]:.2f}", ha="center", va="center", color='k')
        ax.set_title('HMM Regime Transition Matrix')
        ax.set_xlabel('to state')
        ax.set_ylabel('from state')
        plt.colorbar(im, ax=ax)
        plt.show()

    # ---------------- saving / loading ----------------
    def save(self, path):
        payload = {
            'model': self.model,
            'scaler': self.scaler,
            'pca': self.pca,
            'feature_cols': self.feature_cols
        }
        with open(path, 'wb') as f:
            pickle.dump(payload, f)
        print(f"Saved HMM pipeline to {path}")

    def load(self, path):
        with open(path, 'rb') as f:
            payload = pickle.load(f)
        self.model = payload.get('model')
        self.scaler = payload.get('scaler')
        self.pca = payload.get('pca')
        self.feature_cols = payload.get('feature_cols')
        print(f"Loaded HMM pipeline from {path}")

# ---------------- example usage ----------------
if __name__ == "__main__":
    # Example: read your daily CSV (datetime column must be parseable)
    csv_path = "./data/BTC_USDT_1d_5000candles.csv"
    if not os.path.exists(csv_path):
        print(f"Put your daily OHLCV CSV at {csv_path} (columns: datetime,open,high,low,close,volume)")
    else:
        df = pd.read_csv(csv_path, parse_dates=['datetime'], index_col='datetime').sort_index()
        detector = RegimeHMMDetector()
        model, scaler, features = detector.train_regime_hmm(df,
                                                            n_states=3,
                                                            feature_cols=['vol_percentile','trend_strength','volume_ratio','rsi_14'],
                                                            use_pca=False,
                                                            pca_components=4)
        # analyze
        summary = detector.analyze_regimes(features, df, fwd_horizons=(5,10,20))
        labels = detector.auto_label_regimes(features)
        print("\nAuto labels:", labels)
        detector.plot_regimes(df, features, show_probs=True)
        detector.plot_transition_matrix()

        # save
        detector.save('hmm_pipeline.pkl')
