CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O0 -fno-omit-frame-pointer -no-pie

TARGETS = normal_exit segv

all: $(TARGETS)

normal_exit: normal_exit.cpp
	$(CXX) $(CXXFLAGS) -o normal_exit normal_exit.cpp

segv: segv.cpp
	$(CXX) $(CXXFLAGS) -o segv segv.cpp

clean:
	rm -f $(TARGETS)

.PHONY: all clean