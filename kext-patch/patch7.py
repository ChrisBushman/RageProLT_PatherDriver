#!/usr/bin/env python3
# patch7.py -- v7: op=3 (AGP_ALLOC, alloc-only) + op=5 (AGP_UPLOAD, chunked) on the sel-10 handler.
#   v6's op=3 shipped 16KB of texels inline -> MIG_ARRAY_TOO_LARGE (0xfffffecd) before the handler
#   even ran.  Fix mirrors the proven op=0: ALLOC and UPLOAD are separate, and the upload is chunked
#   (<=8KB/call) by the driver.
#   op=3 AGP_ALLOC: in={3,size}(8B) -> new_agp_texture(Shared=uc[0x88],0,size,&out)@0xf64c,
#                   addToGART(accel,tex[4],&gart)@0x4398; out={gart, token=tex[0x54], kva=tex[0x94],
#                   handle=tex}.  Held under accel[0x544] (lock 0x7474/unlock 0x7464).
#   op=5 AGP_UPLOAD: in={5, handle, dst_off, argb[]} -> memcpy(handle[0x94]+dst_off, argb, n).  No lock
#                   (writes only our own GART buffer).
#   ONE edit to the proven handler (0x7208 bne 0x7244 -> bne 0x72d8); DISPATCH+bodies in the dead stock
#   tail [0x72d8,0x73c8).  op0/op1/op2 (windowed) byte-identical to v5.  Built from patched5.
import struct, os, hashlib

BASE = os.path.dirname(os.path.abspath(__file__))
TB = 0x33c
SRC = os.path.join(BASE, "ATIRagePro.patched5")
DST = os.path.join(BASE, "ATIRagePro.patched7")

def _d(op, D, A, imm): return (op << 26) | (D << 21) | (A << 16) | (imm & 0xFFFF)
def lwz(D, d, A):   return _d(32, D, A, d)
def stw(S, d, A):   return _d(36, S, A, d)
def addi(D, A, s):  return _d(14, D, A, s)
def li(D, s):       return _d(14, D, 0, s)
def orr(A, S, B):   return (31 << 26) | (S << 21) | (A << 16) | (B << 11) | (444 << 1)
def mr(A, S):       return orr(A, S, S)
def add(D, A, B):   return (31 << 26) | (D << 21) | (A << 16) | (B << 11) | (266 << 1)
def cmpwi(A, imm):  return (11 << 26) | (A << 16) | (imm & 0xFFFF)
def mtctr(S):       return (31 << 26) | (S << 21) | (0x120 << 11) | (467 << 1)
def bdnz_raw(bd):   return (16 << 26) | (16 << 21) | (0 << 16) | (bd & 0xFFFC)
def bc(bo, bi, bd, lk=0): return (16 << 26) | (bo << 21) | (bi << 16) | (bd & 0xFFFC) | lk
def srwi(A, S, n):
    sh = (32 - n) & 0x1F
    return (21 << 26) | (S << 21) | (A << 16) | (sh << 11) | (n << 6) | (31 << 1)
def branch(pc, tgt, lk=0): return (18 << 26) | ((tgt - pc) & 0x03FFFFFC) | lk

LOCK, UNLOCK, NEW_AGP, ADDGART = 0x7474, 0x7464, 0xf64c, 0x4398
DONE, ORIG_DISP = 0x72c0, 0x7244

CODE = [
    ("@", 0x72d8),
    # ---- DISPATCH (op!=0; op==2 handled by orig at 0x7244) ----
    ("cmpwi", 28, 3), ("beq", "AGP_ALLOC"),
    ("cmpwi", 28, 5), ("beq", "AGP_UPLOAD"),
    ("b", ORIG_DISP),
    # ---- op=3 AGP_ALLOC (alloc only) ----
    ("LBL", "AGP_ALLOC"),
    ("mr", 31, 3),                       # this
    ("lwz", 3, 0x544, 30), ("bl", LOCK),
    ("lwz", 3, 0x88, 31), ("li", 4, 0), ("mr", 5, 29), ("addi", 6, 1, 0x38),
    ("bl", NEW_AGP),                     # -> r3 = tex | 0
    ("cmpwi", 3, 0), ("beq", "AGP_FAIL"),
    ("mr", 31, 3),                       # tex
    ("mr", 3, 30), ("lwz", 4, 0x04, 31), ("addi", 5, 1, 0x34),
    ("bl", ADDGART),                     # gart_off -> [r1+0x34]
    ("lwz", 9, 0x34, 1),
    ("lwz", 3, 0x544, 30), ("bl", UNLOCK),
    ("stw", 9, 0, 27),                   # out[0] = gart_off
    ("lwz", 0, 0x54, 31), ("stw", 0, 4, 27),    # out[1] = token
    ("lwz", 0, 0x94, 31), ("stw", 0, 8, 27),    # out[2] = kva
    ("stw", 31, 0xc, 27),                # out[3] = handle
    ("b", DONE),
    ("LBL", "AGP_FAIL"),
    ("lwz", 3, 0x544, 30), ("bl", UNLOCK),
    ("li", 0, 0), ("stw", 0, 0, 27),
    ("b", DONE),
    # ---- op=5 AGP_UPLOAD (chunk): in={5,handle,dst_off,argb[]} ----
    ("LBL", "AGP_UPLOAD"),
    ("lwz", 9, 8, 4),                    # dst_off = in[2]
    ("lwz", 7, 0x94, 29),                # kva = handle[0x94]  (r29=in[1]=handle)
    ("add", 7, 7, 9),                    # dst = kva + dst_off
    ("addi", 8, 4, 12),                  # src = inPtr + 12
    ("addi", 6, 6, -12),                 # inSize - 12
    ("srwi", 6, 6, 2),                   # words
    ("cmpwi", 6, 0), ("beq", "AGP_UPL_DONE"),
    ("mtctr", 6),
    ("LBL", "AGP_UPL_LOOP"),
    ("lwz", 0, 0, 8), ("stw", 0, 0, 7), ("addi", 8, 8, 4), ("addi", 7, 7, 4),
    ("bdnz", "AGP_UPL_LOOP"),
    ("LBL", "AGP_UPL_DONE"),
    ("b", DONE),
]

labels = {}; pc = None
for e in CODE:
    if e[0] == "@": pc = e[1]
    elif e[0] == "LBL": labels[e[1]] = pc
    else: pc += 4

def rs(t): return labels[t] if isinstance(t, str) else t
words = []; pc = None
for e in CODE:
    op = e[0]
    if op == "@": pc = e[1]; continue
    if op == "LBL": continue
    if   op == "cmpwi": w = cmpwi(e[1], e[2])
    elif op == "beq":   w = bc(12, 2, rs(e[1]) - pc)
    elif op == "bne":   w = bc(4, 2, rs(e[1]) - pc)
    elif op == "b":     w = branch(pc, rs(e[1]), 0)
    elif op == "bl":    w = branch(pc, rs(e[1]), 1)
    elif op == "bdnz":  w = bdnz_raw(rs(e[1]) - pc)
    elif op == "mr":    w = mr(e[1], e[2])
    elif op == "li":    w = li(e[1], e[2])
    elif op == "add":   w = add(e[1], e[2], e[3])
    elif op == "lwz":   w = lwz(e[1], e[2], e[3])
    elif op == "stw":   w = stw(e[1], e[2], e[3])
    elif op == "addi":  w = addi(e[1], e[2], e[3])
    elif op == "mtctr": w = mtctr(e[1])
    elif op == "srwi":  w = srwi(e[1], e[2], e[3])
    else: raise ValueError(op)
    words.append((pc, w)); pc += 4

END = pc
assert END <= 0x73c8, "overrun 0x%x" % END

d = bytearray(open(SRC, "rb").read())
old = struct.unpack('>I', d[0x7208+TB:0x7208+TB+4])[0]
assert old == bc(4, 2, 0x7244 - 0x7208), "0x7208 mismatch %08x" % old
d[0x7208+TB:0x7208+TB+4] = struct.pack('>I', bc(4, 2, 0x72d8 - 0x7208))
for addr, w in words:
    d[addr+TB:addr+TB+4] = struct.pack('>I', w)
open(DST, "wb").write(d)
print("wrote %s  (body 0x72d8..0x%x, %d instrs)" % (os.path.basename(DST), END, len(words)))
print("md5:", hashlib.md5(open(DST, 'rb').read()).hexdigest())
