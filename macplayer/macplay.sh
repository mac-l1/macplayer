#!/bin/bash
set -x

BIN=`pwd`/bin/macplayer
#if [ -z "$1" ]; then FILE=`pwd`/sintel_trailer-1080p.mp4; else FILE=$1; fi
FILE=http://qthttp.apple.com.edgesuite.net/1010qwoeiuryfg/0640/06401.ts
export DISPLAY=:0.0 || true
sudo su -c "EGL_PLATFORM=surfaceflinger $BIN -v 48 '$FILE'"
xrefresh || true
