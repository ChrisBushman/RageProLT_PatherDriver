#!/usr/bin/env python3
"""patch8.py: patched8 -> patched9.  Adds the kernel-side bus-master DMA submit selector (vertex-DMA plan, docs/re-notes/vertex-dma-plan.md).

Selector 7 (GL context; stock read_buffer, unused by the plugin) becomes dma_handler (dma_handler.c, compiled by build-dma.sh).  The code does not fit
in read_buffer, so it spreads over the bodies of three unused selectors (no branch into any of them from outside; their function pointers appear only in
methodDescs):
    R1 = [0x6b3c, 0x7070)  get_surface_info (sel 6) + its islands + read_buffer (sel 7) + its islands   (1332 bytes, nothing outside branches in)
    R2 = [0x6938, 0x6aa8)  get_status (sel 4) code + its two islands (only get_status used them)               (368 bytes)
Layout: R1 = [wrapper][dma_handler][dma_validate][dma_run][stub][pad][state block]; R2 = [dma_build][gui_idle].
The asm wrapper hands the handler the address of the state block in r8 (position independent: bl/mflr).  Selectors 4 and 6 (research-only: get_status, get_surface_info)
are re-pointed at a 3-instruction stub that returns kIOReturnUnsupported, and sel 7's methodDescs entry gets output size 'variable' so the handler can return its 16-byte result.
External calls are rewritten to the kext's own long-branch islands (mutex_lock 0x6290, mutex_unlock 0x6260, IOMallocContiguous 0x2870); every relocation that the kext
loader would apply inside R1/R2 is repointed at a dead word (as patch4.py does).
"""
import struct, sys, pickle, os, hashlib
TB = 0x33c
SRC, DST = os.environ.get('PATCH8_SRC', 'ATIRagePro.patched8'), 'ATIRagePro.patched9'
OBJ = 'dma_handler.o'
R1 = (0x6b3c, 0x7070); R2 = (0x6938, 0x6aa8)
DEAD = 0x73c0
ISLAND = {'_mutex_lock': 0x6290, '_mutex_unlock': 0x6260, '_IOMallocContiguous': 0x2870}
STATE_BYTES = 16

def be(b, o): return struct.unpack('>I', b[o:o+4])[0]

# ---- parse the Mach-O object (32-bit big-endian) ----
o = open(OBJ, 'rb').read()
magic, cpu, sub, ft, ncmds, sz, fl = struct.unpack('>IiiIIII', o[:28]); assert magic == 0xfeedface
off = 28; text = None; symtab = None
for _ in range(ncmds):
    cmd, cs = struct.unpack('>II', o[off:off+8])
    if cmd == 1:
        nsec = struct.unpack('>I', o[off+48:off+52])[0]; p = off + 56
        for i in range(nsec):
            sn = o[p:p+16].rstrip(b'\0'); addr, size, foff, al, reloff, nrel = struct.unpack('>6I', o[p+32:p+56])
            if sn == b'__text': text = (addr, size, foff, reloff, nrel)
            p += 68
    if cmd == 2: symtab = struct.unpack('>4I', o[off+8:off+24])
    off += cs
taddr, tsize, tfoff, treloff, tnrel = text
code = bytearray(o[tfoff:tfoff+tsize])
symoff, nsyms, stroff, strsz = symtab
syms = []
for i in range(nsyms):
    sx, ty, se, de, va = struct.unpack('>IBBhI', o[symoff+12*i:symoff+12*i+12])
    syms.append((o[stroff+sx:o.index(b'\0', stroff+sx)].decode(), ty, se, va))
funcs = sorted((va, n) for n, ty, se, va in syms if se == 1 and n in ('_gui_idle','_dma_validate','_dma_build','_dma_run','_dma_handler'))
fend = {}
for k, (va, n) in enumerate(funcs): fend[n] = funcs[k+1][0] if k+1 < len(funcs) else tsize
fstart = {n: va for va, n in funcs}
def func_bytes(n): return bytes(code[fstart[n]:fend[n]])
print('object text %d bytes:' % tsize, {n: fend[n]-fstart[n] for n in fstart})

# external relocations in the object's text: (offset, symbol)
ext = {}
for i in range(tnrel):
    a, w = struct.unpack('>II', o[treloff+8*i:treloff+8*i+8])
    assert not (a & 0x80000000), 'scattered relocation in the handler object'
    sn = w >> 8; isext = (w >> 4) & 1; rtype = w & 0xf
    assert rtype == 3, 'unexpected relocation type %d' % rtype                         # PPC_RELOC_BR24 = 3
    if not isext:                                           # branch between this object's own functions: displacement already in the instruction
        continue
    ext[a] = syms[sn][0]
print('external call sites:', {hex(a): s for a, s in ext.items()})

# ---- layout ----
d = bytearray(open(SRC, 'rb').read())
WRAP_WORDS = 5
stub_words = [0x3c60e000, 0x606302c7, 0x4e800020]       # lis r3,0xe000 ; ori r3,r3,0x2c7 ; blr   (kIOReturnUnsupported)
gi_in_r2 = (fend['_dma_build'] - fstart['_dma_build'] + 3 & ~3) + (fend['_gui_idle'] - fstart['_gui_idle']) <= R2[1] - R2[0]
r1_order = ['_dma_handler', '_dma_validate', '_dma_run'] + ([] if gi_in_r2 else ['_gui_idle'])
place = {}                                                 # function -> final vaddr
cur = R1[0]
wrap_at = cur; cur += 4 * WRAP_WORDS
for n in r1_order:
    place[n] = cur; cur += (fend[n] - fstart[n] + 3) & ~3
state_at = (R1[1] - STATE_BYTES) & ~15
assert cur <= state_at, 'R1 overflow: code ends 0x%x, state block at 0x%x' % (cur, state_at)
r1_free = state_at - cur
place['_dma_build'] = R2[0]
r2_end = R2[0] + ((fend['_dma_build'] - fstart['_dma_build'] + 3) & ~3)
if gi_in_r2:
    place['_gui_idle'] = r2_end; r2_end += fend['_gui_idle'] - fstart['_gui_idle']
stub_at = r2_end; r2_end += 4 * len(stub_words)
assert r2_end <= R2[1], 'R2 overflow'
print('R1 code ends 0x%x (state block 0x%x, %d bytes spare); R2 ends 0x%x' % (cur, state_at, r1_free, r2_end))

def bl_disp(frm, to, link):
    dsp = to - frm
    assert -0x2000000 <= dsp < 0x2000000 and dsp % 4 == 0
    return 0x48000000 | (dsp & 0x03fffffc) | (1 if link else 0)

# ---- emit function bodies with call fixups ----
def emit(n):
    b = bytearray(func_bytes(n)); base = place[n]
    for k in range(0, len(b), 4):
        oa = fstart[n] + k; w = be(b, k)
        if oa in ext:                                       # external call: rewrite to the island
            assert (w >> 26) == 18, 'external reloc on a non-branch'
            struct.pack_into('>I', b, k, bl_disp(base + k, ISLAND[ext[oa]], w & 1))
        elif (w >> 26) == 18 and not (w & 2):               # b/bl: target inside the object?
            li = w & 0x03fffffc
            if li & 0x02000000: li -= 0x04000000
            tgt = oa + li
            if not (fstart[n] <= tgt < fend[n]):            # leaves this function: must land on another function's start
                assert tgt in fstart.values(), 'branch to the middle of another function at 0x%x' % oa
                tn = [m for m, v in fstart.items() if v == tgt][0]
                struct.pack_into('>I', b, k, bl_disp(base + k, place[tn], w & 1))
        elif (w >> 26) == 16 and not (w & 2):
            bd = w & 0xfffc
            if bd & 0x8000: bd -= 0x10000
            tgt = oa + bd
            assert fstart[n] <= tgt < fend[n], 'conditional branch leaves the function'
    return bytes(b)

def poke(vaddr, blob):
    d[vaddr+TB:vaddr+TB+len(blob)] = blob

# wipe both regions first (islands excluded): fill with `trap` so a stray jump into leftover code is loud
for lo, hi in ((R1[0], state_at + STATE_BYTES), (R2[0], R2[1])):
    for a in range(lo, hi, 4): struct.pack_into('>I', d, a+TB, 0x7fe00008)
for n in r1_order + ['_dma_build'] + (['_gui_idle'] if gi_in_r2 else []): poke(place[n], emit(n))
wrap = [0x7c0802a6, 0x48000005, 0x7d0802a6, 0x7c0803a6, 0x38080000]   # mflr r0; bl .+4; mflr r8; mtlr r0; addi r8,r8,K
wrap[4] = 0x39080000 | ((state_at - (wrap_at + 8)) & 0xffff)         # addi r8,r8,K   (r8 = &state block); 0x39080000 = addi r8,r8,0
# the handler is the next thing after the wrapper: fall through with a direct branch
wrap_words = wrap[:5]
poke(wrap_at, b''.join(struct.pack('>I', w) for w in wrap_words))
assert place['_dma_handler'] == wrap_at + 4 * WRAP_WORDS, 'wrapper must fall through into dma_handler'
poke(stub_at, b''.join(struct.pack('>I', w) for w in stub_words))
poke(state_at, b'\0' * STATE_BYTES)
for a in range(cur, state_at, 4): pass                           # spare words stay `trap`

# ---- neutralize in-region relocs (as patch4.py): repoint their write address to DEAD ----
rl = pickle.load(open('_relocs.pkl', 'rb'))
moved = 0
for t in rl:
    ent, typ = t[0], t[5]
    if typ == 1: continue
    W = be(d, ent); sc = bool(W & 0x80000000); addr = (W & 0xffffff) if sc else W
    if any(lo <= addr < hi for lo, hi in ((R1[0], R1[1]), (R2[0], R2[1]))):
        # keep the islands the surviving code still needs (R2's tail islands are outside R2 anyway); R1's own islands are being replaced
        nw = ((W & 0xff000000) | DEAD) if sc else DEAD
        struct.pack_into('>I', d, ent, nw); moved += 1
print('relocs moved to the dead word: %d' % moved)
for t in rl:
    ent, typ = t[0], t[5]
    if typ == 1: continue
    W = be(d, ent); addr = (W & 0xffffff) if (W & 0x80000000) else W
    assert not (R1[0] <= addr < R1[1] or R2[0] <= addr < R2[1]), 'a relocation still writes into a patched region (0x%x)' % addr


# ---- sel-10 ALLOC failure fix (found Nov 20 from the MDD panic analysis) ----
# The patched ALLOC (handler_mux.py) does record=IOMallocPageable(0x10), sets only record[8]=0 and calls alloc_bestfit, ignoring its result.  When the allocation
# FAILS the record is never linked into the heap, but it is returned in out[1], and the plugin's reject path FREEs it: heap_free() then reads record[0]/[4] (never
# written) as list neighbours and writes through them.  Fix: use alloc_bestfit's RETURN VALUE (r3 = 1 success / 0 failure) -- NOT the offset, because a successful
# allocation at VRAM offset 0 is legal and indistinguishable from failure by offset (my first version used the offset and freed a linked record: crash).
# On failure the record is freed here and out[1] = 0.   out[1] = record * status.
#   0x7280  lwz r3,0x544(r30)  ->  b T2      T2: mr r29,r3 ; lwz r3,0x544(r30) ; b 0x7284      (r29 = status; r29/r28 are dead after the call)
#   0x7290  stw r31,4(r27)     ->  b T3      T3: mullw r9,r31,r29 ; stw r9,4(r27) ; cmpwi r29,0 ; bne 0x72c0 ; mr r3,r31 ; li r4,0x10 ; bl IOFreePageable ; b 0x72c0
# T2/T3 live in the spare words at the end of R2.
def ppc_b(frm, to, link=0): return 0x48000000 | ((to - frm) & 0x03fffffc) | link
def ppc_bne(frm, to): return 0x40820000 | ((to - frm) & 0xfffc)
T2 = r2_end; T3 = T2 + 12
assert T3 + 32 <= R2[1], 'R2 overflow (ALLOC fix)'
t2 = [0x7C7D1B78, 0x807E0544, ppc_b(T2 + 8, 0x7284)]
t3 = [0x7D3FE9D6, 0x913B0004, 0x2C1D0000, ppc_bne(T3 + 12, 0x72c0), 0x7FE3FB78, 0x38800010, ppc_b(T3 + 24, 0x1134c, 1), ppc_b(T3 + 28, 0x72c0)]
EXPECT = {0x7280: 0x807E0544, 0x7284: ppc_b(0x7284, 0x7464, 1), 0x7288: 0x801F0008, 0x728c: 0x901B0000, 0x7290: 0x93FB0004, 0x7294: ppc_b(0x7294, 0x72c0)}
for a_, w_ in EXPECT.items(): assert be(d, a_ + TB) == w_, 'ALLOC code at 0x%x is not the expected stock-patched word (0x%08x)' % (a_, be(d, a_ + TB))
struct.pack_into('>I', d, 0x7280 + TB, ppc_b(0x7280, T2))
struct.pack_into('>I', d, 0x7290 + TB, ppc_b(0x7290, T3))
struct.pack_into('>I', d, 0x7294 + TB, 0x7fe00008)         # the now-dead word traps if ever reached
for i, w in enumerate(t2): struct.pack_into('>I', d, T2 + TB + 4 * i, w)
for i, w in enumerate(t3): struct.pack_into('>I', d, T3 + TB + 4 * i, w)
for t in rl:                                                # no live relocation may write into the edited words
    if t[5] == 1: continue
    W = be(d, t[0]); a_ = (W & 0xffffff) if (W & 0x80000000) else W
    assert not (T2 <= a_ < T3 + 32 or 0x7280 <= a_ < 0x7298), 'relocation writes into the ALLOC fix'
print('ALLOC fix: T2 at 0x%x %s ; T3 at 0x%x %s' % (T2, [hex(w) for w in t2], T3, [hex(w) for w in t3]))

# ---- methodDescs: sel 7 output size variable; sels 4 and 6 -> unsupported stub ----
# __const section: vaddr 0x16e88, file offset = vaddr + const_bias (read from the Mach-O)
base = d
magic, cpu, sub, ft, ncmds2, sz2, fl2 = struct.unpack('>IiiIIII', base[:28]); off = 28; cad = cfo = None
for _ in range(ncmds2):
    cmd, cs = struct.unpack('>II', base[off:off+8])
    if cmd == 1:
        nsec = struct.unpack('>I', base[off+48:off+52])[0]; p = off + 56
        for i in range(nsec):
            sn = base[p:p+16].rstrip(b'\0'); a_, s_, f_ = struct.unpack('>3I', base[p+32:p+44])
            if sn == b'__const': cad, cfo = a_, f_
            p += 68
    off += cs
def cfile(v): return cfo + (v - cad)
tab = 0x174c0
def entry(sel): return tab + 24 * sel          # {object, adj(0xffff), func, flags, count0, count1}
for sel, func in ((4, 0x6938), (6, 0x6b3c), (7, 0x6c50)):
    assert be(d, cfile(entry(sel) + 8)) == func, 'methodDescs entry %d does not hold 0x%x' % (sel, func)
assert be(d, cfile(entry(7) + 12)) == 3 and be(d, cfile(entry(7) + 16)) == 0xffffffff and be(d, cfile(entry(7) + 20)) == 0
struct.pack_into('>I', d, cfile(entry(7) + 8), wrap_at)               # sel 7 -> wrapper
struct.pack_into('>I', d, cfile(entry(7) + 20), 0xffffffff)           # variable output size
struct.pack_into('>I', d, cfile(entry(4) + 8), stub_at)
struct.pack_into('>I', d, cfile(entry(6) + 8), stub_at)
open(DST, 'wb').write(d)
print('wrote %s  md5 %s' % (DST, hashlib.md5(d).hexdigest()))
