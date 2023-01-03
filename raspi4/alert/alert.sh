#!/bin/bash

echo "$0"

# beep
duration=3 ## seconds

if ! which play ; then
	echo "install sox ..."
	if [[ "$OS" == "Windows_NT" ]]; then
		pacman -S mingw-w64-ucrt-x86_64-sox
	else
		sudo apt-get install sox libsox-fmt-all
	fi
fi

## play -n -c1 synth sin %-12 sin %-9 sin %-5 sin %-2 fade q 0.1 $duration 0.1

## generate mp3 file

work_dir=$(dirname $0)
cd $work_dir

if [ ! -e output.mp3 ]; then
	sox -n -r 8000 -c2 output.mp3 synth sin %-12 sin %-9 sin %-5 sin %-2 fade q 0.1 $duration 0.1 
fi

sox output.mp3 -t waveaudio

