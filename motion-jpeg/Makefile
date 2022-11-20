TARGET=motion-jpeg-server

DEBUG ?= 1
OPTIMIZE ?= -O2

CC=gcc -std=gnu99
LINKER=$(CC)

CFLAGS = -Wall -I../include 
LIBS = -lm -lpthread -ljpeg -ljson-c

ifeq ($(DEBUG),1)
CFLAGS += -g -D_DEBUG
OPTIMIZE = -O0
endif

CFLAGS += $(shell pkg-config --cflags gstreamer-1.0 libsoup-2.4)
LIBS += $(shell pkg-config --libs gstreamer-1.0 gstreamer-app-1.0 libsoup-2.4)

SOURCES := $(wildcard *.c)
SOURCES += ../utils/video_source_common.c

OBJECTS := $(SOURCES:%.c=%.o)

all: $(TARGET)

motion-jpeg-server: $(OBJECTS)
	$(LINKER) $(OPTIMIZE) -o $@ $^ $(LIBS)

$(OBJECTS): %.o : %.c
	$(CC) -o $@ -c $< $(CFLAGS)

.PHONY: clean

clean:
	rm -f *.o $(TARGET)





