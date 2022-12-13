#!/bin/bash

## build environment
MSYS_BUILD_ROOT=/ucrt64

EXE_FILE=$1

if [ -z "$EXE_FILE" ] ; then
	echo "Usuage: $0 exe_file.exe"
	exit 1
fi

## copy dependencies
mkdir -p install/bin

ldd "$EXE_FILE" | grep "$MSYS_BUILD_ROOT" > dependencies.list
cat dependencies.list | cut -d '=' -d '>' -d ' ' -f3 > dependencies

while read p; do echo $p; cp $p install/bin/ ;  done < dependencies

## copy resources
mkdir -p install/lib
mkdir -p install/share/glib-2.0/schemas
mkdir -p install/share/icons

if [ ! -d {MSYS_BUILD_ROOT}/share ] ; then
cp -rp ${MSYS_BUILD_ROOT}/share/glib-2.0/schemas/gschemas.compiled install/share/glib-2.0/
cp -rp ${MSYS_BUILD_ROOT}/share/icons/Adwaita install/share/icons
cp -rp ${MSYS_BUILD_ROOT}/lib/gdk-pixbuf-2.0 install/lib
fi

cp "$EXE_FILE" install/bin/ 


## copy gstreamer libraries
GST_LIBS=$(find ${MSYS_BUILD_ROOT}/bin -type f -name "libgst*.dll")
for lib in ${GST_LIBS[@]}
do
	echo "$lib"
	cp -rp $lib install/bin/
done

## copy gst-plugins
cp -rp ${MSYS_BUILD_ROOT}/lib/gstreamer-1.0 install/lib/

cp -rp ${MSYS_BUILD_ROOT}/bin/libopenh264.dll install/bin/
cp -rp ${MSYS_BUILD_ROOT}/bin/liborc-0.4-0.dll install/bin/
cp -rp ${MSYS_BUILD_ROOT}/bin/libjson-glib-1.0-0.dll install/bin/
