import capstone, struct
# VRAM-upload handler replacing new_texture @0x71e4.
# StructIStructO ABI (VERIFIED): r3=this, r4=structInPtr, r5=structOutPtr, r6=structInSize.
#   (earlier BUG: used r5 as the size -> r5 is the OUTPUT pointer, huge -> runaway memcpy -> crash.)
# struct in = { u32 dst_off; u32 argb_texels[] }.  dst = this[0xb0][0xe0] + dst_off.
# Bounds-guarded: skip if dst_off >= 0x800000 (VRAM size) so a bad offset can't scribble kernel mem.
words = [
    0x81040000,  # 71e4 lwz   r8, 0(r4)        ; dst_off = struct[0]
    0x7500FF80,  # 71e8 andis. r0, r8, 0xff80  ; dst_off & 0xff800000  (>= 0x800000?)
    0x4082003C,  # 71ec bne   0x7228 (done)    ; out of VRAM range -> skip
    0x80E300B0,  # 71f0 lwz   r7, 0xb0(r3)     ; accel = this[0xb0]
    0x80E700E0,  # 71f4 lwz   r7, 0xe0(r7)     ; lfb = accel[0xe0]
    0x7CE74214,  # 71f8 add   r7, r7, r8       ; dst = lfb + dst_off
    0x38840004,  # 71fc addi  r4, r4, 4        ; data = struct + 4
    0x38C6FFFC,  # 7200 addi  r6, r6, -4       ; inSize -= 4  (drop header word)
    0x54C6F0BE,  # 7204 srwi  r6, r6, 2        ; words = inSize>>2
    0x2C060000,  # 7208 cmpwi r6, 0
    0x4182001C,  # 720c beq   0x7228 (done)
    0x7CC903A6,  # 7210 mtctr r6
    0x80040000,  # 7214 loop: lwz r0, 0(r4)
    0x90070000,  # 7218       stw r0, 0(r7)
    0x38840004,  # 721c       addi r4, r4, 4
    0x38E70004,  # 7220       addi r7, r7, 4
    0x4200FFF0,  # 7224       bdnz loop (0x7214)
    0x38600000,  # 7228 done: li r3, 0
    0x4E800020,  # 722c blr
]
blob = b''.join(struct.pack('>I', w) for w in words)
open('handler.bin', 'wb').write(blob)
md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)
print("bytes: %d (%d instrs)" % (len(blob), len(words)))
for ins in md.disasm(blob, 0x71e4):
    print("0x%05x: %-8s %s" % (ins.address, ins.mnemonic, ins.op_str))
