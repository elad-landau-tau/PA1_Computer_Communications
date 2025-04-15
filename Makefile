# Simple Makefile to build server and channel applications

ifeq ($(OS), Windows_NT)
	MY_SERVER=my_Server.exe
	MY_CHANNEL=my_channel.exe
else
	MY_SERVER=my_Server
	MY_CHANNEL=my_channel
endif

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2

.PHONY: all clean

all: $(MY_SERVER) $(MY_CHANNEL)

$(MY_SERVER): server.cpp
	$(CXX) $(CXXFLAGS) server.cpp -o my_Server

$(MY_CHANNEL): channel.cpp
	$(CXX) $(CXXFLAGS) channel.cpp -o my_channel

clean:
	rm -f $(MY_SERVER) $(MY_CHANNEL)

