#!/bin/sh
# deploy-and-flurryfs.sh -- rebuild the DRIVER BUNDLE (so driver-side fixes actually deploy), then
# build + run the fullscreen test.  Run as the normal user (NOT sudo -- the sudo is inside):
#     sh ~/deploy-and-flurryfs.sh
#
# The bundle rebuild is the step run-flurryfs.sh does NOT do; skipping it means driver fixes never
# reach the running GL driver (a stale bundle = the "why did my fix not take" trap).
set -e
echo "== [1/2] rebuilding + installing the driver bundle (needs sudo) =="
sudo sh ~/RageProLT/install-pathb.sh
echo ""
echo "== bundle installed at: =="
ls -la /System/Library/Extensions/ATIRageProGLDriver.bundle/Contents/MacOS/ATIRageProGLDriver
echo ""
echo "== [2/2] building + running the FULLSCREEN test =="
sh ~/run-flurryfs.sh
