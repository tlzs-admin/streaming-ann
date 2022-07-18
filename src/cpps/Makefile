DEBUG ?= 1
CXX_STANDARD ?= c++11
OPENCV_VERSION ?=4
OPTIMIZE ?= -O2

TARGETS := libopencv-wrapper.a libopencv-wrapper.so

CC=gcc -std=gnu99
CXX=g++ -std=$(CXX_STANDARD)
AR=ar cor

CFLAGS = -Wall 

ifeq ($(DEBUG),1)
CFLAGS += -g -D_DEBUG
OPTIMIZE = -O0
endif

CXX_FLAGS = $(CFLAGS)
CXX_FLAGS += -pthread

OPENCV_CFLAGS := $(shell pkg-config --cflags opencv$(OPENCV_VERSION))
OPENCV_LIBS := $(shell pkg-config --libs opencv$(OPENCV_VERSION))

CXX_FLAGS += $(OPENCV_CFLAGS)

DEPS := cv-wrapper.h

CXX_SOURCES := $(wildcard *.cpp)
CXX_OBJECTS := $(CXX_SOURCES:%.cpp=obj/%.static.o)
CXX_OBJECTS_DYN := $(CXX_SOURCES:%.cpp=obj/%.dyn.o)

all: do_init tests $(TARGETS)
tests: test_cvmat_wrapper test_cvface_wrapper

libopencv-wrapper.a: $(CXX_OBJECTS)
	$(AR) $@ $^
	
libopencv-wrapper.so: $(CXX_OBJECTS_DYN)
	$(CC) -fPIC -shared $(OPTIMIZE) $(CXX_FLAGS) -o $@ $^ $(OPENCV_LIBS) -lstdc++
	
$(CXX_OBJECTS): obj/%.static.o: %.cpp $(DEPS)
	$(CXX) -o $@ -c $< $(CXX_FLAGS)
	
$(CXX_OBJECTS_DYN): obj/%.dyn.o: %.cpp $(DEPS)
	$(CXX) -fPIC -o $@ -c $< $(CXX_FLAGS)


.PHONY: do_init clean
do_init:
	mkdir -p obj
clean:
	rm -f test_cvmat_wrapper test_cvface_wrapper obj/*.o $(TARGETS)


test_cvmat_wrapper: cvmat-wrapper.cpp
	$(CXX) $(OPTIMIZE) $(CXX_FLAGS) -DTEST_CVMAT_WRAPPER_ -D_STAND_ALONE -o $@ $^ $(OPENCV_LIBS) -lcairo

test_cvface_wrapper: cvface-wrapper.cpp
	$(CXX) $(OPTIMIZE) $(CXX_FLAGS) -DTEST_CVFACE_WRAPPER_ -D_STAND_ALONE -o $@ $^ $(OPENCV_LIBS) -lcairo

