# order_book_client.py
import zmq
import json
import time
from dataclasses import dataclass
from typing import List, Dict, Optional

@dataclass
class PriceLevel:
    price: float
    quantity: float

@dataclass
class OrderBookSnapshot:
    exchange_id: str
    symbol: str
    timestamp: int
    bids: List[PriceLevel]
    asks: List[PriceLevel]
    spread: float
    mid_price: float

class OrderBookClient:
    """Client to subscribe to normalized order book data"""
    
    def __init__(self, endpoint="tcp://localhost:5555"):
        self.context = zmq.Context()
        self.socket = self.context.socket(zmq.SUB)
        self.socket.connect(endpoint)
        
        # Default subscription to all updates
        self.socket.setsockopt_string(zmq.SUBSCRIBE, "orderbook.")
        
    def subscribe_to_exchange(self, exchange_id):
        """Subscribe to a specific exchange"""
        self.socket.setsockopt_string(zmq.SUBSCRIBE, f"orderbook.update.{exchange_id}.")
        self.socket.setsockopt_string(zmq.SUBSCRIBE, f"orderbook.snapshot.{exchange_id}.")
        
    def subscribe_to_symbol(self, exchange_id, symbol):
        """Subscribe to a specific symbol on an exchange"""
        self.socket.setsockopt_string(zmq.SUBSCRIBE, f"orderbook.update.{exchange_id}.{symbol}")
        self.socket.setsockopt_string(zmq.SUBSCRIBE, f"orderbook.snapshot.{exchange_id}.{symbol}")
    
    def receive_orderbook_data(self, timeout_ms=1000):
        """Receive order book updates with timeout"""
        try:
            self.socket.RCVTIMEO = timeout_ms
            topic = self.socket.recv_string()
            data = self.socket.recv_string()
            
            # Parse data
            parsed_data = json.loads(data)
            
            # Determine message type
            if "orderbook.snapshot" in topic:
                return self._parse_snapshot(parsed_data)
            else:
                return self._parse_update(parsed_data)
                
        except zmq.Again:
            return None
            
    def _parse_snapshot(self, data):
        """Parse order book snapshot data"""
        bids = [PriceLevel(level["price"], level["quantity"]) for level in data["bids"]]
        asks = [PriceLevel(level["price"], level["quantity"]) for level in data["asks"]]
        
        return OrderBookSnapshot(
            exchange_id=data["exchange_id"],
            symbol=data["symbol"],
            timestamp=data["timestamp"],
            bids=bids,
            asks=asks,
            spread=data["spread"],
            mid_price=data["mid_price"]
        )
    
    def _parse_update(self, data):
        """Parse order book update data"""
        # Similar to snapshot parsing
        # Might return a different class for updates
        pass

# Usage example
if __name__ == "__main__":
    client = OrderBookClient()
    client.subscribe_to_symbol("binance", "BTCUSDT")
    
    while True:
        data = client.receive_orderbook_data()
        if data:
            print(f"Received {type(data).__name__} for {data.exchange_id}:{data.symbol}")
            print(f"  Best bid: {data.bids[0].price if data.bids else 'None'}")
            print(f"  Best ask: {data.asks[0].price if data.asks else 'None'}")
            print(f"  Spread: {data.spread}")
        time.sleep(0.1)