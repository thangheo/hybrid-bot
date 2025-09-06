#!/usr/bin/env python3
"""
Quick Message Checker - In 10 message đầu tiên rồi thoát
"""

import zmq
import json
import sys
from datetime import datetime

def main():
    context = zmq.Context()
    socket = context.socket(zmq.SUB)
    
    endpoint = "tcp://localhost:5555"
    print(f"🔌 Connecting to {endpoint}")
    
    try:
        socket.connect(endpoint)
        socket.setsockopt(zmq.SUBSCRIBE, b"")
        socket.setsockopt(zmq.RCVTIMEO, 30000)  # 30 second timeout
        
        print("✅ Connected! Waiting for messages...")
        print("=" * 60)
        
        for msg_num in range(1, 11):
            try:
                # Try multi-part first
                try:
                    parts = socket.recv_multipart(zmq.NOBLOCK)
                    
                    timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                    print(f"\n📨 MESSAGE #{msg_num} [{timestamp}]")
                    print(f"📦 {len(parts)} parts")
                    
                    for i, part in enumerate(parts):
                        part_str = part.decode('utf-8', errors='ignore')
                        print(f"  Part {i+1}: {part_str}")
                    
                    print("-" * 60)
                    
                except zmq.Again:
                    # Try single message
                    message = socket.recv_string()
                    
                    timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                    print(f"\n📨 MESSAGE #{msg_num} [{timestamp}]")
                    print(f"📦 Single: {message}")
                    print("-" * 60)
                    
            except zmq.Again:
                print(f"⏰ Timeout on message #{msg_num}")
                break
            except Exception as e:
                print(f"❌ Error on message #{msg_num}: {e}")
                break
        
        print(f"\n🎉 Done!")
        
    except Exception as e:
        print(f"❌ Failed: {e}")
        return 1
    
    finally:
        socket.close()
        context.term()
    
    return 0

if __name__ == "__main__":
    print("🔍 Quick Message Checker")
    print("📋 Print first 10 messages then exit")
    print()
    
    sys.exit(main())