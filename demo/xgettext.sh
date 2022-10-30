#!/bin/bash

xgettext -C --keyword=_ --default-domain="demo" --from-code=utf-8 --to-code=utf-8 -c video_player4/*.{c,h,impl}
