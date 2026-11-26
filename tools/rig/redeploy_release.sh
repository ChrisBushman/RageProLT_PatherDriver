#!/bin/bash
# usage: redeploy_release.sh  - sync the tree to the G4, build the RELEASE kext (no debug selectors), checkfp, reload it.  Use for release verification.
cd "$(dirname "$0")/../.."          # repo root
tar --exclude=docs --exclude=win2k --exclude=.git --exclude=build -cf - . | ssh MDD 'tar xf - -C ~/rage-p1 2>/dev/null'
ssh MDD "P=\$(ps auxww | grep '[r]agelt daemon' | awk '{print \$2}' | tail -1); [ -n \"\$P\" ] && sudo kill \$P 2>/dev/null; sleep 1; sudo kextunload -b com.local.ATIRageProLT 2>&1 | tail -1; cd ~/rage-p1/kext && sudo rm -rf ATIRageProLT.kext && make clean >/dev/null 2>&1; make kext 2>&1 | grep -E 'error|warning: unused|built'; make checkfp | tail -1; sudo chown -R root:wheel ATIRageProLT.kext; sudo kextload ATIRageProLT.kext 2>&1 | tail -1; nm ATIRageProLT | grep -c 'selRegDump\|ovlB0Ejj\|RageLTDevice6regDump'" 2>&1 | grep -v '^g++\|^cc '
