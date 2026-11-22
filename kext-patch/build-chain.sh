#!/bin/sh
# build-chain.sh -- rebuild the whole patched ATIRagePro kext chain from the STOCK kext (no author binaries needed).
#   usage: sh build-chain.sh /path/to/stock/ATIRagePro     (md5 must be 8fc97891fe7aa7605bb57e7820a4d643)
# writes ATIRagePro.patched{,2,4,5,7} into $PWD (run in a scratch dir; scripts are copied in).
# VERIFIED: output md5s match the author's binaries (patch kit received Nov 20): patched a383b2fe, patched2 05b777e9, patched4 421c9c25, patched5 3803034d, patched7 1fbe201f.
set -e
STOCK="$1"; D=`cd \`dirname "$0"\` && pwd`
[ "`md5sum "$STOCK" | cut -d' ' -f1`" = "8fc97891fe7aa7605bb57e7820a4d643" ] || { echo "not the stock Panther 10.3.9 ATIRagePro"; exit 1; }
cp "$D"/*.py . ; cp "$STOCK" ATIRagePro.orig
python3 gen_relocs.py; python3 patch0.py >/dev/null; python3 handler.py >/dev/null; python3 patch.py >/dev/null
python3 handler_mux.py >/dev/null; python3 patch4.py >/dev/null; python3 patch5.py >/dev/null; python3 patch7.py >/dev/null
md5sum ATIRagePro.patched*
# Descriptor-count fix (see patch-clientmemory-desc.sh). Applied last, to a COPY of patched7. Its md5 is not pinned here: it was verified
# functionally on hardware (the G4 runs the same one-instruction change on its installed binary), not against a reference checksum.
cp ATIRagePro.patched7 ATIRagePro.patched8; sh "$D"/patch-clientmemory-desc.sh ATIRagePro.patched8
# DMA submit selector (vertex-DMA plan): needs dma_handler.o, which must be compiled on a Panther/Tiger box (build-dma.sh) -- skipped when absent.
if [ -f "$D"/dma_handler.o ]; then cp "$D"/dma_handler.o . && python3 patch8.py; fi
# patched9 -> patched10: hardened sel-10 handler (tex_handler.c, built by build-tex.sh on a Panther box); needs tex_handler.o and _relocs.pkl alongside
[ -f tex_handler.o ] && PATCH9_SRC=ATIRagePro.patched9 python3 "$D"/patch9.py && echo "patched10 built (install this one)" || echo "no tex_handler.o: patched10 NOT built"
