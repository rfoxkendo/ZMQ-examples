PROGRAMS=pair push reqrep
CXXFLAGS=-g -std=c++20 -lzmq

all : $(PROGRAMS)

pair: pair.cpp
	$(CXX) -o pair pair.cpp $(CXXFLAGS)

push : push.cpp
	$(CXX) -o push push.cpp $(CXXFLAGS)

reqrep: reqrep.cpp
	$(CXX) -o reqrep reqrep.cpp $(CXXFLAGS)

clean:
	rm -f $(PROGRAMS)
