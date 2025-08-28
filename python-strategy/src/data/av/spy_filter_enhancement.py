# ================================================================
# 3. MODIFICATION: Enhanced main.py (chỉ thêm phần SPY filter)
# ================================================================

# THÊM VÀO main.py class EnhancedTradingBot:


# Trong __init__ method của EnhancedTradingBot:
def __init__(self, config_path: str = 'config/trading_bot.yaml'):
    # ... existing code ...
    
    # ✅ ADD: SPY Market Filter
    alpha_vantage_key = self.config.get('alpha_vantage', {}).get('api_key')
    if alpha_vantage_key:
        self.spy_data_manager = SPYDataManager(alpha_vantage_key)
        self.spy_market_filter = SPYMarketFilter(self.config)
        self.spy_filter_enabled = True
        self.logger.info("📊 SPY market filter enabled")
    else:
        self.spy_filter_enabled = False
        self.logger.warning("⚠️ SPY filter disabled - no Alpha Vantage API key")

# ================================================================
# 4. MODIFICATION: Enhanced signal processing với SPY filter
# ================================================================

async def _process_enhanced_signal(self, symbol: str, signal_data: pd.Series, 
                                 market_data: pd.DataFrame, balance: dict):
    """Process enhanced trading signals with SPY FILTER"""
    try:
        # ✅ STEP 1: Get SPY market analysis
        spy_analysis = None
        if self.spy_filter_enabled:
            spy_data = await self.spy_data_manager.get_spy_data(30)
            if spy_data is not None:
                spy_analysis = self.spy_market_filter.analyze_spy_market(spy_data)
                
                # Check if we should pause trading
                should_pause, pause_reason = self.spy_market_filter.should_pause_trading(spy_analysis)
                if should_pause:
                    self.logger.warning(f"🛑 TRADING PAUSED for {symbol}: {pause_reason}")
                    await self.alert_manager.send_alert(
                        'WARNING',
                        f'🛑 Trading paused: {pause_reason}',
                        {'symbol': symbol, 'spy_analysis': spy_analysis}
                    )
                    return  # Exit without trading
        
        # ✅ STEP 2: Apply SPY signal adjustment
        original_signal = float(signal_data['final_signal'])
        original_strength = float(signal_data.get('final_strength', abs(original_signal)))
        
        adjusted_signal = original_signal
        adjusted_strength = original_strength
        adjustment_reason = "No SPY filter"
        
        if spy_analysis:
            # Apply SPY-based signal adjustment
            adjusted_strength, adjustment_reason = self.spy_market_filter.get_btc_signal_adjustment(
                spy_analysis, original_strength
            )
            
            # Recalculate signal direction
            if abs(adjusted_strength) < 0.3:  # Below threshold
                adjusted_signal = 0
            else:
                adjusted_signal = 1 if adjusted_strength > 0 else -1
        
        # ✅ STEP 3: Log SPY analysis and adjustments
        if spy_analysis:
            regime = spy_analysis['regime'].value
            momentum = spy_analysis['momentum_5d']
            spy_price = spy_analysis['current_price']
            
            self.logger.info(f"📊 SPY Analysis for {symbol}:")
            self.logger.info(f"   Regime: {regime}, Momentum 5d: {momentum:+.1%}")
            self.logger.info(f"   SPY Price: ${spy_price:.2f}")
            self.logger.info(f"   Signal adjustment: {original_strength:.2f} → {adjusted_strength:.2f}")
            self.logger.info(f"   Reason: {adjustment_reason}")
            
            # ✅ STEP 4: Enhanced alerting with SPY context
            if original_signal != adjusted_signal:
                self.logger.warning(f"🔄 SPY FILTER CHANGED SIGNAL: {original_signal} → {adjusted_signal}")
        
        # ✅ STEP 5: Continue with adjusted signal
        if adjusted_signal < 0:  # SELL signal
            await self._handle_enhanced_sell_signal(symbol, signal_data, balance)
        elif adjusted_signal > 0:  # BUY signal  
            # Create enhanced signal data with SPY context
            enhanced_signal_data = signal_data.copy()
            enhanced_signal_data['final_signal'] = adjusted_signal
            enhanced_signal_data['final_strength'] = adjusted_strength
            enhanced_signal_data['spy_regime'] = spy_analysis['regime'].value if spy_analysis else 'unknown'
            enhanced_signal_data['spy_momentum'] = spy_analysis['momentum_5d'] if spy_analysis else 0.0
            enhanced_signal_data['adjustment_reason'] = adjustment_reason
            
            await self._handle_enhanced_buy_signal(symbol, enhanced_signal_data, market_data, balance)
        else:
            self.logger.info(f"📊 {symbol}: No signal after SPY filter (strength: {adjusted_strength:.3f})")
            
    except Exception as e:
        self.logger.error(f"❌ Enhanced signal processing failed for {symbol}: {e}")
        import traceback
        traceback.print_exc()

# ================================================================
# 5. CONFIG: Thêm vào config/trading_bot.yaml
# ================================================================

# THÊM section này vào config file:
"""
# Alpha Vantage API for SPY market filter
alpha_vantage:
  api_key: ${ALPHA_VANTAGE_API_KEY}
  
# SPY Market Filter settings
spy_market_filter:
  enabled: true
  correlation_strength: 0.50  # BTC-SPY correlation từ analysis
  
  # Signal adjustment factors
  bull_regime_amplify: 1.3     # Amplify bullish signals in bull market
  bear_regime_reduce: 0.6      # Reduce bullish signals in bear market
  volatile_regime_reduce: 0.8  # Reduce all signals in volatile market
  
  # Thresholds
  bull_momentum_threshold: 0.02   # 2% 5-day gain = bull
  bear_momentum_threshold: -0.02  # 2% 5-day loss = bear
  high_vol_threshold: 0.02        # 2% volatility = high vol
  
  # Emergency pause conditions
  flash_crash_threshold: -0.05    # 5% drop in 3 days = pause
  extreme_volatility: 0.03        # 3% daily volatility = pause
"""

# ================================================================
# 6. TESTING: Test SPY filter functionality  
# ================================================================

async def test_spy_filter():
    """Test SPY filter functionality"""
    
    print("🧪 TESTING SPY FILTER")
    print("=" * 50)
    
    import os
    api_key = os.getenv('ALPHA_VANTAGE_API_KEY')
    if not api_key:
        print("❌ No Alpha Vantage API key found")
        return
    
    # Test SPY data fetching
    spy_manager = SPYDataManager(api_key)
    spy_data = await spy_manager.get_spy_data(30)
    
    if spy_data is None:
        print("❌ Failed to fetch SPY data")
        return
    
    print(f"✅ SPY data fetched: {len(spy_data)} days")
    print(f"   Latest: {spy_data['date'].max()}")
    print(f"   Price: ${spy_data['close'].iloc[-1]:.2f}")
    
    # Test market filter
    config = {}
    spy_filter = SPYMarketFilter(config)
    spy_analysis = spy_filter.analyze_spy_market(spy_data)
    
    print(f"\n📊 SPY Market Analysis:")
    print(f"   Regime: {spy_analysis['regime'].value}")
    print(f"   5-day momentum: {spy_analysis['momentum_5d']:+.2%}")
    print(f"   Strong uptrend: {spy_analysis['strong_uptrend']}")
    print(f"   Volatility: {spy_analysis['volatility']:.3f}")
    
    # Test signal adjustments
    print(f"\n🎯 Signal Adjustment Tests:")
    test_signals = [0.5, -0.5, 0.8, -0.3]
    
    for original_signal in test_signals:
        adjusted, reason = spy_filter.get_btc_signal_adjustment(spy_analysis, original_signal)
        print(f"   {original_signal:+.1f} → {adjusted:+.2f} ({reason})")
    
    # Test pause conditions
    should_pause, pause_reason = spy_filter.should_pause_trading(spy_analysis)
    print(f"\n🛑 Should pause trading: {should_pause}")
    if should_pause:
        print(f"   Reason: {pause_reason}")
    
    print(f"\n🎉 SPY filter test completed!")

if __name__ == "__main__":
    import asyncio
    asyncio.run(test_spy_filter())