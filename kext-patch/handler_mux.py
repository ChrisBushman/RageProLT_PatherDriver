import capstone, struct
# ---------------------------------------------------------------------------
# v4 MULTIPLEXED sel-10 (new_texture) handler @ 0x71e4.
# Region 0x71e4..0x73c8 (up to stock delete_texture) is ours; the mutex islands
# used here (0x7474 lock, 0x7464 unlock) live in delete_texture's tail and stay intact.
#
# StructIStructO ABI (VERIFIED): r3=this, r4=inPtr, r5=outPtr, r6=inSize.
# in = { u32 op; u32 arg; <payload> }
#   op=0 UPLOAD: arg=dst_off; payload=argb texels.  dst = this[0xb0][0xe0]+dst_off.
#   op=1 ALLOC : arg=size.  record=IOMallocPageable(0x10); LOCK; alloc_bestfit(heap,record,size,0x40);
#                UNLOCK; out={record[8]=offset, record}.        heap=this[0xb0][0x78], lock=this[0xb0][0x544]
#   op=2 FREE  : arg=record. LOCK; heap_free(heap,record); UNLOCK; IOFreePageable(record,0x10).
# The LOCK/UNLOCK around the shared-heap ops is the fix for the v3 race (stock methods self-lock too).
# ---------------------------------------------------------------------------
BASE = 0x71e4
words = [
    0x7C0802A6,  # 71e4 mflr  r0
    0xBF61FFEC,  # 71e8 stmw  r27,-0x14(r1)      ; save r27..r31
    0x90010008,  # 71ec stw   r0,8(r1)
    0x9421FFB0,  # 71f0 stwu  r1,-0x50(r1)
    0x7CBB2B78,  # 71f4 mr    r27,r5             ; r27 = outPtr
    0x83840000,  # 71f8 lwz   r28,0(r4)          ; r28 = op
    0x83A40004,  # 71fc lwz   r29,4(r4)          ; r29 = arg
    0x83C300B0,  # 7200 lwz   r30,0xb0(r3)       ; r30 = accel
    0x2C1C0000,  # 7204 cmpwi r28,0
    0x4082003C,  # 7208 bne   0x7244 (DISP2)     ; op!=0
    # --- UPLOAD (op=0) ---
    0x80FE00E0,  # 720c lwz   r7,0xe0(r30)       ; lfb
    0x7CE7EA14,  # 7210 add   r7,r7,r29          ; dst = lfb + dst_off
    0x38840008,  # 7214 addi  r4,r4,8            ; data ptr (skip op,arg)
    0x38C6FFF8,  # 7218 addi  r6,r6,-8           ; size -= 8
    0x54C6F0BE,  # 721c srwi  r6,r6,2            ; nwords
    0x2C060000,  # 7220 cmpwi r6,0
    0x4182009C,  # 7224 beq   0x72c0 (DONE)
    0x7CC903A6,  # 7228 mtctr r6
    0x80040000,  # 722c LOOP: lwz r0,0(r4)
    0x90070000,  # 7230 stw   r0,0(r7)
    0x38840004,  # 7234 addi  r4,r4,4
    0x38E70004,  # 7238 addi  r7,r7,4
    0x4200FFF0,  # 723c bdnz  0x722c (LOOP)
    0x48000080,  # 7240 b     0x72c0 (DONE)
    # --- DISP2 (op!=0) ---
    0x2C1C0002,  # 7244 cmpwi r28,2
    0x41820050,  # 7248 beq   0x7298 (FREE)
    # --- ALLOC (op=1) ---
    0x38600010,  # 724c li    r3,0x10
    0x38800010,  # 7250 li    r4,0x10
    0x4800A195,  # 7254 bl    0x113e8            ; IOMallocPageable -> r3=record
    0x7C7F1B78,  # 7258 mr    r31,r3             ; r31 = record
    0x38000000,  # 725c li    r0,0
    0x901F0008,  # 7260 stw   r0,8(r31)          ; record[8] = 0  (OOM -> offset 0)
    0x807E0544,  # 7264 lwz   r3,0x544(r30)      ; lock ptr
    0x4800020D,  # 7268 bl    0x7474             ; mutex_lock
    0x807E0078,  # 726c lwz   r3,0x78(r30)       ; heap
    0x7FE4FB78,  # 7270 mr    r4,r31             ; record
    0x7FA5EB78,  # 7274 mr    r5,r29             ; size
    0x38C00040,  # 7278 li    r6,0x40            ; align
    0x4800A309,  # 727c bl    0x11584            ; alloc_bestfit
    0x807E0544,  # 7280 lwz   r3,0x544(r30)
    0x480001E1,  # 7284 bl    0x7464             ; mutex_unlock
    0x801F0008,  # 7288 lwz   r0,8(r31)          ; offset (0 if failed)
    0x901B0000,  # 728c stw   r0,0(r27)          ; out[0] = offset
    0x93FB0004,  # 7290 stw   r31,4(r27)         ; out[1] = record
    0x4800002C,  # 7294 b     0x72c0 (DONE)
    # --- FREE (op=2) ---
    0x807E0544,  # 7298 FREE: lwz r3,0x544(r30)
    0x480001D9,  # 729c bl    0x7474             ; mutex_lock
    0x807E0078,  # 72a0 lwz   r3,0x78(r30)       ; heap
    0x7FA4EB78,  # 72a4 mr    r4,r29             ; record
    0x4800A635,  # 72a8 bl    0x118dc            ; heap_free
    0x807E0544,  # 72ac lwz   r3,0x544(r30)
    0x480001B5,  # 72b0 bl    0x7464             ; mutex_unlock
    0x7FA3EB78,  # 72b4 mr    r3,r29             ; record
    0x38800010,  # 72b8 li    r4,0x10
    0x4800A091,  # 72bc bl    0x1134c            ; IOFreePageable
    # --- DONE ---
    0x38600000,  # 72c0 DONE: li r3,0
    0x80010058,  # 72c4 lwz   r0,0x58(r1)
    0x38210050,  # 72c8 addi  r1,r1,0x50
    0x7C0803A6,  # 72cc mtlr  r0
    0xBB61FFEC,  # 72d0 lmw   r27,-0x14(r1)
    0x4E800020,  # 72d4 blr
]
blob = b''.join(struct.pack('>I', w) for w in words)
open('handler_mux.bin', 'wb').write(blob)

# expected absolute targets for the bl/branch instructions -> verify with capstone
EXPECT = {
    0x7208: ('bne',  0x7244), 0x7224: ('beq', 0x72c0), 0x723c: ('bdnz', 0x722c),
    0x7240: ('b',    0x72c0), 0x7248: ('beq', 0x7298), 0x7294: ('b',   0x72c0),
    0x7254: ('bl', 0x113e8), 0x7268: ('bl', 0x7474), 0x727c: ('bl', 0x11584),
    0x7284: ('bl', 0x7464),  0x729c: ('bl', 0x7474), 0x72a8: ('bl', 0x118dc),
    0x72b0: ('bl', 0x7464),  0x72bc: ('bl', 0x1134c),
}
md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)
print("bytes: %d (%d instrs), region 0x71e4..0x%x  (stock delete_texture @0x73c8)" %
      (len(blob), len(words), BASE+len(blob)))
assert BASE+len(blob) <= 0x73c8, "handler overruns into delete_texture!"
ok = True
for ins in md.disasm(blob, BASE):
    tgt = ''
    if ins.address in EXPECT:
        mn, want = EXPECT[ins.address]
        got = int(ins.op_str.split()[-1], 16)
        good = (got == want)
        ok = ok and good
        tgt = '  -> 0x%x %s' % (want, 'OK' if good else 'BAD got 0x%x'%got)
    print("0x%05x: %-8s %s%s" % (ins.address, ins.mnemonic, ins.op_str, tgt))
print("\nbranch/bl targets:", "ALL OK" if ok else "*** MISMATCH ***")
