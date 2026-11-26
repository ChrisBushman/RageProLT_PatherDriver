#!/bin/sh
# Run as root ON the Panther G4 from the unpacked package dir. Reversible: revert-tgl.sh (needs backup dir in /Library/TGL-backup/LAST).
set -e
E=/System/Library/Extensions; P=$(cd "$(dirname "$0")" && pwd)
B=/Library/TGL-backup/$(date +%Y%m%d-%H%M%S)
grep -q '<string>10\.3\.9</string>' /System/Library/CoreServices/SystemVersion.plist || { echo "not 10.3.9"; exit 1; }
[ -e /Library/TGL ] && { echo "/Library/TGL exists"; exit 1; }
for b in GeForceFXGLDriver GeForce3GLDriver GeForce2MXGLDriver; do [ -e $E/$b.bundle ] && { echo "$E/$b.bundle present"; exit 1; }; done
mkdir -p $B; echo $B > /Library/TGL-backup/LAST
cp -p $E/GeForce.kext/Contents/MacOS/GeForce $B/GeForce.bin
cp -p $E/compat104.kext/Contents/MacOS/compat104 $B/compat104.bin
cat $P/GeForce.bin > $E/GeForce.kext/Contents/MacOS/GeForce
cat $P/compat104.bin > $E/compat104.kext/Contents/MacOS/compat104
mkdir -p /Library/TGL/Frameworks /Library/TGL/NoLocal/Resources
cp -R $P/Frameworks/OpenGL.framework /Library/TGL/Frameworks/
cp $P/tgl-run /Library/TGL/tgl-run; chmod 755 /Library/TGL/tgl-run
chown -R root:wheel /Library/TGL; chmod -R go-w /Library/TGL
for b in GeForceFXGLDriver GeForce3GLDriver GeForce2MXGLDriver; do
  cp -R $P/bundles/$b.bundle $E/$b.bundle; chown -R root:wheel $E/$b.bundle
  find $E/$b.bundle -type d -exec chmod 755 {} \; ; find $E/$b.bundle -type f -exec chmod 644 {} \;
done
touch $E
echo "INSTALLED; backup $B. Reboot to load the patched kexts."
