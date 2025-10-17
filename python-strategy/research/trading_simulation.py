"""
Realistic Trading Scenario: Intraday Bitcoin Trading
Demonstrates the ensemble forecasting system with simulated market data
"""

import numpy as np
import matplotlib.pyplot as plt
from ensemble_forecast import EnsembleForecastSystem, Forecast
import pandas as pd

def simulate_price_path(initial_price=50000, n_steps=100, dt=0.1, 
                       drift=0.0001, volatility=0.002):
    """
    Simulate realistic price path using geometric Brownian motion
    
    Args:
        initial_price: Starting price
        n_steps: Number of time steps
        dt: Time increment (hours)
        drift: Drift coefficient
        volatility: Volatility coefficient
    
    Returns:
        times, prices, returns
    """
    times = np.arange(0, n_steps * dt, dt)
    returns = np.random.normal(drift * dt, volatility * np.sqrt(dt), n_steps)
    
    # Add some realistic features
    # 1. Autocorrelation (momentum)
    for i in range(1, len(returns)):
        returns[i] += 0.3 * returns[i-1]
    
    # 2. Volatility clustering
    vol_regime = np.random.choice([1.0, 1.5, 0.7], n_steps, p=[0.6, 0.2, 0.2])
    returns *= vol_regime
    
    # 3. Occasional jumps
    jump_prob = 0.05
    jumps = np.random.binomial(1, jump_prob, n_steps)
    jump_sizes = np.random.normal(0, volatility * 3, n_steps)
    returns += jumps * jump_sizes
    
    prices = initial_price * np.exp(np.cumsum(returns))
    
    return times[:n_steps], prices, returns


def create_forecast_from_indicator(indicator_name, signal, current_time, 
                                   timeframe_hours, confidence):
    """
    Create a forecast based on technical indicator signal
    
    Args:
        indicator_name: Name of indicator
        signal: Signal value (-1 to 1, negative=bearish, positive=bullish)
        current_time: Current time
        timeframe_hours: Forecast horizon
        confidence: Base confidence level
    
    Returns:
        Forecast object
    """
    # Determine parameters based on signal
    mu = signal * 0.01  # Expected return
    v = 0.015 + abs(signal) * 0.01  # Volatility increases with signal strength
    p = confidence * (0.5 + 0.5 * abs(signal))  # Probability scales with signal
    
    # Choose distribution based on signal strength
    if abs(signal) > 0.7:
        dist_type = 'student_t'
        df = 5
    elif abs(signal) > 0.4:
        dist_type = 'laplace'
        df = None
    else:
        dist_type = 'normal'
        df = None
    
    # Add skew if signal is strong
    alpha = 0 if abs(signal) < 0.5 else signal * 3
    
    forecast = Forecast(
        T=current_time + timeframe_hours,
        v=v if signal >= 0 else -v,
        p=min(p, 0.95),
        mu=mu,
        dist_type=dist_type,
        t0=current_time
    )
    
    if dist_type == 'student_t':
        forecast.df = df
    if abs(signal) >= 0.5:
        forecast.dist_type = 'skew_normal'
        forecast.alpha = alpha
    
    return forecast


def run_trading_simulation():
    """
    Run a complete trading simulation with the ensemble system
    """
    print("=" * 80)
    print("REALISTIC TRADING SIMULATION: Intraday BTC Trading")
    print("=" * 80)
    
    # Simulate price data
    np.random.seed(42)
    times, prices, returns = simulate_price_path(
        initial_price=50000,
        n_steps=48,  # 4.8 hours of data
        dt=0.1,  # 6-minute intervals
        drift=0.0001,
        volatility=0.002
    )
    
    print(f"\n✓ Generated {len(prices)} price points over {times[-1]:.1f} hours")
    print(f"  Starting price: ${prices[0]:,.2f}")
    print(f"  Ending price: ${prices[-1]:,.2f}")
    print(f"  Return: {(prices[-1]/prices[0] - 1)*100:+.2f}%")
    
    # Initialize ensemble system
    system = EnsembleForecastSystem(decay_rate=0.2)
    
    # Simulate multiple forecasting models updating over time
    update_intervals = [0, 1.0, 2.0, 3.0, 4.0]  # Update every hour
    
    print("\n" + "-" * 80)
    print("ADDING FORECASTS FROM MULTIPLE MODELS")
    print("-" * 80)
    
    for t_update in update_intervals:
        # Get current price index
        idx = min(int(t_update / 0.1), len(prices) - 1)
        current_price = prices[idx]
        
        print(f"\n📊 Time t={t_update:.1f}h | Price: ${current_price:,.2f}")
        
        # Model 1: RSI-based (short-term)
        rsi_signal = np.random.normal(0.3, 0.3)  # Bullish bias
        rsi_signal = np.clip(rsi_signal, -1, 1)
        f1 = create_forecast_from_indicator(
            'RSI', rsi_signal, t_update, timeframe_hours=0.5, confidence=0.7
        )
        system.add_forecast(f1)
        print(f"  [RSI] Signal: {rsi_signal:+.2f} → Forecast: T={f1.T:.1f}h, "
              f"μ={f1.mu:+.4f}, v={f1.v:+.4f}, p={f1.p:.2f}")
        
        # Model 2: MACD-based (medium-term)
        macd_signal = np.random.normal(0.1, 0.4)
        macd_signal = np.clip(macd_signal, -1, 1)
        f2 = create_forecast_from_indicator(
            'MACD', macd_signal, t_update, timeframe_hours=1.5, confidence=0.65
        )
        system.add_forecast(f2)
        print(f"  [MACD] Signal: {macd_signal:+.2f} → Forecast: T={f2.T:.1f}h, "
              f"μ={f2.mu:+.4f}, v={f2.v:+.4f}, p={f2.p:.2f}")
        
        # Model 3: Volume profile (short-term, high confidence)
        volume_signal = np.random.normal(0.4, 0.25)
        volume_signal = np.clip(volume_signal, -1, 1)
        f3 = create_forecast_from_indicator(
            'Volume', volume_signal, t_update, timeframe_hours=0.25, confidence=0.8
        )
        system.add_forecast(f3)
        print(f"  [Volume] Signal: {volume_signal:+.2f} → Forecast: T={f3.T:.1f}h, "
              f"μ={f3.mu:+.4f}, v={f3.v:+.4f}, p={f3.p:.2f}")
        
        # Bayesian update with recent price change (if not first iteration)
        if t_update > 0:
            recent_return = returns[idx-1] if idx > 0 else 0
            system.bayesian_update(t_update, recent_return)
            print(f"  [Update] Recent return: {recent_return:+.4f} → Probabilities adjusted")
    
    print("\n" + "-" * 80)
    print(f"TOTAL FORECASTS IN SYSTEM: {len(system.forecasts)}")
    print("-" * 80)
    
    # Analyze current state at different time points
    print("\n" + "=" * 80)
    print("ENSEMBLE ANALYSIS AT KEY TIME POINTS")
    print("=" * 80)
    
    analysis_times = [0.5, 1.5, 2.5, 3.5, 4.5]
    
    results = []
    for t in analysis_times:
        n_active = len(system.get_active_forecasts(t))
        p_t = system.P_t(t, 'weighted')
        ci = system.get_confidence_interval(t, 0.95)
        
        results.append({
            'time': t,
            'active_forecasts': n_active,
            'probability': p_t,
            'ci_lower': ci[0],
            'ci_upper': ci[1],
            'expected': (ci[0] + ci[1]) / 2
        })
        
        print(f"\nt = {t:.1f}h:")
        print(f"  Active forecasts: {n_active}")
        print(f"  P(t): {p_t:.4f}")
        print(f"  Expected return: {results[-1]['expected']:+.4f} ({results[-1]['expected']*100:+.2f}%)")
        print(f"  95% CI: [{ci[0]:+.4f}, {ci[1]:+.4f}]")
        
        # Trading signal
        if results[-1]['expected'] > 0.01 and p_t > 0.65:
            signal = "🟢 STRONG BUY"
        elif results[-1]['expected'] > 0.005 and p_t > 0.55:
            signal = "🟡 WEAK BUY"
        elif results[-1]['expected'] < -0.01 and p_t > 0.65:
            signal = "🔴 STRONG SELL"
        elif results[-1]['expected'] < -0.005 and p_t > 0.55:
            signal = "🟠 WEAK SELL"
        else:
            signal = "⚪ NEUTRAL"
        
        print(f"  Signal: {signal}")
    
    # Create results DataFrame
    df = pd.DataFrame(results)
    
    # Generate comprehensive visualization
    print("\n" + "=" * 80)
    print("GENERATING VISUALIZATIONS")
    print("=" * 80)
    
    fig = plt.figure(figsize=(16, 12))
    
    # Plot 1: Price path with confidence bands
    ax1 = plt.subplot(3, 2, 1)
    ax1.plot(times, prices, 'b-', linewidth=2, label='Actual Price', alpha=0.7)
    ax1.set_xlabel('Time (hours)', fontsize=11)
    ax1.set_ylabel('Price ($)', fontsize=11)
    ax1.set_title('Bitcoin Price Path (Simulated)', fontsize=13, fontweight='bold')
    ax1.grid(True, alpha=0.3)
    ax1.legend()
    
    # Plot 2: Returns
    ax2 = plt.subplot(3, 2, 2)
    ax2.bar(times[1:], returns[1:] * 100, color=['g' if r > 0 else 'r' for r in returns[1:]], 
            alpha=0.6, width=0.08)
    ax2.axhline(y=0, color='k', linestyle='-', linewidth=0.5)
    ax2.set_xlabel('Time (hours)', fontsize=11)
    ax2.set_ylabel('Return (%)', fontsize=11)
    ax2.set_title('Price Returns', fontsize=13, fontweight='bold')
    ax2.grid(True, alpha=0.3)
    
    # Plot 3: Ensemble probability P(t)
    ax3 = plt.subplot(3, 2, 3)
    ax3.plot(df['time'], df['probability'], 'b-', linewidth=2, marker='o')
    ax3.fill_between(df['time'], 0, df['probability'], alpha=0.3)
    ax3.axhline(y=0.5, color='k', linestyle='--', alpha=0.5, label='Neutral')
    ax3.set_xlabel('Time (hours)', fontsize=11)
    ax3.set_ylabel('P(t)', fontsize=11)
    ax3.set_title('Ensemble Probability', fontsize=13, fontweight='bold')
    ax3.set_ylim(0, 1)
    ax3.grid(True, alpha=0.3)
    ax3.legend()
    
    # Plot 4: Expected returns with CI
    ax4 = plt.subplot(3, 2, 4)
    ax4.plot(df['time'], df['expected'] * 100, 'g-', linewidth=2, marker='o', label='Expected')
    ax4.fill_between(df['time'], df['ci_lower'] * 100, df['ci_upper'] * 100, 
                     alpha=0.3, color='green', label='95% CI')
    ax4.axhline(y=0, color='k', linestyle='-', linewidth=0.5)
    ax4.set_xlabel('Time (hours)', fontsize=11)
    ax4.set_ylabel('Expected Return (%)', fontsize=11)
    ax4.set_title('Expected Returns with Confidence Intervals', fontsize=13, fontweight='bold')
    ax4.grid(True, alpha=0.3)
    ax4.legend()
    
    # Plot 5: Active forecasts over time
    ax5 = plt.subplot(3, 2, 5)
    ax5.bar(df['time'], df['active_forecasts'], color='steelblue', alpha=0.7, width=0.3)
    ax5.set_xlabel('Time (hours)', fontsize=11)
    ax5.set_ylabel('Number of Active Forecasts', fontsize=11)
    ax5.set_title('Active Forecast Count', fontsize=13, fontweight='bold')
    ax5.grid(True, alpha=0.3)
    
    # Plot 6: Probability heatmap (simplified)
    ax6 = plt.subplot(3, 2, 6)
    t_vals = np.linspace(0, 5, 50)
    v_vals = np.linspace(-0.03, 0.03, 50)
    Z = np.zeros((50, 50))
    for i, t in enumerate(t_vals):
        for j, v in enumerate(v_vals):
            Z[j, i] = system.P_tv(t, v, 'time_decay')
    
    im = ax6.contourf(t_vals, v_vals * 100, Z, levels=30, cmap='viridis')
    plt.colorbar(im, ax=ax6, label='Density')
    ax6.set_xlabel('Time (hours)', fontsize=11)
    ax6.set_ylabel('Expected Return (%)', fontsize=11)
    ax6.set_title('P(t, v) Probability Heatmap', fontsize=13, fontweight='bold')
    
    plt.tight_layout()
    plt.savefig('trading_simulation.png', dpi=300, bbox_inches='tight')
    print("✓ Saved: trading_simulation.png")
    
    # Generate summary report
    print("\n" + "=" * 80)
    print("TRADING SUMMARY REPORT")
    print("=" * 80)
    
    print(f"\nSimulation Period: {times[0]:.1f}h to {times[-1]:.1f}h")
    print(f"Initial Price: ${prices[0]:,.2f}")
    print(f"Final Price: ${prices[-1]:,.2f}")
    print(f"Actual Return: {(prices[-1]/prices[0] - 1)*100:+.2f}%")
    print(f"\nTotal Forecasts Generated: {len(system.forecasts)}")
    print(f"Average Active Forecasts: {df['active_forecasts'].mean():.1f}")
    print(f"Average Ensemble Probability: {df['probability'].mean():.3f}")
    print(f"Average Expected Return: {df['expected'].mean()*100:+.2f}%")
    
    # Calculate accuracy (simplified)
    correct_direction = sum([
        1 for i in range(len(results)-1)
        if np.sign(results[i]['expected']) == np.sign(prices[int(results[i]['time']/0.1)] - prices[int(results[i]['time']/0.1) - 5])
    ])
    accuracy = correct_direction / (len(results) - 1) * 100 if len(results) > 1 else 0
    print(f"Directional Accuracy: {accuracy:.1f}%")
    
    print("\n" + "=" * 80)
    
    return system, df, times, prices, returns


if __name__ == "__main__":
    system, results_df, times, prices, returns = run_trading_simulation()
    
    print("\n🎯 Simulation complete! Check the output visualizations.")
