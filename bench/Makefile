CXX ?= g++
CXXFLAGS ?= -O2 -std=c++11 -Wall -Wextra
# silences a deprecation note from the boost/bind.hpp include in SortedHeap.h
CPPFLAGS += -DBOOST_BIND_GLOBAL_PLACEHOLDERS

HeapBench: HeapBench.cpp ../SortedHeap.h ../SortedHeapV2.h ../SortedHeapV3.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) HeapBench.cpp -o $@

clean:
	rm -f HeapBench

.PHONY: clean
