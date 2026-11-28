#!/bin/sh
# Build the field-picture / dual-prime test discs (disc4..6) used for M1 verification.
# Needs: ffmpeg, dvdauthor (+ libxml2), mjpegtools-2.2.1.tar.gz (sourceforge mjpeg), gcc/g++ + make.
# mpeg2enc 2.2.1's field-picture mode (-I 2) is broken upstream; mpeg2enc/mpeg2enc-2.2.1-fieldpic-fixes.patch fixes it
# (shared reconstruction frame between the two fields, intra clearblock stride, no sequence header between fields,
#  NULL reference at stream start) and adds M2E_STATS / M2E_DMVLOG diagnostics.  Quality is irrelevant: ffmpeg's decode is the reference.
set -e
W=${W:-$PWD/work}; mkdir -p $W; cd $W
[ -d mjpegtools-2.2.1 ] || { curl -sSL -o mj.tgz https://prdownloads.sourceforge.net/sourceforge/mjpeg/mjpegtools-2.2.1.tar.gz; tar xzf mj.tgz; }
(cd mjpegtools-2.2.1 && patch -p1 -N < $OLDPWD/../mpeg2enc/mpeg2enc-2.2.1-fieldpic-fixes.patch || true; \
 [ -f mpeg2enc/.libs/mpeg2enc ] || { ./configure --without-gtk --without-libquicktime --without-libdv --without-libpng --without-dga --without-x --without-v4l --disable-simd-accel --without-libsdl --without-jpeg >/dev/null; make -j8 CXXFLAGS="-g -O2 -std=gnu++17" >/dev/null; })
export LD_LIBRARY_PATH=$PWD/mjpegtools-2.2.1/utils/.libs:$PWD/mjpegtools-2.2.1/mpeg2enc/.libs
ME=$PWD/mjpegtools-2.2.1/mpeg2enc/.libs/mpeg2enc
# sources: interlaced testsrc2 (frame_num overlay) and a slow textured pan with inter-field motion (gives dual-prime candidates)
ffmpeg -y -f lavfi -i "testsrc2=size=720x480:rate=30000/1001,drawtext=text='%{frame_num}':x=20:y=20:fontsize=40:fontcolor=white:box=1:boxcolor=black@0.6,interlace=scan=tff" -t 6 -pix_fmt yuv420p src_il.y4m
ffmpeg -y -f lavfi -i "nullsrc=size=1440x960:rate=60000/1001,geq='128+60*sin((X+T*90)/9)+50*sin((Y+T*40)/13)+20*sin((X+Y)/5)':128:128,crop=720:480:'360+200*sin(t*0.7)':240,tinterlace=interleave_top,fps=30000/1001" -t 5 -pix_fmt yuv420p s_pan.y4m
$ME -f 8 -a 2 -F 4 -q 6 -I 2 -R 0 -o m4.m2v < src_il.y4m                          # disc4: field pictures I/P (field MC, 16x8)
$ME -f 8 -a 2 -F 4 -q 6 -I 2 -R 2 -o m5.m2v < src_il.y4m                          # disc5: + B field pictures (fwd/bwd/bi, 16x8)
$ME -f 8 -a 2 -F 4 -q 6 -I 2 -R 0 --dualprime-mpeg2 -o m6.m2v < s_pan.y4m          # disc6: dual prime
for n in 4 5 6; do
  ffmpeg -y -i m$n.m2v -f lavfi -i anullsrc=r=48000:cl=stereo -c:v copy -c:a ac3 -b:a 192k -shortest -f dvd -muxrate 10080000 t$n.mpg
  sed "s/disc2/disc$n/; s/t2.mpg/t$n.mpg/" ../disc.xml.template > d$n.xml; rm -rf disc$n; VIDEO_FORMAT=NTSC dvdauthor -x d$n.xml
  ffmpeg -y -i t$n.mpg -an -f rawvideo -pix_fmt yuv420p ref$n.yuv      # reference decode
done
