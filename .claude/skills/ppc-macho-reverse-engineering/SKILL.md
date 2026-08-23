---
name: ppc-macho-reverse-engineering
description: Reverse-engineer an old PowerPC Mach-O binary (e.g. a Panther-era driver bundle, dylib, or kext) to recover a private ABI — symbols, function boundaries, dispatch tables, calling conventions. Use when disassembling classic PPC OS X binaries where modern otool fails, including reconstructing PIC-relative address loads and dispatch-table layouts.
---

# Reverse-engineering classic PPC Mach-O binaries

Techniques from recovering the Panther OpenGL "GLD" plugin ABI out of a shipping
`ATIRage128GLDriver.bundle` (Mach-O bundle, ppc).

## Modern otool fails; nm + capstone work

`llvm-otool -tv` (current macOS) often errors on old 32-bit PPC Mach-O with
`"symbol table extends beyond the end of the file"` — an llvm strictness bug, NOT
truncation (verify with `otool -h`: a valid `0xfeedface`, cputype 18 = PPC,
filetype 8 = MH_BUNDLE means the file is fine). Work around it:

- `nm <bin>` reads the symbol table fine — use it for the export/symbol map.
- Disassemble with **capstone** directly on the raw bytes:
  ```python
  from capstone import Cs, CS_ARCH_PPC, CS_MODE_BIG_ENDIAN, CS_MODE_32
  md = Cs(CS_ARCH_PPC, CS_MODE_BIG_ENDIAN + CS_MODE_32)   # PPC is big-endian!
  ```
  Install into a venv (`python -m venv`; system pip is often externally-managed).
  Do NOT name your script `dis.py` — it shadows capstone's stdlib `dis` import
  (circular-import crash).

## The offset trick: in `__text`, file offset often == vaddr

Check the `__text` section header (`otool -l` works even when `-tv` doesn't):
if `addr == offset` (e.g. addr 0x13cc, offset 0x13cc), then **a symbol at vaddr V
is at file offset V** — read `data[V:V+n]` and disassemble. No address math.

## Stripped binaries: exports are your only landmarks

A shipping bundle is usually stripped of local symbols — you get only the
exported functions (here 55 `gld*`). Consecutive export addresses bound each
function; disassemble from an export until `blr` (`0x4e800020`). Internal helper
functions are unnamed — refer to them by address and infer their role from call
sites.

## Reconstruct PIC-relative address loads (the key to data/dispatch tables)

PPC PIC code computes addresses relative to a base register set from the link
register after a `bcl`/`bdnzl $+4`:

```
bdnzl 0x60bb4          ; sets LR = 0x60bb4  (PIC base)
...
mflr  r31              ; r31 = 0x60bb4
addis r2, r31, 1       ; r2 = base + (1<<16)
lwz   r2, -0x76e0(r2)  ; r2 = *(base + 0x10000 - 0x76e0) = a data-section word
```

To decode: track the PIC base (the `bcl/bdnzl` target the `mflr` reads), fold
`lis`/`addis`/`addi`/`ori` into per-register constants, and for
`lwz rD, disp(rA)` where rA is a reconstructed address, read the 4 bytes at that
file offset — that word is frequently a **function pointer** or table entry.
Resolve it against the symbol map.

## Decoding a dispatch/vtable-style ABI

To recover how a host framework calls a plugin, disassemble the function that
INSTALLS the callbacks (here `gldUpdateDispatch(ctx, table, ...)`):

- Watch `stw rX, OFFSET(rTable)` — each is a callback slot; the **set of OFFSETs
  is the ABI** (e.g. slots at 0x18,0x1c,…,0x60).
- The stored pointers are often **internal (stripped) functions** — so you learn
  the table layout and which slots exist, but must write the bodies yourself.
- Branches keyed on a state word (`lwz r0, 0x2c(ctx); andi. r0, 0x20/0x400/...`)
  reveal **variant selection** — different callback sets for texture on/off,
  N texture units, etc. Cross-reference the open-source sibling (e.g. Mesa/DRI)
  whose `setup_tab[]`-style variant table mirrors the same bits.

## Classify primitive/render functions by their count guard

When a dispatch table holds many similar unnamed render functions (one per GL
primitive), you don't need to fully read each — classify by the **minimum-vertex
guard** at the top: `cmpwi <count_reg>, K; ble <exit>` means "needs > K verts",
which pins the primitive family:
- `count>0` → points (1/vtx) · `count>1` → lines (2) · `count>2` → triangles (3)
  · `count>3` → quads (4).
Then the grouping mask disambiguates within a family:
- `rlwinm rX,rX,0,0,0x1e` = round to even → independent pairs (GL_LINES) or
  quad-strip; `rlwinm ...,0,0x1d` = round to mult-of-4 → GL_QUADS.
An **extra argument** (e.g. a 4th param used as an index buffer) distinguishes a
DrawElements/indexed variant from a sequential DrawArrays one — so a doubled set
of point/line/triangle funcs usually means two dispatch vectors (verts vs elts).
Also note struct offsets the funcs read (command-buffer ptr/end, current
primitive type, vertex stride) — that recovers the object layout for free.

## General approach

1. `nm` → export map + addresses; sort by address for boundaries.
2. Bundle `Info.plist` → principal class, version, identifier.
3. Disassemble the lifecycle entry points first (Create/Init/UpdateDispatch) —
   they reveal the object layout (struct offsets) and the dispatch table.
4. Cross-reference an open-source sibling driver for the same chip family to name
   the internal helpers and confirm register/packet formats.
