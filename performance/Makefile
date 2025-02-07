PROGRAMS=pair push pubsub req
CXXFLAGS=-g -std=c++20 -lzmq

all : $(PROGRAMS)

pair: pair.cpp
	$(CXX) -o pair pair.cpp $(CXXFLAGS)

push : push.cpp
	$(CXX) -o push push.cpp $(CXXFLAGS)

req: req.cpp
	$(CXX) -o req req.cpp $(CXXFLAGS)

pubsub: pubsub.cpp
	$(CXX) -o pubsub pubsub.cpp $(CXXFLAGS)

clean:
	rm -f $(PROGRAMS)
