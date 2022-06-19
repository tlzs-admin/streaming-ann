#!/bin/bash

TARGET=${1-"darknet"}

DARKNET_PATH="/localstor/git/darknet"

DARKNET_CFLAGS=" -I${DARKNET_PATH}/include -I. "
DARKNET_LIBS="${DARKNET_PATH}/libdarknet.a"

if [ ! -e plugins ]; then
       ln -s ../../../plugins ./
fi

if [ ! -e include ]; then
	ln -s ../../../include ./
fi

if [ ! -e utils ]; then
	ln -s ../../../utils ./
fi


#if [ -e /usr/local/cuda ] ; then
DARKNET_CFLAGS+=" -DGPU "
DARKNET_LIBS+=" -lcuda -lcudart -lcublas -lcurand "
#fi

function build()
{
	local target=$1
	[ -z "${target}" ] && exit 1

	case "${target}" in
		darknet|darknet-wrapper):
			gcc -std=gnu99 -g -Wall -D_DEBUG -fPIC -shared -o plugins/libaiplugin-darknet.so \
				darknet.c darknet-wrapper.c -Iinclude -I. \
				utils/*.c \
				${DARKNET_CFLAGS} ${DARKNET_LIBS} \
				-lm -lpthread -ljson-c -ljpeg -lpng -lcairo \
				`pkg-config --cflags --libs gio-2.0 glib-2.0`
			;;
		httpclient|ai-plugin_httpclient):
			gcc -std=gnu99 -g -Wall -D_DEBUG -fPIC -shared -o plugins/libaiplugin-httpclient.so \
				httpclient.c ai-plugin_httpclient.c -Iinclude -I. \
				utils/*.c \
				-lm -lpthread -ljson-c -ljpeg -lpng -lcairo -lcurl \
				`pkg-config --cflags --libs gio-2.0 glib-2.0`
			;;
	*)
		exit 1
		;;
	esac
	return 0
}

build ${TARGET}
