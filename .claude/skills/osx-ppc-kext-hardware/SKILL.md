---
name: osx-ppc-kext-hardware
description: Bring up MMIO register access and bus-master DMA in a Mac OS X (10.3/10.4) PPC IOKit kext or userspace driver. Use when reading/writing device registers, mapping BARs, or driving a DMA ring on big-endian PowerPC hardware — covers the machine-check pitfalls, cache/endianness rules, and DMA sequence that are easy to get catastrophically wrong.
---

# OS X PPC kext hardware bring-up

Hard-won lessons from bringing up an ATI mach64 (Rage LT Pro) accelerator on a
PowerBook G3 under OS X 10.3. Most apply to any PPC OS X device driver.

## The cardinal rule: PPC punishes bad MMIO with a machine-check

On PPC a bad memory-mapped access doesn't return garbage — it **machine-checks
and hard-hangs/panics the machine**. The two ways to trigger it:

1. **Cacheable MMIO.** Device memory MUST be mapped uncached. Map with
   `kIOMapInhibitCache`:
   - kernel: `IODeviceMemory::map(kIOMapInhibitCache)` → `getVirtualAddress()`.
   - userspace: `IOConnectMapMemory(..., kIOMapAnywhere | kIOMapInhibitCache)`.
   A normal cached mapping of a register aperture will machine-check on first
   touch, or corrupt silently.
2. **Accessing an unclaimed / disabled aperture.** Reading a BAR the chip
   didn't decode (e.g. an optional aux register aperture left disabled) is a
   master-abort → machine-check. Don't assume a BAR is live because it's sized.

Because a wrong guess hangs the box, **discover, don't assume**: in `start()`,
log every BAR's physaddr+size (`getDeviceMemoryWithRegister(kIOPCIConfigBase
Address0 + i*4)`) and confirm against the chip docs before touching anything.

## Registers may not be where you expect (aperture-relative windows)

On this mach64 the MMIO registers were NOT at a dedicated BAR — the aux aperture
was disabled (machine-checked). They lived in the **last 2KB of the framebuffer
BAR**: `register R -> BAR0 + 0x7FF800 + R`. Always cross-check the actual
register location against a known register: read the chip-ID register and verify
it matches the PCI IDs before trusting the mapping.

## Endianness: the register window is little-endian; the CPU is big-endian

mach64 (and most PCI devices) are little-endian; PPC is big-endian. Every
register access must byte-swap. Use `OSReadLittleInt32` / `OSWriteLittleInt32`
(both kernel and userspace). This also applies to DMA command buffers the device
reads — see below.

## Bus-master DMA ring bring-up

Getting a device to bus-master-read a command buffer from RAM has its own traps:

- **Physically-contiguous, wired, uncached DMA memory.** Allocate with
  `IOBufferMemoryDescriptor::withOptions(kIOMemoryPhysicallyContiguous |
  kIOMemoryKernelUserShared | kIODirectionOutIn, size, align)`. **Call
  `prepare()` BEFORE `getPhysicalSegment()`** — without `prepare()` the returned
  "bus address" is a bogus unwired value (we saw ~0xd0320000 on a 512MB machine)
  and programming it into the DMA engine panics the kernel. On Lombard-era PPC
  there's no DART, so bus addr ≈ physical — but still get it from
  `getPhysicalSegment`, don't compute it.
- **Alignment matters to the engine, not just you.** A DMA descriptor ring that
  the engine treats as a fixed-size circular buffer must be aligned to that size
  (our 16KB ring had to be 16KB-aligned or the engine wouldn't walk it).
- **Map DMA buffers uncached in BOTH the kernel and userspace.** Then CPU writes
  reach RAM without an explicit dcache flush, mirroring on both sides. If you
  must write from a cached mapping, you need a real cache flush before the device
  reads — uncached is simpler and provably correct.
- **Order writes to RAM before the MMIO kick** with a PPC store barrier:
  `__asm__ volatile("sync; eieio" ::: "memory")` (or `OSSynchronizeIO()`).
  Uncached stores still need this ordered against the subsequent register write
  that starts the transfer.
- **Terminate the command stream.** This engine required the DMA register-write
  stream to END with a specific "stop" write (SRC_CNTL=0) to flush/commit; a
  stream without it walked the ring but committed nothing. Check your engine's
  docs for a required trailing command.

### The subtle one: a working ring that "does nothing"

Symptom we chased for several hardware runs: the engine **walked the ring** (its
descriptor pointer advanced) but the target register never changed. Cause: the
bus-master "system-to-register" path routes writes **through the drawing-engine's
register decode**, which reaches the GUI/drawing-engine register block but **NOT
general-purpose scratch registers**. We were testing against a scratch register
that path can't write. Lesson: **validate a DMA register-write path against a
register you KNOW that path can reach** (here, the pattern/drawing registers the
vendor's own driver uses in its DMA self-test), not an arbitrary scratchpad.

## clientMemoryForType must retain the descriptor it returns

When a userspace client maps kext memory, IOKit **releases** the descriptor you
return from `clientMemoryForType` after mapping it. If you hand back a cached
member descriptor without an extra `retain()`, it gets over-released and freed
out from under you → use-after-free panic on the NEXT unmap/unload. Always:

```cpp
if (*mem) { (*mem)->retain(); return kIOReturnSuccess; }
```

## Coexisting with the vendor's shipping driver

If Apple's driver already claims the device, you can still attach a second
personality with a distinct `IOMatchCategory` (so both match the same
`IOPCIDevice`). But the engine is then a **shared, asynchronous resource**: for
DMA, save/restore the vendor driver's engine registers (BUS_CNTL/SRC_CNTL/table
pointer) around your quiesce-and-submit, and accept that without a shared
hardware lock a collision is possible. Acceptable for headless bring-up; a real
driver needs the vendor's locking protocol or full device takeover.

## Debugging discipline that saved us

- One primitive per hardware round-trip; prove it before building on it
  (reg read → reg write → 2D fill → 3D flat → DMA ring → gouraud).
- A kernel panic on PPC often leaves only an on-screen trace — have the operator
  photograph it; the PC/LR + the last IOLog narrow it fast.
- Keep a hard safety guard in userspace DMA tools: refuse to program a bus
  address outside physical RAM (that guard turned a repeat panic into a clean
  abort).
