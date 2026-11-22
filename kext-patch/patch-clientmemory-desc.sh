#!/bin/sh
# Fix for the intermittent kernel panic in ATIRagePro::submit_buffer (found by static analysis, Nov 20).
#
# IOATIRProGLContext::clientMemoryForType (type 1) builds a VendorCommandDescriptor on its stack and calls
# ctx->process_command_buffer(&desc).  For ATIR3GLContext that method is `li r3,0; blr` (kext+0x13990): it NEVER fills the
# descriptor.  The caller then reads desc.count (`lwz r2,0x48(r1)` at kext+0x5ad8) and, if non-zero, calls
# accel->submit_commands(&desc) -> submit_buffer(desc.buf, ..., desc.count).  desc is uninitialised kernel stack, so whenever
# stale stack residue makes the count non-zero the kernel PIO-drains garbage: a wild register offset or NULL buffer ->
# 0x300 data-access panic.  Panic backtraces end ...submit_buffer <- submit_commands(0x12710) <- clientMemoryForType(0x5b10).
# The count is meant to be 0 for a 3D context, so replace the read with `li r2,0` (0x38400000): identical intended behaviour.
#
# usage: patch-clientmemory-desc.sh /path/to/ATIRagePro   (Mach-O object; __text file offset 828; patch at 828+0x5ad8 = 24084)
set -e
F="$1"; OFF=24084
[ -f "$F" ] || { echo "usage: $0 ATIRagePro-binary"; exit 1; }
cur=$(hexdump -s $OFF -n 4 -e '4/1 "%02x"' "$F")
[ "$cur" = "80410048" ] || { echo "unexpected bytes at $OFF: $cur (want 80410048 = lwz r2,0x48(r1)); not patching"; exit 1; }
printf '\070\100\000\000' | dd of="$F" bs=1 seek=$OFF conv=notrunc 2>/dev/null
echo "patched: $(hexdump -s $OFF -n 4 -e '4/1 "%02x"' "$F")"
