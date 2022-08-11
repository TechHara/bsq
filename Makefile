CXXFLAGS = -O3 -DNDEBUG

all: release
debug: CXXFLAGS = -O0 -g
debug: release

release:
	$(CXX) -std=c++14 $(CXXFLAGS) bsq.cc -o bsq

clean:
ifneq (,$(wildcard bsq))
	rm bsq
endif
ifneq (,$(wildcard bsq.dSYM))
	rm -rf bsq.dSYM
endif