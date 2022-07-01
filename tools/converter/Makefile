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


OPENCV_CAFFE_PROTO_SOURCES := opencv/opencv-caffe.pb.cc
OPENCV_CAFFE_PROTO_OBJECTS := opencv/opencv-caffe.pb.o

all: proto_compile $(TARGETS) 

tests: tests.c $(CXX_OBJECTS) $(OPENCV_CAFFE_PROTO_OBJECTS)
	$(LINKER) $(CXX_FLAGS) -o $@ $^  $(CXX_LIBS) -lstdc++


opencv/opencv-caffe.pb.cc: opencv/opencv-caffe.proto
	echo "compile $^ ..."
	cd opencv; protoc --cpp_out=. opencv-caffe.proto
	
$(OPENCV_CAFFE_PROTO_OBJECTS) :  %.pb.o : %.pb.cc
	$(CXX) -o $@ -c $< $(CXX_FLAGS)

$(CXX_OBJECTS) : %.cxx.o : %.cpp
	$(CXX) -o $@ -c $< $(CXX_FLAGS)


.PHONY: proto_compile clean
proto_compile: opencv/opencv-caffe.proto
	echo "compile $^ ..."
	cd opencv; protoc --cpp_out=. opencv-caffe.proto

clean:
	echo "PROTO_CXX_SOURCES: $(PROTO_CXX_SOURCES)"
	echo "PROTO_CXX_OBJECTS: $(PROTO_CXX_OBJECTS)"
	rm -f *.o opencv/*.o




