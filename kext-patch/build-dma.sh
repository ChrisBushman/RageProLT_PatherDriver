#!/bin/sh
# build-dma.sh -- compile dma_handler.c to the PowerPC object that patch8.py splices into the kext.  Run on a Panther/Tiger box with the native cc (the MDD).
#   -mcpu=750: valid on the G3 (Lombard) as well as the G4; -static: direct `bl _sym` (relocated by patch8.py to the kext's own islands); -Os: it has to fit.
set -e
cc -Os -static -fno-builtin -fno-common -mcpu=750 -c dma_handler.c -o dma_handler.o
nm -n dma_handler.o; size dma_handler.o
