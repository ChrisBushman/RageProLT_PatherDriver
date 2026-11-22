#!/bin/sh
# verify-kext-patches.sh <ATIRagePro Mach-O binary>
# Exit 0 only if all three patches this driver depends on are present. Checks fixed file offsets (the __text section starts at file
# offset 0x33c = 828, so file offset = vaddr + 0x33c):
#   1. peek patch        vaddr 0x710c  words 74808000 4082000c 7c03202e   (selector 14 = kernel word read; needed for the render target)
#   2. hardened new_texture (selector 10)  vaddr 0x71e4 words 7c400026 2f830003 7c0802a6 bf01ffe0 (tex_op/tex_handler) and vaddr 0x7484 words 7c400026 39200000 90410004 80430008 (heap_find); the clientClose hook is `b 0x6a78` at 0x54a8; see kext-patch/tex_handler.c
#                        (the stock new_texture prologue there is 7c0802a6 bfa1fff4 ...; this signature was read from the binary that passed the
#                        hardware suite, it is not derived from patch.py's disassembly)
#   3. descriptor fix    vaddr 0x5ad8  word  38400000 (li r2,0)   (stock: 80410048 = lwz r2,0x48(r1) -> intermittent kernel panic)
# The stock binary has md5 8fc97891fe7aa7605bb57e7820a4d643; any other unpatched build fails check 1.
F="$1"; [ -f "$F" ] || { echo "usage: $0 ATIRagePro-binary"; exit 2; }
w() { hexdump -s $(( $1 + 828 )) -n $2 -e "$(i=0; n=$(( $2 / 4 )); while [ $i -lt $n ]; do printf '1/4 "%%08x" '; [ $((i+1)) -lt $n ] && printf '" " '; i=$((i+1)); done)" "$F"; }
bad=0
chk() { got=$(w $2 $3); if [ "$got" = "$4" ]; then echo "ok    $1"; else echo "MISSING  $1 (got '$got', want '$4')"; bad=1; fi; }
chk "peek patch (selector 14)"            $((0x710c)) 12 "74808000 4082000c 7c03202e"
chk "hardened new_texture: tex_alloc (selector 10)" $((0x71e4)) 16 "7c400026 2f830003 7c0802a6 bf01ffe0"
chk "hardened new_texture: dispatcher (selector 10)" $((0x7484)) 16 "7c400026 39200000 90410004 80430008"
chk "clientClose hook (releases a dead client's texture-heap records)" $((0x54a8)) 4 "480015d0"
chk "descriptor-count fix (clientMemoryForType)" $((0x5ad8)) 4 "38400000"
[ $bad = 0 ] && echo "all patches present" || echo "THIS KEXT MUST NOT BE USED WITH THE DRIVER (see kext-patch/README.md, patch-clientmemory-desc.sh)"
exit $bad
