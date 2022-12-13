DEBUG ?= 1
OPTIMIZE ?= -O2
TARGET=bin/app-demo

MSYS_ROOT=/ucrt

CC=gcc -std=gnu99 -D_DEFAULT_SOURCE -D_GNU_SOURCE
CFLAGS = -Wall -Iinclude -DWIN32 -D_WIN32 
LIBS = -lm -lpthread -ljson-c -lcurl -lpng -ljpeg -lcairo

CFLAGS += $(shell pkg-config --cflags gtk+-3.0)
CFLAGS += $(shell pkg-config --cflags gstreamer-1.0 gstreamer-app-1.0)
CFLAGS += $(shell pkg-config --cflags libsoup-2.4)
CFLAGS += $(shell pkg-config --cflags gnutls)

LIBS += $(shell pkg-config --libs gtk+-3.0)
LIBS += $(shell pkg-config --libs gstreamer-1.0 gstreamer-app-1.0)
LIBS += $(shell pkg-config --libs libsoup-2.4)
LIBS += $(shell pkg-config --libs gnutls)

ifeq ($(DEBUG),1)
CFLAGS += -g -D_DEBUG
OPTIMIZE = -O0
endif

SRC_DIR=src
OBJ_DIR=obj

SOURCES := $(wildcard $(SRC_DIR)/*.c)
OBJECTS := $(SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

UTILS_DIR=utils
UTILS_SOURCES := $(wildcard $(UTILS_DIR)/*.c)
UTILS_OBJECTS := $(UTILS_SOURCES:%.c=%.o)

all: do_init $(TARGET)

$(TARGET): $(OBJECTS) $(UTILS_OBJECTS)
	@echo "utils_objects: $(UTILS_OBJECTS)"
	$(CC) $(OPTIMIZE) -mwindows -o $@ $^ $(LIBS) $(CFLAGS)

$(OBJECTS): $(OBJ_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -o $@ -c $< $(CFLAGS)
	
$(UTILS_OBJECTS): %.o : %.c
	$(CC) -o $@ -c $< $(CFLAGS)

.PHONY: do_init clean install
do_init:
	mkdir -p $(OBJ_DIR)

clean:
	rm -f $(OBJ_DIR)/*.o $(UTILS_DIR)/*.o $(TARGET)
	
install: $(TARGET)
	./gtkapp-installer.sh $(TARGET).exe





