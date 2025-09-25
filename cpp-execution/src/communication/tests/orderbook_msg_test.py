#!/usr/bin/env python3

import time
import internal_orderbook_pb2 as orderbook_pb2

def main():
    print("Starting protobuf message generator...")
    
    counter = 0
    
    while True:
        counter += 1
        
        # Create protobuf message
        msg = orderbook_pb2.OrderbookMessage()
        
        # Fill the data
        msg.data.exchange = "BINANCE"
        msg.data.symbol = "BTC/USDT"
        msg.data.bid = 45000.50
        msg.data.ask = 45001.50
        msg.data.price = 45001.00
        msg.data.volume = 1234.56
        msg.data.high24h = 45500.00
        msg.data.low24h = 44500.00
        msg.data.sequence = counter
        msg.data.timestamp = int(time.time() * 1000)
        
        # Add some bid levels
        for i in range(3):
            bid = msg.data.bids.add()
            bid.price = 45000.50 - (i * 0.10)
            bid.quantity = 1.0 + i
        
        # Add some ask levels
        for i in range(3):
            ask = msg.data.asks.add()
            ask.price = 45001.50 + (i * 0.10)
            ask.quantity = 1.0 + i
        
        # Add metadata
        msg.metadata.correlation_id = f"msg_{counter}"
        msg.metadata.priority = "HIGH"
        msg.metadata.timestamp = str(int(time.time() * 1000))
        
        # Print the message
        print(f"=== MESSAGE #{counter} ===")
        print(f"Exchange: {msg.data.exchange}")
        print(f"Symbol: {msg.data.symbol}")
        print(f"Price: {msg.data.price}")
        print(f"Bid: {msg.data.bid}")
        print(f"Ask: {msg.data.ask}")
        print(f"Volume: {msg.data.volume}")
        print(f"High24h: {msg.data.high24h}")
        print(f"Low24h: {msg.data.low24h}")
        print(f"Sequence: {msg.data.sequence}")
        print(f"Timestamp: {msg.data.timestamp}")
        
        print("Bids:")
        for i, bid in enumerate(msg.data.bids):
            print(f"  [{i}] {bid.price} @ {bid.quantity}")
        
        print("Asks:")
        for i, ask in enumerate(msg.data.asks):
            print(f"  [{i}] {ask.price} @ {ask.quantity}")
        
        print(f"Metadata: {msg.metadata.correlation_id}, {msg.metadata.priority}")
        print("-" * 40)
        
        # Wait 1 second
        time.sleep(1)

if __name__ == '__main__':
    main()