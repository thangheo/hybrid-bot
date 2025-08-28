"""
P&L Calculator Module

This module provides profit and loss calculation capabilities for trading strategies.

Classes:
    PnLCalculator: Main class for P&L calculations
"""

import time
import json
import pandas as pd
from collections import defaultdict


class PnLCalculator:
    """
    Calculates and tracks profit and loss for trading activities.
    
    Attributes:
        positions (dict): Current positions by asset
        trades (list): Historical trades
        market_prices (dict): Current market prices by asset
        realized_pnl (dict): Realized P&L by strategy and asset
        unrealized_pnl (dict): Unrealized P&L by strategy and asset
    """
    
    def __init__(self, price_provider=None):
        """
        Initialize the P&L calculator.
        
        Args:
            price_provider: Optional provider for current market prices
        """
        self.positions = defaultdict(dict)  # strategy -> {asset -> position}
        self.trades = []
        self.market_prices = {}
        self.realized_pnl = defaultdict(lambda: defaultdict(float))  # strategy -> {asset -> pnl}
        self.unrealized_pnl = defaultdict(lambda: defaultdict(float))  # strategy -> {asset -> pnl}
        self.price_provider = price_provider
    
    def add_trade(self, trade):
        """
        Add a trade and update P&L calculations.
        
        Args:
            trade (dict): Trade information including:
                         strategy_id, asset, quantity, price, timestamp, etc.
        
        Returns:
            dict: Updated P&L information
        """
        # Add to trade history
        self.trades.append(trade)
        
        strategy_id = trade['strategy_id']
        asset = trade['asset']
        quantity = trade['quantity']  # Positive for buy, negative for sell
        price = trade['price']
        
        # Update position
        if asset not in self.positions[strategy_id]:
            self.positions[strategy_id][asset] = {
                'quantity': 0,
                'average_price': 0,
                'cost_basis': 0
            }
        
        position = self.positions[strategy_id][asset]
        
        # Calculate realized P&L for sells
        if quantity < 0:  # Sell
            # Only calculate P&L if we have an existing position
            if position['quantity'] > 0:
                # Calculate P&L based on average price
                realized_pnl = abs(quantity) * (price - position['average_price'])
                self.realized_pnl[strategy_id][asset] += realized_pnl
        
        # Update position
        old_quantity = position['quantity']
        new_quantity = old_quantity + quantity
        
        # Update average price (for buys only)
        if quantity > 0:  # Buy
            old_cost = old_quantity * position['average_price']
            new_cost = quantity * price
            
            # Calculate new average price
            if new_quantity > 0:
                position['average_price'] = (old_cost + new_cost) / new_quantity
            else:
                position['average_price'] = 0
        
        position['quantity'] = new_quantity
        position['cost_basis'] = position['quantity'] * position['average_price']
        
        # Update market price
        self.market_prices[asset] = price
        
        # Calculate unrealized P&L
        self._calculate_unrealized_pnl(strategy_id, asset)
        
        return {
            'strategy_id': strategy_id,
            'asset': asset,
            'realized_pnl': self.realized_pnl[strategy_id][asset],
            'unrealized_pnl': self.unrealized_pnl[strategy_id][asset],
            'position': position
        }
    
    def update_market_price(self, asset, price):
        """
        Update market price for an asset.
        
        Args:
            asset (str): Asset identifier
            price (float): Current market price
        """
        self.market_prices[asset] = price
        
        # Update unrealized P&L for all strategies with this asset
        for strategy_id in self.positions:
            if asset in self.positions[strategy_id]:
                self._calculate_unrealized_pnl(strategy_id, asset)
    
    def _calculate_unrealized_pnl(self, strategy_id, asset):
        """
        Calculate unrealized P&L for a strategy and asset.
        
        Args:
            strategy_id (str): Strategy identifier
            asset (str): Asset identifier
        """
        position = self.positions[strategy_id].get(asset)
        
        if not position or position['quantity'] == 0:
            self.unrealized_pnl[strategy_id][asset] = 0
            return
        
        market_price = self.market_prices.get(asset)
        
        # Try to get price from provider if not available
        if market_price is None and self.price_provider:
            try:
                market_price = self.price_provider.get_price(asset)
                self.market_prices[asset] = market_price
            except Exception as e:
                print(f"Error getting price for {asset}: {e}")
                return
        
        if market_price is None:
            return
        
        # Calculate unrealized P&L
        self.unrealized_pnl[strategy_id][asset] = position['quantity'] * (market_price - position['average_price'])
    
    def get_pnl_summary(self, strategy_id=None, asset=None, start_time=None, end_time=None):
        """
        Get P&L summary for specified filters.
        
        Args:
            strategy_id (str): Optional strategy filter
            asset (str): Optional asset filter
            start_time (float): Optional start time filter
            end_time (float): Optional end time filter
        
        Returns:
            dict: P&L summary
        """
        # Filter trades
        filtered_trades = self.trades
        
        if strategy_id:
            filtered_trades = [t for t in filtered_trades if t['strategy_id'] == strategy_id]
        
        if asset:
            filtered_trades = [t for t in filtered_trades if t['asset'] == asset]
        
        if start_time:
            filtered_trades = [t for t in filtered_trades if t['timestamp'] >= start_time]
        
        if end_time:
            filtered_trades = [t for t in filtered_trades if t['timestamp'] <= end_time]
        
        # Calculate summary
        if strategy_id and asset:
            # Specific strategy and asset
            realized = self.realized_pnl[strategy_id].get(asset, 0)
            unrealized = self.unrealized_pnl[strategy_id].get(asset, 0)
            position = self.positions[strategy_id].get(asset, {'quantity': 0, 'average_price': 0})
            
            return {
                'strategy_id': strategy_id,
                'asset': asset,
                'realized_pnl': realized,
                'unrealized_pnl': unrealized,
                'total_pnl': realized + unrealized,
                'position': position
            }
        
        # Aggregate P&L
        results = {}
        
        if strategy_id:
            # Aggregate by asset for a specific strategy
            results['strategy_id'] = strategy_id
            results['by_asset'] = {}
            
            for asset in set(self.positions[strategy_id].keys()) | set(self.realized_pnl[strategy_id].keys()):
                realized = self.realized_pnl[strategy_id].get(asset, 0)
                unrealized = self.unrealized_pnl[strategy_id].get(asset, 0)
                position = self.positions[strategy_id].get(asset, {'quantity': 0, 'average_price': 0})
                
                results['by_asset'][asset] = {
                    'realized_pnl': realized,
                    'unrealized_pnl': unrealized,
                    'total_pnl': realized + unrealized,
                    'position': position
                }
            
            # Calculate totals
            results['total_realized_pnl'] = sum(self.realized_pnl[strategy_id].values())
            results['total_unrealized_pnl'] = sum(self.unrealized_pnl[strategy_id].values())
            results['total_pnl'] = results['total_realized_pnl'] + results['total_unrealized_pnl']
            
        elif asset:
            # Aggregate by strategy for a specific asset
            results['asset'] = asset
            results['by_strategy'] = {}
            
            for strategy_id in self.positions:
                if asset in self.positions[strategy_id] or asset in self.realized_pnl[strategy_id]:
                    realized = self.realized_pnl[strategy_id].get(asset, 0)
                    unrealized = self.unrealized_pnl[strategy_id].get(asset, 0)
                    position = self.positions[strategy_id].get(asset, {'quantity': 0, 'average_price': 0})
                    
                    results['by_strategy'][strategy_id] = {
                        'realized_pnl': realized,
                        'unrealized_pnl': unrealized,
                        'total_pnl': realized + unrealized,
                        'position': position
                    }
            
            # Calculate totals
            results['total_realized_pnl'] = sum(self.realized_pnl[s].get(asset, 0) for s in self.realized_pnl)
            results['total_unrealized_pnl'] = sum(self.unrealized_pnl[s].get(asset, 0) for s in self.unrealized_pnl)
            results['total_pnl'] = results['total_realized_pnl'] + results['total_unrealized_pnl']
            
        else:
            # Aggregate all P&L
            results['by_strategy'] = {}
            
            for strategy_id in set(self.positions.keys()) | set(self.realized_pnl.keys()):
                strategy_realized = sum(self.realized_pnl[strategy_id].values())
                strategy_unrealized = sum(self.unrealized_pnl[strategy_id].values())
                
                results['by_strategy'][strategy_id] = {
                    'realized_pnl': strategy_realized,
                    'unrealized_pnl': strategy_unrealized,
                    'total_pnl': strategy_realized + strategy_unrealized
                }
            
            # Calculate totals
            results['total_realized_pnl'] = sum(sum(s.values()) for s in self.realized_pnl.values())
            results['total_unrealized_pnl'] = sum(sum(s.values()) for s in self.unrealized_pnl.values())
            results['total_pnl'] = results['total_realized_pnl'] + results['total_unrealized_pnl']
        
        return results
