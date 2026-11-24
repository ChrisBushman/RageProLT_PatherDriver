#!/bin/sh
# WINDOWED Atlantis (Apple's GLUT sample) on the Rage display: unmodified sample (only depth=24 -> 16, our hardware Z) built against glutshim
# (Carbon window + AGL on the 640x480 display).  usage: atlantis-win.sh [SECS] [COPIES]   (COPIES = simultaneous atlantis processes, default 1)
# Run through ~/rungui.sh (needs the GUI session).
SECS=${1:-30}; N=${2:-1}
T=`cd \`dirname "$0"\` && pwd`; SRC=/Developer/Examples/OpenGL/GLUT/atlantis; W=$HOME/atlantis-win
rm -rf "$W"; cp -R "$SRC" "$W"; cd "$W" || exit 1
sed -i.bak 's/depth=24/depth=16/' atlantis.c
cc -O2 -I"$T/glutshim" -o atlantis atlantis.c dolphin.c shark.c whale.c swim.c "$T/glutshim/glutshim.c" -framework Carbon -framework AGL -framework OpenGL 2>&1 | grep -E "error|undefined" && exit 2
rm -f /tmp/mach64gld.log
i=0; PIDS=""
while [ $i -lt $N ]; do
  GLUTSHIM_SECS=$SECS GLUTSHIM_POS="$((i*320)),$((60 + i*40))" ./atlantis > "$W/out.$i.txt" 2>&1 &
  PIDS="$PIDS $!"; i=`expr $i + 1`
done
echo "atlantis pids:$PIDS  ($N copies, ${SECS}s)"
for p in $PIDS; do wait $p; echo "pid $p exit $?"; done
for f in "$W"/out.*.txt; do echo "== $f"; head -n 6 "$f"; done
