PROGRAMS=pair push
CXXFLAGS=-g -std=c++20 -lzmq

all : $(PROGRAMS)

pair: pair.cpp
	$(CXX) -o pair pair.cpp $(CXXFLAGS)

push : push.cpp
	$(CXX) -o push push.cpp $(CXXFLAGS)


clean:
	rm -f $(PROGRAMS)
