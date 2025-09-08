# python/visualization/order_book_client.py
import zmq
import json
import time
import matplotlib.pyplot as plt
import pandas as pd
import numpy as np
from threading import Thread, Lock
from collections import defaultdict

class OrderBookSubscriber:
    def __init__(self, zmq_endpoint="tcp://localhost:5555"):
        """
        Initialize the OrderBookSubscriber.
        
        Args:
            zmq_endpoint: ZeroMQ endpoint to connect to
        """
        self.zmq_endpoint = zmq_endpoint
        self.context = zmq.Context()
        self.socket = self.context.socket(zmq.SUB)
        self.running = False
        self.thread = None
        self.order_books = {}
        self.lock = Lock()
        self.callbacks = []
        
    def start(self, topics=None):
        """
        Start subscribing to order book topics.
        
        Args:
            topics: List of topics to subscribe to, or None for all
        """
        if topics is None:
            # Subscribe to all order book topics
            self.socket.setsockopt_string(zmq.SUBSCRIBE, "market.orderbook.normalized")
        else:
            # Subscribe to specific topics
            for topic in topics:
                self.socket.setsockopt_string(zmq.SUBSCRIBE, topic)
        
        # Connect to ZeroMQ endpoint
        self.socket.connect(self.zmq_endpoint)
        
        # Start receiver thread
        self.running = True
        self.thread = Thread(target=self._receiver_thread)
        self.thread.daemon = True
        self.thread.start()
        
    def stop(self):
        """Stop the subscriber."""
        self.running = False
        if self.thread:
            self.thread.join(timeout=1.0)
        self.socket.close()
        
    def _receiver_thread(self):
        """Background thread to receive order book updates."""
        while self.running:
            try:
                # Receive topic and message
                topic = self.socket.recv_string()
                message = self.socket.recv_string()
                
                # Parse JSON message
                order_book = json.loads(message)
                
                # Extract exchange and trading pair from topic
                parts = topic.split('.')
                if len(parts) >= 4:
                    exchange_id = parts[3]
                    trading_pair = parts[4] if len(parts) > 4 else ""
                    
                    # Update order book state
                    with self.lock:
                        key = f"{exchange_id}:{trading_pair}"
                        self.order_books[key] = order_book
                    
                    # Notify callbacks
                    for callback in self.callbacks:
                        callback(exchange_id, trading_pair, order_book)
                    
            except Exception as e:
                print(f"Error in receiver thread: {e}")
                time.sleep(0.1)  # Avoid tight loop on error
                
    def get_order_book(self, exchange_id, trading_pair):
        """
        Get the current state of an order book.
        
        Args:
            exchange_id: Exchange identifier
            trading_pair: Trading pair
        
        Returns:
            The order book data or None if not available
        """
        with self.lock:
            key = f"{exchange_id}:{trading_pair}"
            return self.order_books.get(key)
    
    def register_callback(self, callback):
        """
        Register a callback function for order book updates.
        
        Args:
            callback: Function that takes (exchange_id, trading_pair, order_book)
        """
        self.callbacks.append(callback)
        
    def unregister_callback(self, callback):
        """Remove a callback function."""
        if callback in self.callbacks:
            self.callbacks.remove(callback)


class OrderBookVisualizer:
    def __init__(self, subscriber, exchange_id, trading_pair, depth=10):
        """
        Initialize the OrderBookVisualizer.
        
        Args:
            subscriber: OrderBookSubscriber instance
            exchange_id: Exchange to visualize
            trading_pair: Trading pair to visualize
            depth: Number of levels to show
        """
        self.subscriber = subscriber
        self.exchange_id = exchange_id
        self.trading_pair = trading_pair
        self.depth = depth
        self.fig, self.ax = plt.subplots(figsize=(12, 8))
        self.bid_bars = None
        self.ask_bars = None
        self.last_update_time = None
        
        # Register callback for updates
        self.subscriber.register_callback(self._on_order_book_update)
        
    def _on_order_book_update(self, exchange_id, trading_pair, order_book):
        """Handle order book updates."""
        if exchange_id == self.exchange_id and trading_pair == self.trading_pair:
            self.last_update_time = order_book.get('timestamp')
            self.update_visualization(order_book)
            
    def update_visualization(self, order_book):
        """Update the visualization with new order book data."""
        # Clear previous plot
        self.ax.clear()
        
        # Extract bids and asks
        bids = order_book.get('bids', [])
        asks = order_book.get('asks', [])
        
        # Limit to specified depth
        bids = bids[:self.depth]
        asks = asks[:self.depth]
        
        # Convert to numeric values
        bid_prices = [float(bid['price']) for bid in bids]
        bid_quantities = [float(bid['quantity']) for bid in bids]
        ask_prices = [float(ask['price']) for ask in asks]
        ask_quantities = [float(ask['quantity']) for ask in asks]
        
        # Create x positions for bars
        x_bids = np.arange(len(bid_prices))
        x_asks = np.arange(len(bid_prices), len(bid_prices) + len(ask_prices))
        
        # Plot bid and ask bars
        self.bid_bars = self.ax.bar(x_bids, bid_quantities, color='green', alpha=0.7)
        self.ask_bars = self.ax.bar(x_asks, ask_quantities, color='red', alpha=0.7)
        
        # Add price labels
        all_prices = bid_prices + ask_prices
        x_ticks = list(x_bids) + list(x_asks)
        self.ax.set_xticks(x_ticks)
        self.ax.set_xticklabels([f"{p:.2f}" for p in all_prices], rotation=45)
        
        # Add labels and title
        self.ax.set_xlabel('Price')
        self.ax.set_ylabel('Quantity')
        self.ax.set_title(f"Order Book: {self.exchange_id} - {self.trading_pair}")
        
        # Add mid price line
        if bid_prices and ask_prices:
            midPrice = (bid_prices[0] + ask_prices[0]) / 2
            self.ax.axvline(len(bid_prices) - 0.5, color='black', linestyle='--')
            self.ax.text(len(bid_prices) - 0.5, max(bid_quantities + ask_quantities) * 0.9, 
                        f"Mid: {midPrice:.2f}", rotation=90)
        
        # Add timestamp
        if self.last_update_time:
            time_str = time.strftime('%Y-%m-%d %H:%M:%S', 
                                   time.localtime(self.last_update_time / 1000))
            self.ax.text(0.02, 0.98, f"Last Update: {time_str}", 
                        transform=self.ax.transAxes, va='top')
        
        # Redraw
        self.fig.canvas.draw()
        

def main():
    """Main function to demonstrate order book visualization."""
    # Create subscriber
    subscriber = OrderBookSubscriber()
    
    # Start subscribing to all order book topics
    subscriber.start()
    
    # Wait for some data to arrive
    print("Waiting for order book data...")
    time.sleep(2)
    
    # Find available order books
    available_order_books = []
    for key in subscriber.order_books.keys():
        exchange_id, trading_pair = key.split(':')
        available_order_books.append((exchange_id, trading_pair))
    
    if not available_order_books:
        print("No order book data received yet. Exiting.")
        subscriber.stop()
        return
    
    print(f"Available order books: {available_order_books}")
    
    # Select first available order book
    exchange_id, trading_pair = available_order_books[0]
    
    # Create visualizer
    visualizer = OrderBookVisualizer(subscriber, exchange_id, trading_pair)
    
    # Show plot and start interactive mode
    plt.ion()
    plt.show()
    
    try:
        while True:
            # Update plot and process events
            plt.pause(0.1)
    except KeyboardInterrupt:
        print("Exiting...")
    finally:
        subscriber.stop()
        

if __name__ == "__main__":
    main()
