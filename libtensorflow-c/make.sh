#!/bin/bash

LIBTENSORFLOW_PATH=${LIBTENSORFLOW_PATH:-"/opt/google/tensorflow"}
target=${1-"test"}

echo "tensorflow libpath: '$LIBTENSORFLOW_PATH'"
echo -n "build target: $target"

ret=0
case "$target" in 
	test)
		gcc -std=gnu99 -g -Wall \
			-D_TEST_TENSORFLOW_CONTEXT -D_STAND_ALONE \
			-I${LIBTENSORFLOW_PATH}/include -o test_tensorflow_context \
			tensorflow_context.c \
			-L${LIBTENSORFLOW_PATH}/lib -ltensorflow
		;;
	*)
		;;
esac
ret=$?

status="\e[32m[OK]\e[39m"
[ $ret -ne 0 ] && status="\e[31m[NG]\e[39m"

echo -e " ==> status: $status"
exit $ret
