#!/bin/bash

CC=" gcc -std=gnu99 -D_DEFAULT_SOURCE -D_GNU_SOURCE "
CXX=" g++ -std=c++11 -D_DEFAULT_SOURCE -D_GNU_SOURCE "

CFLAGS=" -Wall -g -D_DEBUG "
CXX_FLAGS=

target=${1-"rgb2hsv"}
target=${target/.cpp/}
target=${target/.[ch]/}


echo "build '$target' ..."
case "${target}" in
	rgb2hsv)
		${CC} -o ${target} ${target}.c \
			${CFLAGS} \
			-lm -lpthread \
			$(pkg-config --cflags --libs gtk+-3.0)
		;;
	find_contours)
		${CXX} -o ${target} ${target}.cpp \
			${CFLAGS} \
			-lm -lpthread \
			$(pkg-config --cflags --libs opencv)
		;;
	*)
		echo "no building rules"
		exit 1
		;;
esac
