#!/bin/bash
g++ -std=c++23 -I cpp-execution/include -I cpp-execution/src -I include -I cpp-execution/include/communication -I /usr/include -pthread cpp-execution/tests/communication/test_zmq_client_server.cpp cpp-execution/src/communication/zmq_client.cpp cpp-execution/src/communication/zmq_server.cpp cpp-execution/src/communication/zmq_manager.cpp -lzmq -o cpp-execution/bin/test_zmq_client_server

