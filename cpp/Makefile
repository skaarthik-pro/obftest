CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread
LDFLAGS = -lcurl -pthread

# Detect OS and adjust flags
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    LDFLAGS += -pthread
endif
ifeq ($(UNAME_S),Darwin)
    # macOS - curl should be available via Homebrew or system
    CXXFLAGS += -I/usr/local/include
    LDFLAGS += -L/usr/local/lib
endif

TARGET = obftest
SOURCES = main.cpp

OBJS = $(SOURCES:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean

