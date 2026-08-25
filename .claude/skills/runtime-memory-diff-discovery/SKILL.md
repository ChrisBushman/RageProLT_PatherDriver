---
name: runtime-memory-diff-discovery
description: Locate an opaque runtime field or hardware-state artifact by DUMPING a memory region under two configurations and diffing it by offset. Use when a host framework or driver keeps state you must read but have no symbol for — e.g. which struct offset holds "the bound texture", "texturing enabled", or a caps value — or when VRAM contents at a hardware offset reveal what the engine actually stored. Covers one-variable A/B setup, finding the region base, crash-safe in-process scanning vs gdb, and reading the diff.
---

# Finding opaque state by dumping and diffing memory

How the Panther mach64 GL plugin recovered undocumented GLEngine context fields
(the "gli") and hardware state — e.g. `gli+0x15748` = the bound texture object,
`gli+0x6748` bit 3 = GL_TEXTURE_2D enable, `descriptor+0xb2` = MAX_TEXTURE_SIZE —
without a single symbol for any of them. Static RE tells you a field *exists* and
roughly where code reads it; this method tells you the *exact offset* and its
encoding from the running program.

## The core idea: change ONE thing, diff the region by offset

If two runs differ in exactly one piece of GL state, the memory that encodes that
state is (almost) the only thing that changes. Dump the same region in both runs,
diff dword-by-dword at each offset, and the field you want is the offset that
changed — and *how* it changed is its encoding.

The whole technique lives or dies on isolating one variable. "Both quads came out
orange" was not debuggable until two runs were reduced to *identical except the
bound texture*, at which point the lone changed offset (`0x15748`) fell out.

## Set up a clean one-variable A/B

- Two programs (or one that draws twice) identical in every GL call except the one
  state under test: bind texA vs texB; `glEnable` vs `glDisable(GL_TEXTURE_2D)`;
  caps queried with the extension on vs off.
- Dump at the SAME program point in both — a breakpoint on a stable callback
  (e.g. the draw entry, or `aglSetCurrentContext`), not "somewhere in the middle,"
  or unrelated scratch/counters will differ and drown the signal.
- Keep everything else pinned: same window size, same prior draws, same order.
  Each uncontrolled difference is another false-positive offset to rule out.

## Find the region base before you can talk in offsets

A diff is only useful in *relative* offsets, so anchor the region first. Find the
base by a known back-reference: some field inside the region points back to an
object you already hold. The gli base was recovered by back-scanning from
`ctx+0x10` for the address whose `+0x7898` slot equals `ctx` — that word is the
gli, and every field is quoted as `gli+off` thereafter (the driver reads them via
a `mach64_gli_u32(ctx, off)` helper). Do the anchoring once, hard-code the base
offset, and all later dumps are directly comparable.

## Two ways to dump — and why gdb `find` is out on Panther

- **gdb region dump** (`tools/gli.gdb`, `glidump.gdb`): break at the stable point,
  `dump memory <file> <base> <base+len>`, then diff the two files by offset. Works,
  but the on-box gdb is old (5.3, pre-7.0): **`find` is undefined** and
  `set breakpoint pending on` is unsupported — so you cannot value-search memory
  from gdb, and you must set breakpoints only after the symbol is loaded (break at
  `aglSetCurrentContext`, then set the gld breakpoints).
- **In-process vm_region scan** (the crash-safe workhorse): from inside the driver,
  walk the task's mappings with the Mach `vm_region`/`vm_read` API and scan for a
  value or diff snapshots. It never dereferences an unmapped address (that would
  panic the box or crash the app), so it is the safe substitute for gdb `find`.
  This is what actually cracked the bound-texture channel.

## Reading the diff: expect ONE offset — thousands means you dumped a table

- A well-isolated A/B yields a *handful* of changed offsets; ideally one. That
  offset, and the before/after values, are the answer (`0x08`→`0x00` across
  enable/disable told us `gli+0x6748` bit 3 is the enable).
- If the diff is huge, you captured a big internal **table**, not a scalar. The
  first bound-texture scan drowned because the texobj pointer lives inside a large
  per-context texture-object table in the gli region — every entry looked like a
  hit. Fixes: constrain the region to a tight window around the suspected field;
  make the A/B differ *even less*; and don't rely on a "dense-cluster" heuristic
  (a density filter with a `0 dense` off-by-one silently skipped the real hit).
  A targeted gli-region diff then found `0x15748` with a single change.
- Distinguish a **pointer** from a **value**: if the changed dword is itself a
  valid mapped address (e.g. `0x1xxxx…`), you found a pointer to the object — chase
  it (the bound-texture slot holds the texobj; its own `+0x30` is the state key).

## VRAM contents are evidence too — stale bytes are a clue, not noise

The same dump-and-read applies to the hardware side: read what is actually at a
VRAM/engine offset to learn what the engine stored. A "persistent test image" that
survived reboots was not garbage or the desktop — it was the *stale content of
`TEX_3_OFF`*, which proved the base texel offset must go to `TEX_[maxLog2]_OFF`,
not `TEX_0_OFF`. When a surface shows structured-but-wrong pixels, read the bytes
at the offset you programmed and compare to what you uploaded; a mismatch localizes
the bug to the offset/pitch/format math. (Note the aperture caveat from
[[osx-ppc-kext-hardware]]: the userspace VRAM aperture is not offset-aligned with
engine space, so read through the path the engine actually uses.)

## Confirm the candidate before trusting it

One diff is a hypothesis. Confirm it: read the offset live in the driver on a third
configuration and check it predicts behavior (does `gli+0x6748` bit 3 track every
enable/disable?); or flip the suspected field and see the artifact move. Only then
hard-code the offset — and record it (offset, encoding, how found) in the project
memory, because these are unsymbolized and easy to forget.

## Tooling in this repo

`tools/glidiff.sh` + `gli.gdb`/`glidump.gdb` (region dump+diff), `mach64_texstate.c`
+ `texstatediff.sh` (enable/disable gli-diff), `mach64_texmulti.c` (bound-texture
diff). See also [[driver-dev-test-loop]] (runtime reads reality vs static intent),
[[osx-binary-decomp-for-abi]] (find *where* to look statically first),
[[osx-ppc-kext-hardware]] (the VRAM aperture / engine-offset caveats).
