// Makefile
# Simple Makefile to build server and channel applications
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2

all: my_Server.exe my_channel.exe

my_Server.exe: server.cpp
	$(CXX) $(CXXFLAGS) server.cpp -o my_Server

my_channel.exe: channel.cpp
	$(CXX) $(CXXFLAGS) channel.cpp -o my_channel

clean:
	rm -f my_Server.exe my_channel.exe