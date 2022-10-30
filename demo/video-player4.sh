#!/bin/bash

xset s off
xset -dpms

path_name=$(realpath $0)
work_path=$(dirname $path_name)
cd $work_path
echo "pwd: $PWD"

## check internet connection
while ! ping -W1 -c1 8.8.8.8 ; do  echo not connected; sleep 1; done

## run app
./video-player4
