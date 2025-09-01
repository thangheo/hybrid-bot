CXX = g++
CXXFLAGS = -std=c++14 -Wall -O2 -I. -I../../include -I../../proto/generated/cpp/
LIBS = -lzmq -ljsoncpp -lpthread

all: zmq_server zmq_publisher zmq_subscriber
# add include dir = ../include/

zmq_server: zmq_server.cpp
	$(CXX) $(CXXFLAGS) -o zmq_server zmq_server.cpp $(LIBS)

zmq_publisher: zmq_publisher.cpp
	$(CXX) $(CXXFLAGS) -o zmq_publisher zmq_publisher.cpp $(LIBS)

zmq_subscriber: zmq_subscriber.cpp
	$(CXX) $(CXXFLAGS) -o zmq_subscriber zmq_subscriber.cpp $(LIBS)

clean:
	rm -f zmq_server zmq_publisher zmq_subscriber