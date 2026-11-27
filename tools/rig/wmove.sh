#!/bin/sh
# wmove.sh [N]: WindowServer CPU for N window moves of a Finder window on the Rage display, by AppleScript `set bounds` (no mouse input, can't damage the sidebar),
# 0.1 s apart so WindowServer finishes each. Prints WindowServer CPU before/after, wall time, and the 2D client counters (Fills/Copies/Ops).
N=${1:-60}
WS=`ps auxww | grep '[W]indowServer' | grep -v ws_patcher | awk '{print $2}' | head -1`
osascript -e 'tell application "Finder" to make new Finder window' -e 'tell application "Finder" to set bounds of front window to {20,40,560,420}' >/dev/null 2>&1
sleep 2
c() { ioreg -l -w0 | grep -E "Rage2D(Fills|Copies|Ops)\"" | sed 's/.*"Rage2D//; s/" = /=/' | tr '\n' ' '; }
A=`ps -o time= -p $WS`; T0=`date +%s`; C0=`c`
osascript -e "tell application \"Finder\" to repeat $N times
 set bounds of front window to {20,40,560,420}
 delay 0.05
 set bounds of front window to {80,60,620,440}
 delay 0.05
end repeat" >/dev/null 2>&1
B=`ps -o time= -p $WS`; T1=`date +%s`; C1=`c`
echo "cpu $A -> $B wall `expr $T1 - $T0` s, $N x2 moves; 2D before [$C0] after [$C1]" | sed 's/  */ /g'
osascript -e 'tell application "Finder" to close front window' >/dev/null 2>&1
