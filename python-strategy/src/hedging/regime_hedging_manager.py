"""
Regime-based Hedging Manager
Tích hợp hedging BTC dựa trên regime và p_trend_up từ regime_publisher
"""

import os
import time
import json
from typing import Optional, Dict, Any
from datetime import datetime, timezone
import threading
import logging

# Try to import ccxt, handle gracefully if not available
try:
    import ccxt
    CCXT_AVAILABLE = True
except ImportError:
    CCXT_AVAILABLE = False
    print("Warning: ccxt not available. Install with: pip install ccxt")

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


class RegimeHedgingManager:
    def __init__(self, config: Dict):
        """
        Khởi tạo Hedging Manager
        
        Args:
            config: Dictionary chứa cấu hình hedging
        """
        self.config = config
        self.api_key = os.getenv('BINANCE_API_KEY') or config.get('api_key')
        self.api_secret = os.getenv('BINANCE_API_SECRET') or config.get('api_secret')
        self.testnet = config.get('testnet', True)
        
        # Thresholds
        self.emergency_p_trend_threshold = config.get('emergency_p_trend_threshold', 0.01)
        self.close_p_trend_threshold = config.get('close_p_trend_threshold', 0.5)  # Lowered from 0.9 to 0.5
        self.hedge_amount = config.get('hedge_amount', 0.01)  # BTC
        self.leverage = config.get('leverage', 10)
        
        # Risk management
        self.take_profit_pct = config.get('take_profit_pct', 0.01)  # 1% profit
        self.stop_loss_pct = config.get('stop_loss_pct', 0.02)     # 2% loss  
        self.max_hold_minutes = config.get('max_hold_minutes', 60)  # 60 min max hold
        
        # State tracking
        self.is_hedged = False
        self.current_short_order = None
        self.last_hedge_time = None
        self.entry_price = None  # Track entry price for TP/SL
        self.lock = threading.Lock()
        
        # Initialize exchange
        self._init_exchange()
        
    def _init_exchange(self):
        """Initialize Binance exchange connection"""
        if not CCXT_AVAILABLE or 'ccxt' not in globals():
            logger.error("ccxt library not available")
            self.exchange = None
            return
            
        try:
            if self.testnet:
                self.exchange = ccxt.binance({  # type: ignore
                    'apiKey': str(self.api_key) if self.api_key else '',
                    'secret': str(self.api_secret) if self.api_secret else '',
                    'options': {
                        'defaultType': 'future',
                        'adjustForTimeDifference': True,
                    },
                    'urls': {
                        'api': {
                            'public': 'https://testnet.binancefuture.com/fapi/v1',
                            'private': 'https://testnet.binancefuture.com/fapi/v1',
                        }
                    }
                })
                logger.info("🔧 Using TESTNET for hedging")
            else:
                self.exchange = ccxt.binance({  # type: ignore
                    'apiKey': str(self.api_key) if self.api_key else '',
                    'secret': str(self.api_secret) if self.api_secret else '',
                    'options': {
                        'defaultType': 'future',
                        'adjustForTimeDifference': True,
                    }
                })
                logger.warning("⚠️ Using MAINNET for hedging")
            
            self.symbol = 'BTC/USDT'
            self.exchange.load_markets()
            
            # Check if symbol exists and find correct format
            markets = self.exchange.markets or {}
            if self.symbol not in markets:
                # Try alternative formats for Binance Futures
                alt_symbols = ['BTCUSDT', 'BTC/USDT:USDT', 'BTC-USDT']
                for alt in alt_symbols:
                    if alt in markets:
                        self.symbol = alt
                        logger.info(f"Using symbol: {self.symbol}")
                        break
                else:
                    logger.warning(f"Symbol not found in markets, using default: {self.symbol}")
            
            # Set leverage
            self._set_leverage(self.leverage)
            
        except Exception as e:
            logger.error(f"Failed to initialize exchange: {e}")
            self.exchange = None
    
    def _set_leverage(self, leverage: int):
        """Set leverage for BTC/USDT"""
        try:
            if self.exchange:
                self.exchange.set_leverage(leverage, self.symbol)
                logger.info(f"✅ Set leverage {leverage}x for {self.symbol}")
        except Exception as e:
            logger.error(f"❌ Failed to set leverage: {e}")
    
    def get_current_price(self) -> Optional[float]:
        """Get current BTC price"""
        try:
            if not self.exchange:
                return None
            ticker = self.exchange.fetch_ticker(self.symbol)
            return ticker['last']
        except Exception as e:
            logger.error(f"Failed to get current price: {e}")
            return None
    
    def check_tp_sl_conditions(self, current_price: float) -> Dict[str, Any]:
        """
        Check take profit, stop loss, and time-based exit conditions
        
        Returns:
            Dict with should_exit, reason, pnl_pct, time_held_minutes
        """
        result = {
            'should_exit': False,
            'reason': '',
            'pnl_pct': 0.0,
            'time_held_minutes': 0.0
        }
        
        if not self.is_hedged or not self.entry_price or not self.last_hedge_time:
            return result
        
        # Calculate PnL % (for SHORT: profit when price goes down)
        pnl_pct = (self.entry_price - current_price) / self.entry_price
        result['pnl_pct'] = pnl_pct
        
        # Calculate time held
        time_held = datetime.now(timezone.utc) - self.last_hedge_time
        time_held_minutes = time_held.total_seconds() / 60
        result['time_held_minutes'] = time_held_minutes
        
        # Check take profit
        if pnl_pct >= self.take_profit_pct:
            result['should_exit'] = True
            result['reason'] = f'Take Profit: {pnl_pct*100:.2f}% >= {self.take_profit_pct*100:.2f}%'
            return result
        
        # Check stop loss  
        if pnl_pct <= -self.stop_loss_pct:
            result['should_exit'] = True
            result['reason'] = f'Stop Loss: {pnl_pct*100:.2f}% <= -{self.stop_loss_pct*100:.2f}%'
            return result
            
        # Check max hold time
        if time_held_minutes >= self.max_hold_minutes:
            result['should_exit'] = True
            result['reason'] = f'Max Hold Time: {time_held_minutes:.1f}min >= {self.max_hold_minutes}min'
            return result
            
        return result
    
    def get_positions(self) -> list:
        """Get current positions"""
        try:
            if not self.exchange:
                return []
            positions = self.exchange.fetch_positions([self.symbol])
            active_positions = [p for p in positions if float(p['contracts']) != 0]
            return active_positions
        except Exception as e:
            logger.error(f"Failed to get positions: {e}")
            return []
    
    def has_short_position(self) -> bool:
        """Check if we have an active short position"""
        positions = self.get_positions()
        for pos in positions:
            if pos['side'].lower() == 'short' and float(pos['contracts']) != 0:
                return True
        return False
    
    def open_short_hedge(self) -> bool:
        """
        Open short position for hedging
        
        Returns:
            True if successful, False otherwise
        """
        try:
            if not self.exchange:
                logger.error("Exchange not initialized")
                return False
            
            # Check if already hedged
            if self.has_short_position():
                logger.info("Already have short position, skipping")
                self.is_hedged = True
                return True
            
            current_price = self.get_current_price()
            if not current_price:
                logger.error("Cannot get current price")
                return False
            
            # Open SHORT position using LIMIT order for better execution
            # Set limit price slightly above current price to ensure fill
            limit_price = current_price * 1.0005  # 0.05% above current price
            
            order = self.exchange.create_limit_sell_order(
                symbol=self.symbol,
                amount=self.hedge_amount,
                price=limit_price
            )
            
            with self.lock:
                self.is_hedged = True
                self.current_short_order = order
                self.last_hedge_time = datetime.now(timezone.utc)
                self.entry_price = limit_price  # Track entry price for TP/SL
            
            logger.info(f"🔴 OPENED SHORT HEDGE POSITION (LIMIT ORDER)")
            logger.info(f"📊 Symbol: {self.symbol}")
            logger.info(f"💰 Amount: {self.hedge_amount} BTC")
            logger.info(f"💵 Market Price: ${current_price:,.2f}")
            logger.info(f"💵 Limit Price: ${limit_price:,.2f}")
            logger.info(f"📈 Leverage: {self.leverage}x")
            logger.info(f"🆔 Order ID: {order['id']}")
            
            return True
            
        except Exception as e:
            logger.error(f"❌ Failed to open short hedge: {e}")
            return False
    
    def close_short_hedge(self) -> bool:
        """
        Close short hedge position
        
        Returns:
            True if successful, False otherwise
        """
        try:
            if not self.exchange:
                logger.error("Exchange not initialized")
                return False
            
            # Get current short position
            positions = self.get_positions()
            short_position = None
            
            for pos in positions:
                if pos['side'].lower() == 'short':
                    short_position = pos
                    break
            
            if not short_position:
                logger.info("No short position to close")
                with self.lock:
                    self.is_hedged = False
                    self.current_short_order = None
                    self.entry_price = None
                return True
            
            amount = abs(float(short_position['contracts']))
            
            if amount == 0:
                logger.info("Short position already closed")
                with self.lock:
                    self.is_hedged = False
                    self.current_short_order = None
                    self.entry_price = None
                return True
            
            current_price = self.get_current_price()
            
            # Close SHORT by buying back
            order = self.exchange.create_market_buy_order(
                symbol=self.symbol,
                amount=amount,
                params={
                    'reduceOnly': True
                }
            )
            
            with self.lock:
                self.is_hedged = False
                self.current_short_order = None
                self.entry_price = None
            
            logger.info(f"✅ CLOSED SHORT HEDGE POSITION")
            logger.info(f"📊 Symbol: {self.symbol}")
            logger.info(f"💰 Amount: {amount} BTC")
            logger.info(f"💵 Close Price: ${current_price:,.2f}")
            logger.info(f"🆔 Order ID: {order['id']}")
            
            return True
            
        except Exception as e:
            logger.error(f"❌ Failed to close short hedge: {e}")
            return False
    
    def process_regime_signal(self, btc_regime_data: Dict) -> Dict:
        """
        Process regime signal and make hedging decisions
        
        Args:
            btc_regime_data: BTC regime data from regime publisher
            
        Returns:
            Dictionary with hedging action taken
        """
        result = {
            'timestamp': datetime.now(timezone.utc).isoformat(),
            'action': 'none',
            'reason': '',
            'success': False,
            'current_price': None,
            'is_hedged': self.is_hedged
        }
        
        try:
            # Extract p_trend_up value
            p_trend_up = btc_regime_data.get('p_trend_up')
            regime = btc_regime_data.get('regime', -1)
            
            if p_trend_up is None:
                result['reason'] = 'p_trend_up not available'
                return result
            
            current_price = self.get_current_price()
            result['current_price'] = current_price
            
            # First priority: Check TP/SL conditions if we have a position
            if self.is_hedged and current_price:
                tp_sl_check = self.check_tp_sl_conditions(current_price)
                if tp_sl_check['should_exit']:
                    result['action'] = 'close_short'
                    result['reason'] = f"Risk Management Exit: {tp_sl_check['reason']} (PnL: {tp_sl_check['pnl_pct']*100:.2f}%, Time: {tp_sl_check['time_held_minutes']:.1f}min)"
                    
                    success = self.close_short_hedge()
                    result['success'] = success
                    
                    if success:
                        logger.info(f"💰 TP/SL EXIT: {tp_sl_check['reason']}")
                    else:
                        logger.error(f"Failed to execute TP/SL exit")
                    
                    return result
            
            # Second priority: Decision logic based on thresholds with additional filters
            if (p_trend_up <= self.emergency_p_trend_threshold and 
                btc_regime_data.get('micro_ok', True) and  # Market structure OK
                btc_regime_data.get('stay_prob', 1.0) > 0.5):  # Regime somewhat stable
                # Market dump detected with confirmations - open hedge if not already hedged
                if not self.is_hedged:
                    result['action'] = 'open_short'
                    micro_ok = btc_regime_data.get('micro_ok', True)
                    stay_prob = btc_regime_data.get('stay_prob', 1.0)
                    result['reason'] = f'Market dump confirmed: p_trend_up={p_trend_up:.4f} <= {self.emergency_p_trend_threshold}, micro_ok={micro_ok}, stay_prob={stay_prob:.3f}'
                    
                    success = self.open_short_hedge()
                    result['success'] = success
                    
                    if success:
                        logger.info(f"🚨 HEDGE ACTIVATED: Market dump detected (p_trend_up={p_trend_up:.4f})")
                    else:
                        logger.error(f"Failed to activate hedge")
                else:
                    result['action'] = 'hold_short'
                    micro_ok = btc_regime_data.get('micro_ok', True)
                    stay_prob = btc_regime_data.get('stay_prob', 1.0)
                    result['reason'] = f'Already hedged, conditions confirmed: p_trend_up={p_trend_up:.4f}, micro_ok={micro_ok}, stay_prob={stay_prob:.3f}'
                    result['success'] = True
                    
            elif p_trend_up <= self.emergency_p_trend_threshold:
                # Market dump detected but filters failed
                micro_ok = btc_regime_data.get('micro_ok', True)
                stay_prob = btc_regime_data.get('stay_prob', 1.0)
                result['action'] = 'wait'
                result['reason'] = f'Market dump but filters failed: p_trend_up={p_trend_up:.4f}, micro_ok={micro_ok}, stay_prob={stay_prob:.3f}'
                result['success'] = True
                logger.warning(f"⚠️ HEDGE BLOCKED: {result['reason']}")
                    
            elif p_trend_up >= self.close_p_trend_threshold:
                # Market recovery detected - close hedge if currently hedged
                if self.is_hedged:
                    result['action'] = 'close_short'
                    result['reason'] = f'Market recovery detected: p_trend_up={p_trend_up:.4f} >= {self.close_p_trend_threshold}'
                    
                    success = self.close_short_hedge()
                    result['success'] = success
                    
                    if success:
                        logger.info(f"💚 HEDGE CLOSED: Market recovery detected (p_trend_up={p_trend_up:.4f})")
                    else:
                        logger.error(f"Failed to close hedge")
                else:
                    result['action'] = 'no_position'
                    result['reason'] = f'No hedge position, market trending up: p_trend_up={p_trend_up:.4f}'
                    result['success'] = True
            else:
                # Neutral zone - maintain current state
                if self.is_hedged:
                    result['action'] = 'hold_short'
                    result['reason'] = f'Neutral zone, maintaining hedge: p_trend_up={p_trend_up:.4f}'
                else:
                    result['action'] = 'wait'
                    result['reason'] = f'Neutral zone, waiting for signal: p_trend_up={p_trend_up:.4f}'
                result['success'] = True
            
        except Exception as e:
            result['reason'] = f'Error processing signal: {str(e)}'
            logger.error(f"Error in process_regime_signal: {e}")
        
        return result
    
    def get_status(self) -> Dict:
        """Get current hedging status"""
        positions = self.get_positions()
        current_price = self.get_current_price()
        
        status = {
            'timestamp': datetime.now(timezone.utc).isoformat(),
            'is_hedged': self.is_hedged,
            'current_price': current_price,
            'positions': positions,
            'config': {
                'emergency_threshold': self.emergency_p_trend_threshold,
                'close_threshold': self.close_p_trend_threshold,
                'hedge_amount': self.hedge_amount,
                'leverage': self.leverage,
                'take_profit_pct': self.take_profit_pct,
                'stop_loss_pct': self.stop_loss_pct,
                'max_hold_minutes': self.max_hold_minutes
            }
        }
        
        if self.last_hedge_time:
            status['last_hedge_time'] = self.last_hedge_time.isoformat()
        
        # Add TP/SL status if we have a position
        if self.is_hedged and current_price and self.entry_price:
            tp_sl_info = self.check_tp_sl_conditions(current_price)
            status['risk_management'] = {
                'entry_price': self.entry_price,
                'current_pnl_pct': tp_sl_info['pnl_pct'],
                'time_held_minutes': tp_sl_info['time_held_minutes'],
                'tp_target': self.entry_price * (1 - self.take_profit_pct),  # SHORT: profit when price drops
                'sl_target': self.entry_price * (1 + self.stop_loss_pct),   # SHORT: loss when price rises
                'max_hold_remaining_min': max(0, self.max_hold_minutes - tp_sl_info['time_held_minutes'])
            }
        
        return status