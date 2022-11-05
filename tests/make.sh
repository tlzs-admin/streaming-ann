#!/bin/bash

target=${1-"test-video_source_common"}
target=$(basename ${target})
target=${target/.[ch]/}

workdir=$(realpath $0)
workdir=$(dirname $workdir)
echo "workdir: $workdir"

cd $workdir


case "$target" in
	video_source_common|test-video_source_common)
		echo "pwd: $PWD"
		gcc -std=gnu99 -g -O0 -Wall -D_DEBUG -D_DEFAULT_SOURCE -I../include -I../utils \
			-o test-video_source_common \
			test-video_source_common.c \
			../utils/video_source_common.c \
			$(pkg-config --cflags --libs gstreamer-1.0 gstreamer-app-1.0 gio-2.0)
		;;
	*)
		echo "unknown target: $target"
		exit 1
		;;
esac
exit 0
