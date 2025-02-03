PROGRAMS=pair
CXXFLAGS=-g -lzmq

pair: pair.cpp
	$(CXX) -o pair pair.cpp $(CXXFLAGS)
