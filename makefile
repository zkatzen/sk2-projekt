CXX = g++
CXXFLAGS = -Wall --std=c++11 -pthread -I ..
BOOSTFLAGS = -lboost_system -lboost_filesystem
all: tcp_server
tcp_server: tcp_server.o
	$(CXX) $(CXXFLAGS) -o tcp_server tcp_server.o $(BOOSTFLAGS)
tcp_server.o: tcp_server.cpp
	$(CXX) $(CXXFLAGS) -c tcp_server.cpp $(BOOSTFLAGS)
clean:
	rm *o tcp_server
