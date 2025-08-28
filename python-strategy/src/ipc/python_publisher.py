#!/usr/bin/env python3
"""
Python ZMQ Publisher for C++ Subscriber
"""
import zmq
import time
import random
import json
import threading
from dataclasses import dataclass
from typing import List
import struct

@dataclass
class SensorReading:
    sensor_id: int
    timestamp_us: int
    temperature: float
    humidity: float
    pressure: float
    location: str

class PythonPublisher:
    def __init__(self, port=5556):
        self.context = zmq.Context()
        self.socket = self.context.socket(zmq.PUB)
        
        # Basic optimizations
        try:
            self.socket.setsockopt(zmq.LINGER, 0)
            print("✓ Linger set to 0")
        except Exception as e:
            print(f"Warning: Could not set linger: {e}")
        
        self.socket.bind(f"tcp://*:{port}")
        print(f"Python publisher started on port {port}")
        
        # Initialize random data generators
        self.locations = ["Factory-A", "Factory-B", "Warehouse-1", "Office", "Lab"]
        self.running = False
        
        # Warm up
        time.sleep(0.2)
        
    def create_sensor_reading(self, sensor_id: int) -> SensorReading:
        """Create a sensor reading with current timestamp"""
        return SensorReading(
            sensor_id=sensor_id,
            timestamp_us=int(time.time() * 1_000_000),
            temperature=round(random.uniform(18.0, 35.0), 2),
            humidity=round(random.uniform(30.0, 80.0), 2),
            pressure=round(random.uniform(995.0, 1025.0), 1),
            location=random.choice(self.locations)
        )
    
    def publish_csv_format(self, num_messages=50):
        """Publish sensor data in CSV format (fastest)"""
        print(f"Publishing {num_messages} CSV messages...")
        
        for i in range(num_messages):
            sensor_id = i % 5  # 5 sensors rotating
            reading = self.create_sensor_reading(sensor_id)
            
            # Create CSV message: "sensor.X timestamp_us,sensor_id,temp,humidity,pressure,location"
            csv_data = (f"{reading.timestamp_us},{reading.sensor_id},"
                       f"{reading.temperature},{reading.humidity},"
                       f"{reading.pressure},{reading.location}")
            
            topic = f"sensor.{sensor_id}"
            message = f"{topic} {csv_data}"
            
            # Send message
            self.socket.send_string(message)
            
            if sensor_id == 0:  # Log sensor 0 only
                print(f"✓ Published {topic} at {reading.timestamp_us} µs: "
                      f"{reading.temperature}°C")
            
            time.sleep(0.5)  # 500ms between messages
        
        print(f"Finished publishing {num_messages} CSV messages")
    
    def publish_json_format(self, num_messages=30):
        """Publish sensor data in JSON format"""
        print(f"Publishing {num_messages} JSON messages...")
        
        for i in range(num_messages):
            sensor_id = i % 3  # 3 sensors for JSON test
            reading = self.create_sensor_reading(sensor_id)
            
            # Create JSON message
            data = {
                "sensor_id": reading.sensor_id,
                "timestamp_us": reading.timestamp_us,
                "temperature": reading.temperature,
                "humidity": reading.humidity,
                "pressure": reading.pressure,
                "location": reading.location,
                "message_type": "sensor_data",
                "version": "1.0"
            }
            
            topic = f"json_sensor.{sensor_id}"
            json_data = json.dumps(data, separators=(',', ':'))  # Compact JSON
            message = f"{topic} {json_data}"
            
            self.socket.send_string(message)
            
            if sensor_id == 0:
                print(f"✓ Published JSON {topic}: {reading.temperature}°C at {reading.location}")
            
            time.sleep(0.7)  # 700ms between JSON messages
        
        print(f"Finished publishing {num_messages} JSON messages")
    
    def publish_binary_format(self, num_messages=40):
        """Publish sensor data in binary format (most efficient)"""
        print(f"Publishing {num_messages} binary messages...")
        
        # Binary struct format: uint64_t timestamp, uint32_t sensor_id, 
        # float temp, float humidity, float pressure, char[16] location
        binary_format = "<QIffffffff16s"  # 64 bytes total
        
        for i in range(num_messages):
            sensor_id = i % 4  # 4 sensors for binary test
            reading = self.create_sensor_reading(sensor_id)
            
            # Pack data into binary format
            location_bytes = reading.location.encode('utf-8')[:15]  # Max 15 chars
            location_padded = location_bytes.ljust(16, b'\0')  # Pad to 16 bytes
            
            binary_data = struct.pack(
                binary_format,
                reading.timestamp_us,
                reading.sensor_id,
                reading.temperature,
                reading.humidity,
                reading.pressure,
                0, 0, 0, 0, 0,  # Reserved floats for future use
                location_padded
            )
            
            topic = f"binary.{sensor_id}".encode()
            # Send topic + space + binary data
            message = topic + b" " + binary_data
            
            self.socket.send(message)
            
            if sensor_id == 0:
                print(f"✓ Published binary sensor.{sensor_id}: "
                      f"{reading.temperature}°C at {reading.timestamp_us} µs")
            
            time.sleep(0.3)  # 300ms between binary messages
        
        print(f"Finished publishing {num_messages} binary messages")
    
    def publish_high_frequency(self, duration_seconds=10):
        """Publish high-frequency test messages for latency testing"""
        print(f"Publishing high-frequency messages for {duration_seconds} seconds...")
        
        message_count = 0
        start_time = time.time()
        
        while time.time() - start_time < duration_seconds:
            timestamp_us = int(time.time() * 1_000_000)
            
            # Simple message format for latency testing
            message = f"test {timestamp_us}"
            self.socket.send_string(message)
            
            message_count += 1
            
            if message_count % 100 == 0:
                print(f"Published {message_count} test messages")
            
            time.sleep(0.001)  # 1ms interval = 1000 Hz
        
        print(f"Published {message_count} high-frequency messages")
        return message_count
    
    def publish_market_data(self, num_updates=100):
        """Simulate financial market data publishing"""
        print(f"Publishing {num_updates} market data updates...")
        
        symbols = ["AAPL", "GOOGL", "MSFT", "TSLA", "NVDA"]
        prices = {symbol: random.uniform(100, 300) for symbol in symbols}
        
        for i in range(num_updates):
            symbol = random.choice(symbols)
            
            # Simulate price movement
            change = random.uniform(-0.02, 0.02)  # ±2% max change
            prices[symbol] *= (1 + change)
            
            timestamp_us = int(time.time() * 1_000_000)
            
            # Market data format: "symbol,timestamp_us,price,volume,bid,ask"
            volume = random.randint(1000, 50000)
            bid = round(prices[symbol] * 0.999, 2)
            ask = round(prices[symbol] * 1.001, 2)
            
            csv_data = f"{symbol},{timestamp_us},{prices[symbol]:.2f},{volume},{bid},{ask}"
            message = f"market {csv_data}"
            
            self.socket.send_string(message)
            
            if i % 20 == 0:
                print(f"✓ Market update: {symbol} = ${prices[symbol]:.2f}")
            
            time.sleep(0.05)  # 50ms = 20 Hz market data
        
        print(f"Finished publishing {num_updates} market data updates")
    
    def continuous_publishing(self):
        """Continuously publish mixed data types"""
        print("Starting continuous publishing (Ctrl+C to stop)...")
        self.running = True
        
        try:
            while self.running:
                # Mix of different message types
                self.publish_csv_format(5)
                time.sleep(1)
                
                self.publish_json_format(3)
                time.sleep(1)
                
                self.publish_binary_format(4)
                time.sleep(1)
                
        except KeyboardInterrupt:
            print("\nStopping continuous publishing...")
            self.running = False
    
    def close(self):
        """Clean up resources"""
        self.running = False
        time.sleep(0.1)
        self.socket.close()
        self.context.term()

def main():
    print("=== Python ZMQ Publisher ===")
    print("Choose publishing mode:")
    print("1. CSV format (fast)")
    print("2. JSON format (readable)")
    print("3. Binary format (most efficient)")
    print("4. High-frequency test")
    print("5. Market data simulation")
    print("6. Continuous mixed publishing")
    
    try:
        choice = input("Enter choice (1-6): ").strip()
        
        publisher = PythonPublisher()
        
        if choice == "1":
            publisher.publish_csv_format(30)
        elif choice == "2":
            publisher.publish_json_format(20)
        elif choice == "3":
            publisher.publish_binary_format(25)
        elif choice == "4":
            count = publisher.publish_high_frequency(15)
            print(f"Average frequency: {count/15:.0f} Hz")
        elif choice == "5":
            publisher.publish_market_data(150)
        elif choice == "6":
            publisher.continuous_publishing()
        else:
            print("Invalid choice, using CSV format")
            publisher.publish_csv_format(20)
        
    except KeyboardInterrupt:
        print("\nInterrupted by user")
    finally:
        if 'publisher' in locals():
            publisher.close()

if __name__ == "__main__":
    main()