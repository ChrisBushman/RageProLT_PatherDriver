import struct, capstone, pickle

TEXT_BIAS = 0x33c
SRC = 'ATIRagePro.patched2'      # peek + upload; sel 11 is STILL stock delete_texture here
DST = 'ATIRagePro.patched4'
HANDLER_VADDR = 0x71e4
DEAD = 0x73c0                     # dead scratch vaddr (inside orphaned 0x73b8 island, never executed)
LO, HI = 0x71e4, 0x73c8          # our handler region (must not touch stock delete_texture @0x73c8+)

data = bytearray(open(SRC, 'rb').read())
rl = pickle.load(open('_relocs.pkl', 'rb'))
md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)

def rd(vaddr, n=4): return data[vaddr+TEXT_BIAS: vaddr+TEXT_BIAS+n]
def mdword(sel, field):  # field: 'flags'|'c0'|'c1'
    off = {'flags':0xc, 'c0':0x10, 'c1':0x14}[field]
    v = 0x174c0 + sel*0x18 + off
    return struct.unpack('>I', data[v+TEXT_BIAS:v+TEXT_BIAS+4])[0]

# --- 0. sanity: sel-10 must be StructIStructO variable; sel-11 must be STOCK (not repurposed) ---
print("=== methodDesc sanity ===")
s10 = (mdword(10,'flags'), mdword(10,'c0'), mdword(10,'c1'))
s11 = (mdword(11,'flags'), mdword(11,'c0'), mdword(11,'c1'))
print("  sel10 flags/c0/c1 =", tuple(hex(x) for x in s10), "(want 0x3,0xffffffff,0xffffffff)")
print("  sel11 flags/c0/c1 =", tuple(hex(x) for x in s11), "(want 0x4,0x1,0x0 STOCK)")
assert s10 == (3, 0xffffffff, 0xffffffff), "sel10 not StructIStructO-variable"
assert s11 == (4, 1, 0), "sel11 is NOT stock delete_texture -- refusing to build"

# --- 1. write the multiplexed handler ---
blob = open('handler_mux.bin', 'rb').read()
HEND = HANDLER_VADDR + len(blob)
foff = HANDLER_VADDR + TEXT_BIAS
print("\n=== handler @0x%x -> file 0x%x, %d bytes (ends 0x%x, delete_texture @0x73c8) ===" %
      (HANDLER_VADDR, foff, len(blob), HEND))
assert HEND <= HI
data[foff:foff+len(blob)] = blob

# --- 2. neutralize every in-region non-PAIR reloc (repoint its write address to DEAD) ---
print("\n=== neutralizing in-region relocs -> 0x%x ===" % DEAD)
moved = 0
for t in rl:
    ent = t[0]; typ = t[5]
    if typ == 1:            # PPC_RELOC_PAIR carries a value, not a location -> leave it
        continue
    W = struct.unpack('>I', data[ent:ent+4])[0]
    scattered = bool(W & 0x80000000)
    addr = (W & 0x00ffffff) if scattered else W
    if LO <= addr < HI:
        newW = (W & 0xff000000) | DEAD if scattered else DEAD
        struct.pack_into('>I', data, ent, newW)
        moved += 1
print("  moved %d reloc entries out of [0x%x,0x%x)" % (moved, LO, HI))

# --- 3. verify: no surviving reloc writes into our executed handler bytes ---
survivors = []
for t in rl:
    ent = t[0]; typ = t[5]
    if typ == 1: continue
    W = struct.unpack('>I', data[ent:ent+4])[0]
    addr = (W & 0x00ffffff) if (W & 0x80000000) else W
    if HANDLER_VADDR <= addr < HEND:
        survivors.append((hex(ent), hex(addr), typ))
print("  surviving relocs inside handler bytes:", survivors if survivors else "NONE (good)")
assert not survivors

open(DST, 'wb').write(data)

# --- 4. verify the emitted binary regions ---
print("\n=== patched4 sel-10 handler @0x71e4 (head+tail) ===")
ins = list(md.disasm(bytes(data[foff:foff+len(blob)]), HANDLER_VADDR))
for i in ins[:6] + ins[-3:]:
    print("0x%05x: %-8s %s" % (i.address, i.mnemonic, i.op_str))
print("=== stock delete_texture @0x73c8 (must be UNTOUCHED) ===")
for i in list(md.disasm(bytes(rd(0x73c8, 24)), 0x73c8))[:6]:
    print("0x%05x: %-8s %s" % (i.address, i.mnemonic, i.op_str))
print("=== mutex islands (must be zero-filled lis/ori, reloc-filled at load) ===")
for isl,name in ((0x7464,'unlock'),(0x7474,'lock')):
    d0 = list(md.disasm(bytes(rd(isl, 16)), isl))
    print("  0x%x %s: %s %s | %s %s" % (isl, name, d0[0].mnemonic, d0[0].op_str, d0[1].mnemonic, d0[1].op_str))
print("=== sel-14 peek @0x710c still intact (first 2) ===")
for i in list(md.disasm(bytes(rd(0x710c, 8)), 0x710c))[:2]:
    print("0x%05x: %-8s %s" % (i.address, i.mnemonic, i.op_str))

import hashlib
print("\nwrote", DST, "md5", hashlib.md5(bytes(data)).hexdigest())
