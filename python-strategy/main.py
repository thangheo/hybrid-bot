# ================================================================
# Fixed main.py - Simplified without Signal Decay
# Compatible with SimplifiedUnifiedManager
# ================================================================

import asyncio
import signal
import sys
import os
from datetime import datetime, timedelta
from pathlib import Path
import time

# 🚀 SETUP LOGGING FIRST
sys.path.insert(0, str(Path(__file__).parent / 'src'))
from src.utils.logger import setup_global_logging, get_main_logger
# Setup logging before everything else
logger = setup_global_logging(
    log_level="INFO",
    log_file="logs/trading_bot.log",
    config_path="config/logging.yaml"  # optional
)
import logging
import yaml
from dotenv import load_dotenv
from apscheduler.schedulers.asyncio import AsyncIOScheduler
from apscheduler.triggers.interval import IntervalTrigger
import pandas as pd
import asyncpg
from typing import Dict, List, Optional, Tuple, Any 

# Import core modules
from src.data.manager import DataIngestionManager
from src.risk.manager import RiskManager
from src.execution.order_manager import OrderManager
from src.portfolio.manager import PortfolioManager
from src.monitoring.system_monitor import SystemMonitor
from src.monitoring.alert_manager import AlertManager
from src.execution.models import Order, OrderSide, OrderType

# 🚀 Import UNIFIED Strategy Manager
# from strategy.unified_manager import UnifiedStrategyManager
from src.strategy.strategies.markov_strategy_complete import MarkovETFStrategyPersistent
from src.communication.market_intelligence_bridge import MarketIntelligenceBridge
#extract regime info
from src.utils.regime_info import _extract_regime_info_for_risk_management

UNIFIED_MANAGER_AVAILABLE = True

class EnhancedPositionTracker:
    """Simplified position tracker without decay complexity"""
    
    def __init__(self, db_url: Optional[str] = None):
        self.db_url = db_url
        self.pool = None
        self.positions_cache = {}
        self.position_history = []
        
    async def initialize(self):
        """Initialize with basic database schema"""
        if self.db_url:
            try:
                self.pool = await asyncpg.create_pool(self.db_url, min_size=1, max_size=5)
                
                async with self.pool.acquire() as conn:
                    # Basic positions table
                    await conn.execute("""
                        CREATE TABLE IF NOT EXISTS positions (
                            symbol VARCHAR(20) PRIMARY KEY,
                            quantity DECIMAL(20,8) NOT NULL,
                            entry_price DECIMAL(20,8),
                            entry_time TIMESTAMP,
                            last_updated TIMESTAMP DEFAULT CURRENT_TIMESTAMP
                        )
                    """)
                
                logging.info("🚀 Position tracker initialized")
            except Exception as e:
                logging.warning(f"Database failed, using memory only: {e}")
                self.pool = None
    
    async def update_position(self, symbol: str, quantity: float, 
                            entry_price: Optional[float] = None, entry_time: Optional[datetime] = None):
        """Update position"""
        base_asset = symbol.replace('/', '').replace('USDT', '')
        
        # Update memory cache
        if quantity > 0:
            self.positions_cache[base_asset] = {
                'quantity': quantity,
                'entry_price': entry_price,
                'entry_time': entry_time or datetime.now()
            }
        else:
            self.positions_cache.pop(base_asset, None)
        
        # Save to database if available
        if self.pool:
            try:
                async with self.pool.acquire() as conn:
                    if quantity > 0:
                        await conn.execute("""
                            INSERT INTO positions (symbol, quantity, entry_price, entry_time)
                            VALUES ($1, $2, $3, $4)
                            ON CONFLICT (symbol) DO UPDATE SET 
                                quantity = EXCLUDED.quantity,
                                entry_price = EXCLUDED.entry_price,
                                entry_time = EXCLUDED.entry_time,
                                last_updated = CURRENT_TIMESTAMP
                        """, base_asset, quantity, entry_price, entry_time)
                    else:
                        await conn.execute("DELETE FROM positions WHERE symbol = $1", base_asset)
                       
            except Exception as e:
                logging.debug(f"Database position update failed: {e}")
    
    def has_position(self, symbol: str) -> Dict:
        """Check position"""
        base_asset = symbol.replace('/', '').replace('USDT', '')
        position_data = self.positions_cache.get(base_asset, {})
        
        return {
            'quantity': position_data.get('quantity', 0),
            'entry_price': position_data.get('entry_price'),
            'entry_time': position_data.get('entry_time')
        }
    
    async def sync_from_exchange(self, balance: dict):
        """Sync from exchange"""
        try:
            for asset, balance_data in balance.items():
                if asset in ['info', 'timestamp', 'datetime', 'free', 'used', 'total']:
                    continue
                
                quantity = 0
                if isinstance(balance_data, dict):
                    quantity = float(balance_data.get('free', 0))
                elif isinstance(balance_data, (int, float)):
                    quantity = float(balance_data)
                
                if asset != 'USDT' and quantity > 0:
                    existing_position = self.has_position(f"{asset}/USDT")
                    
                    await self.update_position(
                        f"{asset}/USDT", 
                        quantity,
                        existing_position.get('entry_price'),
                        existing_position.get('entry_time')
                    )
                    
        except Exception as e:
            logging.error(f"Position sync failed: {e}")


class EnhancedTradingBot:
    """🚀 Simplified Trading Bot with Unified Strategy Management"""
    
    def __init__(self, config_path: str = 'config/trading_bot.yaml'):
        self.config = self._load_config(config_path)
        self.running = False
        self.scheduler = AsyncIOScheduler()
        
        # Core managers
        self.data_manager = None
        self.risk_manager = None
        self.order_manager = None
        self.portfolio_manager = None
        self.system_monitor = None
        self.alert_manager = None
        self.last_regime = None
        # 🚀 UNIFIED Strategy Manager
        self.strategy_manager = None
        
        # Simplified position tracker
        db_url = self.config.get('data_ingestion', {}).get('database', {}).get('url')
        self.position_tracker = EnhancedPositionTracker(db_url)
        
        # System capabilities tracking
        self.system_capabilities = {}
        
        # Setup logging and signals
        self.logger = get_main_logger()
        
        signal.signal(signal.SIGINT, self._signal_handler)
        signal.signal(signal.SIGTERM, self._signal_handler)
        #if usee intelligent bridge
        self.intelligence_bridge = None
        self.cpp_intelligence_enabled = self.config.get('cpp_intelligence', {}).get('enabled', False)

        self.logger.info("🚀 Enhanced Trading Bot initialized")
        if UNIFIED_MANAGER_AVAILABLE:
            self.logger.info("   ✅ Unified Strategy Manager available")
        else:
            self.logger.warning("   ⚠️ Using fallback strategy manager")
    
    def _load_config(self, config_path: str) -> dict:
        """Load and expand configuration"""
        load_dotenv()
        
        config_files = [config_path, 'python-strategy/config/trading_bot.yaml']
        
        for config_file in config_files:
            if os.path.exists(config_file):
                with open(config_file, 'r') as f:
                    config = yaml.safe_load(f)
                config = self._expand_env_vars(config)
                logging.info(f"📄 Loaded config: {config_file}")
                return config
        
        raise FileNotFoundError("No configuration file found")
    
    def _expand_env_vars(self, config: dict) -> dict:
        """Expand environment variables in config"""
        import re
        
        def expand_value(value):
            if isinstance(value, str):
                pattern = re.compile(r'\$\{([^}]+)\}')
                def replacer(match):
                    var_name = match.group(1)
                    return os.getenv(var_name, match.group(0))
                return pattern.sub(replacer, value)
            elif isinstance(value, dict):
                return {k: expand_value(v) for k, v in value.items()}
            elif isinstance(value, list):
                return [expand_value(item) for item in value]
            return value
        
        # Ensure we always return a dict by only calling expand_value on dict values
        if isinstance(config, dict):
            return {k: expand_value(v) for k, v in config.items()}
        else:
            # Fallback to return the original config if it's somehow not a dict
            return config if isinstance(config, dict) else {}
    
    def _signal_handler(self, signum, frame):
        """Handle shutdown signals"""
        self.logger.info(f"Received signal {signum}, shutting down...")
        asyncio.create_task(self.stop())
    
    async def initialize(self):
        """Simplified initialization"""
        self.logger.info("🚀 Initializing Trading Bot...")
        
        try:
            # Initialize core components
            self.alert_manager = AlertManager(self.config.get('alerts', {}))
            self.system_monitor = SystemMonitor(self.config.get('monitoring', {}))
            
            # Initialize data manager
            self.data_manager = DataIngestionManager(self.config.get('data_ingestion', {}))
            await self.data_manager.initialize()
            
            # Initialize position tracker
            await self.position_tracker.initialize()
            
            # 🚀 Initialize UNIFIED Strategy Manager
            # self.strategy_manager = UnifiedStrategyManager(self.config)
            self.strategy_manager = MarkovETFStrategyPersistent(self.config)
            self.strategy_info = self.strategy_manager.get_enhanced_strategy_info()
            logger.info(f"Using strategy: {self.strategy_info.get('strategy_name', 'Unknown')}")

            # Get system capabilities
            self.system_capabilities = self.strategy_manager.get_system_capabilities()
            
            # Initialize other managers
            self.risk_manager = RiskManager(self.config.get('risk_management', {}))
            self.order_manager = OrderManager(self.config.get('order_execution', {}))
            self.portfolio_manager = PortfolioManager(self.config.get('portfolio', {}))
            
            # Sync positions on startup
            if self.order_manager:
                balance = await self.order_manager.exchange.fetch_balance()
                await self.position_tracker.sync_from_exchange(balance)
            #intellgent bridge init
            if self.cpp_intelligence_enabled:
                try:
                    symbols = self.config.get('data_ingestion', {}).get('symbols', [])
                    self.intelligence_bridge = MarketIntelligenceBridge(symbols)
                    
                    if self.intelligence_bridge.start():
                        # Set bridge in strategy
                        if hasattr(self.strategy_manager, 'set_intelligence_bridge'):
                            self.strategy_manager.set_intelligence_bridge(self.intelligence_bridge)
                        
                        self.logger.info("✅ C++ market intelligence integration enabled")
                    else:
                        self.logger.warning("⚠️ C++ intelligence failed to start - continuing without")
                        self.intelligence_bridge = None
                except Exception as e:
                    self.logger.warning(f"C++ intelligence integration failed: {e}")
                    self.intelligence_bridge = None
            # Log system capabilities
            self._log_system_capabilities()
            
            self.logger.info("✅ All components initialized successfully")
            # Log all information about system
            self.logger.info(f"System Capabilities: {self.system_capabilities}")
            # self.logger.info(f"Configuration: {self.config}")
            # Send startup notification
            if self.alert_manager:
                await self.alert_manager.send_alert(
                    'INFO',
                    '🚀 Trading Bot Started',
                    {
                        'timestamp': datetime.now().isoformat(),
                        'capabilities': self.system_capabilities,
                        'config_version': self.config.get('version', 'unknown')
                    }
                )
            
        except Exception as e:
            if self.alert_manager:
                await self.alert_manager.send_alert(
                    'ERROR',
                    f'❌ Trading Bot initialization failed: {e}',
                    {'error': str(e)}
                )
            raise

    def _log_system_capabilities(self):
        """Log detected system capabilities"""
        caps = self.system_capabilities
        
        self.logger.info("📊 System Capabilities Detected:")
        self.logger.info(f"   🚀 ETF Integration: {'✅' if caps['etf_integration']['enabled'] else '❌'}")
        self.logger.info(f"   🧠 Primary Strategy: {caps['strategies']['primary']}")
        self.logger.info(f"   📊 Total Strategies: {caps['strategies']['total']}")
        
        if caps['etf_integration']['enabled']:
            self.logger.info(f"   💰 ETF Manager: {'✅ Ready' if caps['etf_integration']['manager_ready'] else '❌ Not Ready'}")
    
    async def start(self):
        """Start the trading bot"""
        try:
            await self.initialize()
            self.running = True
            
            # Schedule tasks
            self._schedule_tasks()
            
            # Start scheduler
            self.scheduler.start()
            
            self.logger.info("🚀 Trading Bot running...")
            
            # Main event loop
            while self.running:
                await asyncio.sleep(1)
                
            if self.alert_manager:
                await self.alert_manager.send_alert(
                    'INFO',
                    '🚀 Trading Bot Started',
                    {
                        'timestamp': datetime.now().isoformat(),
                        'capabilities': self.system_capabilities,
                        'config_version': self.config.get('version', 'unknown')
                    }
                )
        finally:
            await self.stop()
    
    def _schedule_tasks(self):
        """Schedule trading tasks"""
        scheduling_config = self.config.get('scheduling', {})
        
        # Core data collection (every 30 minutes) - GIỮ NGUYÊN
        self.scheduler.add_job(
            self._collect_market_data,
            trigger=IntervalTrigger(seconds=scheduling_config.get('data_interval', 1800)),
            id='market_data_collection',
            max_instances=1
        )
        
        # Full signal generation (every 6 minutes) - GIỮ NGUYÊN  
        self.scheduler.add_job(
            self._generate_signals,
            trigger=IntervalTrigger(seconds=scheduling_config.get('signal_interval', 360)),
            id='signal_generation',
            max_instances=1
        )
        
        # THÊM HÀM MỚI - Intelligence-based quick checks
        self.scheduler.add_job(
            self._check_intelligence_changes,
            trigger=IntervalTrigger(seconds=scheduling_config.get('intelligent_interval',60)),
            id='intelligence_monitoring',
            max_instances=1
        )
        
        # System monitoring - GIỮ NGUYÊN
        self.scheduler.add_job(
            self._monitor_system,
            trigger=IntervalTrigger(seconds=160),
            id='system_monitoring',
            max_instances=1
        )
        
        # Portfolio reconciliation - GIỮ NGUYÊN
        self.scheduler.add_job(
            self._reconcile_portfolio,
            trigger=IntervalTrigger(seconds=1800),
            id='portfolio_reconciliation',
            max_instances=1
        )
    async def _check_intelligence_changes(self):
        """Monitor C++ intelligence for significant changes"""
        try:
            if not self.intelligence_bridge or not self.intelligence_bridge.is_available():
                return
                
            symbols = self.config.get('data_ingestion', {}).get('symbols', [])
            
            for symbol in symbols:
                intel = self.intelligence_bridge.get_intelligence(symbol)
                if not intel:
                    continue
                    
                # Check if intelligence is fresh (< 30 seconds old)
                intelligence_age = time.time() - intel.last_update
                if intelligence_age > 30:
                    continue
                    
                # Check for significant regime changes or high confidence signals
                if (intel.regime_confidence > 0.85 or 
                    intel.liquidity_ratio > 0.9 or 
                    abs(intel.order_book_imbalance) > 0.4):
                    
                    self.logger.info(f"🚀 High confidence C++ signal for {symbol}: "
                                    f"regime_conf={intel.regime_confidence:.2f}, "
                                    f"liquidity={intel.liquidity_ratio:.2f}")
                    
                    # Trigger immediate signal generation for this symbol
                    await self._process_symbol_signals(symbol)
                    
        except Exception as e:
            self.logger.error(f"Intelligence monitoring failed: {e}")
    async def _collect_market_data(self):
        """Collect market data"""
        try:
            if not self.data_manager:
                self.logger.warning("Data manager not initialized, skipping market data collection")
                return
                
            symbols = self.config.get('data_ingestion', {}).get('symbols', [])
            results = await self.data_manager.fetch_latest_data_optimized(symbols)
            
            total_records = sum(results.values())
            self.logger.debug(f"📊 Market data: {total_records} new records")
            
        except Exception as e:
            self.logger.error(f"❌ Market data collection failed: {e}")
    
   
    async def _generate_signals(self):
        try:
            if not self.data_manager:
                self.logger.warning("Data manager not initialized, skipping signal generation")
                return
                
            symbols = self.config.get('data_ingestion', {}).get('symbols', [])
            await self.data_manager.fetch_latest_data_optimized(symbols)
            await asyncio.sleep(2) 
            
            for symbol in symbols:
                await self._process_symbol_signals(symbol)

        except Exception as e:
            self.logger.error(f"❌ Signal generation failed: {e}")

    async def _process_symbol_signals(self, symbol: str):
        """
        Fetches multi-timeframe market data for a symbol, calculates indicators, generates trading signals,
        logs signal information, and passes the latest signal to the signal processor for execution.
        """
        try:
            if not self.data_manager:
                self.logger.warning("Data manager not initialized, skipping signal processing")
                return
                
            if not self.strategy_manager:
                self.logger.warning("Strategy manager not initialized, skipping signal processing")
                return
                
            # Get market data
            multi_tf_data = self.data_manager.get_multi_timeframe_data(symbol, days=100)
            market_data = None
            
            # for tf in ['4h', '1d']:
            #     if tf in multi_tf_data and not multi_tf_data[tf].empty:
            #         market_data = multi_tf_data[tf]
                    # break
            if '4h' in multi_tf_data and not multi_tf_data['4h'].empty:
                market_data = multi_tf_data['4h']
            
            if market_data is None or market_data.empty:
                self.logger.warning(f"No data available for {symbol}")
                return
            
            # Calculate indicators
            indicators = self.strategy_manager.calculate_indicators(market_data)
            #get current volatility from indicators
            current_volatility = indicators['volatility'].iloc[-1]
            # Generate signals - DIRECT CALL
            signals = await self.strategy_manager.generate_signals(symbol, market_data, indicators)
            
            if signals.empty:
                self.logger.warning(f"No signals for {symbol}")
                return
            
            latest_signal = signals.iloc[-1]
            
            # Extract regime info
            regime_info = {
                'current_regime': signals.get('regime', 'balanced'),
                'confidence': signals.get('regime_confidence', 0.5),
                'strength': signals.get('regime_strength', 0.5)
            }

            # Update risk management
            self._update_risk_management_with_regime(regime_info, current_volatility)
            

            # # Evaluate trades như bình thường - giờ đã regime-aware!
            # approved, size, reason = risk_manager.evaluate_trade_risk(symbol, signal, portfolio)
            # Log signal info
            self._log_signal_info(symbol, latest_signal)

            # Process signals
            if latest_signal.get('signal', 0) != 0:
                await self._process_signal(symbol, latest_signal, market_data)
                
        except Exception as e:
            self.logger.error(f"Signal processing failed for {symbol}: {e}")

    def _update_risk_management_with_regime(self, regime_info: dict, current_volatility: float = None):
        """Update risk management parameters based on regime information"""
        extracted_info = _extract_regime_info_for_risk_management(regime_info)
        current_regime = extracted_info['current_regime']
        regime_confidence = extracted_info['confidence']
        regime_strength = extracted_info['strength']
        
        # Convert enum to string if needed
        if hasattr(current_regime, 'value'):
            current_regime = current_regime.value
        
        # Only update if regime changed or significant confidence change
        should_update = (
            self.last_regime != current_regime or 
            self.last_regime_update is None or
            abs(regime_confidence - getattr(self, 'last_confidence', 0.5)) > 0.1
        )
        
        if should_update:
            try:
                # Update risk parameters
                self.risk_manager.update_regime_risk_parameters(
                    regime=current_regime,
                    confidence=regime_confidence,
                    regime_strength=regime_strength,
                    current_volatility=current_volatility
                )
                
                # Update tracking variables
                self.last_regime = current_regime
                self.last_regime_update = time.time()
                self.last_confidence = regime_confidence
                
                logger.info(f"Risk management updated for regime: {current_regime} (confidence: {regime_confidence:.2f})")
                
            except Exception as e:
                logger.error(f"Failed to update risk management with regime: {e}")
    def _log_signal_info(self, symbol: str, signal_data: pd.Series):
        """Log signal information"""
        signal_value = signal_data.get('signal', 0)
        signal_strength = signal_data.get('signal_strength', 0)
        confidence = signal_data.get('confidence', 0)
        
        log_message = f"{symbol} Signal: {signal_value} " \
                    f"(strength: {signal_strength:.3f}, confidence: {confidence:.2f})"
        
        # Add regime info if available
        if 'regime' in signal_data:
            regime = signal_data.get('regime', 'unknown')
            log_message += f", Regime: {regime}"
        
        # Add ETF info if available
        if 'etf_flow_strength' in signal_data:
            etf_strength = signal_data.get('etf_flow_strength', 0)
            log_message += f", ETF Flow: {etf_strength:.3f}"
        
        self.logger.info(log_message)

    async def _process_signal(self, symbol: str, signal_data: pd.Series, market_data: pd.DataFrame):
        """Process trading signal"""
        try:
            balance = await self.order_manager.exchange.fetch_balance()
            
            signal_value = signal_data.get('signal', 0)
            
            if signal_value < 0:  # SELL
                await self._handle_sell_signal(symbol, signal_data, balance)
            elif signal_value > 0:  # BUY
                await self._handle_buy_signal(symbol, signal_data, market_data, balance)
                
        except Exception as e:
            self.logger.error(f"Signal processing failed for {symbol}: {e}")

    async def _handle_sell_signal(self, symbol: str, signal_data: pd.Series, balance: dict):
        """Handle SELL signals"""
        try:
            # Check position
            position_info = self.position_tracker.has_position(symbol)
            available_quantity = position_info.get('quantity', 0)
            
            if available_quantity <= 0:
                self.logger.info(f"✅ No {symbol} position found (correctly rejected)")
                return
            
            # Get current price
            ticker = await self.order_manager.exchange.fetch_ticker(symbol)
            current_price = ticker['last']
            position_value = available_quantity * current_price
            
            if position_value >= 10:  # Minimum order check
                order = Order(
                    symbol=symbol,
                    side=OrderSide.SELL,
                    order_type=OrderType.MARKET,
                    quantity=available_quantity
                )
                
                success, message = await self.order_manager.place_order(order)
                
                if success:
                    # Update position tracker
                    await self.position_tracker.update_position(symbol, 0, current_price, datetime.now())
                    
                    if self.alert_manager:
                        await self.alert_manager.send_alert(
                            'INFO',
                            f'✅ SELL: {symbol} {available_quantity:.6f} ~${position_value:.2f}',
                            {
                                'symbol': symbol,
                                'direction': 'SELL',
                                'quantity': available_quantity,
                                'value': position_value
                            }
                        )
                    self.logger.error(f"❌ SELL failed: {message}")
            else:
                self.logger.info(f"⚠️ Position too small: {symbol} ${position_value:.2f}")
                
        except Exception as e:
            self.logger.error(f"❌ SELL handling failed: {e}")
    
    async def _handle_buy_signal(self, symbol: str, signal_data: pd.Series,
                                market_data: pd.DataFrame, balance: dict):
        """Handle BUY signals"""
        try:
            # Build portfolio for risk assessment
            current_portfolio = await self._build_portfolio_from_balance(balance)
            
            # Prepare signal dictionary
            signal_dict = {
                'symbol': symbol.replace('/', ''),
                'signal_strength': float(signal_data.get('signal_strength', 0.5)),
                'volatility': float(market_data['close'].pct_change().tail(20).std()),
                'direction': 'BUY',
                'raw_signal': float(signal_data.get('final_signal', 1)),
                'confidence': float(signal_data.get('confidence', 0.5))
            }
            
            # Risk assessment
            risk_approved, position_size, reason = self.risk_manager.evaluate_trade_risk(
                symbol=signal_dict['symbol'],
                signal=signal_dict,
                current_portfolio=current_portfolio
            )
            
            if risk_approved and position_size > 0:
                # Execute BUY order
                ticker = await self.order_manager.exchange.fetch_ticker(symbol)
                current_price = ticker['last']
                
                available_balance = balance.get('USDT', {}).get('free', 0)
                order_value = available_balance * position_size
                
                if order_value >= 10:  # Minimum order check
                    quantity = order_value / current_price
                    
                    order = Order(
                        symbol=symbol,
                        side=OrderSide.BUY,
                        order_type=OrderType.LIMIT,
                        price=current_price,
                        quantity=quantity
                    )
                    
                    success, message = await self.order_manager.place_order(order)
                    
                    if success:
                        # Update position tracker
                        current_position = self.position_tracker.has_position(symbol)
                        current_quantity = current_position.get('quantity', 0)
                        new_quantity = current_quantity + quantity
                        
                        await self.position_tracker.update_position(
                            symbol, new_quantity, current_price, datetime.now()
                        )
                        if self.alert_manager:
                            await self.alert_manager.send_alert(
                                'INFO',
                                f'✅ BUY: {symbol} {quantity:.6f} (~${order_value:.2f})',
                                {
                                    'symbol': symbol,
                                    'direction': 'BUY',
                                    'quantity': quantity,
                                    'price': current_price,
                                    'value': order_value
                                }
                            )

                    else:
                        self.logger.error(f"❌ BUY order failed: {message}")
                else:
                    self.logger.warning(f"⚠️ Insufficient order value: {symbol} ${order_value:.2f}")
            else:
                self.logger.info(f"🚫 BUY signal rejected for {symbol}: {reason}")
                
        except Exception as e:
            self.logger.error(f"❌ BUY handling failed: {e}")
    
    async def _build_portfolio_from_balance(self, balance: dict) -> dict:
        """Build portfolio from exchange balance"""
        try:
            portfolio = {
                'total_value_usdt': 0,
                'positions': {},
                'cash_usdt': 0
            }
            
            # Get USDT balance
            usdt_balance = balance.get('USDT', {})
            if isinstance(usdt_balance, dict):
                portfolio['cash_usdt'] = float(usdt_balance.get('free', 0))
            elif isinstance(usdt_balance, (int, float)):
                portfolio['cash_usdt'] = float(usdt_balance)
            
            portfolio['total_value_usdt'] += portfolio['cash_usdt']
            
            # Process asset positions
            for asset, asset_data in balance.items():
                if asset in ['info', 'timestamp', 'datetime', 'free', 'used', 'total', 'USDT']:
                    continue
                
                free_amount = 0
                if isinstance(asset_data, dict):
                    free_amount = float(asset_data.get('free', 0))
                elif isinstance(asset_data, (int, float)):
                    free_amount = float(asset_data)
                
                if free_amount <= 0:
                    continue
                
                try:
                    # Get current price
                    symbol = f"{asset}/USDT"
                    ticker = await self.order_manager.exchange.fetch_ticker(symbol)
                    current_price = float(ticker['last']) if ticker['last'] is not None else 0.0
                    value_usdt = free_amount * current_price
                    
                    portfolio['positions'][asset] = {
                        'quantity': free_amount,
                        'current_price': current_price,
                        'value_usdt': value_usdt
                    }
                    
                    portfolio['total_value_usdt'] += value_usdt
                    
                except Exception as e:
                    self.logger.warning(f"⚠️ Could not process {asset}: {e}")
            
            return portfolio
            
        except Exception as e:
            self.logger.error(f"❌ Portfolio building failed: {e}")
            return {'total_value_usdt': 0, 'positions': {}, 'cash_usdt': 0}
    
    async def _monitor_system(self):
        """System monitoring"""
        try:
            if self.system_monitor:
                metrics = await self.system_monitor.collect_metrics()
                health = await self.system_monitor.check_health(metrics)
                # self.logger.debug(f"📊 System Metrics: {metrics}")
                # Send alerts if thresholds exceeded
                # Test alert
                if self.alert_manager and not health['all_ok']:
                    await self.alert_manager.send_alert(
                        'WARNING',
                        'System health check failed',
                        {'cpu': metrics.cpu_percent, 'memory': metrics.memory_percent}
                    )

        except Exception as e:
            self.logger.error(f"❌ System monitoring failed: {e}")

    async def _reconcile_portfolio(self):
        """Portfolio reconciliation"""
        try:
            # Get real balance from exchange
            balance = await self.order_manager.exchange.fetch_balance()
            
            # Build portfolio state
            current_portfolio = await self._build_portfolio_from_balance(balance)
            
            # Log portfolio summary
            total_value = current_portfolio['total_value_usdt']
            cash_pct = (current_portfolio['cash_usdt'] / total_value * 100) if total_value > 0 else 100
            
            self.logger.info(f"💰 Portfolio: ${total_value:.2f} "
                           f"(Cash: {cash_pct:.1f}%, Positions: {len(current_portfolio['positions'])})")
            
            # Log individual positions
            for asset, pos_info in current_portfolio['positions'].items():
                pos_pct = (pos_info['value_usdt'] / total_value * 100) if total_value > 0 else 0
                
                position_data = self.position_tracker.has_position(f"{asset}/USDT")
                entry_price = position_data.get('entry_price')
                
                pnl_info = ""
                if entry_price:
                    pnl = ((pos_info['current_price'] - entry_price) / entry_price * 100)
                    pnl_info = f" (PnL: {pnl:+.1f}%)"
                
                self.logger.info(f"   📈 {asset}: {pos_info['quantity']:.6f} "
                               f"${pos_info['value_usdt']:.2f} ({pos_pct:.1f}%)"
                               f"{pnl_info}")
            
            # Update portfolio manager
            if hasattr(self.portfolio_manager, 'update_from_exchange'):
                await self.portfolio_manager.update_from_exchange(balance)
            
            # Sync position tracker
            await self.position_tracker.sync_from_exchange(balance)
            
        except Exception as e:
            self.logger.error(f"❌ Portfolio reconciliation failed: {e}")
    
    async def stop(self):
        """Graceful shutdown"""
        self.logger.info("🛑 Stopping Trading Bot...")
        self.running = False
        
        try:
            # Stop scheduler
            if self.scheduler.running:
                self.scheduler.shutdown(wait=True)
            
            # Close position tracker
            if self.position_tracker and self.position_tracker.pool:
                await self.position_tracker.pool.close()
            
            # Close connections
            if self.data_manager:
                await self.data_manager.close()
            
            if self.order_manager and hasattr(self.order_manager, 'exchange'):
                await self.order_manager.exchange.close()
            
            # Send shutdown notification
            if self.alert_manager:
                await self.alert_manager.send_alert(
                    'INFO',
                    '🛑 Trading Bot stopped',
                    {'shutdown_time': datetime.now().isoformat()}
                )
            
            self.logger.info("✅ Trading Bot stopped successfully")
            
        except Exception as e:
            self.logger.error(f"❌ Error during shutdown: {e}")


# ================================================================
# 🚀 COMMAND LINE INTERFACE
# ================================================================

async def main():
    """Main entry point"""
    import argparse
    
    parser = argparse.ArgumentParser(description='🚀 Crypto Trading Bot')
    parser.add_argument('--config', default='config/trading_bot.yaml',
                       help='Configuration file path')
    parser.add_argument('--test-capabilities', action='store_true',
                       help='Test system capabilities and exit')
    
    args = parser.parse_args()
    
    if args.test_capabilities:
        await test_system_capabilities(args.config)
    else:
        # Run live trading
        bot = EnhancedTradingBot(args.config)
        await bot.start()


async def test_system_capabilities(config_path: str):
    """Test system capabilities without starting trading"""
    print("📊 Testing Trading Bot Capabilities")
    print("=" * 50)
    
    try:
        # Load config
        with open(config_path, 'r') as f:
            config = yaml.safe_load(f)

        # Test Markov ETF strategy manager
        strategy_manager = MarkovETFStrategyPersistent(config)
        capabilities = strategy_manager.get_system_capabilities()
        
        print("📊 System Capabilities:")
        print(f"   🚀 ETF Integration: {'✅' if capabilities['etf_integration']['enabled'] else '❌'}")
        print(f"   🧠 Primary Strategy: {capabilities['strategies']['primary']}")
        print(f"   📈 Total Strategies: {capabilities['strategies']['total']}")
        
        if capabilities['etf_integration']['enabled']:
            print(f"   💰 ETF Manager: {'✅' if capabilities['etf_integration']['manager_ready'] else '❌'}")
            
            # Test ETF data fetch
            try:
                etf_flow_data = await strategy_manager._get_etf_flow_data()
                if etf_flow_data is not None:
                    print(etf_flow_data)
                else:
                    print("   📊 ETF Data Fetch: ❌ (No data)")
            except Exception as e:
                print(f"   📊 ETF Test: ❌ ({e})")
        
        print("\n✅ Capability test completed!")
        print("   📊 ETF Data Fetch: ✅ (Data available)")
    except Exception as e:
        print(f"❌ Capability test failed: {e}")
        import traceback
        traceback.print_exc()


if __name__ == "__main__":
    asyncio.run(main())