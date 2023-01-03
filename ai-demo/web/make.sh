#!/bin/bash

CFLAGS=" -Wall "
if [[ "$OS" == "Windows_NT" ]] ; then
	CFLAGS+=" -DWIN32 "
fi

work_dir=$(dirname $0)
echo "work_dir: $work_dir"

cd $work_dir
gcc -std=gnu99 -g -D_DEBUG ${CFLAGS} -I../include -I../utils \
	-o web-ui web-ui.c \
	../utils/utils.c \
	-lm -lpthread -ljson-c -ljpeg -lcairo -lcurl \
	$(pkg-config --cflags --libs libsoup-2.4 gio-2.0)
