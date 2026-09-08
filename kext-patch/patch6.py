#!/usr/bin/env python3
# patch6.py -- v6: add op=3 (AGP_ALLOC) to our sel-10 multiplexed handler.
#   new_agp_texture@0xf64c (Shared=userclient[0x88], j=0, size, &out) -> tex obj (0xa0)
#   upload  : memcpy(tex[0x94] kernel VA, inPtr+8, size)
#   engine  : addToGART(accel, tex[0x04], &gart_off)@0x4398 -> gart_off = TEX_0_OFF
#   locking : hold accel[0x544] (mutex_lock=0x7474 / unlock=0x7464 islands) across the calls
# Insertion: ONE edit to the proven handler -- 0x7208 (bne 0x7244) -> (bne DISPATCH@0x72d8).
#            DISPATCH + AGP_ALLOC body placed in the dead stock tail [0x72d8,0x738c).
#            op0/op1/op2 (windowed textures, proven) stay byte-identical.
#            op4/free deferred (leak is fine for the first test).
import struct, os, sys

BASE = os.path.dirname(os.path.abspath(__file__))
TB = 0x33c  # __text file_off = vaddr + 0x33c
SRC = os.path.join(BASE, "ATIRagePro.patched5")
DST = os.path.join(BASE, "ATIRagePro.patched6")

# ---- tiny PPC big-endian assembler -----------------------------------------
def R(n):
    assert 0 <= n <= 31
    return n

def _d(op, D, A, imm):          # D-form (lwz/stw/addi/...)
    return (op << 26) | (D << 21) | (A << 16) | (imm & 0xFFFF)

def lwz(D, d, A):   return _d(32, D, A, d)
def stw(S, d, A):   return _d(36, S, A, d)
def stwu(S, d, A):  return _d(37, S, A, d)
def addi(D, A, s):  return _d(14, D, A, s)
def addis(D, A, s): return _d(15, D, A, s)
def li(D, s):       return _d(14, D, 0, s)
def lis(D, s):      return _d(15, D, 0, s)
def ori(A, S, u):   return (24 << 26) | (S << 21) | (A << 16) | (u & 0xFFFF)
def orr(A, S, B):   return (31 << 26) | (S << 21) | (A << 16) | (B << 11) | (444 << 1)
def mr(A, S):       return orr(A, S, S)
def cmpwi(A, imm):  return (11 << 26) | (0 << 23) | (0 << 21) | (A << 16) | (imm & 0xFFFF)
def mtctr(S):       return (31 << 26) | (S << 21) | (0x120 << 11) | (467 << 1)
def bdnz_raw(bd):   return (16 << 26) | (16 << 21) | (0 << 16) | (bd & 0xFFFC)
def bc(bo, bi, bd, lk=0): return (16 << 26) | (bo << 21) | (bi << 16) | (bd & 0xFFFC) | lk
def srwi(A, S, n):  # rlwinm A,S,32-n,n,31
    sh = (32 - n) & 0x1F; mb = n; me = 31
    return (21 << 26) | (S << 21) | (A << 16) | (sh << 11) | (mb << 6) | (me << 1)
def branch(pc, tgt, lk=0):  # b / bl
    li_ = (tgt - pc) & 0x03FFFFFC
    return (18 << 26) | li_ | lk

# ---- code: list of (op, *args); labels are ("LBL", name) -------------------
# targets that are ints are absolute vaddrs; strings are labels.
LOCK, UNLOCK = 0x7474, 0x7464
NEW_AGP, ADDGART = 0xf64c, 0x4398
DONE, ORIG_DISP = 0x72c0, 0x7244

CODE = [
    ("@", 0x72d8),
    # ---- DISPATCH ----
    ("cmpwi", 28, 3),
    ("beq",  "AGP_ALLOC"),
    ("b",    ORIG_DISP),           # op2->free / else->alloc (unchanged)
    # ---- AGP_ALLOC ----
    ("LBL", "AGP_ALLOC"),
    ("mr",   31, 3),               # r31 = this (userclient)
    ("mr",   28, 4),               # r28 = inPtr (op no longer needed)
    ("lwz",  3, 0x544, 30),        # lock
    ("bl",   LOCK),
    ("lwz",  3, 0x88, 31),         # Shared = this[0x88]
    ("li",   4, 0),                # j = 0
    ("mr",   5, 29),               # size (bytes)
    ("addi", 6, 1, 0x38),          # &out
    ("bl",   NEW_AGP),             # new_agp_texture -> r3 = tex | 0
    ("cmpwi", 3, 0),
    ("beq",  "AGP_FAIL"),
    ("mr",   31, 3),               # r31 = tex object
    ("lwz",  7, 0x94, 31),         # dst = kernel VA
    ("addi", 8, 28, 8),            # src = inPtr + 8
    ("mr",   9, 29),               # size bytes
    ("srwi", 9, 9, 2),             # -> words
    ("cmpwi", 9, 0),
    ("beq",  "AGP_GART"),
    ("mtctr", 9),
    ("LBL", "COPY"),
    ("lwz",  0, 0, 8),
    ("stw",  0, 0, 7),
    ("addi", 8, 8, 4),
    ("addi", 7, 7, 4),
    ("bdnz", "COPY"),
    ("LBL", "AGP_GART"),
    ("mr",   3, 30),               # accel
    ("lwz",  4, 0x04, 31),         # tex[0x04] = memdesc
    ("addi", 5, 1, 0x34),          # &gart_off
    ("bl",   ADDGART),             # addToGART
    ("lwz",  9, 0x34, 1),          # gart_off (engine TEX_0_OFF)
    ("lwz",  3, 0x544, 30),        # unlock
    ("bl",   UNLOCK),
    ("stw",  9, 0, 27),            # out[0] = gart_off
    ("lwz",  0, 0x54, 31),
    ("stw",  0, 4, 27),            # out[4] = token
    ("stw",  31, 8, 27),           # out[8] = tex handle
    ("b",    DONE),
    ("LBL", "AGP_FAIL"),
    ("lwz",  3, 0x544, 30),        # unlock (we still hold it)
    ("bl",   UNLOCK),
    ("li",   0, 0),
    ("stw",  0, 0, 27),            # out[0] = 0  => failure
    ("b",    DONE),
]

# ---- two-pass assemble -----------------------------------------------------
labels = {}
pc = None
# pass 1: addresses
for e in CODE:
    if e[0] == "@":
        pc = e[1]; continue
    if e[0] == "LBL":
        labels[e[1]] = pc; continue
    pc += 4

def resolve(t):
    return labels[t] if isinstance(t, str) else t

# pass 2: encode
words = []  # (addr, word)
pc = None
for e in CODE:
    op = e[0]
    if op == "@":
        pc = e[1]; continue
    if op == "LBL":
        continue
    if op == "cmpwi":  w = cmpwi(e[1], e[2])
    elif op == "beq":  w = bc(12, 2, resolve(e[1]) - pc)
    elif op == "bne":  w = bc(4, 2, resolve(e[1]) - pc)
    elif op == "b":    w = branch(pc, resolve(e[1]), 0)
    elif op == "bl":   w = branch(pc, resolve(e[1]), 1)
    elif op == "bdnz": w = bdnz_raw(resolve(e[1]) - pc)
    elif op == "mr":   w = mr(e[1], e[2])
    elif op == "li":   w = li(e[1], e[2])
    elif op == "lis":  w = lis(e[1], e[2])
    elif op == "ori":  w = ori(e[1], e[2], e[3])
    elif op == "lwz":  w = lwz(e[1], e[2], e[3])
    elif op == "stw":  w = stw(e[1], e[2], e[3])
    elif op == "stwu": w = stwu(e[1], e[2], e[3])
    elif op == "addi": w = addi(e[1], e[2], e[3])
    elif op == "addis":w = addis(e[1], e[2], e[3])
    elif op == "mtctr":w = mtctr(e[1])
    elif op == "srwi": w = srwi(e[1], e[2], e[3])
    else: raise ValueError("unknown op " + op)
    words.append((pc, w)); pc += 4

END = pc
assert END <= 0x73c8, "handler overruns stock delete_texture: 0x%x" % END

# ---- apply -----------------------------------------------------------------
d = bytearray(open(SRC, "rb").read())

# redirect: 0x7208 bne 0x7244 -> bne 0x72d8 (DISPATCH)
old = struct.unpack('>I', d[0x7208+TB:0x7208+TB+4])[0]
assert old == bc(4, 2, 0x7244 - 0x7208), "0x7208 not the expected 'bne 0x7244': %08x" % old
newredir = bc(4, 2, 0x72d8 - 0x7208)
d[0x7208+TB:0x7208+TB+4] = struct.pack('>I', newredir)

# write the DISPATCH + AGP_ALLOC body
for addr, w in words:
    d[addr+TB:addr+TB+4] = struct.pack('>I', w)

open(DST, "wb").write(d)
print("wrote %s  (handler body 0x72d8..0x%x, %d instrs; redirect @0x7208)" %
      (os.path.basename(DST), END, len(words)))
import hashlib
print("md5:", hashlib.md5(open(DST,'rb').read()).hexdigest())
