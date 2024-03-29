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
    demo|demo-06)
		gcc -std=gnu99 -g -Wall -I../include  -D_DEBUG \
            -o ${target} ${target}.c da_panel.c classes_counter.c \
            ../lib/libann-utils.a  \
            -lm -lpthread -ljson-c -ldl \
            -lcairo -ljpeg \
            `pkg-config --cflags --libs gstreamer-1.0 glib-2.0 gio-2.0 gtk+-3.0 libsoup-2.4`
        ;;
    demo-04)
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
	video-player|video_source2|classes_counter|da_panel)
		if uname -r | grep tegra >/dev/null ; then
			echo "build on jetson tx ..."
		    CFLAGS+=" -DJETSON_TX2 "
		fi
		gcc -std=gnu99 -g -Wall -I../include  -D_DEBUG -D_GNU_SOURCE \
		    ${CFLAGS} \
		    -o video-player video-player.c da_panel.c video_source2.c classes_counter.c \
		    ../lib/libann-utils.a  \
		    -lm -lpthread -ljson-c -ljpeg -lpng -lcairo -ldl \
		    `pkg-config --libs --cflags gio-2.0 glib-2.0 gtk+-3.0 gstreamer-1.0`
		;;
	video-player2)
		## compile cv-wrapper
		pushd $(pwd)
		cd ../src/cpps
		make
		popd
		
		## build video-player2
		gcc -std=gnu99 -g -Wall -I../include  -D_DEBUG -D_GNU_SOURCE \
		    ${CFLAGS} \
		    -I../src/cpps \
		    -o video-player2 video-player2.c da_panel.c video_source2.c classes_counter.c \
		    ../src/cpps/obj/cvface-wrapper.static.o \
		    ../src/cpps/obj/cvmat-wrapper.static.o \
		    ../lib/libann-utils.a  \
		    -lm -lpthread -ljson-c -ljpeg -lpng -lcairo -ldl \
		    $(pkg-config --libs --cflags gio-2.0 glib-2.0 gtk+-3.0 gstreamer-1.0) \
		    $(pkg-config --cflags --libs opencv4) -lstdc++
		;;
	
	video-player3|video-player3-settings)
		gcc -std=gnu99 -g -Wall -I../include  -D_DEBUG -D_GNU_SOURCE \
		    ${CFLAGS} \
		    -o video-player3 \
		    video-player3.c da_panel.c video_source2.c classes_counter.c \
		    video-player3-settings.c \
		    ../lib/libann-utils.a  \
		    -lm -lpthread -ljson-c -ljpeg -lpng -lcairo -ldl \
		    `pkg-config --libs --cflags gio-2.0 glib-2.0 gtk+-3.0 gstreamer-1.0`
		;;
	
    test1|blur)
        gcc -std=gnu99 -g -Wall -I../include  -D_DEBUG -D_GNU_SOURCE \
             ${CFLAGS} \
             -o test1 test1.c da_panel.c video_source2.c blur.c \
             ../lib/libann-utils.a  \
             -lm -lpthread -ljson-c -ljpeg -lpng -lcairo -ldl \
             `pkg-config --libs --cflags gio-2.0 glib-2.0 gtk+-3.0 gstreamer-1.0`
         ;;
         
    streaming-proxy)
		 gcc -std=gnu99 -g -Wall -I../include  -D_DEBUG -D_GNU_SOURCE \
			-DTEST_STREAMING_PROXY_ -D_STAND_ALONE \
			${CFLAGS} \
			-o test_streaming-proxy streaming-proxy.c \
			../utils/video_source_common.c ../utils/img_proc.c ../utils/utils.c \
			 -lm -lpthread -ljson-c -ljpeg -lpng -lcairo -ldl \
			`pkg-config --libs --cflags gio-2.0 glib-2.0 gtk+-3.0 gstreamer-1.0 gstreamer-app-1.0 libsoup-2.4`
		;;
    streaming-client)
        gcc -std=gnu99 -g -Wall -D_DEBUG -I../include -o streaming-client streaming-client.c -lm -lpthread -lcurl -ljpeg $(pkg-config --cflags --libs gstreamer-1.0 gstreamer-app-1.0 gio-2.0) -ljson-c
            ;;
            
    camera-switch)
        gcc -std=gnu99 -g -Wall -D_DEBUG -I../include -o camera-switch camera-switch.c \
            ../utils/video_source_common.c ../utils/img_proc.c \
            -lm -lpthread -lcurl -ljpeg -lcairo $(pkg-config --cflags --libs gstreamer-1.0 gstreamer-app-1.0 gio-2.0 libsoup-2.4) -ljson-c
            ;;

    *)
		echo "no building rules"
		exit 1
        ;;
esac

