TARGETS=test1

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


OBJECTS := protobuf_model.o
PROTO_OBJECTS := opencv/opencv-caffe.pb.o onnx/onnx.pb.o



all: $(PROTO_OBJECTS) $(TARGETS)

test1: test1.c $(OBJECTS) $(PROTO_OBJECTS)
	$(CXX) -O0 -o $@ $^ $(CFLAGS) $(CXX_FLAGS) $(CXX_LIBS) 

$(PROTO_OBJECTS): %.pb.o : %.pb.cc
	$(CXX) -o $@ -c $< $(CXX_FLAGS)
	
$(OBJECTS): %.o : %.cpp
	$(CXX) -o $@ -c $< $(CXX_FLAGS)

## compile proto
opencv/opencv-caffe.pb.cc: opencv/opencv-caffe.proto
	echo "compile $^ ..."
	cd opencv; protoc --cpp_out=. opencv-caffe.proto
	
onnx/onnx.pb.cc: onnx/onnx.proto
	echo "compile $^ ..."
	cd onnx; protoc --cpp_out=. onnx.proto

.PHONY: clean
clean:
	rm -f *.o opencv/*.o




