#!/bin/sh
# gl_loop.sh N  : N back-to-back windowed GL sessions on the Rage display (env passes through), WindowServer state after each
i=0
while [ $i -lt $1 ]; do
  /Users/sam/RageProLT/tools/mach64_wintest noenv rage > /tmp/gl_loop.$i.out 2>&1
  echo "session $i exit $?  WS: `ps -axww -o pid,state,%cpu,command | grep \"[W]indowServer\" | cut -c1-30`"
  i=`expr $i + 1`
done
