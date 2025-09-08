#!/usr/bin/env python3
"""
Order Book Analyzer
------------------
A tool for visualizing and analyzing order book data from cryptocurrency exchanges.
Connects to the data ingestion layer via ZeroMQ, processes order book updates,
and provides real-time visualization and analysis.

Usage:
    python order_book_analyzer.py [--exchange EXCHANGE] [--pair PAIR] [--mode MODE]

Options:
    --exchange EXCHANGE    Exchange to analyze (default: all)
    --pair PAIR            Trading pair to analyze (default: BTC/USDT)
    --mode MODE            Visualization mode: 'depth', 'heatmap', 'imbalance' (default: depth)
"""

import zmq
import time
import json
import argparse
import threading
from datetime import datetime
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from matplotlib.ticker import FuncFormatter
import seaborn as sns
from collections import deque
import pickle
import os
import logging
from typing import Dict, List, Tuple, Optional, Union, Any

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger("OrderBookAnalyzer")

# Let's assume we have a protobuf structure similar to this
class FakeProto:
    """Placeholder for protobuf objects when not available"""
    def ParseFromString(self, data):
        try:
            self.data = pickle.loads(data)
        except:
            self.data = json.loads(data.decode('utf-8'))
    
    def __getattr__(self, name):
        if hasattr(self.data, name):
            return getattr(self.data, name)
        if isinstance(self.data, dict) and name in self.data:
            return self.data[name]
        raise AttributeError(f"No attribute {name}")

# Try to import actual protobuf definitions, use fake if not available
try:
    import order_book_pb2 as pb
except ImportError:
    logger.warning("Could not import protobuf definitions, using fallback deserialization")
    pb = type('FakeProtoModule', (), {
        'MessageEnvelope': FakeProto,
        'OrderBookUpdate': FakeProto
    })

class OrderBookData:
    """Class to store and manage order book data"""
    def __init__(self, max_history_length=100):
        self.current_book = {}
        self.history = deque(maxlen=max_history_length)
        self.metrics_history = deque(maxlen=max_history_length)
        self.timestamps = deque(maxlen=max_history_length)
    
    def update(self, order_book: Dict) -> None:
        """Update the order book with new data"""
        self.current_book = order_book
        self.history.append(order_book)
        self.timestamps.append(datetime.fromtimestamp(order_book['received_timestamp'] / 1e9))
        
        # Calculate and store metrics
        metrics = self.calculate_metrics(order_book)
        self.metrics_history.append(metrics)
    
    def calculate_metrics(self, order_book: Dict) -> Dict:
        """Calculate order book metrics"""
        if not order_book or 'bids' not in order_book or 'asks' not in order_book:
            return {}
            
        try:
            # Extract bid and ask data
            bids = [(float(level['price']), float(level['quantity'])) 
                   for level in order_book['bids']]
            asks = [(float(level['price']), float(level['quantity'])) 
                   for level in order_book['asks']]
            
            if not bids or not asks:
                return {}
                
            # Calculate basic metrics
            bidVolume = sum(qty for _, qty in bids)
            askVolume = sum(qty for _, qty in asks)
            
            # Bid/Ask volume ratio
            bid_ask_ratio = bidVolume / askVolume if askVolume > 0 else float('inf')
            
            # Calculate mid price and spread
            bestBid = max(p for p, _ in bids) if bids else 0
            bestAsk = min(p for p, _ in asks) if asks else 0
            midPrice = (bestBid + bestAsk) / 2 if bestBid > 0 and bestAsk > 0 else 0
            spread = bestAsk - bestBid if bestBid > 0 and bestAsk > 0 else 0
            spread_pct = (spread / midPrice) * 100 if midPrice > 0 else 0
            
            # Weighted mid price
            bid_weighted_price = sum(price * qty for price, qty in bids) / bidVolume if bidVolume > 0 else 0
            ask_weighted_price = sum(price * qty for price, qty in asks) / askVolume if askVolume > 0 else 0
            weighted_mid_price = (bid_weighted_price * askVolume + ask_weighted_price * bidVolume) / (bidVolume + askVolume) if (bidVolume + askVolume) > 0 else 0
            
            # Calculate imbalance at different depth levels
            imbalance_metrics = {}
            depthLevels = [5, 10, 20, 50, 100]
            
            for depth in depthLevels:
                top_bids = bids[:min(depth, len(bids))]
                top_asks = asks[:min(depth, len(asks))]
                
                top_bid_volume = sum(qty for _, qty in top_bids)
                top_ask_volume = sum(qty for _, qty in top_asks)
                
                # Calculate imbalance
                imbalance = (top_bid_volume - top_ask_volume) / (top_bid_volume + top_ask_volume) if (top_bid_volume + top_ask_volume) > 0 else 0
                imbalance_metrics[f"imbalance_d{depth}"] = imbalance
            
            # Price impact to trade X amount
            def price_impact(side: str, amount: float) -> float:
                """Calculate the price impact of trading amount of base currency"""
                levels = bids if side == 'bid' else asks
                levels = sorted(levels, key=lambda x: x[0], reverse=(side == 'bid'))
                
                remaining = amount
                total_cost = 0
                last_price = levels[0][0] if levels else 0
                
                for price, qty in levels:
                    if remaining <= 0:
                        break
                    filled = min(remaining, qty)
                    total_cost += filled * price
                    remaining -= filled
                    last_price = price
                
                # If we couldn't fill the entire amount, use the last price
                if remaining > 0:
                    total_cost += remaining * last_price
                
                return total_cost / amount if amount > 0 else 0
            
            # Calculate price impact for different order sizes
            bid_impact_1 = price_impact('bid', 1)
            bid_impact_10 = price_impact('bid', 10)
            ask_impact_1 = price_impact('ask', 1)
            ask_impact_10 = price_impact('ask', 10)
            
            return {
                'bidVolume': bidVolume,
                'askVolume': askVolume,
                'bid_ask_ratio': bid_ask_ratio,
                'bestBid': bestBid,
                'bestAsk': bestAsk,
                'midPrice': midPrice,
                'spread': spread,
                'spread_pct': spread_pct,
                'weighted_mid_price': weighted_mid_price,
                'bid_impact_1': bid_impact_1,
                'bid_impact_10': bid_impact_10,
                'ask_impact_1': ask_impact_1,
                'ask_impact_10': ask_impact_10,
                **imbalance_metrics
            }
            
        except Exception as e:
            logger.error(f"Error calculating metrics: {e}")
            return {}
    
    def get_metrics_df(self) -> pd.DataFrame:
        """Convert metrics history to DataFrame"""
        if not self.metrics_history:
            return pd.DataFrame()
        
        df = pd.DataFrame(list(self.metrics_history))
        df['timestamp'] = self.timestamps
        df.set_index('timestamp', inplace=True)
        return df
    
    def detect_imbalances(self, threshold=0.3) -> List[Dict]:
        """Detect significant order book imbalances"""
        if not self.metrics_history:
            return []
        
        imbalances = []
        for i, metrics in enumerate(self.metrics_history):
            if 'imbalance_d20' in metrics and abs(metrics['imbalance_d20']) > threshold:
                imbalances.append({
                    'timestamp': self.timestamps[i],
                    'imbalance': metrics['imbalance_d20'],
                    'midPrice': metrics['midPrice'],
                    'bidVolume': metrics['bidVolume'],
                    'askVolume': metrics['askVolume']
                })
        
        return imbalances


class OrderBookClient:
    """Client for connecting to ZeroMQ and receiving order book data"""
    def __init__(self, zmq_address: str = "tcp://localhost:5555", topic: str = "market.orderbook.normalized"):
        """Initialize ZeroMQ subscriber"""
        self.context = zmq.Context()
        self.socket = self.context.socket(zmq.SUB)
        self.socket.connect(zmq_address)
        self.socket.setsockopt_string(zmq.SUBSCRIBE, topic)
        self.running = False
        self.callback = None
    
    def set_callback(self, callback):
        """Set callback function to receive order book updates"""
        self.callback = callback
    
    def start(self):
        """Start listening for order book updates in a separate thread"""
        self.running = True
        self.thread = threading.Thread(target=self._listen_loop)
        self.thread.daemon = True
        self.thread.start()
        
    def stop(self):
        """Stop listening for updates"""
        self.running = False
        if hasattr(self, 'thread'):
            self.thread.join(timeout=1.0)
        self.socket.close()
        self.context.term()
    
    def _listen_loop(self):
        """Main listener loop"""
        while self.running:
            try:
                if self.socket.poll(timeout=100):
                    topic = self.socket.recv_string()
                    message = self.socket.recv()
                    
                    # Parse message
                    order_book = self._parse_message(message)
                    
                    # Call callback if set
                    if self.callback and order_book:
                        self.callback(order_book)
            except Exception as e:
                logger.error(f"Error in listen loop: {e}")
                time.sleep(0.1)
    
    def _parse_message(self, message: bytes) -> Optional[Dict]:
        """Parse ZeroMQ message containing order book data"""
        try:
            # Try to parse as protobuf first
            envelope = pb.MessageEnvelope()
            envelope.ParseFromString(message)
            
            order_book = pb.OrderBookUpdate()
            order_book.ParseFromString(envelope.payload)
            
            # Convert to dictionary
            return {
                'exchange_id': order_book.exchange_id,
                'trading_pair': order_book.trading_pair,
                'exchangeTimestamp': order_book.exchangeTimestamp,
                'received_timestamp': order_book.received_timestamp,
                'sequenceNumber': order_book.sequenceNumber,
                'isSnapshot': order_book.isSnapshot,
                'bids': [{'price': level.price, 'quantity': level.quantity} 
                         for level in order_book.bids],
                'asks': [{'price': level.price, 'quantity': level.quantity} 
                         for level in order_book.asks]
            }
        except Exception as e:
            # Fallback to JSON parsing
            logger.warning(f"Failed to parse protobuf: {e}, trying JSON")
            try:
                data = json.loads(message)
                return data
            except:
                logger.error(f"Failed to parse message as JSON")
                return None


class OrderBookVisualizer:
    """Visualizes order book data using matplotlib"""
    def __init__(self, order_book_data: OrderBookData):
        self.order_book_data = order_book_data
        self.exchange_id = None
        self.trading_pair = None
        self.mode = 'depth'
        self.fig = None
        self.ax = None
        self.animation = None
        
    def set_mode(self, mode: str):
        """Set visualization mode"""
        valid_modes = ['depth', 'heatmap', 'imbalance', 'metrics']
        if mode in valid_modes:
            self.mode = mode
        else:
            logger.warning(f"Invalid mode: {mode}. Using default: depth")
            self.mode = 'depth'
    
    def setup_plot(self):
        """Setup plot based on current mode"""
        plt.style.use('dark_background')
        self.fig = plt.figure(figsize=(14, 8))
        
        if self.mode == 'depth':
            self.ax = self.fig.add_subplot(1, 1, 1)
            self.bid_line, = self.ax.plot([], [], 'g-', linewidth=2, label='Bids')
            self.ask_line, = self.ax.plot([], [], 'r-', linewidth=2, label='Asks')
            self.ax.set_xlabel('Price')
            self.ax.set_ylabel('Cumulative Volume')
            self.ax.grid(True, alpha=0.3)
            self.ax.legend()
            
        elif self.mode == 'heatmap':
            self.ax = self.fig.add_subplot(1, 1, 1)
            self.heatmap = self.ax.imshow(np.zeros((40, 40)), 
                                         aspect='auto', 
                                         cmap='coolwarm', 
                                         interpolation='nearest',
                                         origin='lower')
            plt.colorbar(self.heatmap, label='Volume')
            self.ax.set_xlabel('Time')
            self.ax.set_ylabel('Price Levels')
            
        elif self.mode == 'imbalance':
            self.ax1 = self.fig.add_subplot(2, 1, 1)
            self.ax2 = self.fig.add_subplot(2, 1, 2, sharex=self.ax1)
            
            self.price_line, = self.ax1.plot([], [], 'w-', linewidth=1)
            self.imbalance_line, = self.ax2.plot([], [], 'g-', linewidth=1)
            
            self.ax1.set_ylabel('Price')
            self.ax2.set_ylabel('Imbalance')
            self.ax2.set_xlabel('Time')
            
            self.ax1.grid(True, alpha=0.3)
            self.ax2.grid(True, alpha=0.3)
            
            # Add threshold lines
            self.ax2.axhline(y=0.3, color='g', linestyle='--', alpha=0.5)
            self.ax2.axhline(y=-0.3, color='r', linestyle='--', alpha=0.5)
            
        elif self.mode == 'metrics':
            gs = self.fig.add_gridspec(3, 2)
            self.ax_price = self.fig.add_subplot(gs[0, :])
            self.ax_volume = self.fig.add_subplot(gs[1, 0])
            self.ax_ratio = self.fig.add_subplot(gs[1, 1])
            self.ax_spread = self.fig.add_subplot(gs[2, 0])
            self.ax_imbalance = self.fig.add_subplot(gs[2, 1])
            
            # Initialize lines
            self.price_line, = self.ax_price.plot([], [], 'w-', linewidth=1)
            self.bid_vol_line, = self.ax_volume.plot([], [], 'g-', linewidth=1)
            self.ask_vol_line, = self.ax_volume.plot([], [], 'r-', linewidth=1)
            self.ratio_line, = self.ax_ratio.plot([], [], 'y-', linewidth=1)
            self.spread_line, = self.ax_spread.plot([], [], 'c-', linewidth=1)
            self.imbalance_line, = self.ax_imbalance.plot([], [], 'm-', linewidth=1)
            
            # Labels and grids
            self.ax_price.set_ylabel('Price')
            self.ax_volume.set_ylabel('Volume')
            self.ax_ratio.set_ylabel('Bid/Ask Ratio')
            self.ax_spread.set_ylabel('Spread %')
            self.ax_imbalance.set_ylabel('Imbalance')
            
            self.ax_price.grid(True, alpha=0.3)
            self.ax_volume.grid(True, alpha=0.3)
            self.ax_ratio.grid(True, alpha=0.3)
            self.ax_spread.grid(True, alpha=0.3)
            self.ax_imbalance.grid(True, alpha=0.3)
            
        # Set title
        if self.exchange_id and self.trading_pair:
            plt.suptitle(f"{self.exchange_id} - {self.trading_pair} Order Book Analysis", 
                        fontsize=16)
    
    def update_visualization(self, frame):
        """Update visualization based on current mode"""
        if not self.order_book_data.current_book:
            return
        
        # Update exchange and pair info if not set
        if not self.exchange_id and 'exchange_id' in self.order_book_data.current_book:
            self.exchange_id = self.order_book_data.current_book['exchange_id']
        
        if not self.trading_pair and 'trading_pair' in self.order_book_data.current_book:
            self.trading_pair = self.order_book_data.current_book['trading_pair']
            
        if self.exchange_id and self.trading_pair:
            if self.mode == 'depth':
                self.fig.suptitle(f"{self.exchange_id} - {self.trading_pair} Order Book Depth", fontsize=16)
            elif self.mode == 'heatmap':
                self.fig.suptitle(f"{self.exchange_id} - {self.trading_pair} Order Book Heatmap", fontsize=16)
            elif self.mode == 'imbalance':
                self.fig.suptitle(f"{self.exchange_id} - {self.trading_pair} Order Book Imbalance", fontsize=16)
            elif self.mode == 'metrics':
                self.fig.suptitle(f"{self.exchange_id} - {self.trading_pair} Order Book Metrics", fontsize=16)
        
        # Update based on mode
        if self.mode == 'depth':
            return self._update_depth_chart()
        elif self.mode == 'heatmap':
            return self._update_heatmap()
        elif self.mode == 'imbalance':
            return self._update_imbalance_chart()
        elif self.mode == 'metrics':
            return self._update_metrics_chart()
    
    # def _update_depth_chart(self):
    #     """Update order book depth chart"""
    #     try:
    #         order_book = self.order_book_data.current_book
            
    #         # Convert to numeric values
    #         bids = [(float(level['price']), float(level['quantity'])) 
    #                for level in order_book['bids']]
    #         asks = [(float(level['price']), float(level['quantity'])) 
    #                for level in order_book['asks']]
            
    #         # Sort bids (descending) and asks (ascending)
    #         bids = sorted(bids, key=lambda x: x[0], reverse=True)
    #         asks = sorted(asks, key=lambda x: x[0])
            
    #         # Calculate cumulative volumes
    #         bid_prices = [p for p, _ in bids]
    #         bid_volumes = [q for _, q in bids]
    #         bid_cum_volumes = np.cumsum(bid_volumes)
            
    #         ask_prices = [p for p, _ in asks]
    #         ask_volumes = [q for _, q in asks]
    #         ask_cum_volumes = np.cumsum(ask_volumes)
            
    #         # Update plot data
    #         self.bid_line.set_data(bid_prices, bid_cum_volumes)
    #         self.ask_line.set_data(ask_prices, ask_cum_volumes)
            
    #         # Adjust plot limits
    #         if bid_prices and ask_prices:
    #             # Calculate price range with padding
    #             min_price = min(bid_prices[-1] if bid_prices else float('inf'), 
    #                           ask_prices[0] if ask_prices else float('inf'))
    #             max_price = max(bid_prices[0] if bid_prices else float('-inf'), 
    #                           ask_prices[-1] if ask_prices else float('-inf'))
    #             price_range = max_price - min_price
                
    #             self.ax.set_xlim(min_price - price_range * 0.1, max_price + price_range * 0.1)
    #             max_volume = max(max(bid_cum_volumes) if bid_cum_volumes.size > 0 else 0, 
    #                            max(ask_cum_volumes) if ask_cum_volumes.size > 0 else 0)
    #             self.ax.set_ylim(0, max_volume * 1.1)
            
    #         return self.bid_line, self.ask_line
        
    #     except Exception as e:
    #         logger.error(f"Error updating depth chart: {e}")
    #         return ()
    # This is a focused fix for the visualization issue in the order_book_analyzer.py file

    def _update_depth_chart(self):
        """Update order book depth chart"""
        try:
            order_book = self.order_book_data.current_book
            
            # If no order book data yet, return empty artists
            if not order_book or 'bids' not in order_book or 'asks' not in order_book:
                return (self.bid_line, self.ask_line)
            
            # Convert to numeric values
            bids = [(float(level['price']), float(level['quantity'])) 
                for level in order_book['bids']]
            asks = [(float(level['price']), float(level['quantity'])) 
                for level in order_book['asks']]
            
            if not bids or not asks:
                return (self.bid_line, self.ask_line)
            
            # Sort bids (descending) and asks (ascending)
            bids = sorted(bids, key=lambda x: x[0], reverse=True)
            asks = sorted(asks, key=lambda x: x[0])
            
            # Calculate cumulative volumes
            bid_prices = np.array([p for p, _ in bids])
            bid_volumes = np.array([q for _, q in bids])
            bid_cum_volumes = np.cumsum(bid_volumes)
            
            ask_prices = np.array([p for p, _ in asks])
            ask_volumes = np.array([q for _, q in asks])
            ask_cum_volumes = np.cumsum(ask_volumes)
            
            # Update plot data
            self.bid_line.set_data(bid_prices, bid_cum_volumes)
            self.ask_line.set_data(ask_prices, ask_cum_volumes)
            
            # Adjust plot limits
            if bid_prices.size > 0 and ask_prices.size > 0:
                # Calculate price range with padding
                min_price = min(bid_prices[-1] if bid_prices.size > 0 else float('inf'), 
                            ask_prices[0] if ask_prices.size > 0 else float('inf'))
                max_price = max(bid_prices[0] if bid_prices.size > 0 else float('-inf'), 
                            ask_prices[-1] if ask_prices.size > 0 else float('-inf'))
                price_range = max_price - min_price
                
                self.ax.set_xlim(min_price - price_range * 0.1, max_price + price_range * 0.1)
                max_volume = max(max(bid_cum_volumes) if bid_cum_volumes.size > 0 else 0, 
                            max(ask_cum_volumes) if ask_cum_volumes.size > 0 else 0)
                self.ax.set_ylim(0, max_volume * 1.1)
            
            return (self.bid_line, self.ask_line)
        
        except Exception as e:
            logger.error(f"Error updating depth chart: {e}")
            return (self.bid_line, self.ask_line)

    # Update the FuncAnimation call in the start_animation method
    def start_animation(self, interval=500):
        """Start the animation"""
        self.setup_plot()
        self.animation = FuncAnimation(
            self.fig, 
            self.update_visualization, 
            blit=True, 
            interval=interval,
            save_count=100  # Limit the cache to 100 frames
        )
        plt.tight_layout()
        plt.show()
    def _update_heatmap(self):
        """Update order book heatmap"""
        try:
            if len(self.order_book_data.history) < 2:
                return (self.heatmap,)
            
            # Create heatmap data
            history = list(self.order_book_data.history)
            num_samples = min(40, len(history))
            
            # Get price range from the latest order book
            latest = history[-1]
            bids = [(float(level['price']), float(level['quantity'])) 
                   for level in latest['bids']]
            asks = [(float(level['price']), float(level['quantity'])) 
                   for level in latest['asks']]
            
            if not bids or not asks:
                return (self.heatmap,)
                
            bestBid = max(p for p, _ in bids)
            bestAsk = min(p for p, _ in asks)
            midPrice = (bestBid + bestAsk) / 2
            
            # Create price levels around mid price
            price_step = (bestAsk - bestBid) / 4
            price_levels = np.linspace(midPrice - price_step * 10, 
                                      midPrice + price_step * 10, 
                                      40)
            
            # Build heatmap matrix
            heatmap_data = np.zeros((40, num_samples))
            
            for i in range(num_samples):
                ob = history[-(i+1)]
                bids = [(float(level['price']), float(level['quantity'])) 
                       for level in ob['bids']]
                asks = [(float(level['price']), float(level['quantity'])) 
                       for level in ob['asks']]
                
                # Combine bids and asks
                all_levels = bids + asks
                
                # Fill heatmap column
                for j, price in enumerate(price_levels):
                    volume = 0
                    for p, q in all_levels:
                        # Find volume at price levels close to our grid
                        if abs(p - price) < price_step / 2:
                            volume += q
                    heatmap_data[j, i] = volume
            
            # Normalize for better visualization
            max_val = np.max(heatmap_data)
            if max_val > 0:
                heatmap_data = heatmap_data / max_val
            
            # Add bid/ask coloring
            mid_idx = 20  # Middle index
            heatmap_data[mid_idx:, :] = heatmap_data[mid_idx:, :] * -1
            
            # Update heatmap
            self.heatmap.set_array(heatmap_data)
            self.heatmap.set_clim(-1, 1)
            
            # Update labels
            self.ax.set_yticks([0, 20, 39])
            self.ax.set_yticklabels([f"{price_levels[0]:.2f}", 
                                    f"{price_levels[20]:.2f}", 
                                    f"{price_levels[-1]:.2f}"])
            
            self.ax.set_xticks([0, num_samples//2, num_samples-1])
            time_labels = [history[-(i+1)]['received_timestamp'] for i in [0, num_samples//2, num_samples-1]]
            time_labels = [datetime.fromtimestamp(ts/1e9).strftime('%H:%M:%S') for ts in time_labels]
            self.ax.set_xticklabels(time_labels)
            
            return (self.heatmap,)
        
        except Exception as e:
            logger.error(f"Error updating heatmap: {e}")
            return ()
    
    def _update_imbalance_chart(self):
        """Update imbalance chart"""
        try:
            metrics_df = self.order_book_data.get_metrics_df()
            
            if metrics_df.empty:
                return self.price_line, self.imbalance_line
            
            # Get last N data points
            n_points = min(50, len(metrics_df))
            df = metrics_df.iloc[-n_points:]
            
            # Update lines
            timestamps = df.index
            mid_prices = df['midPrice'].values
            imbalances = df['imbalance_d20'].values
            
            self.price_line.set_data(timestamps, mid_prices)
            self.imbalance_line.set_data(timestamps, imbalances)
            
            # Update limits
            if len(timestamps) > 0:
                self.ax1.set_xlim(timestamps[0], timestamps[-1])
                
                if len(mid_prices) > 0:
                    min_price = np.min(mid_prices)
                    max_price = np.max(mid_prices)
                    price_range = max_price - min_price
                    self.ax1.set_ylim(min_price - price_range * 0.1, max_price + price_range * 0.1)
                
                self.ax2.set_ylim(-1, 1)
                
                # Format x-axis
                self.ax2.xaxis.set_major_formatter(
                    plt.FuncFormatter(lambda x, _: datetime.fromtimestamp(x.tolist()/1e9).strftime('%H:%M:%S'))
                )
                plt.setp(self.ax2.xaxis.get_majorticklabels(), rotation=45)
            
            return self.price_line, self.imbalance_line
        
        except Exception as e:
            logger.error(f"Error updating imbalance chart: {e}")
            return ()
    
    def _update_metrics_chart(self):
        """Update metrics chart with multiple subplots"""
        try:
            metrics_df = self.order_book_data.get_metrics_df()
            
            if metrics_df.empty:
                return self.price_line, self.bid_vol_line, self.ask_vol_line, self.ratio_line, self.spread_line, self.imbalance_line
            
            # Get last N data points
            n_points = min(50, len(metrics_df))
            df = metrics_df.iloc[-n_points:]
            
            # Extract timestamps and metrics
            timestamps = df.index
            mid_prices = df['midPrice'].values
            bid_volumes = df['bidVolume'].values
            ask_volumes = df['askVolume'].values
            bid_ask_ratios = df['bid_ask_ratio'].values
            spread_pcts = df['spread_pct'].values
            imbalances = df['imbalance_d20'].values
            
            # Update lines
            self.price_line.set_data(timestamps, mid_prices)
            self.bid_vol_line.set_data(timestamps, bid_volumes)
            self.ask_vol_line.set_data(timestamps, ask_volumes)
            self.ratio_line.set_data(timestamps, bid_ask_ratios)
            self.spread_line.set_data(timestamps, spread_pcts)
            self.imbalance_line.set_data(timestamps, imbalances)
            
            # Update limits
            if len(timestamps) > 0:
                # Set x limits for all subplots
                self.ax_price.set_xlim(timestamps[0], timestamps[-1])
                
                # Price chart limits
                if len(mid_prices) > 0:
                    min_price = np.min(mid_prices)
                    max_price = np.max(mid_prices)
                    price_range = max_price - min_price
                    self.ax_price.set_ylim(min_price - price_range * 0.1, max_price + price_range * 0.1)
                
                # Volume chart limits
                if len(bid_volumes) > 0 and len(ask_volumes) > 0:
                    max_vol = max(np.max(bid_volumes), np.max(ask_volumes))
                    self.ax_volume.set_ylim(0, max_vol * 1.1)
                
                # Ratio chart limits
                if len(bid_ask_ratios) > 0:
                    max_ratio = np.max(bid_ask_ratios)
                    if max_ratio > 10:  # Cap extremely high ratios
                        max_ratio = 10
                    self.ax_ratio.set_ylim(0, max_ratio * 1.1)
                
                # Spread chart limits
                if len(spread_pcts) > 0:
                    max_spread = np.max(spread_pcts)
                    self.ax_spread.set_ylim(0, max_spread * 1.5)
                
                # Imbalance chart limits
                self.ax_imbalance.set_ylim(-1, 1)
                
                # Format x-axis for time
                formatter = plt.FuncFormatter(
                    lambda x, _: datetime.fromtimestamp(x.tolist()/1e9).strftime('%H:%M:%S')
                )
                
                self.ax_price.xaxis.set_major_formatter(formatter)
                self.ax_volume.xaxis.set_major_formatter(formatter)
                self.ax_ratio.xaxis.set_major_formatter(formatter)
                self.ax_spread.xaxis.set_major_formatter(formatter)
                self.ax_imbalance.xaxis.set_major_formatter(formatter)
                # Add labels and legends
                self.ax_price.set_title('Mid Price')
                self.ax_volume.set_title('Volume')
                self.ax_ratio.set_title('Bid/Ask Ratio')
                self.ax_spread.set_title('Spread %')
                self.ax_imbalance.set_title('Order Book Imbalance')
                
                self.ax_volume.legend(['Bid Volume', 'Ask Volume'])
                
                # Add threshold lines for imbalance
                self.ax_imbalance.axhline(y=0.3, color='g', linestyle='--', alpha=0.5)
                self.ax_imbalance.axhline(y=-0.3, color='r', linestyle='--', alpha=0.5)
                
                # Tight layout
                self.fig.tight_layout(rect=[0, 0, 1, 0.95])
            
            return self.price_line, self.bid_vol_line, self.ask_vol_line, self.ratio_line, self.spread_line, self.imbalance_line
        
        except Exception as e:
            logger.error(f"Error updating metrics chart: {e}")
            return ()
    
    # def start_animation(self, interval=500):
    #     """Start the animation"""
    #     self.setup_plot()
    #     self.animation = FuncAnimation(
    #         self.fig, self.update_visualization, 
    #         blit=True, interval=interval
    #     )
    #     plt.tight_layout()
    #     plt.show()
    
    def save_snapshot(self, filename=None):
        """Save current visualization to a file"""
        if not filename:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = f"orderbook_{self.exchange_id}_{self.trading_pair}_{self.mode}_{timestamp}.png"
        
        if self.fig:
            self.fig.savefig(filename, dpi=300, bbox_inches='tight')
            logger.info(f"Saved snapshot to {filename}")
        else:
            logger.error("No figure to save")


class OrderBookAnalyzer:
    """Main class for analyzing order books and detecting imbalances"""
    def __init__(self, zmq_address="tcp://localhost:5555", 
                exchange=None, pair="BTC/USDT", mode="depth"):
        self.exchange = exchange
        self.pair = pair
        self.mode = mode
        self.zmq_address = zmq_address
        
        # Initialize data storage
        self.order_book_data = OrderBookData(max_history_length=1000)
        
        # Initialize ZeroMQ client
        self.client = OrderBookClient(zmq_address)
        
        # Initialize visualizer
        self.visualizer = OrderBookVisualizer(self.order_book_data)
        self.visualizer.set_mode(mode)
        
        # Imbalance alert settings
        self.imbalance_threshold = 0.3
        self.imbalance_alert_cooldown = 10  # seconds
        self.last_alert_time = 0
        
        # Data recording
        self.recording = False
        self.record_file = None
        
    def on_order_book_update(self, order_book):
        """Callback for handling order book updates"""
        # Filter by exchange and pair if specified
        if self.exchange and order_book.get('exchange_id') != self.exchange:
            return
        
        if self.pair and order_book.get('trading_pair') != self.pair:
            return
            
        # Update data
        self.order_book_data.update(order_book)
        
        # Check for imbalances
        self._check_imbalances()
        
        # Record data if enabled
        if self.recording and self.record_file:
            self._record_data(order_book)
    
    def _check_imbalances(self):
        """Check for significant order book imbalances and alert"""
        if not self.order_book_data.metrics_history:
            return
        
        # Get latest metrics
        latest_metrics = self.order_book_data.metrics_history[-1]
        
        # Check if we have imbalance metrics
        if 'imbalance_d20' not in latest_metrics:
            return
        
        # Check if imbalance exceeds threshold
        imbalance = latest_metrics['imbalance_d20']
        
        current_time = time.time()
        if (abs(imbalance) > self.imbalance_threshold and 
            current_time - self.last_alert_time > self.imbalance_alert_cooldown):
            
            direction = "bullish" if imbalance > 0 else "bearish"
            strength = abs(imbalance)
            
            logger.warning(f"*** IMBALANCE ALERT *** {direction.upper()} imbalance detected! "
                          f"Strength: {strength:.2f}, Price: {latest_metrics['midPrice']:.2f}")
            
            # Update last alert time
            self.last_alert_time = current_time
    
    def _record_data(self, order_book):
        """Record order book data to file"""
        try:
            # Add timestamp to the order book data
            data_to_save = {
                'timestamp': datetime.now().isoformat(),
                'order_book': order_book
            }
            
            # Write to file
            self.record_file.write(json.dumps(data_to_save) + "\n")
            self.record_file.flush()
        except Exception as e:
            logger.error(f"Error recording data: {e}")
    
    def start_recording(self, filename=None):
        """Start recording order book data to file"""
        if not filename:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = f"orderbook_data_{timestamp}.jsonl"
        
        try:
            self.record_file = open(filename, 'w')
            self.recording = True
            logger.info(f"Started recording to {filename}")
        except Exception as e:
            logger.error(f"Error starting recording: {e}")
    
    def stop_recording(self):
        """Stop recording data"""
        if self.recording and self.record_file:
            self.recording = False
            self.record_file.close()
            self.record_file = None
            logger.info("Stopped recording")
    
    def start(self):
        """Start the analyzer"""
        # Set callback for order book updates
        self.client.set_callback(self.on_order_book_update)
        
        # Start ZeroMQ client
        self.client.start()
        logger.info(f"Connected to ZeroMQ at {self.zmq_address}")
        
        # Start visualization
        logger.info(f"Starting visualization in {self.mode} mode")
        self.visualizer.start_animation()
    
    def stop(self):
        """Stop the analyzer"""
        self.client.stop()
        if self.recording:
            self.stop_recording()
        logger.info("Analyzer stopped")
    
    def save_snapshot(self, filename=None):
        """Save current visualization to file"""
        self.visualizer.save_snapshot(filename)


class ImbalanceSimulator:
    """Simulates order book data with imbalances for testing visualization"""
    def __init__(self, base_price=50000.0):
        self.base_price = base_price
        self.current_price = base_price
        self.volatility = 0.001
        self.tick = 0
        self.imbalance_state = 0  # -1: sell imbalance, 0: balanced, 1: buy imbalance
        self.imbalance_duration = 0
        self.max_imbalance_duration = 20
    
    def get_next_order_book(self):
        """Generate next simulated order book"""
        self.tick += 1
        
        # Randomly change imbalance state
        if self.imbalance_duration <= 0:
            # Chance to start a new imbalance
            if np.random.random() < 0.05:
                self.imbalance_state = np.random.choice([-1, 1])
                self.imbalance_duration = np.random.randint(5, self.max_imbalance_duration)
        else:
            self.imbalance_duration -= 1
            if self.imbalance_duration <= 0:
                self.imbalance_state = 0
        
        # Update price based on imbalance and random walk
        price_change = np.random.normal(0, self.volatility)
        if self.imbalance_state != 0:
            # Add directional bias based on imbalance
            price_change += 0.0005 * self.imbalance_state
        
        self.current_price *= (1 + price_change)
        
        # Create order book with imbalance
        bids = []
        asks = []
        
        # Number of levels
        num_levels = 20
        
        # Reference price points
        midPrice = self.current_price
        
        # Create bid levels
        for i in range(num_levels):
            price = midPrice * (1 - 0.0001 * (i + 1))
            
            # Base quantity
            quantity = np.random.lognormal(3, 0.5)
            
            # Add imbalance if in imbalance state
            if self.imbalance_state == 1:  # Buy imbalance
                imbalance_factor = 2.0
                if i < 5:  # Stronger imbalance at top of book
                    imbalance_factor = 3.0
                quantity *= imbalance_factor
            
            bids.append({
                'price': f"{price:.2f}",
                'quantity': f"{quantity:.8f}"
            })
        
        # Create ask levels
        for i in range(num_levels):
            price = midPrice * (1 + 0.0001 * (i + 1))
            
            # Base quantity
            quantity = np.random.lognormal(3, 0.5)
            
            # Add imbalance if in imbalance state
            if self.imbalance_state == -1:  # Sell imbalance
                imbalance_factor = 2.0
                if i < 5:  # Stronger imbalance at top of book
                    imbalance_factor = 3.0
                quantity *= imbalance_factor
            
            asks.append({
                'price': f"{price:.2f}",
                'quantity': f"{quantity:.8f}"
            })
        
        # Create order book
        timestamp = int(time.time() * 1e9)  # nanoseconds
        order_book = {
            'exchange_id': 'SIMULATOR',
            'trading_pair': 'BTC/USDT',
            'exchangeTimestamp': timestamp,
            'received_timestamp': timestamp,
            'sequenceNumber': self.tick,
            'isSnapshot': True,
            'bids': bids,
            'asks': asks
        }
        
        return order_book


def simulate_order_book(callback, interval=1.0):
    """Run order book simulator"""
    simulator = ImbalanceSimulator()
    
    while True:
        try:
            order_book = simulator.get_next_order_book()
            callback(order_book)
            time.sleep(interval)
        except KeyboardInterrupt:
            break
        except Exception as e:
            logger.error(f"Simulation error: {e}")
            time.sleep(interval)


def parse_arguments():
    """Parse command line arguments"""
    parser = argparse.ArgumentParser(description='Order Book Analyzer')
    
    parser.add_argument('--exchange', type=str, default=None,
                       help='Exchange to analyze (default: all)')
    parser.add_argument('--pair', type=str, default="BTC/USDT",
                       help='Trading pair to analyze (default: BTC/USDT)')
    parser.add_argument('--mode', type=str, default='depth',
                       choices=['depth', 'heatmap', 'imbalance', 'metrics'],
                       help='Visualization mode (default: depth)')
    parser.add_argument('--zmq', type=str, default="tcp://localhost:5555",
                       help='ZeroMQ address (default: tcp://localhost:5555)')
    parser.add_argument('--simulate', action='store_true',
                       help='Use simulator instead of connecting to ZeroMQ')
    parser.add_argument('--record', action='store_true',
                       help='Record order book data to file')
    parser.add_argument('--threshold', type=float, default=0.3,
                       help='Imbalance alert threshold (default: 0.3)')
    
    return parser.parse_args()


def main():
    """Main function"""
    # Parse arguments
    args = parse_arguments()
    
    # Create analyzer
    analyzer = OrderBookAnalyzer(
        zmq_address=args.zmq,
        exchange=args.exchange,
        pair=args.pair,
        mode=args.mode
    )
    
    # Set imbalance threshold
    analyzer.imbalance_threshold = args.threshold
    
    # Start recording if enabled
    if args.record:
        analyzer.start_recording()
    
    try:
        # Use simulator or ZeroMQ
        if args.simulate:
            logger.info("Using order book simulator")
            # Start a thread for simulation
            sim_thread = threading.Thread(
                target=simulate_order_book,
                args=(analyzer.on_order_book_update,),
                daemon=True
            )
            sim_thread.start()
            
            # Start visualization
            analyzer.visualizer.start_animation()
        else:
            # Start analyzer with ZeroMQ connection
            analyzer.start()
            
    except KeyboardInterrupt:
        logger.info("Interrupted by user")
    except Exception as e:
        logger.error(f"Error in main: {e}")
    finally:
        analyzer.stop()


if __name__ == "__main__":
    main()