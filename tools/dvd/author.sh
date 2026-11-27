#!/bin/sh
# Author the B10 test disc (NTSC 720x480 MPEG-2 + AC-3, frame counter overlay, 3 chapters) into ./disc/VIDEO_TS.
# Needs ffmpeg and dvdauthor on PATH (dvdauthor is built from https://github.com/ldo/dvdauthor; libxml2 from source, see docs/final-findings-nov27.md).
set -e
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "testsrc2=size=720x480:rate=30000/1001,drawtext=text='%{frame_num}':x=20:y=20:fontsize=40:fontcolor=white:box=1:boxcolor=black@0.6" \
  -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=30,volume=0.5" -t 30 -target ntsc-dvd -aspect 4:3 -c:a ac3 -b:a 192k -ac 2 test_ntsc.mpg
rm -rf disc; VIDEO_FORMAT=NTSC dvdauthor -x "$(dirname "$0")/dvd.xml"
# check on the host: ffmpeg -f dvdvideo -i disc -t 5 -f null -
