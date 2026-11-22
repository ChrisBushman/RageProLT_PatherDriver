#!/usr/bin/env python3
"""patch9.py: patched9 -> patched10.  Replaces the hand-assembled multiplexed new_texture handler (GL selector 10) with the hardened C handler
(tex_handler.c, built by build-tex.sh): UPLOAD only into op-1 heap blocks, FREE only of op-1 records found in the heap, ALLOC decided by the
allocator's return value, AGP ops removed, short input/small output refused.  See tex_handler.c for the rules.

Space: A = [0x71e4, 0x73c8)  old sel-10 handler (484 bytes; 0x73c8 is delete_texture, kept)
       B = [0x7484, 0x7684)  sel 12 become_global_shared + sel 13 pageoff_texture (512 bytes; the plugin never calls them; no branch from outside;
                             their methodDescs entries are re-pointed at a 3-word stub returning kIOReturnUnsupported)
       C = [0x6a68, 0x6aa8)  spare tail of patch8's R2 (stub + the dead word that absorbs relocations)
The kext islands at 0x7440..0x7484 (IOMallocPageable at 0x7464) are kept.  Input: ATIRagePro.patched9 (env PATCH9_SRC), output ATIRagePro.patched10.
"""
import struct, sys, pickle, os, hashlib
TB = 0x33c
SRC, DST = os.environ.get('PATCH9_SRC', 'ATIRagePro.patched9'), 'ATIRagePro.patched10'
OBJ = 'tex_handler.o'
A = (0x71e4, 0x7484); B = (0x7484, 0x7684); C = (0x6a68, 0x6aa8)
STUB_AT, DEAD2, HOOK_AT = 0x6a68, 0x6a74, 0x6a78
CALL = {'_mutex_lock': 0x6290, '_mutex_unlock': 0x6260, '_IOMallocPageable': 0x113e8, '_IOFreePageable': 0x1134c, '_alloc_bestfit': 0x11584, '_heap_free': 0x118dc}
LAYOUT = {'_tex_handler': None, '_heap_find': None, '_tex_op': None, '_tex_alloc': None}
A_ORDER = ['_tex_op', '_tex_handler']; B_ORDER = ['_heap_find', '_tex_alloc']
def be(b, o): return struct.unpack('>I', b[o:o+4])[0]

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
funcs = sorted((va, n) for n, ty, se, va in syms if (ty & 0x0e) == 0x0e and n in LAYOUT)       # defined in section (N_SECT)
assert [n for _, n in funcs] and set(n for _, n in funcs) == set(LAYOUT), 'object does not define exactly %s: %s' % (sorted(LAYOUT), funcs)
fstart = {n: va for va, n in funcs}; fend = {}
for k, (va, n) in enumerate(funcs): fend[n] = funcs[k+1][0] if k+1 < len(funcs) else tsize
for n in list(fend):                                      # the object pads functions to 8 bytes with nops: do not spend kext space on them
    while fend[n] - 4 > fstart[n] and be(code, fend[n] - 4) == 0x60000000: fend[n] -= 4
size_of = lambda n: (fend[n] - fstart[n] + 3) & ~3
print('object functions:', {n: fend[n]-fstart[n] for n in fstart})
ext = {}
for i in range(tnrel):
    a, w = struct.unpack('>II', o[treloff+8*i:treloff+8*i+8])
    assert not (a & 0x80000000), 'scattered relocation in the handler object'
    sn = w >> 8; isext = (w >> 4) & 1; rtype = w & 0xf
    assert rtype == 3, 'unexpected relocation type %d' % rtype
    if isext: ext[a] = syms[sn][0]
for n in ext.values(): assert n in CALL, 'unmapped external %s' % n
print('external call sites:', {hex(a): s for a, s in ext.items()})

place = {}
for reg, order in ((A, A_ORDER), (B, B_ORDER)):
    cur = reg[0]
    for n in order: place[n] = cur; cur += size_of(n)
    assert cur <= reg[1], 'region 0x%x overflow: ends 0x%x' % (reg[0], cur)
    print('region 0x%x: code ends 0x%x (limit 0x%x)' % (reg[0], cur, reg[1]))

d = bytearray(open(SRC, 'rb').read())
def bl_disp(frm, to, link):
    dsp = to - frm; assert -0x2000000 <= dsp < 0x2000000 and dsp % 4 == 0
    return 0x48000000 | (dsp & 0x03fffffc) | (1 if link else 0)
def emit(n):
    b = bytearray(code[fstart[n]:fend[n]]); base = place[n]
    for k in range(0, len(b), 4):
        oa = fstart[n] + k; w = be(b, k)
        if oa in ext:
            assert (w >> 26) == 18
            struct.pack_into('>I', b, k, bl_disp(base + k, CALL[ext[oa]], w & 1))
        elif (w >> 26) == 18 and not (w & 2):
            li = w & 0x03fffffc
            if li & 0x02000000: li -= 0x04000000
            tgt = oa + li
            if not (fstart[n] <= tgt < fend[n]):
                assert tgt in fstart.values(), 'branch to the middle of another function at 0x%x' % oa
                tn = [m for m, v in fstart.items() if v == tgt][0]
                struct.pack_into('>I', b, k, bl_disp(base + k, place[tn], w & 1))
        elif (w >> 26) == 16 and not (w & 2):
            bd = w & 0xfffc
            if bd & 0x8000: bd -= 0x10000
            assert fstart[n] <= oa + bd < fend[n], 'conditional branch leaves the function'
    return bytes(b)
def poke(v, blob): d[v+TB:v+TB+len(blob)] = blob

# sanity: the islands this code branches to really are what we think
def word(v): return be(d, v + TB)
# the call targets, cross-checked against the code being replaced (patched9's sel-10 handler): IOMallocPageable(0x10,0x10) is `bl 0x113e8`, the island
# at 0x7474 is mutex_lock and 0x7464 is mutex_unlock (NOT IOMallocPageable: wiring 0x7464 there called unlock(0x10) and panicked the MDD, Nov 20)
def tgt_of(v):
    w = word(v); assert w >> 26 == 18 and not w & 2, 'not a bl at 0x%x' % v
    li = w & 0x03fffffc
    if li & 0x02000000: li -= 0x04000000
    return v + li
assert tgt_of(0x7254) == 0x113e8 and word(0x724c) == 0x38600010 and word(0x7250) == 0x38800010, 'old IOMallocPageable call site is not as analysed'
assert tgt_of(0x7268) == 0x7474 and tgt_of(0x7284) == 0x7464, 'old mutex islands moved'
assert CALL['_IOMallocPageable'] == 0x113e8 and CALL['_IOFreePageable'] == 0x1134c
assert word(0x6290) >> 26 in (15, 18) and word(0x6260) >> 26 in (15, 18), 'mutex islands moved'
assert word(0x6290) >> 26 in (15, 18) and word(0x6260) >> 26 in (15, 18), 'mutex islands moved'
assert word(0x73c8) == 0x7c0802a6, 'delete_texture does not start at 0x73c8'
assert word(0x7484) == 0x7c0802a6 and word(0x758c) == 0x7c0802a6 and word(0x7684) == 0x7c0802a6, 'sel 12/13 bounds are not as analysed'

for lo, hi in ((A[0], A[1]), (B[0], B[1]), (C[0], C[1])):
    for a in range(lo, hi, 4): struct.pack_into('>I', d, a + TB, 0x7fe00008)       # trap fill
for n in place: poke(place[n], emit(n))
stub = [0x3c60e000, 0x606302c7, 0x4e800020]                                       # lis r3,0xe000 ; ori r3,r3,0x2c7 ; blr
poke(STUB_AT, b''.join(struct.pack('>I', w) for w in stub))

# relocations that would write into any of the regions (or the old dead word 0x73c0, now code) -> the new dead word
rl = pickle.load(open('_relocs.pkl', 'rb'))
regions = (A, B, C)
moved = 0
for t in rl:
    ent, typ = t[0], t[5]
    if typ == 1: continue
    W = be(d, ent); sc = bool(W & 0x80000000); addr = (W & 0xffffff) if sc else W
    if any(lo <= addr < hi for lo, hi in regions) and addr != DEAD2:
        struct.pack_into('>I', d, ent, ((W & 0xff000000) | DEAD2) if sc else DEAD2); moved += 1
print('relocs moved to the new dead word 0x%x: %d' % (DEAD2, moved))
for t in rl:
    if t[5] == 1: continue
    W = be(d, t[0]); addr = (W & 0xffffff) if (W & 0x80000000) else W
    assert addr == DEAD2 or not any(lo <= addr < hi for lo, hi in regions), 'a relocation still writes into a patched region (0x%x)' % addr

# methodDescs: sel 12/13 -> stub (sel 10 still points at 0x71e4)
magic, cpu, sub, ft, ncmds2, sz2, fl2 = struct.unpack('>IiiIIII', d[:28]); off = 28; cad = cfo = None
for _ in range(ncmds2):
    cmd, cs = struct.unpack('>II', d[off:off+8])
    if cmd == 1:
        nsec = struct.unpack('>I', d[off+48:off+52])[0]; p = off + 56
        for i in range(nsec):
            sn = d[p:p+16].rstrip(b'\0'); a_, s_, f_ = struct.unpack('>3I', d[p+32:p+44])
            if sn == b'__const': cad, cfo = a_, f_
            p += 68
    off += cs
cfile = lambda v: cfo + (v - cad)
tab = 0x174c0; entry = lambda sel: tab + 24 * sel
for sel, func in ((10, 0x71e4), (11, 0x73c8), (12, 0x7484), (13, 0x758c)):
    assert be(d, cfile(entry(sel) + 8)) == func, 'methodDescs entry %d does not hold 0x%x' % (sel, func)
struct.pack_into('>I', d, cfile(entry(10) + 8), place['_tex_handler'])      # sel 10 -> the new dispatcher
struct.pack_into('>I', d, cfile(entry(11) + 8), STUB_AT)      # stock delete_texture: nothing creates stock textures any more (sel 10 is ours) and nothing calls it
struct.pack_into('>I', d, cfile(entry(12) + 8), STUB_AT)
struct.pack_into('>I', d, cfile(entry(13) + 8), STUB_AT)
# clientClose hook: at 0x54a8 (`lwz r0,0x110(r3)`, r31 = this, frame built) branch to a trampoline that runs the reaper then replays that instruction
assert word(0x54a8) == 0x80030110 and word(0x54a0) == 0x7c7f1b78 and word(0x54a4) >> 16 == 0x9421, 'clientClose is not as analysed'
hook = [0x7fe3fb78, 0x38800000, 0x38a00000, 0x3cc0ffff, 0x60c6fffe, 0x38e00000, 0, 0x7fe3fb78, 0x80030110, 0]
hook[6] = bl_disp(HOOK_AT + 6 * 4, place['_tex_handler'], 1)              # bl tex_handler(this, NULL, NULL, REAP_MAGIC, NULL)
hook[9] = 0x48000000 | ((0x54ac - (HOOK_AT + 9 * 4)) & 0x03fffffc)       # b 0x54ac
assert HOOK_AT + 4 * len(hook) <= C[1], 'hook does not fit'
poke(HOOK_AT, b''.join(struct.pack('>I', w) for w in hook))
struct.pack_into('>I', d, 0x54a8 + TB, 0x48000000 | ((HOOK_AT - 0x54a8) & 0x03fffffc))   # b HOOK
open(DST, 'wb').write(d)
print('wrote %s  md5 %s' % (DST, hashlib.md5(d).hexdigest()))
