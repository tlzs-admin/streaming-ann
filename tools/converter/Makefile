TARGETS=tests

DEBUG ?= 1
CC = gcc -std=gnu99
CXX = g++ -std=c++11
LINKER=$(CC)


CXX_FLAGS = -I. -I./opencv -Wall

ifeq ($(DEBUG),1)
CXX_FLAGS += -g
endif



CXX_FLAGS += $(shell pkg-config --cflags protobuf)
CXX_LIBS = $(shell pkg-config --libs protobuf) -ljson-c 


CXX_SOURCES := opencv-caffe_wrapper.cpp
CXX_OBJECTS := $(CXX_SOURCES:%.cpp=%.cxx.o)

PROTO_CXX_SOURCES := $(wildcard opencv/*.pb.cc)
PROTO_CXX_OBJECTS := $(PROTO_CXX_SOURCES:%.pb.cc=%.pb.o)


all: $(TARGETS) 
	echo "PROTO_CXX_SOURCES: $(PROTO_CXX_SOURCES)"

tests: tests.c $(CXX_OBJECTS) $(PROTO_CXX_OBJECTS)
	$(LINKER) $(CXX_FLAGS) -o $@ $^  $(CXX_LIBS) -lstdc++


$(PROTO_CXX_OBJECTS): %.pb.o : %.pb.cc
	$(CXX) -o $@ -c $< $(CXX_FLAGS)

$(CXX_OBJECTS) : %.cxx.o : %.cpp
	$(CXX) -o $@ -c $< $(CXX_FLAGS)



test1: test1.cpp 
	g++ -std=c++11 -o $@ $^  $(pkg-config --cflags --libs protobuf)

.PHONY: clean
clean:
	echo "PROTO_CXX_SOURCES: $(PROTO_CXX_SOURCES)"
	echo "PROTO_CXX_OBJECTS: $(PROTO_CXX_OBJECTS)"
	rm -f *.o opencv/*.o




