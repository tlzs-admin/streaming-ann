DEBUG ?= 1
OPTIMIZE ?= -O2
TARGET=bin/app-demo
WIN32_NO_CONSOLE=1

MSYS_ROOT=/ucrt

CC=gcc -std=gnu99 -D_DEFAULT_SOURCE -D_GNU_SOURCE
CFLAGS = -Wall -Iinclude 
CFLAGS += -Isrc
LIBS = -lm -lpthread -ljson-c -lcurl -lpng -ljpeg -lcairo

ifeq ($(OS),Windows_NT)
CFLAGS += -DWIN32 -D_WIN32
LIBS += -liphlpapi -lWs2_32

ifeq ($(WIN32_NO_CONSOLE),1)
OPTIMIZE += -mwindows
endif
endif


CFLAGS += $(shell pkg-config --cflags gtk+-3.0)
CFLAGS += $(shell pkg-config --cflags gstreamer-1.0 gstreamer-app-1.0)
CFLAGS += $(shell pkg-config --cflags libsoup-2.4)
CFLAGS += $(shell pkg-config --cflags gnutls)

LIBS += $(shell pkg-config --libs gtk+-3.0)
LIBS += $(shell pkg-config --libs gstreamer-1.0 gstreamer-app-1.0)
LIBS += $(shell pkg-config --libs libsoup-2.4)
LIBS += $(shell pkg-config --libs gnutls)
LIBS += -lsecp256k1

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

AUX_DIR=auxiliary
AUX_SOURCES := $(wildcard $(AUX_DIR)/*.c)
AUX_OBJECTS := $(AUX_SOURCES:%.c=%.o)

## VIDEO_PLAYRE4_DIR = video_player4
VIDEO_PLAYRE5_DIR = video_player5

VIDEO_PLAYRE5_SOURCES := $(wildcard $(VIDEO_PLAYRE5_DIR)/*.c)
VIDEO_PLAYRE5_OBJECTS := $(VIDEO_PLAYRE5_SOURCES:%.c=%.o)


all: do_init $(TARGET) bin/video-player5

$(TARGET): $(OBJECTS) $(UTILS_OBJECTS)
	@echo "utils_objects: $(UTILS_OBJECTS)"
	$(CC) $(OPTIMIZE) -o $@ $^ $(LIBS) $(CFLAGS)
	#cp $(TARGET).exe install/bin/

$(OBJECTS): $(OBJ_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -o $@ -c $< $(CFLAGS)
	
$(UTILS_OBJECTS): %.o : %.c
	$(CC) -o $@ -c $< $(CFLAGS)
	
$(AUX_OBJECTS): %.o : %.c
	$(CC) -o $@ -c $< $(CFLAGS)

$(VIDEO_PLAYRE5_OBJECTS): %.o : %.c
	$(CC) -o $@ -c $< $(CFLAGS)	
	
bin/video-player5: $(VIDEO_PLAYRE5_OBJECTS) video-player5.c \
        $(OBJ_DIR)/classes_counter.o $(OBJ_DIR)/ai-client.o $(OBJ_DIR)/da_panel.o \
        $(OBJ_DIR)/video_source_common.o \
        $(OBJ_DIR)/streaming-proxy.o \
        $(UTILS_OBJECTS) \
        $(AUX_DIR)/license-manager.o 
	$(CC) $(OPTIMIZE) -o $@ $^ $(LIBS) $(CFLAGS) 


bin/send_mail: utils/send_mail.c utils/auto_buffer.c utils/utils.c
	$(CC) $(OPTIMIZE) $(CFLAGS) \
        -DTEST_SMTP_CLIENT_ -D_STAND_ALONE \
        -o $@ $^  \
        -lm -lpthread -ljson-c -lcurl

.PHONY: do_init clean install
do_init:
	mkdir -p $(OBJ_DIR) bin install/bin

clean:
	rm -f $(OBJ_DIR)/*.o $(UTILS_DIR)/*.o $(TARGET) $(AUX_DIR)/*.o $(VIDEO_PLAYRE5_DIR)/*.o
	
install: $(TARGET)
	./gtkapp-installer.sh $(TARGET).exe





