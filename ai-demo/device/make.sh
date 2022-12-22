#!/bin/bash

CFLAGS=" -Wall "
OPTIMIZE=" -O2 "
if [[ "$DEBUG" == 1 ]] ; then
	CFLAGS+=" -g -D_DEBUG "
	OPTIMIZE=" -O0 "
fi


gcc -std=gnu99 -D_DEFAULT_SOURCE -D_GNU_SOURCE \
	${CFLAGS} ${OPTIMIZE} \
	-I../include -I../utils \
	-o camera-controller \
	*.c \
	../src/video_source_common.c \
	../utils/*.c \
	-lm -lpthread -ljson-c -ljpeg -lcurl -lgnutls -lcairo \
	$(pkg-config --cflags --libs gstreamer-1.0 gstreamer-app-1.0 libsoup-2.4)

