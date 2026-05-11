CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g
TARGET   = ./crashlens

$(TARGET): main.cpp
	$(CXX) $(CXXFLAGS) -o $(TARGET) main.cpp

clean:
	rm -f $(TARGET)

.PHONY: clean
