#!/bin/sh
# Phase 4 soak on the G4 (debug kext + ragelt daemon logging to $LOG).  Run through ~/rungui.sh (needs the GUI session; the Rage display primary: ~/mach64_arrange rage).
#   usage: soak.sh LOG [FLURRY_S ATLANTIS_S SONICR_S CYCLES]      defaults 300 300 360 20
# Per phase it records: IOKitDiagnostics counters (Container/IOMalloc/Instance allocation), kextstat line, and the kext's cumulative profile mark 0xd0063000
# {starved, submits-with-pending, engine errors, VRAM bytes in use} (last surviving mark in LOG), plus the app's own RAGE_STATS lines.
LOG=$1; FS=${2:-300}; AS=${3:-300}; SS=${4:-360}; CY=${5:-20}
OUT=$HOME/soak-out; mkdir -p $OUT; : > $OUT/summary.txt
SAVER=/System/Library/Frameworks/ScreenSaver.framework/Versions/A/Resources/ScreenSaverEngine.app/Contents/MacOS/ScreenSaverEngine
SONIC=/Applications/SonicR.app/Contents/MacOS/SonicR
say() { echo "`date +%H:%M:%S` $*" | tee -a $OUT/summary.txt; }
snap() { d=`ioreg -l -w0 | grep IOKitDiagnostics`; echo "$d" | sed 's/.*"Container allocation" *= *\([0-9]*\).*"IOMalloc allocation" *= *\([0-9]*\).*/container=\1 iomalloc=\2/' | cut -c1-60 | tr '\n' ' '; echo "$d" | tr ',' '\n' | grep -E '"(RageLT[A-Za-z]*|IOMemoryMap|IOBufferMemoryDescriptor|IOMachPort|IOUserClient)"=' | tr -d '"' | tr '\n' ' '; }
hx() { echo $((0x$1)); }
marks() { l=`awk '$5=="d0063000" {x=$0} END {print x}' $LOG`; set -- $l; [ -n "$6" ] && echo "starved=`hx $6` pending_submits=`hx $7` engine_errors=`hx $8` vram_in_use=`hx $9`" || echo "(no mark yet)"; }
state() { say "[$1] diag: `snap`"; say "[$1] kext: `kextstat | grep ATIRageProLT | awk '{print $4, $6}'`  $(marks)"; }
stats() { grep RAGE_STATS $1 2>/dev/null | awk '{s+=$2; n++} END {if (n) printf "%d s of RAGE_STATS, %.1f presents/s, %d presents\n", n, s/n, s}'; }
state start
say "== Flurry ${FS}s"
RAGE_STATS=1 $SAVER > $OUT/flurry.out 2> $OUT/flurry.stats & P=$!; sleep $FS; kill $P; sleep 3
say "Flurry: `stats $OUT/flurry.stats`"; state after-flurry
say "== Atlantis ${AS}s"
cd $HOME/atlantis-win && (RAGE_STATS=1 GLUTSHIM_SECS=$AS GLUTSHIM_POS="0,60" ./atlantis > $OUT/atlantis.out 2> $OUT/atlantis.stats & P=$!; wait $P; echo "atlantis exit $?" >> $OUT/summary.txt)
say "Atlantis: `stats $OUT/atlantis.stats`; `tail -3 $OUT/atlantis.out | tr '\n' ' '`"; state after-atlantis
say "== Sonic R ${SS}s"
cd /Applications/SonicR.app/Contents/Resources && (RAGE_STATS=1 $SONIC > $OUT/sonicr.out 2> $OUT/sonicr.stats & P=$!; sleep $SS; kill $P; sleep 3)
say "Sonic R: `stats $OUT/sonicr.stats`"; state after-sonicr
say "== process cycle x$CY (Atlantis 6 s + Sonic R 12 s alternating)"
i=0
while [ $i -lt $CY ]; do
  cd $HOME/atlantis-win && GLUTSHIM_SECS=6 GLUTSHIM_POS="0,60" ./atlantis > $OUT/cyc_a.out 2>&1; ra=$?
  cd /Applications/SonicR.app/Contents/Resources && ($SONIC > $OUT/cyc_s.out 2>&1 & P=$!; sleep 12; kill $P; sleep 2)
  i=`expr $i + 1`; say "cycle $i/$CY atlantis exit $ra"; [ `expr $i % 5` = 0 ] && state cycle-$i
done
state end
say "DONE"
