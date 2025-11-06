#!/usr/bin/env python3
"""
Enhanced regime detection with profit-aware labeling
Chỉ tạo BUY/SELL signals khi expected profit > 0.1% trading fees
"""

import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), '..'))

import numpy as np
import pandas as pd
from research.markov_regime_detection import train_and_detect
from typing import Optional, Tuple
import matplotlib.pyplot as plt

# Import profit-aware training (will create this if needed)
try:
    from src.training.profit_aware_training import apply_profit_aware_training_to_regime_model
except ImportError:
    print("Warning: profit_aware_training module not found, using fallback implementation")
    def apply_profit_aware_training_to_regime_model(X_market, regime_results, prices, min_profit_pct):
        # Fallback implementation
        return regime_results, None

def load_market_data(filepath: Optional[str] = None) -> Tuple[np.ndarray, np.ndarray]:
    """Load market data for training"""
    if filepath is None:
        # Generate synthetic data for demo
        print("Generating synthetic market data...")
        np.random.seed(42)
        T = 2000
        
        # Create realistic price series with multiple regimes
        returns = []
        volumes = []
        
        # Regime 1: Uptrend (0-600)
        trend1 = np.random.normal(0.0008, 0.015, 600)  
        vol1 = np.random.lognormal(8, 0.5, 600)
        
        # Regime 2: Crash (600-800) 
        trend2 = np.random.normal(-0.002, 0.025, 200)
        vol2 = np.random.lognormal(9, 0.7, 200)
        
        # Regime 3: Recovery (800-1200)
        trend3 = np.random.normal(0.001, 0.018, 400)
        vol3 = np.random.lognormal(8.2, 0.6, 400)
        
        # Regime 4: Sideways (1200-1600)
        trend4 = np.random.normal(0.0001, 0.012, 400)
        vol4 = np.random.lognormal(7.5, 0.4, 400)
        
        # Regime 5: Volatility spike (1600-2000)
        trend5 = np.random.normal(0.0003, 0.030, 400)
        vol5 = np.random.lognormal(8.8, 0.8, 400)
        
        returns = np.concatenate([trend1, trend2, trend3, trend4, trend5])
        volumes = np.concatenate([vol1, vol2, vol3, vol4, vol5])
        
        # Calculate prices
        prices = 100 * np.exp(np.cumsum(returns))
        
        # Add additional features
        momentum = np.zeros(T)
        volatility = np.zeros(T)
        
        for i in range(20, T):
            momentum[i] = np.mean(returns[i-10:i])
            volatility[i] = np.std(returns[i-20:i])
        
        # Construct market matrix
        X_market = np.column_stack([
            returns,
            volumes,
            momentum,
            volatility,
            np.random.randn(T)  # noise feature
        ])
        
        return X_market, prices
    
    else:
        # Load real data
        print(f"Loading market data from {filepath}...")
        df = pd.read_csv(filepath)
        
        # Assume columns: timestamp, open, high, low, close, volume
        prices = np.array(df['close'].values, dtype=np.float64)
        log_prices = np.log(prices)
        returns = np.diff(log_prices)
        returns = np.concatenate([[0.0], returns])  # Prepend 0 for first return
        volumes = np.array(df['volume'].values, dtype=np.float64)
        
        # Calculate additional features
        T = len(prices)
        momentum = np.zeros(T)
        volatility = np.zeros(T)
        rsi = np.zeros(T)
        
        for i in range(20, T):
            momentum[i] = np.mean(returns[i-10:i])
            volatility[i] = np.std(returns[i-20:i])
            
            # Simple RSI
            gains = np.maximum(returns[i-14:i], 0)
            losses = np.maximum(-returns[i-14:i], 0)
            avg_gain = np.mean(gains)
            avg_loss = np.mean(losses)
            if avg_loss != 0:
                rs = avg_gain / avg_loss
                rsi[i] = 100 - (100 / (1 + rs))
        
        X_market = np.column_stack([
            returns,
            volumes,
            momentum,
            volatility,
            rsi
        ])
        
        return X_market, prices

def enhanced_train_and_detect_with_profit_awareness(data_file: Optional[str] = None, 
                                                  min_profit_pct: float = 0.002,
                                                  confidence_threshold: float = 0.6):
    """
    Complete enhanced training workflow with profit-aware labeling
    """
    print("="*80)
    print("ENHANCED REGIME DETECTION WITH PROFIT-AWARE LABELING") 
    print(f"Minimum profit threshold: {min_profit_pct:.1%}")
    print(f"Trading fee: 0.1%")
    print("="*80)
    
    # Step 1: Load data
    X_market, prices = load_market_data(data_file)
    print(f"Loaded market data: {X_market.shape[0]} periods, {X_market.shape[1]} features")
    
    # Step 2: Traditional regime detection
    print("\n[1/3] Running traditional regime detection...")
    model, regime_results, traditional_signals = train_and_detect(X_market, confidence_threshold)
    
    # Step 3: Apply profit-aware enhancement
    print("\n[2/3] Applying profit-aware labeling...")
    enhanced_signals, profit_trainer = apply_profit_aware_training_to_regime_model(
        X_market, regime_results, prices, min_profit_pct
    )
    
    # Step 4: Compare results and visualize
    print("\n[3/3] Creating enhanced visualizations...")
    
    # Create comparison plot
    fig, axes = plt.subplots(4, 1, figsize=(15, 12))
    T = len(prices)
    time = np.arange(T)
    
    # Panel 1: Price and regimes
    ax1 = axes[0]
    ax1.plot(time, prices, 'k-', alpha=0.7, label='Price')
    ax1.set_ylabel('Price')
    ax1.set_title('Price Series with Regime Detection', fontweight='bold')
    ax1.legend()
    ax1.grid(alpha=0.3)
    
    # Color background by dominant regime
    regime_colors = ['lightgreen', 'lightcoral', 'lightyellow', 'lightblue', 'lightgray']
    for i in range(5):
        mask = regime_results['most_likely'] == i
        if np.any(mask):
            ax1.fill_between(time, ax1.get_ylim()[0], ax1.get_ylim()[1], 
                           where=mask, alpha=0.3, color=regime_colors[i])
    
    # Panel 2: Traditional signals
    ax2 = axes[1]
    trad_pos = traditional_signals['position']
    ax2.plot(time, trad_pos, 'b-', alpha=0.7, label='Traditional Signals')
    ax2.fill_between(time, 0, trad_pos, alpha=0.3)
    ax2.set_ylabel('Position')
    ax2.set_title('Traditional Regime-Based Signals', fontweight='bold')
    ax2.legend()
    ax2.grid(alpha=0.3)
    ax2.set_ylim([-1.2, 1.2])
    
    # Panel 3: Enhanced profit-aware signals
    ax3 = axes[2]
    enh_pos = enhanced_signals['position']
    ax3.plot(time, enh_pos, 'g-', alpha=0.7, label='Profit-Aware Signals')
    ax3.fill_between(time, 0, enh_pos, alpha=0.3, color='green')
    ax3.set_ylabel('Position') 
    ax3.set_title('Enhanced Profit-Aware Signals (Only when Expected Profit > 0.2%)', fontweight='bold')
    ax3.legend()
    ax3.grid(alpha=0.3)
    ax3.set_ylim([-1.2, 1.2])
    
    # Panel 4: Expected profit and confidence
    ax4 = axes[3]
    if 'expected_profit' in enhanced_signals:
        ax4.plot(time, enhanced_signals['expected_profit'] * 100, 'purple', 
                label='Expected Profit (%)', alpha=0.7)
    ax4.plot(time, enhanced_signals['confidence'] * 100, 'orange', 
            label='Signal Confidence (%)', alpha=0.7)
    ax4.axhline(y=min_profit_pct*100, color='red', linestyle='--', 
               label=f'Min Profit Threshold ({min_profit_pct:.1%})')
    ax4.set_ylabel('Percentage')
    ax4.set_xlabel('Time')
    ax4.set_title('Expected Profit and Signal Confidence', fontweight='bold')
    ax4.legend()
    ax4.grid(alpha=0.3)
    
    plt.tight_layout()
    plt.savefig('./enhanced_regime_analysis.png', dpi=150, bbox_inches='tight')
    print("Enhanced visualization saved to: enhanced_regime_analysis.png")
    
    # Calculate and compare performance metrics
    print("\n" + "="*80)
    print("PERFORMANCE COMPARISON")
    print("="*80)
    
    # Traditional signals stats
    trad_buy_pct = (trad_pos == 1).mean() * 100
    trad_sell_pct = (trad_pos == -1).mean() * 100
    trad_neutral_pct = (trad_pos == 0).mean() * 100
    
    print(f"\nTRADITIONAL SIGNALS:")
    print(f"  BUY signals:     {trad_buy_pct:5.1f}%")
    print(f"  SELL signals:    {trad_sell_pct:5.1f}%") 
    print(f"  NEUTRAL:         {trad_neutral_pct:5.1f}%")
    
    # Enhanced signals stats
    enh_buy_pct = (enh_pos == 1).mean() * 100
    enh_sell_pct = (enh_pos == -1).mean() * 100
    enh_neutral_pct = (enh_pos == 0).mean() * 100
    
    print(f"\nPROFIT-AWARE SIGNALS:")
    print(f"  BUY signals:     {enh_buy_pct:5.1f}%")
    print(f"  SELL signals:    {enh_sell_pct:5.1f}%")
    print(f"  NEUTRAL:         {enh_neutral_pct:5.1f}%")
    
    # Signal reduction
    total_traditional = trad_buy_pct + trad_sell_pct
    total_enhanced = enh_buy_pct + enh_sell_pct
    
    print(f"\nSIGNAL EFFICIENCY:")
    print(f"  Traditional total trading: {total_traditional:.1f}%")
    print(f"  Enhanced total trading:    {total_enhanced:.1f}%")
    print(f"  Reduction in trading:      {total_traditional - total_enhanced:.1f}%")
    
    if total_traditional > 0:
        efficiency_gain = (total_traditional - total_enhanced) / total_traditional * 100
        print(f"  Efficiency improvement:    {efficiency_gain:.1f}%")
    
    # Profit trainer statistics
    if profit_trainer is not None:
        trainer_stats = profit_trainer.get_statistics()
        if trainer_stats['total_signals'] > 0:
            print(f"\nPROFIT MODEL PERFORMANCE:")
            print(f"  Analyzed signals:    {trainer_stats['total_signals']}")
            print(f"  Win rate:           {trainer_stats['win_rate']:.1%}")
            print(f"  Average profit:     {trainer_stats['average_profit']:.4f} ({trainer_stats['average_profit']*100:.2f}%)")
            print(f"  Profit factor:      {trainer_stats['profit_factor']:.2f}")
    
    # Save models and results
    print(f"\n" + "="*80)
    print("SAVING RESULTS...")
    print("="*80)
    
    # Save enhanced results
    results_df = pd.DataFrame({
        'traditional_position': traditional_signals['position'],
        'enhanced_position': enhanced_signals['position'], 
        'confidence': enhanced_signals['confidence'],
        'expected_profit': enhanced_signals.get('expected_profit', np.zeros(len(enh_pos))),
        'regime': regime_results['most_likely'],
        'regime_entropy': regime_results['entropy']
    })
    
    results_df.to_csv('./enhanced_regime_results.csv', index=False)
    print("✓ Enhanced results saved to: enhanced_regime_results.csv")
    
    # Save profit model
    if profit_trainer is not None:
        profit_trainer.save_model('./profit_aware_model.joblib')
        print("✓ Profit model saved to: profit_aware_model.joblib")
    else:
        print("! Profit model not available (using fallback mode)")
    
    print("\n" + "="*80)
    print("✓ COMPLETE! Enhanced regime detection with profit-aware labeling finished.")
    print("Key improvements:")
    print("  - Reduced false signals that don't meet profit threshold")
    print("  - Historical profit tracking for better signal quality")
    print("  - Expected profit estimation for each signal")
    print("  - Adaptive learning from realized outcomes")
    print("="*80)
    
    return {
        'model': model,
        'regime_results': regime_results,
        'traditional_signals': traditional_signals,
        'enhanced_signals': enhanced_signals,
        'profit_trainer': profit_trainer,
        'prices': prices
    }

if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(description='Enhanced regime detection with profit-aware labeling')
    parser.add_argument('--data', type=str, help='Path to market data CSV file')
    parser.add_argument('--min-profit', type=float, default=0.002, 
                       help='Minimum expected profit threshold (default: 0.2%)')
    parser.add_argument('--confidence', type=float, default=0.6,
                       help='Confidence threshold for signal generation (default: 0.6)')
    
    args = parser.parse_args()
    
    results = enhanced_train_and_detect_with_profit_awareness(
        data_file=args.data,
        min_profit_pct=args.min_profit,
        confidence_threshold=args.confidence
    )
    
    print(f"\nTo use the enhanced model in live trading:")
    print("1. Load the profit model: profit_trainer.load_model('./profit_aware_model.joblib')")
    print("2. For each signal, check: expected_profit > min_threshold before trading")
    print("3. Update model with realized outcomes using: profit_trainer.updateHistory(...)")
    print("\nThis approach should significantly reduce unprofitable trades!")