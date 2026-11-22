#!/bin/sh
# build-tex.sh -- compile tex_handler.c for patch9.py (run on a Panther/Tiger box with the native cc; -mcpu=750 is valid on the Lombard's G3 as well as the G4)
set -e
cc -Os -static -fno-builtin -fno-common -mcpu=750 -c tex_handler.c -o tex_handler.o
nm -n tex_handler.o; size tex_handler.o
