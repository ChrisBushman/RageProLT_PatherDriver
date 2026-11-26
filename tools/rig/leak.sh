#!/bin/sh
# usage: leak.sh  -> prints Container/Instance/IOMalloc allocation counters for: control(15 notifies), 15 suite runs, 45 suite runs
cd /Users/sam/RageProLT/tools
snap() { ioreg -l -w0 | grep IOKitDiagnostics | sed 's/.*"Container allocation" = \([0-9]*\).*"IOMalloc allocation" = \([0-9]*\).*"Instance allocation" = \([0-9]*\).*/C=\1 M=\2 I=\3/'; }
runs() { n=$1; mode=$2; i=0; bad=0
  while [ $i -lt $n ]; do
    sudo /Users/sam/stubctl notify >/dev/null 2>&1; sleep 2
    if [ "$mode" = suite ]; then sudo ./mach64_pathb_suite gouraud > /Users/sam/leak-run.out 2>&1; grep -q "SUITE: 5 passed, 0 failed" /Users/sam/leak-run.out || bad=$((bad+1)); fi
    i=$((i+1)); done; echo "bad=$bad"; }
echo "start:   $(snap)"
runs 15 notify;  echo "control(15 notify only):  $(snap)"
runs 15 suite;   echo "15 suite runs:            $(snap)"
runs 45 suite;   echo "+45 suite runs:           $(snap)"
echo DONE
