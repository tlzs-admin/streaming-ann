#!/bin/bash

LIBTENSORFLOW_PATH="/opt/google/tensorflow"

target=${1-"test"}

case "$target" in 
	test)
		gcc -std=gnu99 -g -Wall \
			-D_TEST_TENSORFLOW_CONTEXT -D_STAND_ALONE \
			-I${LIBTENSORFLOW_PATH}/include -o test_tensorflow_context \
			tensorflow_context.c \
			-L${LIBTENSORFLOW_PATH}/lib -ltensorflow
		;;
	*)
		exit 1
		;;
esac

