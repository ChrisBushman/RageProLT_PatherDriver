import struct, capstone

TEXT_BIAS = 0x33c
SRC = 'ATIRagePro.patched'      # start from the PEEK-patched kext (keep sel-14 peek intact)
DST = 'ATIRagePro.patched2'
HANDLER_VADDR = 0x71e4

# 3 in-region __text relocs the kext loader would apply INTO my handler, corrupting it.
# Each entry: 8 bytes = { int32 r_address; uint32 r_info }.  Neutralize by repointing r_address
# to a DEAD word in the orphaned jump-table region (0x7234+), so kld fixes up dead data, not code.
# (file_off, expected_r_address, new_dead_r_address)
RELOCS = [
    (0x1d914, 0x7208, 0x7234),   # JBSR _mutex_lock
    (0x1d904, 0x7218, 0x7238),   # HA16 jump-table hi
    (0x1d8f4, 0x7220, 0x723c),   # LO16 jump-table lo
]

data = bytearray(open(SRC, 'rb').read())

# --- verify the reloc entries match what RE found, BEFORE touching anything ---
print("=== reloc entries (verify before patch) ===")
ok = True
for foff, exp_addr, _new in RELOCS:
    r_addr, r_info = struct.unpack('>Ii', data[foff:foff+8]) if False else struct.unpack('>II', data[foff:foff+8])
    tag = 'OK' if r_addr == exp_addr else 'MISMATCH!'
    if r_addr != exp_addr: ok = False
    print("  entry@0x%05x: r_address=0x%08x r_info=0x%08x  (expect 0x%x) %s" % (foff, r_addr, r_info, exp_addr, tag))
if not ok:
    print("ABORT: reloc entries do not match RE -- not patching."); raise SystemExit(1)

# --- 1. write the handler ---
blob = open('handler.bin', 'rb').read()
foff = HANDLER_VADDR + TEXT_BIAS
print("\n=== handler: vaddr 0x%x -> file 0x%x, %d bytes ===" % (HANDLER_VADDR, foff, len(blob)))
print("  orig first 8 bytes:", data[foff:foff+8].hex())
data[foff:foff+len(blob)] = blob

# --- 2. neutralize the 3 relocs (repoint r_address to dead words) ---
print("=== neutralizing relocs ===")
for foff_r, exp_addr, new_addr in RELOCS:
    struct.pack_into('>I', data, foff_r, new_addr)
    print("  entry@0x%05x: r_address 0x%x -> 0x%x (dead)" % (foff_r, exp_addr, new_addr))

open(DST, 'wb').write(data)

# --- 3. verify: disassemble the patched handler ---
md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)
fh = HANDLER_VADDR + TEXT_BIAS
print("\n=== patched2 @0x71e4 (my handler) ===")
for ins in md.disasm(bytes(data[fh:fh+len(blob)]), HANDLER_VADDR):
    print("0x%05x: %-8s %s" % (ins.address, ins.mnemonic, ins.op_str))
# confirm the peek (sel 14) is untouched
print("=== peek @0x710c still intact ===")
fp = 0x710c + TEXT_BIAS
for ins in list(md.disasm(bytes(data[fp:fp+20]), 0x710c))[:4]:
    print("0x%05x: %-8s %s" % (ins.address, ins.mnemonic, ins.op_str))
print("\nwrote", DST)
