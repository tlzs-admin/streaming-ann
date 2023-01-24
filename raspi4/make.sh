#!/bin/bash

target=${1-"license-manager"}
target=$(basename "$target")
target=${target/.[ch]/}

CC="gcc -std=gnu99 -g -Wall -Iinclude -I../include -I../ -I../widgets "
#~ LIBS=$(pkg-config --cflags --libs gtk+-3.0 gstreamer-1.0 gstreamer-app-1.0 libcurl)
#~ SOURCES="src/app.c ../widgets/time_spin_widget.c"
#~ echo "$CC -o app src/app.c -lm -lpthread -ljson-c "
#~ $CC -o app ${SOURCES} -lm -lpthread -ljson-c ${LIBS}

case "${target}" in
	license-manager)
		gcc -std=gnu99 -g -Wall -Iinclude -Iutils -I. -I../include \
			-DTEST_LICENSE_MANAGER_ -D_STAND_ALONE \
			-o license-manager src/license-manager.c \
			utils/crypto.c utils/utils.c \
			-lm -lpthread -lsecp256k1 -lgnutls
		;;
	license-server)
		gcc -std=gnu99 -g -Wall -Iinclude -Iutils -I. -I../include \
			-o license-server src/license-server.c \
			src/license-manager.c \
			utils/crypto.c utils/utils.c \
			-lm -lpthread -lsecp256k1 -lgnutls $(pkg-config --cflags --libs libsoup-2.4)
		;;
	alert-service)
		echo "build $target ..."
		gcc -std=gnu99 -g -Wall -Iinclude -Iutils -I. -I../include \
			-DTEST_ALERT_SERVICE_ -D_STAND_ALONE \
			-o test_alert-service src/alert-service.c \
			-lm -lpthread -lcurl -ljson-c $(pkg-config --cflags --libs libsoup-2.4)
		;;
	alert-server)
		gcc -std=gnu99 -g -Wall -Iinclude -Iutils -I. -I../include \
			-o alert-server src/alert-server.c src/alert-service.c \
			-lm -lpthread -lcurl -ljson-c $(pkg-config --cflags --libs libsoup-2.4)
		;;
	camera-switch)
		gcc -std=gnu99 -D_GNU_SOURCE -g -Wall -D_DEBUG \
			-Iinclude -I../include \
			-pthread \
			-o camera-switch \
			src/camera-switch.c \
			-lm -lpthread -ljson-c -lrt \
			$(pkg-config --cflags --libs libsoup-2.4) \
			$(pkg-config --cflags --libs gstreamer-1.0 gstreamer-app-1.0) 
		;;
	*)
		exit 1
		;;
esac

