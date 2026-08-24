---
name: osx-driver-info-sources
description: Where to find the missing information needed to write a driver for classic/early OS X (10.3/10.4 PPC) when there is no SDK doc, no public spec, and the API is a private version-specific ABI. Use when you're blocked on "what does this undocumented function expect / what's this struct layout / what registers does this chip have" — a checklist of ground-truth sources ranked by reliability.
---

# Sourcing missing info for OS X driver development

Writing a driver for a 20-year-old OS against undocumented private interfaces
means the info you need is never in a doc. It exists, but as *evidence* you have
to extract. Ranked by how much you should trust each source.

## 1. Apple's own shipped binaries (highest-value, always available)

The OS *contains* working implementations of exactly the ABI you're targeting:

- **A peer driver bundle.** If Apple shipped a GLDriver/accelerator for a
  *different* chip in the same family/era, it implements the identical plugin ABI.
  It is your reference implementation. (We used `ATIRage128GLDriver.bundle` to
  derive every gld* contract for a Rage LT Pro driver.) Find them under
  `/System/Library/Extensions/*.bundle` and framework `Resources/`.
- **The framework binaries that call your driver.** `OpenGL.framework/Versions/A/OpenGL`
  (`com.apple.opengl`) and `.../Resources/GLEngine.bundle/GLEngine` contain the
  code that *decides* whether to accept/use your driver — i.e. the exact contract,
  including the checks you must pass. Copy the whole file (they're small,
  self-contained) and disassemble.
- **IOKit kext binaries** define the user-client selector tables, struct sizes,
  and IOKit properties (`IOGLBundleName`, `IOMatchCategory`) your driver must
  match. Read the Info.plist *and* the binary.

Decompile these with the workflow in [[osx-binary-decomp-for-abi]].

## 2. Crash logs (turns "wrong" into "exactly where")

`/Library/Logs/CrashReporter/<proc>.crash.log` gives a symbolicated backtrace
(named framework functions), the faulting PC, the fault address, and the "Binary
Images" section (load base per image — the key to mapping runtime↔file offsets).
A single crash log often localizes an ABI mistake to one struct field.

## 3. Live introspection on the running system

- **IOKit registry**: `ioreg -l` (and `IORegistryExplorer`) shows the service
  hierarchy, properties, and which drivers matched — how to find the service your
  bundle attaches to and the property that names it.
- **gdb on the live process/kernel** reads the actual runtime structs the OS
  built (the ground truth static analysis can't give). Keep an era-appropriate
  gdb on the target.
- **`nm` / `otool` on the target itself** (native, old enough to parse the old
  Mach-O) when the modern host toolchain chokes.

## 4. Hardware register specs (for the chip half)

- **Open-source drivers for the same silicon on other OSes** — XFree86/X.Org,
  Linux/BSD framebuffer and DRM drivers — give register offsets, bitfields, and
  init sequences the OS X binary only uses opaquely. (mach64/Rage register names
  and the CRTC/GUI/DST register layout came from the X.org atimach64 driver.)
- **The vendor programmer's guide** if it survives online (register reference,
  engine setup, DMA descriptor format).
- Cross-check spec offsets against what the OS binary actually pokes — apertures
  and windows can be remapped (e.g. a register block living at a `+0x400` block
  base so `DST_OFF_PITCH` reads at `0x500` = MM `0x100`).

## 5. Period documentation & community knowledge

Apple's old developer docs (Darwin/IOKit Fundamentals, the OpenGL programming
guide of the era), WWDC-era release notes, and retro/hobbyist reverse-engineering
writeups. Useful for concepts and naming; never authoritative for the private
ABI — always confirm against the shipped binary.

## Rule of thumb

Docs tell you the *concept*; the shipped binary tells you the *contract*. When
they disagree, the binary wins — you're implementing what the code checks, not
what the manual describes. Every claim you can't read out of a binary or a live
system is a hypothesis to verify, not a fact.

See also [[osx-binary-decomp-for-abi]], [[osx-gl-framework-integration]],
[[ppc-macho-reverse-engineering]].
