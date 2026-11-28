#!/bin/sh
# M0 rig (runs ON the MDD): simulate a machine without VSP so DVD.framework takes the hardware-assist (IODVDBundleName) path.
#   m0_rig.sh on      back up DVD.framework's binary, patch the VSP probe symbol name ('VSPMaximumThreads' -> 'VSPMaximumThreadX'), install the stub bundle
#   m0_rig.sh off     restore the original DVD binary (byte-verified) and remove the stub bundle
#   m0_rig.sh status  show whether the system DVD binary equals the backup
# The patch only changes a string in DVD.framework (the probe's CFBundleGetFunctionPointerForName lookup then fails => VSP "unusable").
FW=/System/Library/PrivateFrameworks/DVD.framework/Versions/A/DVD
BK=$HOME/dvdfw_backup/DVD
BUN=/System/Library/Extensions/ATIRageProDVDDriver.bundle
SRC=$HOME/rage-p1/dvdbundle/ATIRageProDVDDriver.bundle
case "$1" in
on)
  mkdir -p $HOME/dvdfw_backup
  [ -f $BK ] || { cp -p $FW $BK || exit 1; }
  cmp $FW $BK >/dev/null 2>&1 || { echo "DVD binary differs from backup - refusing (run off first)"; exit 1; }
  python - <<PY || exit 1
d = open("$FW", "rb").read()
k = "VSPMaximumThreads"
assert d.count(k) == 1, d.count(k)
i = d.find(k)
open("$HOME/dvdfw_backup/DVD.patched", "wb").write(d[:i] + "VSPMaximumThreadX" + d[i+len(k):])
PY
  sudo cp $HOME/dvdfw_backup/DVD.patched $FW && sudo rm -rf $BUN && sudo cp -R $SRC $BUN && sudo chown -R root:wheel $BUN && echo "rig ON" ;;
off)
  [ -f $BK ] || { echo "no backup"; exit 1; }
  sudo cp -p $BK $FW && cmp $FW $BK && echo "DVD binary RESTORED (byte-identical)"
  sudo rm -rf $BUN; ls -d $BUN 2>/dev/null || echo "stub bundle removed" ;;
status)
  cmp $FW $BK >/dev/null 2>&1 && echo "DVD binary: ORIGINAL" || echo "DVD binary: PATCHED/DIFFERENT"
  ls -d $BUN 2>/dev/null ;;
*) echo "usage: $0 on|off|status"; exit 1 ;;
esac
