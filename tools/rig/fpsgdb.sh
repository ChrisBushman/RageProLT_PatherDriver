#!/bin/sh
# usage: fpsgdb.sh PID SECONDS  -> frames (CGLFlushDrawable calls) during SECONDS, via an ignore-count breakpoint
PID=$1; SECS=$2
cat > /tmp/fpsgdb.cmds <<EOC
set confirm off
break CGLFlushDrawable
ignore 1 100000000
continue
info breakpoints
detach
quit
EOC
gdb -batch -x /tmp/fpsgdb.cmds -p $PID > /tmp/fpsgdb.out 2>&1 &
G=$!
sleep $SECS
kill -INT $PID
wait $G
grep -E "already hit" /tmp/fpsgdb.out
