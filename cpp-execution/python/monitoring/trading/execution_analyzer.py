"""
Execution Analyzer Module

This module analyzes execution quality including slippage, fill rates, and market impact.

Classes:
    ExecutionQualityAnalyzer: Main class for execution quality analysis
"""

import time
import pandas as pd
import numpy as np
from collections import defaultdict


class ExecutionQualityAnalyzer:
    """
    Analyzes execution quality metrics for trading activities.
    
    Attributes:
        orders (dict): Dictionary of orders by order ID
        executions (list): List of execution records
        order_metrics (dict): Calculated metrics by order ID
        exchange_metrics (dict): Aggregated metrics by exchange
    """
    
    def __init__(self):
        """Initialize the execution quality analyzer."""
        self.orders = {}
        self.executions = []
        self.order_metrics = {}
        self.exchange_metrics = defaultdict(lambda: {
            'order_count': 0,
            'fill_rate': 0,
            'avg_slippage_bps': 0,
            'avg_execution_time_ms': 0,
            'total_fees': 0
        })
    
    def add_order(self, order):
        """
        Add a new order for tracking.
        
        Args:
            order (dict): Order information including:
                         order_id, exchange, symbol, side, type, quantity,
                         price, timestamp, etc.
        """
        self.orders[order['order_id']] = {
            **order,
            'status': 'new',
            'filled_quantity': 0,
            'filled_price': 0,
            'execution_time_ms': None,
            'slippage_bps': None,
            'fill_rate': 0,
            'fees': 0
        }
    
    def add_execution(self, execution):
        """
        Add an execution report for an order.
        
        Args:
            execution (dict): Execution information including:
                            order_id, execution_id, quantity, price,
                            timestamp, fees, etc.
        
        Returns:
            dict: Updated order metrics
        """
        order_id = execution['order_id']
        
        if order_id not in self.orders:
            print(f"Warning: Execution for unknown order {order_id}")
            return None
        
        self.executions.append(execution)
        
        order = self.orders[order_id]
        
        # Update order status
        if execution['status'] in ['filled', 'partially_filled', 'canceled', 'rejected']:
            order['status'] = execution['status']
        
        # Update filled quantity and price
        if 'filled_quantity' in execution:
            new_fill = execution['filled_quantity']
            old_fill = order['filled_quantity']
            
            if new_fill > 0:
                # Update average fill price
                if 'price' in execution:
                    if old_fill == 0:
                        order['filled_price'] = execution['price']
                    else:
                        order['filled_price'] = (
                            (order['filled_price'] * old_fill + execution['price'] * new_fill) /
                            (old_fill + new_fill)
                        )
                
                order['filled_quantity'] = old_fill + new_fill
        
        # Calculate fill rate
        if order['quantity'] > 0:
            order['fill_rate'] = min(1.0, order['filled_quantity'] / order['quantity'])
        
        # Calculate execution time
        if 'timestamp' in execution and order['status'] in ['filled', 'canceled', 'rejected']:
            order['execution_time_ms'] = (execution['timestamp'] - order['timestamp']) * 1000
        
        # Calculate slippage (for market orders or filled limit orders)
        if (order['type'] == 'market' or 
            (order['type'] == 'limit' and order['status'] == 'filled')):
            
            # Calculate slippage in basis points
            if order['side'] == 'buy':
                # For buys, positive slippage means paying more than expected
                expected_price = order.get('expected_price', order.get('price', order['filled_price']))
                slippage = (order['filled_price'] - expected_price) / expected_price
            else:
                # For sells, positive slippage means receiving less than expected
                expected_price = order.get('expected_price', order.get('price', order['filled_price']))
                slippage = (expected_price - order['filled_price']) / expected_price
            
            order['slippage_bps'] = slippage * 10000  # Convert to basis points
        
        # Update fees
        if 'fees' in execution:
            order['fees'] += execution['fees']
        
        # Update metrics dictionary
        self.order_metrics[order_id] = {
            'order_id': order_id,
            'exchange': order['exchange'],
            'symbol': order['symbol'],
            'side': order['side'],
            'type': order['type'],
            'quantity': order['quantity'],
            'price': order.get('price'),
            'filled_quantity': order['filled_quantity'],
            'filled_price': order['filled_price'],
            'status': order['status'],
            'fill_rate': order['fill_rate'],
            'execution_time_ms': order['execution_time_ms'],
            'slippage_bps': order['slippage_bps'],
            'fees': order['fees']
        }
        
        # Update exchange metrics
        exchange = order['exchange']
        completed_orders = [o for o in self.orders.values() 
                           if o['exchange'] == exchange and 
                           o['status'] in ['filled', 'canceled', 'rejected']]
        
        if completed_orders:
            self.exchange_metrics[exchange]['order_count'] = len(completed_orders)
            
            filled_orders = [o for o in completed_orders if o['status'] == 'filled']
            
            if filled_orders:
                self.exchange_metrics[exchange]['fill_rate'] = sum(o['fill_rate'] for o in filled_orders) / len(filled_orders)
                
                slippages = [o['slippage_bps'] for o in filled_orders if o['slippage_bps'] is not None]
                
                if slippages:
                    self.exchange_metrics[exchange]['avg_slippage_bps'] = sum(slippages) / len(slippages)
                
                times = [o['execution_time_ms'] for o in filled_orders if o['execution_time_ms'] is not None]
                
                if times:
                    self.exchange_metrics[exchange]['avg_execution_time_ms'] = sum(times) / len(times)
                
                self.exchange_metrics[exchange]['total_fees'] = sum(o['fees'] for o in filled_orders)
        
        return self.order_metrics[order_id]
    
    def get_order_metrics(self, order_id):
        """
        Get metrics for a specific order.
        
        Args:
            order_id (str): Order identifier
        
        Returns:
            dict: Order metrics
        """
        return self.order_metrics.get(order_id)
    
    def get_exchange_metrics(self, exchange=None):
        """
        Get aggregated metrics by exchange.
        
        Args:
            exchange (str): Optional exchange filter
        
        Returns:
            dict: Exchange metrics
        """
        if exchange:
            return self.exchange_metrics.get(exchange)
        
        return dict(self.exchange_metrics)
    
    def get_metrics_summary(self, start_time=None, end_time=None, exchange=None, symbol=None):
        """
        Get summary metrics for specified filters.
        
        Args:
            start_time (float): Optional start time filter
            end_time (float): Optional end time filter
            exchange (str): Optional exchange filter
            symbol (str): Optional symbol filter
        
        Returns:
            dict: Summary metrics
        """
        # Filter orders
        filtered_orders = list(self.orders.values())
        
        if start_time:
            filtered_orders = [o for o in filtered_orders if o['timestamp'] >= start_time]
        
        if end_time:
            filtered_orders = [o for o in filtered_orders if o['timestamp'] <= end_time]
        
        if exchange:
            filtered_orders = [o for o in filtered_orders if o['exchange'] == exchange]
        
        if symbol:
            filtered_orders = [o for o in filtered_orders if o['symbol'] == symbol]
        
        # Calculate summary metrics
        completed_orders = [o for o in filtered_orders if o['status'] in ['filled', 'canceled', 'rejected']]
        filled_orders = [o for o in filtered_orders if o['status'] == 'filled']
        
        if not completed_orders:
            return {
                'order_count': 0,
                'fill_rate': 0,
                'avg_slippage_bps': 0,
                'avg_execution_time_ms': 0,
                'total_fees': 0
            }
        
        # Calculate metrics
        result = {
            'order_count': len(completed_orders)
        }
        
        if filled_orders:
            result['fill_rate'] = sum(o['fill_rate'] for o in filled_orders) / len(filled_orders)
            
            slippages = [o['slippage_bps'] for o in filled_orders if o['slippage_bps'] is not None]
            
            if slippages:
                result['avg_slippage_bps'] = sum(slippages) / len(slippages)
                result['median_slippage_bps'] = np.median(slippages)
                result['min_slippage_bps'] = min(slippages)
                result['max_slippage_bps'] = max(slippages)
                result['stddev_slippage_bps'] = np.std(slippages) if len(slippages) > 1 else 0
            
            times = [o['execution_time_ms'] for o in filled_orders if o['execution_time_ms'] is not None]
            
            if times:
                result['avg_execution_time_ms'] = sum(times) / len(times)
                result['median_execution_time_ms'] = np.median(times)
                result['min_execution_time_ms'] = min(times)
                result['max_execution_time_ms'] = max(times)
                result['stddev_execution_time_ms'] = np.std(times) if len(times) > 1 else 0
            
            result['total_fees'] = sum(o['fees'] for o in filled_orders)
        
        return result
    
    def analyze_market_impact(self, order_id):
        """
        Analyze market impact of a specific order.
        
        Args:
            order_id (str): Order identifier
        
        Returns:
            dict: Market impact analysis
        """
        # TODO: Implement market impact analysis
        return {}
