#!/usr/bin/env python3
"""
Simplified Order Book Analyzer
------------------------------
A streamlined version for visualizing order book data with better debug output
for ZeroMQ connections.

Usage:
    python simplified_order_book_analyzer.py [--simulate] [--zmq ZMQ_ADDRESS]
"""

import zmq
import time
import argparse
import threading
import logging
import json
from datetime import datetime
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from collections import deque

# Configure logging
logging.basicConfig(
    level=logging.DEBUG,  # Changed to DEBUG for more verbose output
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger("OrderBookAnalyzer")

class OrderBookData:
    """Class to store and manage order book data"""
    def __init__(self, max_history_length=100):
        self.current_book = None
        self.history = deque(maxlen=max_history_length)
        self.timestamps = deque(maxlen=max_history_length)
    
    def update(self, order_book):
        """Update the order book with new data"""
        self.current_book = order_book
        self.history.append(order_book)
        
        # Extract timestamp or use current time
        timestamp = order_book.get('received_timestamp')
        if timestamp:
            try:
                # Convert to seconds if in nanoseconds
                if timestamp > 1e12:
                    timestamp /= 1e9
                self.timestamps.append(datetime.fromtimestamp(timestamp))
            except:
                self.timestamps.append(datetime.now())
        else:
            self.timestamps.append(datetime.now())


class OrderBookClient:
    """Client for connecting to ZeroMQ and receiving order book data"""
    def __init__(self, zmq_address="tcp://localhost:5555", topic="market.orderbook.normalized"):
        """Initialize ZeroMQ subscriber"""
        self.zmq_address = zmq_address
        self.topic = topic
        self.context = zmq.Context()
        self.socket = self.context.socket(zmq.SUB)
        self.running = False
        self.callback = None
        self.last_received = 0
        self.message_count = 0
        
        logger.debug(f"Initializing ZeroMQ client to {zmq_address}, subscribing to '{topic}'")
    
    def connect(self):
        """Connect to ZeroMQ server"""
        try:
            self.socket.connect(self.zmq_address)
            self.socket.setsockopt_string(zmq.SUBSCRIBE, self.topic)
            logger.debug(f"Connected to ZeroMQ server at {self.zmq_address}")
            return True
        except Exception as e:
            logger.error(f"Failed to connect to ZeroMQ server: {e}")
            return False
    
    def set_callback(self, callback):
        """Set callback function to receive order book updates"""
        self.callback = callback
    
    def start(self):
        """Start listening for order book updates in a separate thread"""
        if self.connect():
            self.running = True
            self.thread = threading.Thread(target=self._listen_loop)
            self.thread.daemon = True
            self.thread.start()
            logger.info(f"Started ZeroMQ listener thread")
            return True
        return False
    
    def stop(self):
        """Stop listening for updates"""
        self.running = False
        if hasattr(self, 'thread'):
            self.thread.join(timeout=1.0)
        self.socket.close()
        self.context.term()
        logger.info("Stopped ZeroMQ client")
    
    def _listen_loop(self):
        """Main listener loop"""
        while self.running:
            try:
                if self.socket.poll(timeout=100):
                    topic = self.socket.recv_string()
                    message = self.socket.recv()
                    self.last_received = time.time()
                    self.message_count += 1
                    
                    if self.message_count % 10 == 0:
                        logger.debug(f"Received {self.message_count} messages so far. Last topic: {topic}")
                    
                    try:
                        # Try to parse as JSON first
                        order_book = json.loads(message)
                        if self.callback:
                            self.callback(order_book)
                    except json.JSONDecodeError:
                        # Try other formats or log the error
                        logger.warning(f"Received non-JSON message: {message[:100]}...")
                        try:
                            # Try pickle format as fallback
                            import pickle
                            order_book = pickle.loads(message)
                            if self.callback:
                                self.callback(order_book)
                        except:
                            logger.error("Failed to parse message as JSON or pickle")
            except Exception as e:
                logger.error(f"Error in listen loop: {e}")
                time.sleep(0.1)
            
            # Check if we've received any messages lately
            if self.message_count > 0 and time.time() - self.last_received > 5.0:
                logger.warning(f"No messages received in the last 5 seconds. Last message count: {self.message_count}")


class OrderBookVisualizer:
    """Visualizes order book data using matplotlib"""
    def __init__(self, order_book_data):
        self.order_book_data = order_book_data
        self.fig = None
        self.ax = None
        self.bid_line = None
        self.ask_line = None
        self.animation = None
    
    def setup_plot(self):
        """Setup plot for depth chart"""
        plt.style.use('dark_background')
        self.fig, self.ax = plt.subplots(figsize=(10, 6))
        
        # Initialize empty lines for bids and asks
        self.bid_line, = self.ax.plot([], [], 'g-', linewidth=2, label='Bids')
        self.ask_line, = self.ax.plot([], [], 'r-', linewidth=2, label='Asks')
        
        # Setup labels and grid
        self.ax.set_xlabel('Price')
        self.ax.set_ylabel('Cumulative Volume')
        self.ax.set_title('Order Book Depth')
        self.ax.grid(True, alpha=0.3)
        self.ax.legend()
        
        return self.bid_line, self.ask_line
    
    def update_plot(self, frame):
        """Update the depth chart"""
        order_book = self.order_book_data.current_book
        
        # Return empty lines if no data yet
        if not order_book or 'bids' not in order_book or 'asks' not in order_book:
            return self.bid_line, self.ask_line
        
        try:
            # Extract bid and ask data
            bids = []
            asks = []
            
            for bid in order_book['bids']:
                # Handle different possible formats
                if isinstance(bid, dict) and 'price' in bid and 'quantity' in bid:
                    price = float(bid['price'])
                    quantity = float(bid['quantity'])
                    bids.append((price, quantity))
                elif isinstance(bid, list) and len(bid) >= 2:
                    # Some exchanges use array format [price, quantity]
                    price = float(bid[0])
                    quantity = float(bid[1])
                    bids.append((price, quantity))
            
            for ask in order_book['asks']:
                if isinstance(ask, dict) and 'price' in ask and 'quantity' in ask:
                    price = float(ask['price'])
                    quantity = float(ask['quantity'])
                    asks.append((price, quantity))
                elif isinstance(ask, list) and len(ask) >= 2:
                    price = float(ask[0])
                    quantity = float(ask[1])
                    asks.append((price, quantity))
            
            # Sort bids (descending) and asks (ascending)
            bids = sorted(bids, key=lambda x: x[0], reverse=True)
            asks = sorted(asks, key=lambda x: x[0])
            
            if not bids or not asks:
                logger.warning("No valid bid/ask data found in order book")
                return self.bid_line, self.ask_line
            
            # Calculate cumulative volumes
            bid_prices = [p for p, _ in bids]
            bid_volumes = [q for _, q in bids]
            bid_cum_volumes = np.cumsum(bid_volumes)
            
            ask_prices = [p for p, _ in asks]
            ask_volumes = [q for _, q in asks]
            ask_cum_volumes = np.cumsum(ask_volumes)
            
            # Update plot data
            self.bid_line.set_data(bid_prices, bid_cum_volumes)
            self.ask_line.set_data(ask_prices, ask_cum_volumes)
            
            # Update plot limits
            min_price = min(bid_prices[-1] if bid_prices else float('inf'),
                          ask_prices[0] if ask_prices else float('inf'))
            max_price = max(bid_prices[0] if bid_prices else float('-inf'),
                          ask_prices[-1] if ask_prices else float('-inf'))
            
            price_range = max_price - min_price
            if price_range > 0:
                self.ax.set_xlim(min_price - price_range * 0.1, max_price + price_range * 0.1)
            
            max_volume = max(max(bid_cum_volumes) if bid_cum_volumes else 0,
                          max(ask_cum_volumes) if ask_cum_volumes else 0)
            if max_volume > 0:
                self.ax.set_ylim(0, max_volume * 1.1)
            
            # Update title with exchange/pair info if available
            if 'exchange_id' in order_book and 'trading_pair' in order_book:
                self.ax.set_title(f"Order Book: {order_book['exchange_id']} - {order_book['trading_pair']}")
            
        except Exception as e:
            logger.error(f"Error updating plot: {e}")
        
        # Always return the artists for animation
        return self.bid_line, self.ask_line
    
    def start_animation(self, interval=500):
        """Start the animation"""
        # Setup the plot
        self.setup_plot()
        
        # Create animation
        self.animation = FuncAnimation(
            self.fig,
            self.update_plot,
            frames=None,
            blit=True,
            interval=interval,
            save_count=100  # Explicitly set save_count
        )
        
        # Show the plot
        plt.tight_layout()
        plt.show()


class ImbalanceSimulator:
    """Simulates order book data with imbalances for testing"""
    def __init__(self, base_price=50000.0):
        self.base_price = base_price
        self.current_price = base_price
        self.volatility = 0.001
        self.tick = 0
        self.imbalance_state = 0  # -1: sell imbalance, 0: balanced, 1: buy imbalance
        self.imbalance_duration = 0
    
    def get_next_order_book(self):
        """Generate next simulated order book"""
        self.tick += 1
        
        # Randomly change imbalance state
        if self.imbalance_duration <= 0:
            if np.random.random() < 0.05:
                self.imbalance_state = np.random.choice([-1, 1])
                self.imbalance_duration = np.random.randint(5, 20)
        else:
            self.imbalance_duration -= 1
            if self.imbalance_duration <= 0:
                self.imbalance_state = 0
        
        # Update price based on imbalance and random walk
        price_change = np.random.normal(0, self.volatility)
        if self.imbalance_state != 0:
            price_change += 0.0005 * self.imbalance_state
        
        self.current_price *= (1 + price_change)
        
        # Create order book with imbalance
        bids = []
        asks = []
        
        # Create bid levels
        for i in range(20):
            price = self.current_price * (1 - 0.0001 * (i + 1))
            quantity = np.random.lognormal(3, 0.5)
            
            # Add imbalance if in imbalance state
            if self.imbalance_state == 1:
                imbalance_factor = 2.0
                if i < 5:
                    imbalance_factor = 3.0
                quantity *= imbalance_factor
            
            bids.append({
                'price': f"{price:.2f}",
                'quantity': f"{quantity:.8f}"
            })
        
        # Create ask levels
        for i in range(20):
            price = self.current_price * (1 + 0.0001 * (i + 1))
            quantity = np.random.lognormal(3, 0.5)
            
            # Add imbalance if in imbalance state
            if self.imbalance_state == -1:
                imbalance_factor = 2.0
                if i < 5:
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
            'exchange_timestamp': timestamp,
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


def test_zmq_connection(zmq_address, topic="market.orderbook.normalized", timeout=10):
    """Test ZeroMQ connection and print received message format"""
    context = zmq.Context()
    socket = context.socket(zmq.SUB)
    socket.connect(zmq_address)
    socket.setsockopt_string(zmq.SUBSCRIBE, topic)
    
    logger.info(f"Testing connection to {zmq_address}, topic: {topic}")
    logger.info(f"Waiting for messages for {timeout} seconds...")
    
    start_time = time.time()
    message_received = False
    
    while time.time() - start_time < timeout:
        if socket.poll(timeout=100):
            try:
                topic = socket.recv_string()
                message = socket.recv()
                message_received = True
                
                logger.info(f"Message received on topic: {topic}")
                logger.info(f"Message type: {type(message)}")
                logger.info(f"Message length: {len(message)} bytes")
                
                # Try to parse as JSON
                try:
                    data = json.loads(message)
                    logger.info("Message is valid JSON")
                    logger.info(f"Message structure: {list(data.keys()) if isinstance(data, dict) else 'Not a dictionary'}")
                    
                    # Check for bids/asks keys
                    if isinstance(data, dict):
                        if 'bids' in data and 'asks' in data:
                            logger.info("Message contains bids and asks - looks like order book data!")
                            logger.info(f"Bid format: {data['bids'][0] if data['bids'] else 'Empty'}")
                            logger.info(f"Ask format: {data['asks'][0] if data['asks'] else 'Empty'}")
                        else:
                            logger.warning("Message doesn't contain 'bids' and 'asks' keys")
                except json.JSONDecodeError:
                    logger.warning("Message is not valid JSON")
                    
                    # Try other formats
                    try:
                        import pickle
                        data = pickle.loads(message)
                        logger.info("Message is valid pickle data")
                        logger.info(f"Message structure: {list(data.keys()) if isinstance(data, dict) else 'Not a dictionary'}")
                    except:
                        logger.warning("Message is not pickle data either")
                        
                        # Print hex representation of first bytes
                        logger.info(f"First 50 bytes: {message[:50].hex()}")
                
                break
            except Exception as e:
                logger.error(f"Error receiving message: {e}")
        
        time.sleep(0.1)
    
    if not message_received:
        logger.warning(f"No messages received in {timeout} seconds. Is the ZeroMQ publisher running?")
    
    socket.close()
    context.term()
    
    return message_received


def main():
    # Parse arguments
    parser = argparse.ArgumentParser(description='Simplified Order Book Analyzer')
    parser.add_argument('--simulate', action='store_true', help='Use simulator instead of ZeroMQ')
    parser.add_argument('--zmq', type=str, default="tcp://localhost:5555", help='ZeroMQ address')
    parser.add_argument('--topic', type=str, default="market.orderbook.normalized", help='ZeroMQ topic')
    parser.add_argument('--test', action='store_true', help='Only test the ZeroMQ connection')
    
    args = parser.parse_args()
    
    # Test ZeroMQ connection if requested
    if args.test:
        test_zmq_connection(args.zmq, args.topic)
        return
    
    # Create data storage
    order_book_data = OrderBookData()
    
    # Create visualizer
    visualizer = OrderBookVisualizer(order_book_data)
    
    try:
        if args.simulate:
            # Use simulator
            logger.info("Using order book simulator")
            
            # Start simulator in a separate thread
            sim_thread = threading.Thread(
                target=simulate_order_book,
                args=(lambda ob: order_book_data.update(ob),),
                daemon=True
            )
            sim_thread.start()
            
            # Start visualization
            visualizer.start_animation()
        else:
            # Connect to ZeroMQ
            logger.info(f"Connecting to ZeroMQ at {args.zmq}")
            
            # First test the connection
            if not test_zmq_connection(args.zmq, args.topic, timeout=5):
                logger.error("ZeroMQ connection test failed. Please check your ZeroMQ publisher.")
                return
            
            # Create client
            client = OrderBookClient(args.zmq, args.topic)
            
            # Set callback
            client.set_callback(lambda ob: order_book_data.update(ob))
            
            # Start client
            if client.start():
                # Start visualization
                visualizer.start_animation()
                # Stop client when done
                client.stop()
            else:
                logger.error("Failed to start ZeroMQ client")
    
    except KeyboardInterrupt:
        logger.info("Interrupted by user")
    except Exception as e:
        logger.error(f"Error: {e}")


if __name__ == "__main__":
    main()