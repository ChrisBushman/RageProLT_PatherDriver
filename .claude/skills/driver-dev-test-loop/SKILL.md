---
name: driver-dev-test-loop
description: Structure the develop/build/test loop for a from-scratch hardware or OS driver where every test needs real hardware, each round-trip is slow, and a bad write can hang or corrupt the machine. Use when planning how to validate a driver incrementally — the ladder of standalone proofs, the capture→replicate→verify cycle, off-screen-before-visible risk staging, and the round-trip discipline that keeps a remote/fragile target usable.
---

# Testing a driver as you build it

From bringing up an OpenGL driver + kext on a remote PowerBook where the only way
to test is on the actual hardware, the human runs every `sudo` test, and a wrong
register write hard-hangs the machine. These practices kept it moving.

## Build a ladder of standalone proofs — never debug two unknowns at once

Prove each hardware primitive in a **tiny standalone user tool** before any
higher layer depends on it. The order that worked:

```
kext loads & coexists → MMIO read/write → 2D fill → 3D flat triangle →
bus-master DMA ring → gouraud → kext-driven ring → translation core →
dispatch-table render → framework first-light → on-screen
```

Each rung is a separate binary that exercises exactly one new thing against the
proven-good layer below it. When something breaks you know it's the new rung, not
a regression three layers down. A driver is dozens of interacting unknowns;
this is the only way to keep the unknown count at 1.

## The capture → replicate → verify cycle for undocumented formats

When you must produce a byte format some black box will consume:
1. **Capture** a known-good instance from a live run (gdb dump of the structure
   the reference/OS built, or a working peer's data).
2. **Replicate** it in your code — verbatim first, understand fields later.
3. **Verify** the black box accepts it, then vary one field at a time to learn
   what each means. Don't hand-derive a format you can capture.

Corollary: when there's **no known-good source** (e.g. no working peer driver on
the box), you must reverse the *encoder* that produces the format, not guess the
bytes. Know which situation you're in before you start.

## Stage risk: off-screen before on-screen, fence before pixels

Rendering to the live framebuffer can corrupt the display or wedge the machine.
Stage it:
1. Render to an **off-screen scratch** VRAM region first. Prove the pipeline with
   a **kext-side stamp/fence that only advances when DMA actually executed** —
   that's your ground truth, independent of whether pixels are visible.
2. Only once the fence advances, point the target at the real framebuffer.
3. Keep a known-good recovery: full-screen capture failures clear on reboot; a
   register-corruption path may need the device moved aside (see
   [[osx-ppc-kext-hardware]]). Tell the operator the recovery step *before* the
   risky run.

## Round-trip discipline (slow/remote/human-in-the-loop target)

- **One change per test.** Each round-trip is expensive; changing two things means
  you learn nothing from the result. Make the smallest change that answers one
  question.
- **Verify the change actually landed before testing.** File sync silently
  truncates; header changes may not retrigger a rebuild (Makefiles that only
  syntax-check, or `.o` files that don't depend on the header). `grep -c` the new
  string on the target, and confirm the binary's mtime/size changed. A "failed"
  test is often just stale code — check that first, always.
- **Make the test self-describing.** The test tool should print what it did and
  what it expects, and dump the driver log inline, so one artifact tells the whole
  story without a second round-trip.
- **Predict the branches before you run.** Write down "if X → means A, do B; if Y →
  means C, do D" so the operator's one result immediately advances you instead of
  triggering another think.
- **Instrument, don't speculate.** When you catch yourself reasoning in circles
  about what a black box does, stop and add a log / breakpoint that answers it
  directly. One capture beats ten hypotheses.

## Runtime debugging reads reality; static analysis reads intent

Static disassembly tells you what code *would* do; a live gdb read tells you what
the actual pointers/structs *are* right now. Use static RE to find *where* to
look (the deciding instruction, the struct offset), then a runtime capture to get
the real values. Keep an old debugger that speaks the target ABI on the box
(gdb 5.3 for PPC/Panther) — modern host tools often can't parse the old binaries
or attach to the old kernel.

**Before blaming your code or the hardware for wrong output, DUMP THE INPUT your
code actually receives.** A bug that looks like yours may be upstream. Real case
(cost ~8 hardware round-trips): sub-4-wide textures rendered scrambled; every
theory (row pitch, VRAM offset, cache, draw order, a HW minimum size) was wrong.
One `gld_log` of the source bytes the driver was handed showed the data was
*already* corrupt on arrival — the test app fed GL tightly-packed RGB without
`glPixelStorei(GL_UNPACK_ALIGNMENT,1)`, so GL mis-strode it before the driver ran.
The tell was in the data all along (only widths whose byte-rows weren't 4-aligned
failed). Dumping the received bytes is one cheap log line; it should come *before*
a chain of hardware experiments, not after.

See also [[osx-gl-framework-integration]], [[panther-kext-build-test]],
[[osx-ppc-kext-hardware]].
