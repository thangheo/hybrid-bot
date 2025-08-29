#!/usr/bin/env python3
"""
Basic ZMQ Subscriber - Works with any ZMQ version
"""
import zmq
import time
import sys
from proto.trading_messages_pb2 import *

def get_zmq_info():
    """Get ZMQ version info"""
    try:
        zmq_version = zmq.zmq_version()
        pyzmq_version = zmq.pyzmq_version()
        return zmq_version, pyzmq_version
    except:
        return "unknown", "unknown"
    
class BasicSubscriber:
    def __init__(self, address="tcp://localhost:5556"):
        zmq_ver, pyzmq_ver = get_zmq_info()
        print(f"ZMQ version: {zmq_ver}, PyZMQ version: {pyzmq_ver}")
        
        self.context = zmq.Context()
        self.socket = self.context.socket(zmq.SUB)
        
        # Only use basic options that work everywhere
        try:
            self.socket.setsockopt(zmq.SUBSCRIBE, b"")  # Subscribe to all
            self.socket.setsockopt(zmq.RCVTIMEO, 1000)  # 1 second timeout
            print("✓ Basic socket options set")
        except Exception as e:
            print(f"Warning: Could not set some socket options: {e}")
        
        self.socket.connect(address)
        print(f"Connected to {address}")
    
    def subscribe_to_sensor(self, sensor_id):
        """Subscribe to specific sensor"""
        topic = f"sensor.{sensor_id}".encode()
        try:
            self.socket.setsockopt(zmq.SUBSCRIBE, topic)
            print(f"✓ Subscribed to sensor.{sensor_id}")
        except Exception as e:
            print(f"Error subscribing to sensor.{sensor_id}: {e}")
    
    def subscribe_to_bench(self):
        """Subscribe to benchmark messages"""
        try:
            self.socket.setsockopt(zmq.SUBSCRIBE, b"bench")
            print("✓ Subscribed to benchmark messages")
        except Exception as e:
            print(f"Error subscribing to bench: {e}")
    
    def listen_for_messages(self, max_messages=20, mode="sensor"):
        """Listen for messages and measure latency"""
        print(f"Listening for up to {max_messages} {mode} messages...")
        
        latencies_ms = []
        messages_received = 0
        start_time = time.time()
        
        while messages_received < max_messages:
            try:
                # Try to receive message
                message_bytes = self.socket.recv(zmq.NOBLOCK)
                receive_time_us = int(time.time() * 1_000_000)
                
                # Decode message
                try:
                    message = message_bytes.decode('utf-8')
                except UnicodeDecodeError:
                    print("Warning: Could not decode message")
                    continue
                
                # Parse message based on mode
                if mode == "sensor":
                    latency_ms = self.parse_sensor_message(message, receive_time_us)
                elif mode == "bench":
                    latency_ms = self.parse_bench_message(message, receive_time_us)
                else:
                    continue
                
                if latency_ms is not None:
                    latencies_ms.append(latency_ms)
                    messages_received += 1
                
            except zmq.Again:
                # No message available
                time.sleep(0.001)  # 1ms sleep
                
                # Timeout check
                if time.time() - start_time > 60:  # 60 second timeout
                    print("Timeout waiting for messages")
                    break
                    
            except KeyboardInterrupt:
                print("\nInterrupted by user")
                break
            except Exception as e:
                print(f"Error receiving message: {e}")
                continue
        
        # Print results
        if latencies_ms:
            self.print_statistics(latencies_ms, mode)
        else:
            print("No valid messages received!")
        
        return latencies_ms
    
    def parse_sensor_message(self, message, receive_time_us):
        """Parse sensor message and calculate latency"""
        try:
            # Parse: "sensor.X timestamp_us,sensor_id,temp,humidity,pressure"
            parts = message.split(' ', 1)
            if len(parts) != 2:
                return None
                
            topic, csv_data = parts
            fields = csv_data.split(',')
            
            if len(fields) >= 5:
                sent_timestamp_us = int(fields[0])
                sensor_id = int(fields[1])
                temperature = float(fields[2])
                humidity = float(fields[3])
                pressure = float(fields[4])
                
                latency_us = receive_time_us - sent_timestamp_us
                latency_ms = latency_us / 1000.0
                
                print(f"Sensor {sensor_id}: {temperature:.2f}°C, "
                      f"{humidity:.2f}% RH, {pressure:.1f} hPa "
                      f"→ latency {latency_ms:.3f} ms")
                
                return latency_ms
                
        except (ValueError, IndexError) as e:
            print(f"Error parsing sensor message: {e}")
        
        return None
    
    def parse_bench_message(self, message, receive_time_us):
        """Parse benchmark message and calculate latency"""
        try:
            # Parse: "bench timestamp_us"
            parts = message.split(' ')
            if len(parts) == 2 and parts[0] == "bench":
                sent_time_us = int(parts[1])
                latency_us = receive_time_us - sent_time_us
                latency_ms = latency_us / 1000.0
                
                if len([l for l in [latency_ms] if l is not None]) % 50 == 1:
                    print(f"Benchmark message latency: {latency_ms:.3f} ms")
                
                return latency_ms
                
        except (ValueError, IndexError) as e:
            print(f"Error parsing benchmark message: {e}")
        
        return None
    
    def print_statistics(self, latencies_ms, mode):
        """Print latency statistics"""
        if not latencies_ms:
            return
        
        avg_latency = sum(latencies_ms) / len(latencies_ms)
        min_latency = min(latencies_ms)
        max_latency = max(latencies_ms)
        
        print(f"\n=== {mode.upper()} LATENCY STATISTICS ===")
        print(f"Messages:     {len(latencies_ms)}")
        print(f"Average:      {avg_latency:.3f} ms")
        print(f"Min:          {min_latency:.3f} ms")
        print(f"Max:          {max_latency:.3f} ms")
        
        # Simple percentiles
        if len(latencies_ms) >= 10:
            sorted_latencies = sorted(latencies_ms)
            p90_idx = int(0.90 * len(sorted_latencies))
            p95_idx = int(0.95 * len(sorted_latencies))
            
            print(f"P90:          {sorted_latencies[p90_idx]:.3f} ms")
            print(f"P95:          {sorted_latencies[p95_idx]:.3f} ms")
        
        # Performance assessment
        if avg_latency < 1.0:
            print("✅ Excellent latency!")
        elif avg_latency < 5.0:
            print("✅ Good latency")
        elif avg_latency < 15.0:
            print("⚠ Moderate latency")
        else:
            print("❌ High latency - needs optimization")
    
    def close(self):
        """Clean up resources"""
        try:
            self.socket.close()
            self.context.term()
        except:
            pass

def main():
    print("Basic ZMQ Subscriber Test")
    
    # Check command line arguments
    if len(sys.argv) > 1 and sys.argv[1] == "bench":
        # Benchmark mode
        print("Running in BENCHMARK mode")
        subscriber = BasicSubscriber()
        subscriber.subscribe_to_bench()
        subscriber.listen_for_messages(100, "bench")
    else:
        # Sensor mode
        print("Running in SENSOR mode")
        subscriber = BasicSubscriber()
        subscriber.subscribe_to_sensor(0)  # Subscribe to sensor.0 only
        subscriber.listen_for_messages(15, "sensor")
    
    subscriber.close()

if __name__ == "__main__":
    main()