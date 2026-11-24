#!/bin/sh
# Build biosrun for the G4 (Panther): sh build-g5g4.sh   (run in this directory)
set -e
cc -O2 -std=gnu99 -DHW -w -Ilibx86emu -Ilibx86emu/include -o biosrun biosrun.c libx86emu/api.c libx86emu/decode.c libx86emu/mem.c libx86emu/ops.c libx86emu/ops2.c libx86emu/prim_ops.c -framework IOKit && echo BUILD_OK
