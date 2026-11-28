#!/bin/sh
# Author the DVD-size coverage discs (frame pictures via ffmpeg; PAL field pictures via the patched mpeg2enc).
# usage: make_size_discs.sh WORKDIR   (needs ffmpeg, dvdauthor on PATH, patched mpeg2enc in $ME)
# disc  system size     structure
#  8    ntsc   704x480  progressive, bf2
#  9    ntsc   352x480  interlaced (ildct/ilme), bf2
# 10    ntsc   352x240  progressive, bf2
# 11    pal    720x576  interlaced, bf2
# 12    pal    704x576  progressive, bf2
# 13    pal    352x576  interlaced, bf2
# 14    pal    352x288  progressive, bf2
# 15    pal    720x576  field pictures (mpeg2enc -I 2 -R 2)
set -e
W=${1:-work}; mkdir -p $W; cd $W
mk() { n=$1; sys=$2; w=$3; h=$4; il=$5; rate=30000/1001; [ $sys = pal ] && rate=25
  vf="testsrc2=size=${w}x${h}:rate=$rate,drawtext=text='%{frame_num}':x=10:y=10:fontsize=30:fontcolor=white:box=1:boxcolor=black@0.6"
  fl=""; [ $il = 1 ] && { vf="$vf,interlace=scan=tff"; fl="-flags +ildct+ilme -top 1"; }
  ffmpeg -hide_banner -loglevel error -y -f lavfi -i "$vf" -f lavfi -i anullsrc=r=48000:cl=stereo -t 6 -c:v mpeg2video $fl -b:v 4M -maxrate 8M -bufsize 1835008 -g 15 -bf 2 -pix_fmt yuv420p -c:a ac3 -b:a 192k -shortest -f dvd -muxrate 10080000 t$n.mpg
  fin $n $sys $w $h; }
fin() { n=$1; sys=$2; w=$3; h=$4
  cat > d$n.xml <<X
<dvdauthor dest="disc$n"><vmgm /><titleset><titles><video format="$sys" aspect="4:3" resolution="${w}x${h}" /><audio format="ac3" lang="en" /><pgc><vob file="t$n.mpg" chapters="0,0:10,0:20" /></pgc></titles></titleset></dvdauthor>
X
  rm -rf disc$n; VIDEO_FORMAT=$(echo $sys | tr a-z A-Z) dvdauthor -x d$n.xml >/dev/null 2>&1
  ffmpeg -hide_banner -loglevel error -y -i t$n.mpg -an -f rawvideo -pix_fmt yuv420p ref$n.yuv
  echo "disc$n ${w}x${h} $sys: $(( $(stat -c %s ref$n.yuv) / (w*h*3/2) )) ref frames"; }
mk 8 ntsc 704 480 0; mk 9 ntsc 352 480 1; mk 10 ntsc 352 240 0
mk 11 pal 720 576 1; mk 12 pal 704 576 0; mk 13 pal 352 576 1; mk 14 pal 352 288 0
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "testsrc2=size=720x576:rate=25,drawtext=text='%{frame_num}':x=20:y=20:fontsize=40:fontcolor=white:box=1:boxcolor=black@0.6,interlace=scan=tff" -t 6 -pix_fmt yuv420p s15.y4m
$ME -f 8 -a 3 -F 3 -q 6 -I 2 -R 2 -o m15.m2v < s15.y4m >/dev/null 2>&1
ffmpeg -hide_banner -loglevel error -y -i m15.m2v -f lavfi -i anullsrc=r=48000:cl=stereo -c:v copy -c:a ac3 -b:a 192k -shortest -f dvd -muxrate 10080000 t15.mpg
fin 15 pal 720 576
