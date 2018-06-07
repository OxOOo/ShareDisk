
CXX_SOURCES = $(wildcard src/*.cpp)
CXX_HEADERS = $(wildcard src/*.h)

CXX_FLAGS = -Wall -O2 --std=c++14 -Iplog/include -lpthread
CXX_FLAGS += $(shell pkg-config fuse3 --cflags --libs)

all: main

main: $(CXX_SOURCES) $(CXX_HEADERS)
	g++ -Wall $(CXX_SOURCES) -o main $(CXX_FLAGS)

run: main
	rm -rf $(PWD)/real/*
	LD_LIBRARY_PATH=/usr/local/lib/x86_64-linux-gnu ./main $(PWD)/mount $(PWD)/real name1:key1
