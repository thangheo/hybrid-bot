"""
Practical Guide: Using the Dependent Origination Regime Model
with Real Market Data
"""

import os
import glob
from pathlib import Path

import numpy as np
import pandas as pd
from research.DependentOriginationRegimeModel import DependentOriginationRegimeModel

# ============================================================================
# OPTION 1: Load from CSV file
# ============================================================================

def load_market_data_from_csv(filepath):
    """
    Load market data from CSV file
    
    Expected columns:
    - date/timestamp
    - close (or price)
    - volume
    - Any other features you have (optional)
    
    Returns: X_market array suitable for model
    """
    df = pd.read_csv(filepath) #.head(1000)
    
    # Calculate returns
    df['returns'] = df['close'].pct_change()
    
    # Calculate rolling volatility (if not provided)
    df['volatility'] = df['returns'].rolling(20).std()
    
    # Use volume (if available)
    if 'volume' in df.columns:
        df['volume'] = df['volume']
    else:
        df['volume'] = 1.0  # placeholder
    
    # Drop NaN rows
    df = df.dropna()
    
    # Create feature matrix
    X_market = df[['returns', 'volume', 'volatility']].values
    
    return X_market, df

 
# ============================================================================
# OPTION 3: Load from your own data source
# ============================================================================

def prepare_your_data(prices, volumes=None):
    """
    Convert your raw price/volume data into model format
    
    Args:
        prices: array or list of prices
        volumes: array or list of volumes (optional)
    
    Returns:
        X_market: properly formatted for model
    """
    prices = np.array(prices)
    
    # Calculate returns
    returns = np.diff(prices) / prices[:-1]
    
    # Calculate rolling volatility
    vol_window = 20
    volatility = np.array([
        np.std(returns[max(0, i-vol_window):i+1]) 
        for i in range(len(returns))
    ])
    
    # Handle volume
    if volumes is None:
        volumes = np.ones(len(returns))
    else:
        volumes = np.array(volumes)[1:]  # align with returns
    
    # Stack into feature matrix
    X_market = np.column_stack([returns, volumes, volatility])
    
    return X_market


# ============================================================================
# MAIN WORKFLOW: Train and Use the Model
# ============================================================================

def train_and_detect_regimes(X_market, save_results=True, model_name=None):
    """
    Complete workflow: train model and detect regimes
    
    Args:
        X_market: (T, 3+) array with [returns, volume, other features]
        save_results: whether to save outputs
    
    Returns:
        model: trained model
        results: detection results
        signals: trading signals based on regimes
    """
    
    # 1. Initialize model
    print("Initializing model...")
    model = DependentOriginationRegimeModel(
        n_regimes=5,
        n_factors=5
        # allow_emptiness=True
    )
    
    # 2. Train model
    print("Training model (this may take 1-2 minutes)...")
    model.fit(X_market, n_iterations=50)
    
    # 3. Detect regimes
    print("Detecting regimes...")
    results = model.predict(X_market)
    
    # 4. Generate trading signals
    signals = generate_trading_signals(results)
    
    # 5. Visualize
    print("Creating visualizations...")
    model.visualize_regime_waves(results)
    
    # # 6. Save results
    # if save_results:
    #     save_results_to_csv(results, signals)
    
    print("\nComplete!")
    # save model to file
    # model.save("quantum_regime_model.pkl")
    if model_name:
        model.save(model_name)
    return model, results, signals


def prepare_df_market(df):
    """
    Prepare feature matrix X_market from a DataFrame with at least a 'close' column.
    Returns X_market, df_processed (with returns/volatility/volume)
    """
    df = df.copy()
    if 'close' not in df.columns and 'price' in df.columns:
        df['close'] = df['price']

    # ensure sorted by date if present
    if 'date' in df.columns or 'timestamp' in df.columns:
        date_col = 'date' if 'date' in df.columns else 'timestamp'
        df = df.sort_values(by=date_col)

    # Calculate returns
    df['returns'] = df['close'].pct_change()

    # Calculate rolling volatility (if not provided)
    df['volatility'] = df['returns'].rolling(20).std()

    # Use volume (if available)
    if 'volume' in df.columns:
        df['volume'] = df['volume']
    else:
        df['volume'] = 1.0  # placeholder

    df = df.dropna()
    X_market = df[['returns', 'volume', 'volatility']].values
    return X_market, df


def save_results_to_csv(symbol, results, signals, outdir='data'):
    """Save detection results and signals to CSV under outdir with symbol prefix."""
    os.makedirs(outdir, exist_ok=True)
    df_results = pd.DataFrame({
        'uptrend_prob': results['regime_probs'][:, 0],
        'downtrend_prob': results['regime_probs'][:, 1],
        'chaos_prob': results['regime_probs'][:, 2],
        'stagnation_prob': results['regime_probs'][:, 3],
        # if there are 5 regimes, index 4 exists
        'emptiness_prob': results['regime_probs'][:, 4] if results['regime_probs'].shape[1] > 4 else 0.0,
        'dominant_regime': results['most_likely'],
        'entropy': results.get('entropy'),
        'is_emptiness': results.get('emptiness_periods'),
        'signal_position': signals['position'],
        'signal_confidence': signals['confidence'],
        'risk_adjustment': signals['risk_adjustment']
    })

    out_csv = os.path.join(outdir, f"{symbol}_regime_results.csv")
    df_results.to_csv(out_csv, index=False)
    print(f"Results saved to: {out_csv}")


def load_model(path):
    """Load a saved model. Try class-level load if available, else fall back to pickle."""
    # Try class-level loader first
    load_fn = getattr(DependentOriginationRegimeModel, 'load', None)
    if load_fn is not None:
        try:
            return load_fn(path)
        except Exception:
            pass

    # Fallback to pickle
    import pickle
    with open(path, 'rb') as f:
        return pickle.load(f)

# function to 

def generate_trading_signals(results):
    """
    Convert regime probabilities into actionable trading signals
    
    Returns:
        signals: dict with trading recommendations
    """
    regime_probs = results['regime_probs']
    most_likely = results['most_likely']
    entropy = results['entropy']
    emptiness = results['emptiness_periods']
    
    T = len(regime_probs)
    signals = {
        'position': np.zeros(T),  # 1=long, -1=short, 0=neutral
        'confidence': np.zeros(T),  # 0-1 scale
        'risk_adjustment': np.ones(T)  # multiplier for position size
    }
    
    for t in range(T):
        regime = most_likely[t]
        max_prob = regime_probs[t, regime]
        
        # Emptiness = reduce risk
        if emptiness[t]:
            signals['position'][t] = 0
            signals['confidence'][t] = 0
            signals['risk_adjustment'][t] = 0.25  # 25% normal size
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
            signals['position'][t] = 0  # Stay flat in chaos
            signals['confidence'][t] = max_prob
            signals['risk_adjustment'][t] = 0.5
        elif regime == 3:  # Stagnation
            # Mean reversion strategy
            signals['position'][t] = 0  # or implement mean reversion
            signals['confidence'][t] = max_prob
            signals['risk_adjustment'][t] = 0.75
    
    return signals

 
# ============================================================================
# EXAMPLE USAGE PATTERNS
# ============================================================================
if __name__ == "__main__":
    import argparse
    # CLI: accept a single CSV, a directory of CSVs, or a CSV containing a 'symbol' column
    parser = argparse.ArgumentParser(description="Regime Detection - per symbol trainer")
    parser.add_argument("--input", type=str, required=True,
                        help="Path to CSV file or directory of CSV files. If CSV has 'symbol' column, each symbol will be processed.")
    parser.add_argument("--outdir", type=str, default="data", help="Output directory for models and results")
    parser.add_argument("--n_iterations", type=int, default=50, help="Training iterations passed to model.fit")
    parser.add_argument("--predict", action="store_true", help="Load a saved model and run prediction on a CSV (provide --model and --input CSV)")
    parser.add_argument("--model", type=str, help="Path to saved model to load for prediction")
    args = parser.parse_args()

    input_path = Path(args.input)
    outdir = args.outdir

    files_to_process = []

    # If predict-mode is requested, do only prediction and exit (no training)
    if args.predict:
        if not args.model:
            print("--predict requires --model path to be provided")
            exit(1)

        model_path = args.model
        if not os.path.exists(model_path):
            print(f"Model path does not exist: {model_path}")
            exit(1)

        print(f"Loading model from: {model_path}")
        model = load_model(model_path)

        # We expect input_path to be a CSV for prediction
        if not input_path.is_file():
            print("For --predict provide a single CSV file as --input")
            exit(1)

        df = pd.read_csv(str(input_path))
        X_market, df_proc = prepare_df_market(df)

        print("Running prediction on loaded model...")
        results = model.predict(X_market)
        signals = generate_trading_signals(results)

        # save prediction results with model name prefix
        model_name = Path(model_path).stem
        pred_symbol = model_name + "_pred"
        out_csv = os.path.join(outdir, f"{pred_symbol}_regime_results.csv")
        save_results_to_csv(pred_symbol, results, signals, outdir=outdir)

        # Append summary row to the saved CSV and write last row to /tmp/{symbol_name}_.dat
        try:
            df_all = pd.read_csv(out_csv)

            long_pct = (signals['position'] == 1).mean() * 100
            short_pct = (signals['position'] == -1).mean() * 100
            neutral_pct = (signals['position'] == 0).mean() * 100
            avg_conf = float(signals['confidence'].mean())

            # Prepare a summary row that keeps existing columns and adds summary fields
            summary_cols = {
                'summary_long_pct': long_pct,
                'summary_short_pct': short_pct,
                'summary_neutral_pct': neutral_pct,
                'summary_avg_confidence': avg_conf
            }

            # Create a row dict with NaN for existing columns
            row = {c: pd.NA for c in df_all.columns}
            # Merge summary columns
            row.update(summary_cols)

            # Append and save back
            df_all = pd.concat([df_all, pd.DataFrame([row])], ignore_index=True)
            df_all.to_csv(out_csv, index=False)

            # Write a compact summary file to /tmp/{symbol_name}_.dat (header + single row)
            symbol_name = model_name.replace('_model', '').replace('_pred', '')
            tmp_path = f"/tmp/{symbol_name}_.dat"
            summary_df = pd.DataFrame([summary_cols])
            summary_df.to_csv(tmp_path, index=False)
            print(f"Prediction complete and saved. Summary dumped to: {tmp_path}")
        except Exception as e:
            print(f"Prediction saved but failed to create summary dump: {e}")
        exit(0)

    if input_path.is_dir():
        # all CSV files in dir
        files_to_process = list(map(Path, glob.glob(str(input_path / "*.csv"))))
    elif input_path.is_file():
        # single CSV file — check for 'symbol' column
        df_all = pd.read_csv(str(input_path))
        if 'symbol' in df_all.columns:
            symbols = df_all['symbol'].unique()
            for s in symbols:
                df_sym = df_all[df_all['symbol'] == s].copy()
                try:
                    X_market, df_proc = prepare_df_market(df_sym)
                except Exception as e:
                    print(f"Skipping symbol {s}: failed to prepare data: {e}")
                    continue

                print(f"Training symbol: {s} (rows: {len(df_proc)})")
                model = DependentOriginationRegimeModel(n_regimes=5, n_factors=5)
                model.fit(X_market, n_iterations=args.n_iterations)
                results = model.predict(X_market)
                signals = generate_trading_signals(results)

                # save model
                os.makedirs(outdir, exist_ok=True)
                model_path = os.path.join(outdir, f"{s}_model.pkl")
                try:
                    model.save(model_path)
                except Exception:
                    # fallback: try with pickle
                    import pickle
                    with open(model_path, 'wb') as f:
                        pickle.dump(model, f)
                print(f"Saved model: {model_path}")

                # save results
                save_results_to_csv(s, results, signals, outdir=outdir)
        else:
            files_to_process = [input_path]
    else:
        print("Input path does not exist")
        exit(1)

    # process files (each file assumed to contain data for one symbol)
    for fp in files_to_process:
        try:
            df = pd.read_csv(str(fp))
        except Exception as e:
            print(f"Failed to read {fp}: {e}")
            continue

        # infer symbol from filename if possible
        symbol = fp.stem
        try:
            X_market, df_proc = prepare_df_market(df)
        except Exception as e:
            print(f"Skipping {fp}: failed to prepare data: {e}")
            continue

        print(f"Training symbol/file: {symbol} (rows: {len(df_proc)})")
        model = DependentOriginationRegimeModel(n_regimes=5, n_factors=5)
        model.fit(X_market, n_iterations=args.n_iterations)
        results = model.predict(X_market)
        signals = generate_trading_signals(results)

        # save model
        os.makedirs(outdir, exist_ok=True)
        model_path = os.path.join(outdir, f"{symbol}_model.pkl")
        try:
            model.save(model_path)
        except Exception:
            import pickle
            with open(model_path, 'wb') as f:
                pickle.dump(model, f)
        print(f"Saved model: {model_path}")

        # save results
        save_results_to_csv(symbol, results, signals, outdir=outdir)

    # If predict-mode requested, but batch training also allowed, support loading model and running predict
    if args.predict:
        if not args.model:
            print("--predict requires --model path to be provided")
            exit(1)

        def load_model(path):
            # Try model class loader first
            try:
                m = DependentOriginationRegimeModel()
                # some model implementations might have a classmethod load or instance method load
                # try class-level load if present on the class
                load_fn = getattr(DependentOriginationRegimeModel, 'load', None)
                if load_fn is not None:
                    try:
                        loaded = load_fn(path)
                        return loaded
                    except Exception:
                        pass
                # fallback: try pickle
            except Exception:
                pass

            import pickle
            with open(path, 'rb') as f:
                return pickle.load(f)

        model_path = args.model
        if not os.path.exists(model_path):
            print(f"Model path does not exist: {model_path}")
            exit(1)

        print(f"Loading model from: {model_path}")
        model = load_model(model_path)

        # We expect input_path to be a CSV for prediction
        if not input_path.is_file():
            print("For --predict provide a single CSV file as --input")
            exit(1)

        df = pd.read_csv(str(input_path))
        X_market, df_proc = prepare_df_market(df)

        print("Running prediction on loaded model...")
        results = model.predict(X_market)
        signals = generate_trading_signals(results)

        # save prediction results with model name prefix
        model_name = Path(model_path).stem
        save_results_to_csv(model_name + "_pred", results, signals, outdir=outdir)
        print("Prediction complete and saved.")
        #use default
        # X_market, df = load_market_data_from_csv('../data/ETH_USDT_1d_5000candles.csv')
        # model, results, signals = train_and_detect_regimes(X_market)

       
    
    # Summary printing is only valid if we have results/signals from a single-run in globals
    results_local = globals().get('results')
    signals_local = globals().get('signals')
    if results_local is not None and signals_local is not None:
        regime_names = ['Uptrend', 'Downtrend', 'Chaos', 'Stagnation', 'Emptiness']

        for i, name in enumerate(regime_names):
            if results_local.get('most_likely') is None:
                continue
            pct = (results_local['most_likely'] == i).mean() * 100
            avg_prob = results_local['regime_probs'][:, i].mean()
            print(f"{name:12s}: {pct:5.1f}% of time (avg prob: {avg_prob:.3f})")

        print("\n" + "="*70)
        print("TRADING SIGNAL SUMMARY")
        print("="*70)

        long_pct = (signals_local['position'] == 1).mean() * 100
        short_pct = (signals_local['position'] == -1).mean() * 100
        neutral_pct = (signals_local['position'] == 0).mean() * 100

        print(f"Long signals:    {long_pct:5.1f}%")
        print(f"Short signals:   {short_pct:5.1f}%")
        print(f"Neutral/wait:    {neutral_pct:5.1f}%")
        print(f"Avg confidence:  {signals_local['confidence'].mean():.3f}")

        print("\n" + "="*70)
        print("RISK MANAGEMENT")
        print("="*70)

        avg_risk_adj = signals_local['risk_adjustment'].mean()
        reduced_risk_pct = (signals_local['risk_adjustment'] < 1.0).mean() * 100

        print(f"Average risk adjustment: {avg_risk_adj:.2f}x")
        print(f"Periods with reduced risk: {reduced_risk_pct:.1f}%")

        print("\n" + "="*70)
        print("OUTPUTS")
        print("="*70)
        print("1. Visualization: regime_waves.png")
        print("2. Data table: regime_results.csv")
        print("3. Trained model: stored in 'model' variable")
    else:
        print("No single-run results available to summarize (script likely ran per-symbol and saved outputs in the output directory).")









# ============================================================================
# ADVANCED: Real-time / Online Usage
# ============================================================================

def update_regime_realtime(model, new_data_point):
    """
    Update regime detection with a single new data point
    
    Args:
        model: pre-trained model
        new_data_point: [return, volume, volatility] for latest period
    
    Returns:
        regime_probs: current regime probabilities
        signal: trading signal
    """
    # Extract factors from new data
    X_new = np.array([new_data_point])
    F_new = model.extract_factors(X_new)[0]
    
    # Compute regime probabilities
    regime_probs = model.compute_regime_probabilities(F_new)
    
    # Check for emptiness
    is_empty, entropy = model.detect_emptiness(regime_probs)
    
    # Generate signal
    if is_empty:
        signal = {'position': 0, 'confidence': 0, 'reason': 'emptiness'}
    else:
        dominant = np.argmax(regime_probs)
        regime_names = ['uptrend', 'downtrend', 'chaos', 'stagnation', 'emptiness']
        
        if dominant == 0:  # Uptrend
            signal = {'position': 1, 'confidence': regime_probs[0], 
                     'reason': 'uptrend detected'}
        elif dominant == 1:  # Downtrend
            signal = {'position': -1, 'confidence': regime_probs[1],
                     'reason': 'downtrend detected'}
        else:
            signal = {'position': 0, 'confidence': regime_probs[dominant],
                     'reason': f'{regime_names[dominant]} detected'}
    
    return regime_probs, signal


# Example real-time usage:
"""
# Train model once on historical data
model = DependentOriginationRegimeModel()
model.fit(historical_X_market)

# Then, in your trading loop:
while market_is_open():
    # Get latest data
    latest_return = get_latest_return()
    latest_volume = get_latest_volume()
    latest_vol = calculate_volatility()
    
    new_point = [latest_return, latest_volume, latest_vol]
    
    # Update regime
    probs, signal = update_regime_realtime(model, new_point)
    
    # Act on signal
    if signal['confidence'] > 0.7:
        execute_trade(signal['position'])
"""
