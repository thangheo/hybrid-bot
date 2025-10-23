"""
Practical Guide: Using the Dependent Origination Regime Model
with Real Market Data
"""

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


# def save_results_to_csv(results, signals):
#     """Save detection results to CSV for further analysis"""
    
#     # Create DataFrame
#     df_results = pd.DataFrame({
#         'uptrend_prob': results['regime_probs'][:, 0],
#         'downtrend_prob': results['regime_probs'][:, 1],
#         'chaos_prob': results['regime_probs'][:, 2],
#         'stagnation_prob': results['regime_probs'][:, 3],
#         'emptiness_prob': results['regime_probs'][:, 4],
#         'dominant_regime': results['most_likely'],
#         'entropy': results['entropy'],
#         'is_emptiness': results['emptiness_periods'],
#         'signal_position': signals['position'],
#         'signal_confidence': signals['confidence'],
#         'risk_adjustment': signals['risk_adjustment']
#     })
    
#     # Save
#     df_results.to_csv('regime_results.csv', index=False)
#     print("Results saved to: regime_results.csv")

# ============================================================================
# EXAMPLE USAGE PATTERNS
# ============================================================================
if __name__ == "__main__":
    import argparse
    # this part of code should only call for training model only once
    parser = argparse.ArgumentParser(description="Regime Detection")
    parser.add_argument("--csv", type=str, help="Path to your CSV file")
    args = parser.parse_args()

    if args.csv:
        X_market, df = load_market_data_from_csv(args.csv)
        model, results, signals = train_and_detect_regimes(X_market)
    else:
        print("No CSV path provided. Skipping example usage patterns.")
        exit()
        #use default
        # X_market, df = load_market_data_from_csv('../data/ETH_USDT_1d_5000candles.csv')
        # model, results, signals = train_and_detect_regimes(X_market)

       
    
    regime_names = ['Uptrend', 'Downtrend', 'Chaos', 'Stagnation', 'Emptiness']
    
    for i, name in enumerate(regime_names):
        pct = (results['most_likely'] == i).mean() * 100
        avg_prob = results['regime_probs'][:, i].mean()
        print(f"{name:12s}: {pct:5.1f}% of time (avg prob: {avg_prob:.3f})")
    
    print("\n" + "="*70)
    print("TRADING SIGNAL SUMMARY")
    print("="*70)
    
    long_pct = (signals['position'] == 1).mean() * 100
    short_pct = (signals['position'] == -1).mean() * 100
    neutral_pct = (signals['position'] == 0).mean() * 100
    
    print(f"Long signals:    {long_pct:5.1f}%")
    print(f"Short signals:   {short_pct:5.1f}%")
    print(f"Neutral/wait:    {neutral_pct:5.1f}%")
    print(f"Avg confidence:  {signals['confidence'].mean():.3f}")
    
    print("\n" + "="*70)
    print("RISK MANAGEMENT")
    print("="*70)
    
    avg_risk_adj = signals['risk_adjustment'].mean()
    reduced_risk_pct = (signals['risk_adjustment'] < 1.0).mean() * 100
    
    print(f"Average risk adjustment: {avg_risk_adj:.2f}x")
    print(f"Periods with reduced risk: {reduced_risk_pct:.1f}%")
    
    print("\n" + "="*70)
    print("OUTPUTS")
    print("="*70)
    print("1. Visualization: regime_waves.png")
    print("2. Data table: regime_results.csv")
    print("3. Trained model: stored in 'model' variable")


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
