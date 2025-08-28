# New files to add for margin trading support
import asyncio
from typing import Dict
# src/execution/margin_manager.py
class MarginManager:
    """Dedicated margin trading manager"""
    
    def __init__(self, config: dict):
        self.config = config
        self.client = BinanceClient(config)
        self.risk_manager = MarginRiskManager(config)
    
    async def enable_margin_trading(self, symbol: str):
        """Enable margin trading for specific symbol"""
        try:
            # Check if margin trading available for symbol
            exchange_info = await self.client.get_margin_all_pairs()
            
            available_pairs = [pair['symbol'] for pair in exchange_info]
            if symbol not in available_pairs:
                raise Exception(f"Margin trading not available for {symbol}")
            
            # Enable isolated margin account for symbol
            result = await self.client.create_isolated_margin_account(
                base=symbol[:-4],  # Remove USDT
                quote=symbol[-4:]  # USDT
            )
            
            logger.info(f"Enabled margin trading for {symbol}")
            return True
            
        except Exception as e:
            logger.error(f"Failed to enable margin trading: {e}")
            return False
    
    async def calculate_max_buy_quantity(self, symbol: str, price: float) -> float:
        """Calculate maximum buyable quantity với margin"""
        try:
            account_info = await self.client.get_margin_account()
            
            # Get available USDT balance
            usdt_balance = 0
            for asset in account_info['userAssets']:
                if asset['asset'] == 'USDT':
                    usdt_balance = float(asset['free'])
                    break
            
            # Calculate with 2x leverage
            max_value = usdt_balance * 2.0
            max_quantity = max_value / price
            
            return max_quantity * 0.95  # 95% to account for fees
            
        except Exception as e:
            logger.error(f"Failed to calculate max quantity: {e}")
            return 0

# src/monitoring/margin_monitor.py
class MarginMonitor:
    """Monitor margin account health"""
    
    def __init__(self, config: dict):
        self.config = config
        self.client = BinanceClient(config)
        self.alert_manager = AlertManager(config)
        
        # Monitoring thresholds
        self.warning_level = config.get('margin_level_warning', 1.5)
        self.critical_level = config.get('margin_level_emergency', 1.2)
    
    async def monitor_margin_health(self):
        """Continuous margin health monitoring"""
        while True:
            try:
                account_info = await self.client.get_margin_account()
                margin_level = float(account_info.get('marginLevel', 0))
                
                # Check margin level và send alerts
                if margin_level < self.critical_level:
                    await self._handle_critical_margin(account_info)
                elif margin_level < self.warning_level:
                    await self._handle_warning_margin(account_info)
                
                # Sleep for 30 seconds
                await asyncio.sleep(30)
                
            except Exception as e:
                logger.error(f"Margin monitoring error: {e}")
                await asyncio.sleep(60)  # Longer sleep on error
    
    async def _handle_critical_margin(self, account_info: Dict):
        """Handle critical margin level"""
        margin_level = float(account_info.get('marginLevel', 0))
        
        await self.alert_manager.send_alert(
            level="CRITICAL",
            message=f"CRITICAL: Margin level {margin_level:.3f}",
            details={
                'margin_level': margin_level,
                'total_asset': account_info.get('totalAssetOfBtc'),
                'total_liability': account_info.get('totalLiabilityOfBtc')
            }
        )
        
        # Auto-action: Close riskiest positions
        await self._emergency_reduce_exposure()
    
    async def _emergency_reduce_exposure(self):
        """Emergency reduction of margin exposure"""
        try:
            # Get all open positions
            positions = await self.client.get_margin_account()
            
            # Find positions với highest risk
            risky_positions = []
            for asset in positions['userAssets']:
                borrowed = float(asset.get('borrowed', 0))
                if borrowed > 0:
                    risky_positions.append({
                        'asset': asset['asset'],
                        'borrowed': borrowed,
                        'interest': float(asset.get('interest', 0))
                    })
            
            # Sort by borrowed amount (highest first)
            risky_positions.sort(key=lambda x: x['borrowed'], reverse=True)
            
            # Close positions starting with highest borrowed
            for pos in risky_positions[:2]:  # Close top 2 risky positions
                await self._close_margin_position(pos['asset'])
                
        except Exception as e:
            logger.error(f"Emergency exposure reduction failed: {e}")
    
    async def _close_margin_position(self, asset: str):
        """Close specific margin position"""
        try:
            # This would implement position closing logic
            # For simplicity, assume we're selling the asset
            
            symbol = f"{asset}USDT"
            current_price = await self._get_current_price(symbol)
            
            # Get asset balance
            account_info = await self.client.get_margin_account()
            asset_balance = 0
            
            for user_asset in account_info['userAssets']:
                if user_asset['asset'] == asset:
                    asset_balance = float(user_asset['free'])
                    break
            
            if asset_balance > 0:
                # Sell all available balance
                await self.client.create_margin_order(
                    symbol=symbol,
                    side='SELL',
                    type='MARKET',
                    quantity=asset_balance,
                    isIsolated='TRUE'
                )
                
                logger.info(f"Emergency closed position for {asset}")
                
        except Exception as e:
            logger.error(f"Failed to close position for {asset}: {e}")

# src/backtesting/margin_backtester.py
class MarginBacktester:
    """Backtesting với margin trading simulation"""
    
    def __init__(self, config: dict):
        self.config = config
        self.max_leverage = config.get('max_leverage', 2.0)
        self.borrowing_rate = config.get('borrowing_rate', 0.0001)  # Daily rate
        
        # Simulation state
        self.cash_balance = 0
        self.borrowed_amount = 0
        self.asset_holdings = {}
        self.margin_calls = 0
        self.liquidations = 0
    
    def simulate_margin_trade(self, signal: dict, current_price: float, 
                            available_cash: float) -> dict:
        """Simulate margin trade execution"""
        
        # Calculate position size với leverage
        base_position_size = signal['position_size'] * available_cash
        leveraged_size = base_position_size * self.max_leverage
        
        # Calculate borrowing needed
        borrow_needed = max(0, leveraged_size - available_cash)
        
        # Check if borrowing exceeds limits
        max_borrowable = available_cash * (self.max_leverage - 1)
        if borrow_needed > max_borrowable:
            borrow_needed = max_borrowable
            leveraged_size = available_cash + borrow_needed
        
        # Calculate actual quantity
        quantity = leveraged_size / current_price
        
        # Simulate borrowing cost
        daily_interest = borrow_needed * self.borrowing_rate
        
        return {
            'quantity': quantity,
            'borrowed_amount': borrow_needed,
            'daily_interest': daily_interest,
            'leverage_used': leveraged_size / available_cash if available_cash > 0 else 1,
            'margin_level': self._calculate_margin_level(available_cash, borrow_needed, current_price * quantity)
        }
    
    def _calculate_margin_level(self, cash: float, borrowed: float, 
                              asset_value: float) -> float:
        """Calculate margin level"""
        if borrowed == 0:
            return float('inf')
        
        total_equity = cash + asset_value - borrowed
        return total_equity / borrowed if borrowed > 0 else float('inf')
    
    def check_margin_call(self, current_price: float, position: dict) -> bool:
        """Check if margin call should be triggered"""
        if position['borrowed_amount'] == 0:
            return False
        
        current_asset_value = position['quantity'] * current_price
        margin_level = self._calculate_margin_level(
            self.cash_balance, 
            position['borrowed_amount'], 
            current_asset_value
        )
        
        # Margin call at level 1.3
        if margin_level < 1.3:
            self.margin_calls += 1
            return True
        
        return False