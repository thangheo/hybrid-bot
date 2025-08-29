CXX = g++
CXXFLAGS = -std=c++11 -Wall -O2
LIBS = -lzmq -ljsoncpp -lpthread

all: zmq_server zmq_publisher zmq_subscriber

zmq_server: zmq_server.cpp
	$(CXX) $(CXXFLAGS) -o zmq_server zmq_server.cpp $(LIBS)

zmq_publisher: zmq_publisher.cpp
	$(CXX) $(CXXFLAGS) -o zmq_publisher zmq_publisher.cpp $(LIBS)
zmq_subscriber: zmq_subscriber.cpp
	$(CXX) $(CXXFLAGS) -o zmq_subscriber zmq_subscriber.cpp $(LIBS)
clean:
	rm -f zmq_server zmq_publisher zmq_subscriber