#!/bin/bash

echo "$0"

# beep
duration=3 ## seconds

if ! which play ; then
	echo "install sox ..."
	sudo apt-get install sox libsox-fmt-all
fi

play -n -c1 synth sin %-12 sin %-9 sin %-5 sin %-2 fade q 0.1 $duration 0.1
