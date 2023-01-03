#!/bin/bash

gcc -std=gnu99 -g -Wall -D_DEBUG -I../include -I../utils \
	-o web-ui web-ui.c \
	../utils/utils.c \
	-lm -lpthread -ljson-c -ljpeg -lcairo \
	$(pkg-config --cflags --libs libsoup-2.4 gio-2.0)
