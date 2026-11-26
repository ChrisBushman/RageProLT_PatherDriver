#!/bin/sh
defaults -currentHost write com.apple.screensaver moduleName Flurry
defaults -currentHost write com.apple.screensaver modulePath "/System/Library/Screen Savers/Flurry.saver"
export MACH64_GLD_FSYNC=1 MACH64_GLD_APPEND=1
E=/System/Library/Frameworks/ScreenSaver.framework/Versions/A/Resources/ScreenSaverEngine.app/Contents/MacOS/ScreenSaverEngine
$E > $HOME/saver.out 2>&1 &
P=$!
echo "saver pid $P"
sleep ${1:-40}
kill $P
sleep 2
echo killed
