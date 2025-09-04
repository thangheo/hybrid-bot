#!/usr/bin/env python3
"""
Quick system test script
"""

import sys
import os
import time
import subprocess
import signal

# Add generated protobuf path
# sys.path.append(os.path.join(os.path.dirname(__file__), 'generated', 'python'))
import os 
current_dir = os.path.dirname(os.path.abspath(__file__))
proto_path = os.path.join(current_dir, 'generated', 'python', 'proto')
sys.path.append(proto_path)


def test_protobuf():
    """Test protobuf import"""
    try:
        import trading_messages_pb2
        print("✓ Protobuf messages imported")
        return True
    except ImportError as e:
        print(f"✗ Protobuf import failed: {e}")
        return False

def test_zmq():
    """Test ZMQ import"""
    try:
        import zmq
        print(f"✓ ZeroMQ {zmq.zmq_version()} available")
        return True
    except ImportError:
        print("✗ ZeroMQ not available")
        return False

def test_cpp_server():
    """Test if C++ server can be built"""
    try:
        if not os.path.exists("build/trading_server"):
            print("✗ C++ server not built. Run: ./build.sh")
            return False
        print("✓ C++ server executable found")
        return True
    except Exception as e:
        print(f"✗ C++ server test failed: {e}")
        return False

def run_integration_test():
    """Run basic integration test"""
    print("\nRunning integration test...")
    
    # Start C++ server in background
    server_process = None
    try:
        print("Starting C++ server...")
        server_process = subprocess.Popen(
            ["./build/trading_server"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )
        
        # Give server time to start
        time.sleep(2)
        
        # Run Python client test
        print("Running Python client test...")
        result = subprocess.run([
            sys.executable, "python/zmq_client.py", 
            "--mode", "test", "--symbol", "AAPL"
        ], capture_output=True, text=True, timeout=10)
        
        if result.returncode == 0:
            print("✓ Integration test passed")
            print(result.stdout)
            return True
        else:
            print("✗ Integration test failed")
            print(result.stderr)
            return False
            
    except subprocess.TimeoutExpired:
        print("✗ Integration test timed out")
        return False
    except Exception as e:
        print(f"✗ Integration test error: {e}")
        return False
    finally:
        if server_process:
            server_process.terminate()
            server_process.wait(timeout=5)

if __name__ == "__main__":
    print("Hybrid Trading System Test Suite")
    print("=" * 40)
    
    tests_passed = 0
    total_tests = 4
    
    # Run tests
    if test_protobuf(): tests_passed += 1
    if test_zmq(): tests_passed += 1  
    if test_cpp_server(): tests_passed += 1
    if run_integration_test(): tests_passed += 1
    
    print(f"\nTest Results: {tests_passed}/{total_tests} passed")
    
    if tests_passed == total_tests:
        print("✓ All tests passed! System ready for use.")
    else:
        print("✗ Some tests failed. Check setup and dependencies.")
        sys.exit(1)
