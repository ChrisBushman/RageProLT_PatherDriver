import struct, capstone, hashlib

# v5 = v4 (patched4: peek + sel-10 multiplex allocator) + enlarge the PIO command buffer 4KB -> 64KB.
# Two sites, both `li r,0x1000` -> `lis r,1` (0x10000), so the whole geometry ceiling rises ~15 -> ~200
# tris/frame with ONE present/frame.  RE-verified (2026-09-07): sizing flows through allocAllContextBuffers'
# single size arg (grows cmd buffer + both staging buffers) + the per-map capacity re-init.
SRC = 'ATIRagePro.patched4'
DST = 'ATIRagePro.patched5'
TEXT_BIAS = 0x33c

# (file_off, old4bytes, new4bytes, vaddr, desc)
SITES = [
    (0x054a0, b'\x38\x80\x10\x00', b'\x3c\x80\x00\x01', 0x5164, 'allocAllContextBuffers size arg: li r4,0x1000 -> lis r4,1'),
    (0x06320, b'\x38\xa0\x10\x00', b'\x3c\xa0\x00\x01', 0x5fe4, 'type-2 map capacity re-init: li r5,0x1000 -> lis r5,1'),
]

data = bytearray(open(SRC, 'rb').read())
md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)

print("=== verify current bytes (must match orig `li r,0x1000`) ===")
ok = True
for foff, old, new, vaddr, desc in SITES:
    cur = bytes(data[foff:foff+4])
    tag = 'OK' if cur == old else 'MISMATCH'
    if cur != old: ok = False
    print("  @0x%05x (vaddr 0x%x): %s (expect %s) %s -- %s" % (foff, vaddr, cur.hex(), old.hex(), tag, desc))
if not ok:
    print("ABORT: site bytes don't match -- refusing to patch"); raise SystemExit(1)

for foff, old, new, vaddr, desc in SITES:
    data[foff:foff+4] = new

open(DST, 'wb').write(data)

print("\n=== patched5 disasm at each site ===")
for foff, old, new, vaddr, desc in SITES:
    for ins in md.disasm(bytes(data[foff:foff+4]), vaddr):
        print("  0x%05x: %-8s %s" % (ins.address, ins.mnemonic, ins.op_str))

# body capacity the kext will now advertise: (0x10000 - 0x20)/4
cap = (0x10000 - 0x20) // 4
print("\nnew body capacity = %d dwords (%d {reg,val} pairs); submit_buffer cap = %d dwords (%s)" %
      (cap, cap // 2, 0x4000, 'SAFE' if cap <= 0x4000 else 'OVER CAP!'))
assert cap <= 0x4000
# sanity: v4 regions untouched (sel-10 handler head @0x71e4, peek @0x710c)
print("=== v4 regions still intact ===")
for v in (0x710c, 0x71e4, 0x73c8):
    for ins in list(md.disasm(bytes(data[v+TEXT_BIAS:v+TEXT_BIAS+4]), v))[:1]:
        print("  0x%05x: %-8s %s" % (ins.address, ins.mnemonic, ins.op_str))
print("\nwrote", DST, "md5", hashlib.md5(bytes(data)).hexdigest())
