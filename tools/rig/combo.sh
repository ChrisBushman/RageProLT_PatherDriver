#!/bin/sh
cd $HOME/atlantis-win || exit 1
export MACH64_GLD_FSYNC=1 MACH64_GLD_APPEND=1
GLUTSHIM_SECS=${1:-90} GLUTSHIM_POS="0,60" ./atlantis > out.c0.txt 2>&1 &
P1=$!
sleep 5
E=/System/Library/Frameworks/ScreenSaver.framework/Versions/A/Resources/ScreenSaverEngine.app/Contents/MacOS/ScreenSaverEngine
$E > $HOME/saver.out 2>&1 &
P2=$!
echo "combo pids: $P1 $P2"
wait $P1
kill $P2
sleep 2
echo done
