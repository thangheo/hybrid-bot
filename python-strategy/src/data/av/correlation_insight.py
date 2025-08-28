#!/usr/bin/env python3
"""
BTC Correlation Analysis & Trading Insights
Analysis of your successful correlation results
"""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns

def analyze_correlation_results():
    """
    Analyze the correlation results from your successful run
    """
    # Your correlation matrix results
    correlation_data = {
        'BTC': [1.0, 0.5013, 0.4979, 0.0884, 0.0371, 0.5065, 0.1623, -0.0524],
        'SPY': [0.5013, 1.0, 0.9848, -0.1286, 0.2852, 0.9981, 0.3672, -0.2732],
        'QQQ': [0.4979, 0.9848, 1.0, -0.1446, 0.2316, 0.9829, 0.3840, -0.2785],
        'GLD': [0.0884, -0.1286, -0.1446, 1.0, 0.0327, -0.1370, -0.6540, 0.6580],
        'TLT': [0.0371, 0.2852, 0.2316, 0.0327, 1.0, 0.2823, -0.1315, 0.0080],
        'VTI': [0.5065, 0.9981, 0.9829, -0.1370, 0.2823, 1.0, 0.3653, -0.2724],
        'USD/JPY': [0.1623, 0.3672, 0.3840, -0.6540, -0.1315, 0.3653, 1.0, -0.8186],
        'EUR/USD': [-0.0524, -0.2732, -0.2785, 0.6580, 0.0080, -0.2724, -0.8186, 1.0]
    }
    
    assets = ['BTC', 'SPY', 'QQQ', 'GLD', 'TLT', 'VTI', 'USD/JPY', 'EUR/USD']
    correlation_matrix = pd.DataFrame(correlation_data, index=assets)
    
    print("🔍 COMPREHENSIVE BTC CORRELATION ANALYSIS")
    print("=" * 60)
    
    # 1. BTC Correlation Rankings
    btc_correlations = correlation_matrix['BTC'].drop('BTC').sort_values(key=abs, ascending=False)
    
    print("\n📊 BTC CORRELATIONS (Ranked by Strength):")
    print("-" * 50)
    
    for asset, corr in btc_correlations.items():
        if abs(corr) > 0.5:
            strength = "🔥 STRONG"
            color = "📈" if corr > 0 else "📉"
        elif abs(corr) > 0.3:
            strength = "💪 MODERATE"
            color = "📈" if corr > 0 else "📉"
        elif abs(corr) > 0.1:
            strength = "⚡ WEAK"
            color = "📈" if corr > 0 else "📉"
        else:
            strength = "❄️  MINIMAL"
            color = "📊"
        
        print(f"   {color} {asset:<8}: {corr:+.3f} ({strength})")
    
    # 2. Key Insights
    print(f"\n💡 KEY INSIGHTS:")
    print("-" * 30)
    
    # Strong equity correlations
    strong_equity_corrs = btc_correlations[btc_correlations > 0.4]
    if len(strong_equity_corrs) > 0:
        print(f"✅ STRONG EQUITY CORRELATIONS:")
        for asset, corr in strong_equity_corrs.items():
            print(f"   • BTC moves strongly with {asset} ({corr:.3f})")
            if asset in ['SPY', 'QQQ', 'VTI']:
                print(f"     → BTC behaves like a RISK ASSET, not safe haven")
    
    # Safe haven analysis
    gld_corr = btc_correlations.get('GLD', 0)
    print(f"\n🥇 SAFE HAVEN ANALYSIS:")
    if abs(gld_corr) < 0.2:
        print(f"   • BTC-Gold correlation: {gld_corr:.3f} (WEAK)")
        print(f"   → BTC and Gold are largely INDEPENDENT")
        print(f"   → BTC is NOT a traditional safe haven like Gold")
    
    # Forex insights
    usdjpy_corr = btc_correlations.get('USD/JPY', 0)
    eurusd_corr = btc_correlations.get('EUR/USD', 0)
    
    print(f"\n💱 FOREX INSIGHTS:")
    print(f"   • USD/JPY correlation: {usdjpy_corr:+.3f}")
    print(f"   • EUR/USD correlation: {eurusd_corr:+.3f}")
    
    if usdjpy_corr > 0.1:
        print(f"   → BTC tends to rise when USD/JPY rises (risk-on)")
    if abs(eurusd_corr) < 0.1:
        print(f"   → BTC largely independent of EUR/USD")
    
    # 3. Trading Strategy Implications
    print(f"\n🎯 TRADING STRATEGY IMPLICATIONS:")
    print("-" * 40)
    
    print(f"📈 RISK-ON ENVIRONMENT INDICATORS:")
    risk_on_assets = ['SPY', 'QQQ', 'VTI']
    for asset in risk_on_assets:
        if asset in btc_correlations:
            corr = btc_correlations[asset]
            if corr > 0.4:
                print(f"   • When {asset} is bullish → BTC likely bullish too")
    
    print(f"\n📉 RISK-OFF SCENARIOS:")
    print(f"   • If SPY/QQQ crash → BTC likely to follow (high correlation)")
    print(f"   • Gold rising strongly → neutral for BTC (low correlation)")
    print(f"   • USD strengthening → mixed signals for BTC")
    
    # 4. Market Regime Classification
    print(f"\n🌍 MARKET REGIME CLASSIFICATION:")
    print("-" * 35)
    
    avg_equity_corr = np.mean([btc_correlations['SPY'], btc_correlations['QQQ'], btc_correlations['VTI']])
    
    if avg_equity_corr > 0.5:
        regime = "🔥 HIGH-BETA RISK ASSET"
        print(f"   Current Regime: {regime}")
        print(f"   • BTC moves strongly with stocks ({avg_equity_corr:.3f} avg)")
        print(f"   • Treat BTC like a high-beta tech stock")
        print(f"   • Use equity market signals for BTC timing")
    elif avg_equity_corr > 0.3:
        regime = "⚡ MODERATE RISK ASSET"  
        print(f"   Current Regime: {regime}")
        print(f"   • BTC partially follows equity markets")
        print(f"   • Some independent movement remains")
    else:
        regime = "🌟 INDEPENDENT ASSET"
        print(f"   Current Regime: {regime}")
        print(f"   • BTC moves independently from traditional markets")
    
    return correlation_matrix, btc_correlations

def create_trading_signals_framework(btc_correlations):
    """
    Create actionable trading signals framework
    """
    print(f"\n🚀 ACTIONABLE TRADING SIGNALS FRAMEWORK")
    print("=" * 55)
    
    # Signal Categories
    signals = {
        'primary_confirmation': [],
        'secondary_confirmation': [],  
        'leading_indicators': [],
        'divergence_opportunities': []
    }
    
    # Categorize assets by correlation strength
    for asset, corr in btc_correlations.items():
        if abs(corr) > 0.4:
            signals['primary_confirmation'].append((asset, corr))
        elif abs(corr) > 0.2:
            signals['secondary_confirmation'].append((asset, corr))
        elif abs(corr) > 0.1:
            signals['leading_indicators'].append((asset, corr))
        else:
            signals['divergence_opportunities'].append((asset, corr))
    
    # Print signal framework
    print(f"\n1️⃣ PRIMARY CONFIRMATION SIGNALS (|corr| > 0.4):")
    for asset, corr in signals['primary_confirmation']:
        direction = "same direction" if corr > 0 else "opposite direction"
        print(f"   • {asset}: When {asset} moves strong, BTC likely moves {direction}")
        print(f"     Strategy: Use {asset} breakouts to confirm BTC entries")
    
    if signals['secondary_confirmation']:
        print(f"\n2️⃣ SECONDARY CONFIRMATION (0.2 < |corr| < 0.4):")
        for asset, corr in signals['secondary_confirmation']:
            print(f"   • {asset}: Moderate correlation ({corr:+.3f})")
            print(f"     Strategy: Supporting evidence for BTC direction")
    
    if signals['leading_indicators']:
        print(f"\n3️⃣ POTENTIAL LEADING INDICATORS (0.1 < |corr| < 0.2):")
        for asset, corr in signals['leading_indicators']:
            print(f"   • {asset}: Weak correlation - may lead BTC moves")
            print(f"     Strategy: Monitor for early warning signals")
    
    if signals['divergence_opportunities']:
        print(f"\n4️⃣ DIVERGENCE OPPORTUNITIES (|corr| < 0.1):")
        for asset, corr in signals['divergence_opportunities']:
            print(f"   • {asset}: Independent movement from BTC")
            print(f"     Strategy: Look for divergence trading opportunities")

def create_implementation_roadmap():
    """
    Create step-by-step implementation roadmap
    """
    print(f"\n📋 IMPLEMENTATION ROADMAP")
    print("=" * 35)
    
    roadmap = {
        "Week 1": {
            "title": "🎯 Focus on Strongest Correlations",
            "actions": [
                "Monitor SPY, QQQ, VTI for BTC confirmation signals",
                "Create alerts when these assets break key levels",
                "Backtest: Does BTC follow within 24-48 hours?"
            ]
        },
        "Week 2": {
            "title": "💱 Add Forex Context",
            "actions": [
                "Track USD/JPY for risk sentiment (0.162 correlation)",
                "Rising USD/JPY = risk-on = potentially bullish BTC",
                "Create USD/JPY momentum indicators"
            ]
        },
        "Week 3": {
            "title": "🥇 Safe Haven Monitoring",
            "actions": [
                "Monitor Gold (GLD) as independent indicator",
                "When Gold spikes, BTC may not follow (low correlation)",
                "Use Gold divergence for BTC opportunities"
            ]
        },
        "Week 4": {
            "title": "🔄 Regime Detection System",
            "actions": [
                "Build composite risk-on/risk-off indicator",
                "Combine equity correlations for regime classification",
                "Adjust BTC strategy based on current regime"
            ]
        }
    }
    
    for week, plan in roadmap.items():
        print(f"\n{week}: {plan['title']}")
        for i, action in enumerate(plan['actions'], 1):
            print(f"   {i}. {action}")

def generate_specific_trade_ideas():
    """
    Generate specific, actionable trade ideas
    """
    print(f"\n💰 SPECIFIC TRADE IDEAS")
    print("=" * 30)
    
    print(f"\n🎯 CORRELATION-BASED STRATEGIES:")
    
    print(f"\n1. EQUITY CONFIRMATION STRATEGY:")
    print(f"   Setup: SPY/QQQ breaks above key resistance")
    print(f"   Action: Look for BTC to follow within 1-2 days")
    print(f"   Logic: 0.50+ correlation suggests BTC will catch up")
    print(f"   Risk: If BTC doesn't follow, correlation may be weakening")
    
    print(f"\n2. RISK-ON/RISK-OFF FILTER:")
    print(f"   Setup: USD/JPY rising + SPY momentum positive")
    print(f"   Action: Increase BTC position size")
    print(f"   Logic: Double confirmation of risk-on environment")
    print(f"   Risk: Correlations can break during crisis periods")
    
    print(f"\n3. GOLD DIVERGENCE PLAY:")
    print(f"   Setup: Gold spiking but BTC not following")
    print(f"   Action: Buy BTC if other risk assets stable")
    print(f"   Logic: BTC independent of Gold (0.088 correlation)")
    print(f"   Risk: May indicate broader market stress")
    
    print(f"\n4. REGIME CHANGE DETECTION:")
    print(f"   Setup: BTC correlation with SPY breaks down")
    print(f"   Action: Reduce position size, wait for new regime")
    print(f"   Logic: Correlation changes signal regime shifts")
    print(f"   Risk: May miss strong trending moves")
    
    print(f"\n⚠️  IMPORTANT DISCLAIMERS:")
    print(f"   • Correlations change over time - update regularly")
    print(f"   • Past correlation ≠ future correlation")
    print(f"   • Use as context, not primary trading signals")
    print(f"   • Always combine with your existing crypto analysis")

def main():
    """
    Run comprehensive analysis of correlation results
    """
    print("📊 ANALYZING YOUR SUCCESSFUL BTC CORRELATION RESULTS")
    print("=" * 65)
    
    # Analyze correlation results
    correlation_matrix, btc_correlations = analyze_correlation_results()
    
    # Create trading framework
    create_trading_signals_framework(btc_correlations)
    
    # Implementation roadmap
    create_implementation_roadmap()
    
    # Specific trade ideas
    generate_specific_trade_ideas()
    
    print(f"\n✅ ANALYSIS COMPLETE!")
    print(f"\n🎉 CONGRATULATIONS! You've successfully:")
    print(f"   • Fixed the Alpha Vantage crypto API integration")
    print(f"   • Generated real BTC correlation data")
    print(f"   • Identified BTC as a high-beta risk asset (0.50+ equity correlation)")
    print(f"   • Created actionable trading signals framework")
    
    print(f"\n🚀 NEXT STEPS:")
    print(f"   1. Implement equity market monitoring in your trading bot")
    print(f"   2. Create SPY/QQQ momentum filters for BTC signals")
    print(f"   3. Add USD/JPY risk sentiment indicator")
    print(f"   4. Build regime detection system")
    print(f"   5. Backtest correlation-based entry/exit timing")

if __name__ == "__main__":
    main()
