TARGETS = video-player video-player2 video-player3 video-player4

DEBUG ?= 1
OPTIMIZE ?= -O2

CC=gcc -std=gnu99 -D_DEFAULT_SOURCE -D_GNU_SOURCE
LINKER=gcc -std=gnu99 -D_DEFAULT_SOURCE -D_GNU_SOURCE
AR=ar cr

CFLAGS = -Wall -I. -I../include -pthread
LIBS = ../lib/libann-utils.a -lm -lpthread -ldl -ljpeg -lcurl

ifeq ($(DEBUG),1)
CFLAGS += -g -D_DEBUG
OPTIMIZE = -O0
else
CFLAGS += -DNDEBUG
endif

CFLAGS += $(shell pkg-config --cflags gtk+-3.0 gstreamer-1.0 json-c gstreamer-app-1.0)
LIBS += $(shell pkg-config --libs  gtk+-3.0 gstreamer-1.0 json-c gstreamer-app-1.0)



LDFLAGS = $(CFLAGS) $(OPTIMIZE)

COMMON_DEPS := video_source2.h da_panel.h classes_counter.h
DEPS = app.h shell.h video_player4/shell_private.c.impl $(wildcard video_player4/*.h)

COMMON_SOURCES := video_source2.c da_panel.c classes_counter.c ../utils/video_source_common.c
COMMON_OBJECTS := $(COMMON_SOURCES:%.c=%.o)

#~ VIDEO_PLAYER4_SOURCES := video-player4.c video_player4/app.c video_player4/shell.c \
#~                          video_player4/video_streams.c video_player4/stream_viewer.c \
#~                          video_player4/area-settings.c
     
VIDEO_PLAYER4_SOURCES := video-player4.c $(wildcard video_player4/*.c)
VIDEO_PLAYER4_OBJECTS := $(VIDEO_PLAYER4_SOURCES:%.c=%.o)

VIDEO_PLAYER5_SOURCES := video-player5.c streaming-proxy.c $(wildcard video_player5/*.c)
VIDEO_PLAYER5_OBJECTS := $(VIDEO_PLAYER5_SOURCES:%.c=%.o)

CV_WRAPPER_OBJECTS := ../src/cpps/cvmat-wrapper.o.dyn ../src/cpps/cvface-wrapper.o.dyn

all: languages $(TARGETS)

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


$(VIDEO_PLAYER5_OBJECTS): %.o : %.c $(DEPS)
	$(CC) -o $@ -c $< $(CFLAGS) $(shell pkg-config --cflags libsoup-2.4)

video-player5: $(VIDEO_PLAYER5_OBJECTS) $(COMMON_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS) $(shell pkg-config --libs libsoup-2.4)

plugins/libaiplugin-cvface.so : $(CV_WRAPPER_OBJECTS) opencv-plugin.c ../utils/utils.c
	$(LINKER) -fPIC -shared -pthread -g -Wall -o $@ $^ $(CFLAGS) $(shell pkg-config --cflags --libs opencv4) -lm -lpthread -ljson-c -ldl -lstdc++
	
$(CV_WRAPPER_OBJECTS) : ../src/cpps/%.o.dyn : ../src/cpps/%.cpp
	g++ -std=c++11 -fPIC -g -Wall -D_DEBUG -o $@ -c $< $(shell pkg-config --cflags opencv4) 


.PHONY: clean languages

languages: langs/ja.po
	if ! which gettext >/dev/null 2>&1 ; then sudo apt-get install gettext; fi
	msgfmt --output-file langs/ja/LC_MESSAGES/demo.mo langs/ja.po

clean:
	rm -f $(COMMON_OBJECTS) $(VIDEO_PLAYER4_OBJECTS) $(VIDEO_PLAYER5_OBJECTS) 
	rm -f video-player4 video-player5

