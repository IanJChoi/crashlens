CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra

OBJECTS = crlns.o crlns-info.o

crlns: $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o crlns $(OBJECTS)

$(OBJECTS): crlns.h

clean:
	rm -f *.o crlns

.PHONY: clean