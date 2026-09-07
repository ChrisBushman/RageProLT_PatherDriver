#!/bin/sh
# deploy-and-fsprobe.sh -- rebuild the driver bundle, then run the SAFE fullscreen ATTACH probe.
# fsprobe attaches a fullscreen context (no rendering, no present) so setup_target's [st] PROBE line
# reads surf[0x178] for the FULLSCREEN surface -- the go/no-go value vs windowed's 0xffff.  Attach-only
# + the new display-associated bail => no panic.  Run as the normal user:  sh ~/deploy-and-fsprobe.sh
set -e
echo "== [1/2] rebuild + install the driver bundle (needs sudo) =="
sudo sh ~/RageProLT/install-pathb.sh
echo ""
echo "== [2/2] build + run the fullscreen attach probe (no render) =="
sh ~/run-fsprobe.sh
