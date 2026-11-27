#!/bin/sh
# scroll_ab.sh [N] [delay_us]: WindowServer CPU for N scroll-wheel events over a TextEdit window on the Rage display (needs ~/scrollwheel and /tmp/words.txt).
N=${1:-200}; D=${2:-30000}
[ -f /tmp/words.txt ] || head -4000 /usr/share/dict/words > /tmp/words.txt
open -a TextEdit /tmp/words.txt; sleep 6
osascript -e 'tell application "TextEdit" to set bounds of front window to {20,40,560,440}' >/dev/null 2>&1; sleep 2
WS=`ps auxww | grep '[W]indowServer' | grep -v ws_patcher | awk '{print $2}' | head -1`
c() { ioreg -l -w0 | grep -E "Rage2D(Fills|Copies|Ops)\"" | sed 's/.*"Rage2D//; s/" = /=/' | tr '\n' ' '; }
A=`ps -o time= -p $WS`; T0=`date +%s`; C0=`c`
$HOME/scrollwheel 300 240 $N $D 3
B=`ps -o time= -p $WS`; T1=`date +%s`; C1=`c`
echo "cpu $A -> $B wall `expr $T1 - $T0` s, $N scroll events; 2D before [$C0] after [$C1]" | sed 's/  */ /g'
screencapture -x /tmp/scroll_end.png
osascript -e 'tell application "TextEdit" to quit' >/dev/null 2>&1; sleep 2
