#!/bin/bash

target=${1-"demo-01"}
target=${target/.[ch]/}

if [[ "$OS" == "Windows_NT" ]] ; then
CFLAGS+=" -D_WIN32 -IC:/msys64/mingw64/include "
fi

echo "build '${target}' ..."
case ${target} in
    demo-01|demo-02|demo-03|config-tools|demo-01-win)
        gcc -std=gnu99 -g -Wall -I../include  -D_DEBUG \
            -o ${target} ${target}.c da_panel.c \
            ../lib/libann-utils.a  \
            -lm -lpthread -ljson-c -ldl \
            -lcairo -ljpeg \
            `pkg-config --cflags --libs gstreamer-1.0 glib-2.0 gio-2.0 gtk+-3.0 libcurl`
        ;;
    ai-server)
        gcc -std=gnu99 -O3 -Wall -I../include \
            -o ai-server ai-server.c \
            ../lib/libann-utils.a  \
            -lm -lpthread -ljson-c -ldl \
            -ljpeg -lcairo \
            `pkg-config --cflags --libs libsoup-2.4` -lcuda -lcudart -lcublas -lcurand

        ;;
    demo)
		gcc -std=gnu99 -g -Wall -I../include  -D_DEBUG \
            -o ${target} ${target}.c da_panel.c \
            ../lib/libann-utils.a  \
            -lm -lpthread -ljson-c -ldl \
            -lcairo -ljpeg \
            `pkg-config --cflags --libs gstreamer-1.0 glib-2.0 gio-2.0 gtk+-3.0 libsoup-2.4`
        ;;
    demo-04|da_panel)
		gcc -std=gnu99 -g -Wall -I../include  -D_DEBUG \
            -o demo-04 demo-04.c da_panel.c \
            ../lib/libann-utils.a  \
            -lm -lpthread -ljson-c -ldl \
            -lcairo -ljpeg \
            `pkg-config --cflags --libs glib-2.0 gio-2.0 gtk+-3.0`
        ;;
    demo-05)
		## check dependencies
		if ! pkg-config --list-all | grep tesseract > /dev/null ; then
			echo "tesseract not found"
			sudo apt-get -y install libtesseract-dev tesseract-ocr-jpn libleptonica-dev
			[ ! -z $? ] && exit 1
		fi

		## build
		gcc -std=gnu99 -g -Wall -I../include  -D_DEBUG \
            -o demo-05 demo-05.c da_panel.c \
            ../lib/libann-utils.a  \
            -lm -lpthread -ljson-c -ldl \
            -lcairo -ljpeg -ltesseract -llept \
            `pkg-config --cflags --libs glib-2.0 gio-2.0 gtk+-3.0`
        ;;
    webserver|webserver-utils)
		gcc -std=gnu99 -g -Wall -I../include  -D_DEBUG -D_GNU_SOURCE \
		    -o webserver webserver.c \
		    ../lib/libann-utils.a  \
		    -lm -lpthread -ljson-c -ljpeg -lpng -lcairo -ldl \
		    `pkg-config --libs --cflags libsoup-2.4 gio-2.0 glib-2.0`
		;;
	video-player|video_source2)
		if uname -r | grep tegra >/dev/null ; then
			echo "build on jetson tx ..."
		    CFLAGS+=" -DJETSON_TX2 "
		fi
		gcc -std=gnu99 -g -Wall -I../include  -D_DEBUG -D_GNU_SOURCE \
		    ${CFLAGS} \
		    -o video-player video-player.c da_panel.c video_source2.c \
		    ../lib/libann-utils.a  \
		    -lm -lpthread -ljson-c -ljpeg -lpng -lcairo -ldl \
		    `pkg-config --libs --cflags gio-2.0 glib-2.0 gtk+-3.0 gstreamer-1.0`
		;;

    *)
		echo "no building rules"
		exit 1
        ;;
esac

