#!/bin/sh
# rungui.sh OUT TIMEOUT cmd...  : run cmd inside the logged-in GUI session (Terminal), capture stdout+stderr in OUT, wait up to TIMEOUT s.
# (Terminal only executes a .command when freshly launched, so it also quits when no other windows remain.)
# The Terminal window it opens closes itself when the command ends, and the temp script is removed.
OUT=$1; T=$2; shift 2
rm -f "$OUT" "$OUT.done"
ID=rungui-$$
C=/tmp/$ID.command
cat > $C <<EOF
#!/bin/sh
$* > $OUT 2>&1
echo \$? > $OUT.done
( sleep 1; osascript -e 'tell application "Terminal"' -e 'quit' -e 'end tell' >/dev/null 2>&1; rm -f $C ) &
exit 0
EOF
chmod +x $C
# Terminal only executes a .command when open(1) launches it fresh: make sure none is running (kill by PID; it holds no state we need)
for p in `ps -axww -o pid,command | grep "[T]erminal.app/Contents" | awk '{print $1}'`; do kill $p; done
j=0; while ps -axww | grep -q "[T]erminal.app/Contents" && [ $j -lt 15 ]; do sleep 1; j=$((j+1)); done
open -a Terminal $C
i=0; while [ ! -f "$OUT.done" ] && [ $i -lt $T ]; do sleep 1; i=$((i+1)); done
cat "$OUT"; [ -f "$OUT.done" ] && echo "[exit `cat $OUT.done`]" || echo "[TIMEOUT after ${T}s]"
