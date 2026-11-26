#!/bin/sh
# Real applications against the Apple-NDRV memory layout (G4, RELEASE kext, RageStubFB with the canary block below the frame buffer).
# Run it through ~/rungui.sh (GUI session) with the Rage display primary and held at the wanted mode (cgmode ... force).
#   usage: layout_exercise.sh OUTDIR [ATLANTIS_S FLURRY_S CHESS_S SONICR_S]      defaults 40 40 30 120
# After every app: cursor_guard (canary intact?), overlay status, kext still loaded; screenshots OUTDIR/<app>_N.png (screencapture -> PDF -> sips).
O=$1; AS=${2:-40}; FS=${3:-40}; CS=${4:-30}; SS=${5:-120}; mkdir -p $O; : > $O/summary.txt
R=$HOME/rage-p1/tools/ragegl
SAVER=/System/Library/Frameworks/ScreenSaver.framework/Versions/A/Resources/ScreenSaverEngine.app/Contents/MacOS/ScreenSaverEngine
SONIC=/Applications/SonicR.app/Contents/MacOS/SonicR
say() { echo "`date +%H:%M:%S` $*" | tee -a $O/summary.txt; }
snap() { /usr/sbin/screencapture -x $O/$1.pdf && sips -s format png $O/$1.pdf --out $O/$1.png >/dev/null 2>&1 && rm -f $O/$1.pdf; say "screenshot $1.png"; }
check() { say "[$1] guard: `sudo $R/cursor_guard | tail -1`; overlay: `$R/ovl_gate status`; kext: `kextstat | grep -c ATIRageProLT` loaded"; }
panic() { n=`ls /Library/Logs/CrashReporter 2>/dev/null | wc -l`; say "crash logs in /Library/Logs/CrashReporter: $n"; }
sudo $R/cursor_guard fill >/dev/null; check start; panic
say "== Atlantis ${AS}s"
cd $HOME/atlantis-win && (RAGE_STATS=1 GLUTSHIM_SECS=$AS GLUTSHIM_POS="0,60" ./atlantis > $O/atlantis.out 2> $O/atlantis.stats & P=$!; sleep 15; snap atlantis_1; sleep 10; snap atlantis_2; wait $P; echo "atlantis exit $?" >> $O/summary.txt)
check after-atlantis
say "== Flurry (screensaver) ${FS}s"
RAGE_STATS=1 $SAVER > $O/flurry.out 2> $O/flurry.stats & P=$!; sleep 15; snap flurry_1; sleep 10; snap flurry_2; sleep `expr $FS - 25`; kill $P; sleep 3
check after-flurry
say "== Chess ${CS}s"
defaults write com.apple.Chess 'NSWindow Frame MBCMainWindowFrame' '100 100 630 440 0 0 1024 768'   # the saved frame may lie on the other display
open -a Chess; sleep 12; snap chess_1; sleep `expr $CS - 12`; snap chess_2; osascript -e 'tell application "Chess" to quit' >/dev/null 2>&1; sleep 3
check after-chess
say "== Sonic R ${SS}s"
cd /Applications/SonicR.app/Contents/Resources && (RAGE_STATS=1 $SONIC > $O/sonicr.out 2> $O/sonicr.stats & P=$!; sleep 25; snap sonicr_1; sleep 35; snap sonicr_2; sleep 45; snap sonicr_3; sleep `expr $SS - 105`; snap sonicr_4; kill $P; sleep 3)   # NO input: the title screen's idle timeout starts the automated race demo
check after-sonicr
panic; say DONE
