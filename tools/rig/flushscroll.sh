#!/bin/sh
# flushscroll.sh [N]: WindowServer CPU for N big scroll jumps (40 lines, more than the window height so no engine copy can serve them) over a large image in Preview on the Rage display (needs ~/mkimg, ~/cmdkey, ~/scrollwheel).
N=${1:-100}
[ -f /tmp/big.bmp ] || $HOME/mkimg /tmp/big.bmp
open -a Preview /tmp/big.bmp
sleep 30; $HOME/cmdkey 29; sleep 4   # Preview decodes the 12 MB image slowly and opens scaled to fit; Cmd-0 = Actual Size so there is something to scroll
WS=`ps auxww | grep '[W]indowServer' | grep -v ws_patcher | awk '{print $2}' | head -1`
c() { ioreg -l -w0 | grep -E "Rage2D(Fills|Copies|Ops)\"" | sed 's/.*"Rage2D//; s/" = /=/' | tr '\n' ' '; }
A=`ps -o time= -p $WS`; T0=`date +%s`; C0=`c`
$HOME/scrollwheel 300 240 $N 60000 40
B=`ps -o time= -p $WS`; T1=`date +%s`; C1=`c`
echo "cpu $A -> $B wall `expr $T1 - $T0` s, $N jumps; 2D before [$C0] after [$C1]" | sed 's/  */ /g'
osascript -e 'tell application "Preview" to quit' >/dev/null 2>&1; sleep 2
