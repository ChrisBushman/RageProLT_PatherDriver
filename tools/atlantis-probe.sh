#!/bin/sh
# Foolproof Path B test: run atlantis with the -DGLD_APPLE_PROBE bundle (so the real GLEngine-driven
# bundle registers+shapes+binds a surface on Apple's ATIRagePro kext and logs the result), with the
# driver log left ON so the [apple-probe] lines are captured.  Sets APPLE_PROBE itself (INSIDE the
# script, after sudo) so there is no env var to pass through sudo.  Run on the Lombard console:
#   sudo sh tools/atlantis-probe.sh
DIR=`cd \`dirname "$0"\` && pwd`
APPLE_PROBE=1
export APPLE_PROBE
exec sh "$DIR/atlantis.sh"
