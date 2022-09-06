TARGETS = video-player video-player2 video-player3 video-player4

DEBUG ?= 1
OPTIMIZE ?= -O2

CC=gcc -std=gnu99 -D_DEFAULT_SOURCE -D_GNU_SOURCE
LINKER=gcc -std=gnu99 -D_DEFAULT_SOURCE -D_GNU_SOURCE
AR=ar cr

CFLAGS = -Wall -I. -I../include
LIBS = ../lib/libann-utils.a -lm -lpthread -ldl -ljpeg 

ifeq ($(DEBUG),1)
CFLAGS += -g -D_DEBUG
OPTIMIZE = -O0
else
CFLAGS += -DNDEBUG
endif

CFLAGS += $(shell pkg-config --cflags gtk+-3.0 gstreamer-1.0 json-c)
LIBS += $(shell pkg-config --libs  gtk+-3.0 gstreamer-1.0 json-c)



LDFLAGS = $(CFLAGS) $(OPTIMIZE)

COMMON_DEPS := video_source2.h da_panel.h classes_counter.h
DEPS = app.h shell.h video_player4/shell_private.c.impl video_player4/video_streams.h video_player4/area-settings.h

COMMON_SOURCES := video_source2.c da_panel.c classes_counter.c
COMMON_OBJECTS := $(COMMON_SOURCES:%.c=%.o)

VIDEO_PLAYER4_SOURCES := video-player4.c video_player4/app.c video_player4/shell.c \
                         video_player4/video_streams.c video_player4/stream_viewer.c \
                         video_player4/area-settings.c
                         
VIDEO_PLAYER4_OBJECTS := $(VIDEO_PLAYER4_SOURCES:%.c=%.o)

all: $(TARGETS)

video-player: video-player.c
	./make.sh $@


video-player2: video-player2.c
	./make.sh $@
	

video-player3: video-player3.c 
	./make.sh $@


$(COMMON_OBJECTS): %.o : %.c $(COMMON_DEPS)
	$(CC) -o $@ -c $< $(CFLAGS)

$(VIDEO_PLAYER4_OBJECTS): %.o : %.c $(DEPS)
	$(CC) -o $@ -c $< $(CFLAGS)


video-player4: $(VIDEO_PLAYER4_OBJECTS) $(COMMON_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)
	
.PHONY: clean

clean:
	rm -f $(COMMON_OBJECTS) $(VIDEO_PLAYER4_OBJECTS) 
	rm -f video-player4

