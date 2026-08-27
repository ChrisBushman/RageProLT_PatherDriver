#!/bin/sh
# Conservative standalone Path B submit test.  UNLOADS our kext first so Apple's ATIRagePro kext
# owns the device (running both contends -> lockup, see the atlantis lesson), then drives the full
# validated sequence + writes one harmless SCRATCH_REG0 command + probes SAFE triggers only.
# Safe from ssh (no display takeover, no fullscreen).  Run on the Lombard:
#   sudo sh tools/submittest.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`

echo "=== ensuring our kext is UNLOADED (Apple's ATIRagePro must own the device) ==="
kextunload /tmp/Mach64Accelerator.kext 2>/dev/null
kextstat 2>/dev/null | grep -iE "Mach64|ATIRagePro" | head

# SUBMIT_SKIP adds -DSKIP_REGMAP and/or -DSKIP_TRIGGER to bisect.  DEFAULT skips the drain trigger
# (the riskiest step) so we first confirm map/write are clean on this boot; set SUBMIT_SKIP="" to
# enable the trigger once the earlier steps are proven safe.
: ${SUBMIT_SKIP="-DSKIP_TRIGGER"}
cc -O2 $SUBMIT_SKIP -o "$DIR/mach64_submittest" "$DIR/mach64_submittest.c" -framework IOKit 2>&1 \
    | grep -viE "warning:|incompatible pointer|In function" ; true

echo "=== running submittest (idx=${1:-0x13}) SUBMIT_SKIP='$SUBMIT_SKIP' ==="
"$DIR/mach64_submittest" ${1:+"$1"} 2>&1 | tee "$ROOT/submittest-out.txt"
echo "--- done ---"
