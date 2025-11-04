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
        self.api_key = config.get('api_key')
        self.api_secret = config.get('api_secret')
        self.testnet = config.get('testnet', True)
        
        # Thresholds
        self.emergency_p_trend_threshold = config.get('emergency_p_trend_threshold', 0.01)
        self.close_p_trend_threshold = config.get('close_p_trend_threshold', 0.9)
        self.hedge_amount = config.get('hedge_amount', 0.01)  # BTC
        self.leverage = config.get('leverage', 10)
        
        # State tracking
        self.is_hedged = False
        self.current_short_order = None
        self.last_hedge_time = None
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
            
            # Open SHORT position
            order = self.exchange.create_market_sell_order(
                symbol=self.symbol,
                amount=self.hedge_amount,
                params={
                    'positionSide': 'SHORT'
                }
            )
            
            with self.lock:
                self.is_hedged = True
                self.current_short_order = order
                self.last_hedge_time = datetime.now(timezone.utc)
            
            logger.info(f"🔴 OPENED SHORT HEDGE POSITION")
            logger.info(f"📊 Symbol: {self.symbol}")
            logger.info(f"💰 Amount: {self.hedge_amount} BTC")
            logger.info(f"💵 Price: ${current_price:,.2f}")
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
                return True
            
            amount = abs(float(short_position['contracts']))
            
            if amount == 0:
                logger.info("Short position already closed")
                with self.lock:
                    self.is_hedged = False
                    self.current_short_order = None
                return True
            
            current_price = self.get_current_price()
            
            # Close SHORT by buying back
            order = self.exchange.create_market_buy_order(
                symbol=self.symbol,
                amount=amount,
                params={
                    'positionSide': 'SHORT',
                    'reduceOnly': True
                }
            )
            
            with self.lock:
                self.is_hedged = False
                self.current_short_order = None
            
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
            
            # Decision logic based on thresholds
            if p_trend_up <= self.emergency_p_trend_threshold:
                # Market dump detected - open hedge if not already hedged
                if not self.is_hedged:
                    result['action'] = 'open_short'
                    result['reason'] = f'Market dump detected: p_trend_up={p_trend_up:.4f} <= {self.emergency_p_trend_threshold}'
                    
                    success = self.open_short_hedge()
                    result['success'] = success
                    
                    if success:
                        logger.info(f"🚨 HEDGE ACTIVATED: Market dump detected (p_trend_up={p_trend_up:.4f})")
                    else:
                        logger.error(f"Failed to activate hedge")
                else:
                    result['action'] = 'hold_short'
                    result['reason'] = f'Already hedged, market still dumping: p_trend_up={p_trend_up:.4f}'
                    result['success'] = True
                    
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
                'leverage': self.leverage
            }
        }
        
        if self.last_hedge_time:
            status['last_hedge_time'] = self.last_hedge_time.isoformat()
        
        return status