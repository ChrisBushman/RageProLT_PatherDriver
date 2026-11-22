# Memory map: what the driver may touch

The recurring failure in this project's history was a kernel panic from writing to, or peeking at, an address the driver was
never meant to use. This is the verified map of every address range the driver (kext, plugin, tools) touches, why each is safe,
and where it is enforced. Values are from the Rage LT Pro (PCI 1002:4c49 rev 0xdc) in the MDD G4; the BAR *addresses* are
assigned by Open Firmware and differ per machine — read them from `ioreg`, never hard-code them.

## 1. The card (from `assigned-addresses` / `IODeviceMemory`)

| PCI reg | Space | Size | Role | Notes |
|---|---|---|---|---|
| BAR0 (0x10) | memory | 16 MB | linear aperture: **8 MB VRAM + chip registers inside it** | only the first 8 MB is backed |
| BAR1 (0x14) | I/O | 256 B | classic register window | reads fault unless I/O decode is on (`setIOEnable(true)`) |
| BAR2 (0x18) | memory | 4 KB | memory-mapped register block (kext `MEM_REGISTERS`) | |
| ROM (0x30) | memory | 128 KB | PC option ROM (64 KB valid) | decode only enabled while mapped |

## 2. Inside BAR0 (offsets from the base of BAR0)

```
0x000000 ............ VRAM start
   ...               render surfaces, Z buffer, texture heap (see §3)
0x7FF800 ............ REGISTER WINDOW, block 0 (DRI 0x000-0x3FF)  <- NOT memory
0x7FFC00 ............ REGISTER WINDOW, block 1 (DRI 0x400-0x7FF)  <- NOT memory
0x800000 ............ end of the LE VRAM view; 0x800000-0xFFFFFF of the 16MB BAR is NOT probed (it may be a byte-swapped alias, unverified) -- do not access
```

**Usable VRAM is `[0, 0x7FF800)`, not `[0, 0x800000)`.** The top 2 KB of the 8 MB aperture *is* the register file. Any surface,
Z buffer, texture, fill or blit that reaches it is a register write (observed: hang or panic).

- Enforced by `MACH64_VRAM_USABLE_END` and `mach64_vram_span_ok(off, len)` in `common/mach64_regs.h`. A start-offset check alone
  is not enough; the **whole span** is checked.
- Applied at: `mach64_set_render_target`, `mach64_set_zbuffer` (`gld_state.c`); Apple-allocator results in `mach64_vram_alloc`,
  `mach64_fill_rect`, `mach64_blit_surface` (`gld_render.c`); texture heap via `mach64_set_texheap` (`gld_texture.c`, clamped).
- Policy cap, stricter than the hardware limit: the double-buffer path keeps the texture heap below `0x770000`
  (the framebuffer/cursor block near the top; a texture-heavy game uploading past it panicked).
- Tested by `mach64_pathb_suite guards` (hostile spans must be refused with registers and top-of-VRAM unchanged).

## 3. VRAM layout in use

Headless / Path B: Apple's allocator (`new_texture` selector) hands out offsets starting at 0; typical: colour surface at 0,
Z buffer after it (e.g. `0x41000`), texture heap after that (`0x61000`+). The plugin rejects allocator results that are zero,
not 8-byte aligned, or whose span leaves usable VRAM. Legacy dbuf path: front `0x600000`, back/Z above it.

## 4. Register writes through Apple's `submit_buffer`

Apple's drain computes `stwbrx(val, this[0x550], off)` where `this[0x550]` = BAR0 + `0x7FFC00` and `off = reg − 0x400` for the
DRI register number `reg`; it does **no bounds check**. Block-0 registers (below 0x400) give a negative offset that wraps to
BAR0 + `0x7FF800+reg`, still inside the same mapped page. Therefore `reg` must stay in `0x000..0x7FC`.

- `mach64_submit` refuses (and drops the whole batch) if any expanded register exceeds `MACH64_DRI_REG_MAX` (0x7FC).
- MEASURED (suite check `regaudit`, 82-check run on the G4, Nov 20): the plugin wrote exactly 45 registers through submit, all in
  `0x240–0x774` (vertex/setup block 0x240–0x304; 2D/3D engine 0x500–0x774). `BUS_CNTL` (0x4A0), `BM_ADDR` (0x648),
  `BM_GUI_TABLE_CMD` (0x64C) and `BM_GUI_TABLE` (0x1B8) were never written. The guard protects against a corrupt batch.

## 5. Kernel-memory peeks (selector 14 / `kATIRPro_scale_surface` with flag `0x80000000`)

The patched `ATIRagePro.kext` exposes a peek that **dereferences the supplied kernel address unchecked**. The plugin walks
Apple's object graph with it (surface → buffer → render-target record, accelerator fields). A stale or garbage pointer is a kernel
panic.

- Every peek goes through `m64_peek()` (`gld_render.c`, declared in `gld.h`), which refuses addresses outside
  `0x10000 .. 0xDFFFFFFF` (`M64_KADDR_MAX` = `VM_MAX_KERNEL_ADDRESS` in xnu-517 `osfmk/mach/ppc/vm_param.h`; kernel pointers are virtual, independent of RAM size) or not 4-byte aligned.
  A refusal logs `[peek] REFUSED ...` and returns zeros.
- All 26 call sites were converted; no raw `0x80000000u |` peeks remain in the plugin.
- Checked on the G4 (2 GB RAM, Panther): peeked objects so far sit at 0x2bfa000..0x3260fa00 (surface, buffer, kexts), all under 1 GB
  but growing upward as the kernel allocates, so the old 0x40000000 cap would eventually have refused valid pointers. The 0xDFFFFFFF
  ceiling is from the xnu ppc headers (not measured on the machine). The range check cannot prove a pointer is *mapped*; that still
  relies on only following pointers returned by earlier peeks.
- Derive peek addresses only from pointers a previous peek returned; never from guesses or scans.
  (Broad kernel-memory scans have crashed machines on this project.)

## 5b. User-space pointers the plugin dereferences

GLEngine's gli/texture objects and the app's texel buffers are read directly. They may live anywhere in Panther user space
(`0x10000..M64_UADDR_MAX = 0xFFFEFFFF`; xnu-517 `vm_param.h`: `VM_MAX_ADDRESS` 0xFFFFFFFF, `USER_STACK_END` 0xffff0000); the plugin used a 1 GB cap that
silently dropped textures whose data was above it (verified with texel data on the stack at 0xbffffba0).

## 5c. VRAM byte order for texture uploads

Observed headless: texels stored through the patched kext's upload arrive byte-reversed (red 0xffff0000 read back as 0x0000ffff). The
VRAM view the Apple driver gets there is `RageStubFB`'s plain little-endian aperture. Whether Apple's own framebuffer driver supplies a
byte-swapped view (which would explain why the swap is not needed there) is **not established**. `tex_need_bswap()` therefore swaps only
when `RageStubFB` is present (override `MACH64_TEX_BSWAP=0/1`).

## 6. Reads that hang rather than fault

*(Items below are lessons from earlier sessions on this project, not re-verified now; do not test them on a live machine.)*

- A PPC read to the chip while it is unpowered/un-clocked/un-initialised **blocks the CPU forever** (no PCI timeout). The boot
  init runs the option ROM first; never read registers/VRAM on a card that has not been through `tools/biosemu/biosrun`.
- BAR1 (I/O) reads without I/O decode enabled machine-check.
- Never run `mach64_post` without explicit arguments (default mode replays an old recorded trace and froze the G4).
- `BUS_CNTL.BUS_EXT_REG_EN` (bit 27) must be set after BIOS init (done at the end of `biosrun`), or CPU-written 3D registers latch
  but do not fire the setup engine.

## 7. Process rules that follow from this

1. Any new offset that reaches the engine gets a `mach64_vram_span_ok()` check at the point it enters the plugin.
2. Any new kernel peek uses `m64_peek()`.
3. Add a refusal case to `mach64_pathb_suite guards` for every new guard.
4. Do not unload `ATIRagePro.kext` on a running system (crashes the machine); use a reboot.

## 8. What is verified, and how (Nov 20)

| Claim | Status |
|---|---|
| BAR0 16 MB mem, BAR1 256 B I/O, BAR2 4 KB mem, ROM 128 KB | measured: OF `assigned-addresses` on the G4 (`pci1002,4c49`) |
| Panther kernel is xnu-517; kernel map ends 0xDFFFFFFF | G4 reports `xnu-517.12.7`; limit read from that tag's `osfmk/mach/ppc/vm_param.h` (not measured at run time) |
| Peeked kernel objects lie below 1 GB today | measured, but only a few pointers (0x2bfa000, 0x262e0000, kexts ~0x2f4a7000, panic log 0x3260fa00); no maximum known |
| Registers written through submit | measured (`regaudit`), see §4 |
| Span checks refuse hostile spans (targets, Z, fill, blit) | measured: suite `guards` (registers and top-2KB-of-VRAM unchanged) + host test `tests/vram_span_test.c` (found and fixed a 32-bit overflow) |
| No kernel object leak across contexts | measured: 15 notifies (control) vs 15 and then 60 suite runs, IOKitDiagnostics Container/IOMalloc/Instance identical to the control |
| Register-window layout (0x7FF800/0x7FFC00), `reg-0x400` submit mapping | from earlier hardware-proven RE (source comments in `mach64_submit`, `mach64_regs.h`); not re-derived today |
| Apple's drain has no bounds check | from earlier RE; not re-derived |
| Upper 8 MB of BAR0, §6 hang/fault behaviours, why the texel swap is needed | NOT verified (deliberately not probed / cause unknown) |

## 9. Kernel bug in Apple's ATIRagePro (found Nov 20): uninitialised descriptor -> spurious drain

`IOATIRProGLContext::clientMemoryForType` (type 1, the call the plugin uses to trigger a drain) builds a `VendorCommandDescriptor` on the
kernel stack and calls `ctx->process_command_buffer(&desc)`. For `ATIR3GLContext` that method is `li r3,0; blr` (kext+0x13990): the
descriptor is never filled. The caller still reads `desc.count` (`lwz r2,0x48(r1)`, kext+0x5ad8) and, when non-zero, calls
`accel->submit_commands(&desc)` which PIO-drains `desc.buf` for `desc.count` dwords. With stale stack residue this drains garbage: a wild
register offset or a NULL/tiny buffer pointer, i.e. an intermittent kernel panic (0x300 data access) inside `submit_buffer`
(backtrace: submit_buffer <- submit_commands (kext+0x12710) <- clientMemoryForType (kext+0x5b10)). About 10% of processes crashed.
The fix is `kext-patch/patch-clientmemory-desc.sh` (replace the read with `li r2,0`). Verified: 60 notify+1500-frame cycles and the full suite.
