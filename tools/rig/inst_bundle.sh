#!/bin/bash
# sync gldriver+common to the G4, rebuild the bundle and install it
cd "$(dirname "$0")/../.."          # repo root
tar -cf - gldriver common tools/ragegl | ssh MDD 'tar xf - -C ~/rage-p1 2>/dev/null'
ssh MDD 'cd ~/rage-p1/gldriver && make clean >/dev/null; make 2>&1 | grep -E "error|built"; sudo rm -rf /System/Library/Extensions/ATIRageProGLDriver.bundle; sudo cp -R ATIRageProGLDriver.bundle /System/Library/Extensions/; sudo chown -R root:wheel /System/Library/Extensions/ATIRageProGLDriver.bundle'
