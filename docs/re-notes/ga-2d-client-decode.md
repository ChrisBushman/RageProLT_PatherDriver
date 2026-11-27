# Stock ATIRageProGA.plugin and 2D context user client: static decode

Static analysis only (nothing run on hardware). Sources: stock `ATIRagePro.kext`
(md5 8fc97891, MH_OBJECT with symbols), `ATIRageProGA.plugin` (G4 copy md5
6c8e4b06...), Panther IOKit / IOGraphicsFamily / CoreGraphics pulled read-only
from the G4, and the IOKit graphics headers (`IOGraphicsInterface.h`,
`IOGraphicsInterfaceTypes.h`, `IOAccelTypes.h`, `IOAccelClientConnect.h`).
Addresses are file/section offsets in the stock binaries. Anything marked
"not established" was not proven.

## 1. Loader chain (how the plugin gets found and started)

- Accelerator personality has `IOCFPlugInTypes`:
  `ACCF0000-0000-0000-0000-000a2789904e` -> `ATIRageProGA.plugin`.
  That UUID is `kIOGraphicsAcceleratorTypeID` (IOGraphicsInterface.h);
  interface UUID `6766E94A-0000-0000-0000-000A2789904E` is
  `kIOGraphicsAcceleratorInterfaceID`.
- Plugin factory UUID `10024749-0000-0000-0000-000a27898d3e`
  (`ATIRageProGAFactory`, 0x12a4). QueryInterface answers IUnknown and the
  interface UUID. Plugin lives in `/System/Library/Extensions/ATIRageProGA.plugin`
  (sibling of the kext, not inside it).
- Plugin Info.plist `Personality` has `IOProviderClass = IONDRVFramebuffer`.
- IOKit `IOFindPlugIns` (0x905adebc in the pulled IOKit): reads the service's
  `IOCFPlugInTypes`, builds `/System/Library/Extensions/<name>`, CFPlugInCreate,
  reads the plugin `Personality`, calls `IOServiceMatchPropertyTable(service,
  personality)`. The service is the FRAMEBUFFER, which is why stock
  `foundFramebuffer` copies `IOCFPlugInTypes` onto the framebuffer.
- Consequence for the G4 rig: `RageStubFB` is a plain IOFramebuffer subclass, so
  the stock personality (IONDRVFramebuffer) will probably not match. Use a copy of
  the plugin with only the plist `IOProviderClass` changed to `IOFramebuffer`
  (binary identical). Not established: that matching checks IOProviderClass by
  metaCast.
- Who in userspace calls the plugin (presumably CoreGraphics/WindowServer) is not
  established. The pulled CoreGraphics imports `IOAccelFindAccelerator` (once) and
  contains IOAccel surface strings (`IOAccelCreateSurface`, `IOAccelWriteLockSurface`,
  `IOAccelSetSurfaceFramebufferShape`, ...) but contains no `IOCreatePlugIn*`, no
  ACCF UUID bytes and no `IOAccelTypes` string. A scan of other frameworks for
  `IOCreatePlugInInterfaceForService` found only IOKit, ForceFeedback, GLUT,
  IOBluetooth, AppleVA, DiskManagement, ICANotifications, VideoConference. So the
  plugin consumer is not established.

### IOAccelFindAccelerator (IOKit 0x905bd450)
`(io_service_t fb, io_service_t *accel, UInt32 *index)`: reads fb property
`IOAccelTypes` (CFString, registry path) -> `IORegistryEntryFromPath`; requires
`IOObjectConformsTo(service, "IOAccelerator")`; reads fb `IOAccelIndex`
(CFNumber SInt32). Failure returns 0xe00002bc.

### Framebuffer properties set by stock `foundFramebuffer` (0x398)
- `IOCFPlugInTypes`: copy of the accelerator property.
- `IOAccelTypes`: accelerator path in the IOService plane.
- `IOAccelIndex`: u32 framebuffer index (accel+0xc8 range).
- `IOAccelRevision`: u32 2 (also set on the accelerator in
  `IOATIRProAccelerator::start` 0x1904).
It then registers `addFramebufferNotification` (`display_change_handler` 0x20c).
`findFramebuffers` is at 0x55c.
Ours: `publishFramebuffer` (RageLTSurfaceClient.cpp) sets the last three but NOT
`IOCFPlugInTypes`; Info.plist has no `IOCFPlugInTypes`.

### Plugin Start (0x2aac): class conformance gate
1. `IOAccelFindAccelerator(fb, &accel, &this+0x84)`.
2. `IOObjectConformsTo(accel, "ATIRagePro")`; on failure prints
   "Unusual class (%s)" and returns 0x2c7. Our `RageLTDevice` is an IOAccelerator
   subclass that only does `setName("ATIRagePro")`, which does NOT satisfy
   conformance (conformance is by class chain). A class named exactly
   `ATIRagePro` must appear in the chain, and it cannot coexist with the stock
   kext's metaclass of the same name.
3. `IOServiceOpen(accel, mach_task_self(), 2, &conn)`: type 2 = 2D context.
4. selector 1 (get_config, 2 scalar outputs into this+0x88/0x8c), then Reset.

Plugin vtable at 0x3004: QueryInterface, AddRef, Release, version 0x00010002
(matches `kCurrentGraphicsInterfaceVersion/Revision` 1/2), then Probe (score
2000), Start, Stop, Reset (0x13a4), GetCapabilities (returns 0x2c7), Flush
(0x14d0; flags&0x10000 = kIOBlitFlushWithSwap appends an op 0x02 packet when a
surface is bound), Synchronize (returns 0), GetBeamPosition (writes 0, returns
0x2c7), AllocateSurface (0x2928), FreeSurface (0x2a30), LockSurface (0xf40),
UnlockSurface (0xfe4), SwapSurface (0x1074), SetDestination (0xf38),
GetBlitter (0x27e4), WaitComplete (0x28bc).

## 2. Accelerator newUserClient (0x2998)

| type | vtbl slot | class |
|---|---|---|
| 0 | +0x590 new_surface | ATIR3Surface (surface client) |
| 1 | +0x598 new_gl_context | ATIR3GLContext |
| 2 | +0x594 new_2d_context (0x12484) | ATIR32DContext (0x114 bytes) |
| >2 | returns 0xe00002c2 | |

Then init, attach (+0x3a4), start (+0x348).

## 3. 2D context selectors

IOExternalMethod entry = 24 bytes (object, delta 0xffff, pfn, flags, count0,
count1). Flags: 0 ScalarIScalarO, 2 ScalarIStructO, 3 StructIStructO,
4 ScalarIStructI. Base table `IOATIRPro2DContext` methodDescs at 0x17c68
(16 entries). `ATIR32DContext::getTargetAndMethodForIndex` (0x14130): 0-15 base
table (ctx+0xfc), 16-18 derived table 0x19b70 (ctx+0x110).

| # | name (addr) | type | in / out | notes |
|---|---|---|---|---|
| 0 | set_surface (0x8c3c) | ScalarIStructO | in (id, mode); out struct, plugin passes 0x30 | see 3.1 |
| 1 | get_config (0x8eb0) | ScalarIScalarO | out 2: accel+0x98, accel+0x9c | accel+0x98 = 0x02000000 (GY/LG) or 0x04000000 (LI); +0x9c starts 0; meaning of +0x9c not established |
| 2 | get_surface_info (0x8ed4) | ScalarIStructO | (id, mode) / struct | same lookup as set_surface, not found = 0x2f0, no state change |
| 3 | swap_surface (0x8fc4) | ScalarIScalarO | in 1 (ignored), out 1 | needs bound surface and accel+0x7c ready else 0x2be; retries 1000x with IOSleep(1), timeout 0x2bc; flush_surface (0xc4dc); returns surf+0x168 |
| 4 | scale_surface (0x9188) | ScalarIScalarO | (flags, w, h) / 0 | requires bound surface and flags&1 else 0x2c7; builds two {0,0,w,h} rects, set_scaling (0xcc2c) |
| 5 | lock_memory (0x9260) | ScalarIScalarO | in 1 (ignored), out 2 | no surface: 0x2cc and out0=0xDEADBEEF; maps surf+0x16c descriptor into owner task (ctx+0x78) at ctx+0x108/0x10c; returns ctx+0x10c + offset from surface_write_lock_int (0xc438); surf+0x13e++; meaning of out1 not established |
| 6 | unlock_memory (0x9504) | ScalarIScalarO | in 1, out 1 | surf+0x13e-- (surface_write_unlock_int 0xc4c0); if arg<0 also swap_surface; out=0 otherwise |
| 7 | finish (0x95ec) | ScalarIStructI | in 1 (mode) | 0 = sleep until this context's stamp (ctx+0x7c); 1 = accel+0x50 (all work); 2 = accel+0x50-1; else 0x2c2 |
| 8 | declare_image (0x96b0) | ScalarIScalarO | in 3, out 1 | IOATIRProShared (ctx+0x88, 0x28 bytes); new_agp_texture; unused by GA |
| 9 | create_image (0x97b4) | ScalarIScalarO | in 2, out 2 | unused by GA |
| 10 | create_transfer (0x98c4) | ScalarIScalarO | in 2, out 2 | unused by GA |
| 11 | delete_image (0x9a88) | ScalarIStructI | in 1 | unused |
| 12 | wait_image (0x9bd0) | ScalarIStructI | in 1 | unused |
| 13 | set_surface_paging_options (0x9fcc) | StructIStructO | 0xc / 0xc | returns 0x2c7 |
| 14 | set_surface_vsync_options (0x9fd8) | StructIStructO | 0xc / 0xc | returns 0x2c7 |
| 15 | set_macrovision (0x9fe4) | ScalarIStructI | in 1 | loops framebuffers, safeMetaCast IONDRVFramebuffer, vtbl +0x70c control 0x92; else 0x2c0; slot meaning not established |
| 16 | read_regs (0x14368) | StructIStructO | | Rage Pro subclass only. reg & 0x3ff, byte-reversed access via accel+0x550; size multiple of 4 |
| 17 | write_regs (0x143c8) | ScalarIStructI | (reg,val) pairs | size multiple of 8; no allow-list |
| 18 | write_2_regs (0x14414) | ScalarIStructI | 2 scalars + pairs | no allow-list |

16-18 are never used by the stock GA plugin; they are an unrestricted register
window and must be omitted (or fenced) in our implementation.

### 3.1 set_surface semantics
Under accel mutex (accel+0x544); args id=r4, mode=r5, out=r6, size=r7.
- Mode bit 0x800 (surface-backed): `find_surface_for_id` (0x4bd0); not found =
  0x2c2 (id 0 clears). Detaches old surface (`remove_2d_context_from_list`
  0xad68); ctx+0xf0 = surf, ctx+0x100 = 0xffff; surf+0x154 =
  (surf+0x154 & 0x803f) | (mode & 0xFFFF803F); ctx+0x8c = 0x20000001
  (0x20000003 if mode bit 0x400); `add_2d_context_to_list` (0xad48).
- Otherwise id is a display index: must be < accel+0xc8 and set in mask
  accel+0xcc else 0x2c2; ctx+0x100 = id, ctx+0xf0 = 0, ctx+0x8c = 0x20000000.
- Then: if accel+0x74 == this clear it; ctx+0x92 = 0; `invalidate()` (+0x5a4);
  `set_destination` (+0x5b0, 0x14348) -> `get_buffer_info` (+0x5b4, 0x1428c).
- Other mode bits are not established.

### 3.2 get_buffer_info output (0x1428c)
Requires *size == 0x30 else 0x2c2. +0 short x(0), +2 short y(0), +4 short w,
+6 short h, +8 long w, +0xc long h (from surf+0x150/0x152 or display +0x1c/0x1e),
+0x10 = lhz +0x14 (pitch units not established), +0x14 = 0, +0x18 = 0,
+0x1c = lhz +0x16, +0x20 = (v>1 ? v>>1 : 1), +0x24 = pixcode byte (+0x2c),
+0x28 = 0, +0x2c unwritten. Pixel codes: 2=8bpp, 3=1555, 4=565, 6=8888.

## 4. Client memory (clientMemoryForType 0x82ec)

- Type 0: command buffer AND doorbell. Each map takes the mutex, waits for the
  surface (1000 tries), context-switches, `process_command_buffer`,
  `submit_commands`, handles swap, rotates the ring of command buffer
  descriptors, `init_command_buffer_header`, and returns a fresh buffer.
- Type 1: context buffer (ctx+0xb4; `init_context_buffer_header` 0x9f54: +0x1c=1,
  +0x10 = (0x1000-0x20)/4-2). The plugin only zeroes its +0x14 count.
- Type 2: accel+0xa8 shared region.
- Others 0x2c2. Options returned = accel+0x530 (0).
- The plugin imports no IOConnectUnmapMemory. Whether repeated type-0 maps are
  coalesced by IOKit or leak is not established; use a fixed ring of descriptors.
- Context start() allocates buffers (`allocAllContextBuffers` 0x9cc4).

## 5. Command buffer / stream protocol

`init_command_buffer_header` (0x9f8c): 4 KB buffer, 32-byte header: +0x10
capacity words ((0x1000-0x20)/4), +0x14 surface flags word (surf+0x168), +0x1c
initial packet header = 1 (plugin resets to 0 after each map), +0x20 body start
(0x01000000 terminator initially).

Packet header word = (op<<24) | dword length to the next header, patched by
OR-ing `(cur - prevHeader)>>2`. Plugin fields: this+0x7c prev header ptr,
+0x80 write ptr, +0x18 mapped cmd buffer, +0x14 ctx buffer, +0xc connection,
+0x10 accel service, +0x84 fb index, +0x88/+0x8c get_config outputs (0x8c later
reused), +0x4c 0x30-byte set_surface output, +0x1c snapshot of it (Reset),
+0x70 live struct +0x24.

`process_command_buffer` (ATIR32DContext 0x14170, vtbl +0x5ac):
- op 0x02: flush/present request; header rewritten to 0xffffffff; returns 1
  (swap requested).
- op 0x09: word1 = display index (bounds check vs accel+0xc8 else 0); header
  becomes reg 0x180 (SRC_OFF_PITCH), word1 = accel+idx*0x30+0x130.
- op 0x0d: header becomes 0x180, word1 = [surface+0x12c]+0x28, or display struct
  (accel+0x108+idx*0x30)+0x28.
- op 0x01: terminator.
- Odd word count padded with 0xffffffff. Output descriptor: start=base+0x20,
  +4 ring buffer+0x20, +8 count, +0xc end.
- Everything else is raw (reg,val) pairs with NO register validation.

`ATIRagePro::submit_buffer` (0x12730, via submit_commands 0x126f0): PIO drain,
register base accel+0x550, at most 8 words per chunk; waits on FIFO_STAT (0x310)
low 16 bits vs a thermometer threshold with thread_block between polls;
pseudo-register 0x1fc = wait GUI_STAT (0x338) bit0 == 0 (idle); writes via
`stwbrx val,[base+reg]` with NO masking of reg. Returns stamp (accel+0x50,
post-incremented). Rejects counts > 0x4000. The (-1,-1) pad pair writing at
base-1 is not established. Register base: `ATIRagePro::start` (0x11ec0) maps
BAR0+0x7ffc00 length 0x400 into accel+0x558/0x550 (our `fRegs` is BAR0+0x7FF800,
so stream reg r = our offset - 0x400). Chip id at reg 0xe0 (0x4759 GY, 0x4c47 LG,
0x4c49 LI).

### Pseudo-register 0x1fc inside kernel-built streams
`build_swap` (0x1534c, 0x15588) and `flush_surface` (0xc574 area) emit 0x1fc as
a wait-idle marker between state changes (e.g. before DP_PIX_WIDTH 0x2d0).
(`disp_mode_did_change` 0x49f4 `addi r9,r30,0x1fc` is an unrelated struct offset.)

### `submit_context_buffer` (0x14468)
When ctx buffer header +0x1c bit0 is set, rebuilds dest state and clears it.
Pairs: (0x100 DST_OFF_PITCH from [surf+0xc4] or display +0x130),
(0x2d0 DP_PIX_WIDTH = b|b<<8|b<<16, b = pixcode byte [surf+0xc8] or display
+0x134; stock has no 0x6000xxxx high bits, unlike our RAGE_PIXW_*),
(0x2a8 SC_LEFT_RIGHT = (w-1)<<16), (0x2b4 SC_TOP_BOTTOM = (h-1)<<16), pad -1 if
odd. `invalidate()` (0x14744, +0x5a4) sets bit0.

### Per-op GA streams (plugin)
`rProCopy` 0x15e0, `rProCopyRegion` 0x1960, `rProFill` 0x1d68, `rProSolidLines`
0x2294, `rProSolidScanlines` 0x2568. Registers emitted by the plugin:
DST_Y_X 0x10c, DST_HEIGHT_WIDTH 0x118 (trigger), SRC_Y_X 0x18c, SRC_WIDTH1
0x190, line regs 0x120/0x124/0x128/0x12c, DST_CNTL 0x130, SRC_CNTL 0x1b4,
PAT_REG0/1 0x280/0x284, DP_BKGD 0x2c0, DP_FRGD 0x2c4, DP_MIX 0x2d4, DP_SRC
0x2d8, CLR_CMP 0x300/0x304/0x308 (mask 0xffff for pixcode 3 else -1).
Kernel-injected: 0x180, 0x100, 0x2d0, 0x2a8, 0x2b4.
- rProCopy: op 0x0d, word1 0, pairs (0x1b4,0), (0x2d8,0x300), (0x2d4,0x70003).
- rProCopyRegion: op 0x09, word1 = this+0x84; requires dest == screen snapshot
  (size and pixcode).
- rProFill appends pairs to the current packet with no new header.
- Each op checks free space and maps type 0 to flush when low.

### GetBlitter (0x27e4; kind r5, mode r6 & 0x7FFFF000), table at 0x30c0
| condition | blitter |
|---|---|
| kind 1, mode 0 or 0x1000 | rProCopy (0x30d0) |
| kind 4, mode 0x1000 | rProCopyRegion (0x30cc) |
| (kind & ~0xF000)==0 and mode 0x6000, or ==0x100 and mode 0x5000 | rProFill (0x30c8) |
| kind 2, mode 0x6000 | rProSolidLines (0x30c4) |
| kind 3, mode 0x6000 | rProSolidScanlines (0x30c0) |
| else | 0x2c7 |

Blit types follow IOBlitType verbs (Rects 0, CopyRects 1, Lines 2, Scanlines 3,
CopyRegion 4) and IOBlitSourceType (Framebuffer 0x1000, Solid 0x6000, ...) in
the pulled headers.

## 6. Plugin use of the client (summary)
Selectors used: 0 SetSurface, 1 get_config (Start), 3 Swap, 4 AllocateSurface
scale (flags&2), 5 Lock, 6 Unlock, 7 WaitComplete (scalarI_structureI).
Map type 1 = context buffer, type 0 = command buffer/doorbell. Whether Panther
WindowServer ever calls AllocateSurface with flags&1 is not established. For
LI caps = 0x04000000, so the stock YUV rejection (caps&0x02000000, GY/LG) does
not apply.

## 7. Engine-state interaction (decision A5 input)
- `accel+0x74` is the single "current context" pointer shared by 2D contexts
  (switch counter accel+0x4b8) and GL contexts (accel+0x4b4).
- On switch the incoming context re-emits its own context buffer (lazy replay
  from shadow state). Nothing is saved back from hardware.
- Kernel operations that clobber engine state (flush_surface,
  invalidate_contexts, update_contexts, build_swap, set_surface, prune_buffers,
  stop, `disp_mode_did_change` 0x49a8) null accel+0x74 and/or invalidate every
  context (+0x5a4), so the next submit rebuilds state.
- Stock model = replay-from-shadow, not save/restore.
- Per-op register set per context is the list in section 5; dest/scissor
  (0x100, 0x2d0, 0x2a8, 0x2b4) are kernel-owned.

## 8. O2: can a third-party kext subclass IOAccelerator?
Yes. IOGraphicsFamily exports `__ZN13IOAccelerator10gMetaClassE`,
`__ZTV13IOAccelerator` and the ctors (pulled IOGraphicsFamily symbols). Stock
imports `IOAccelerator` from IOGraphicsFamily; IONDRVSupport is needed only for
the `IONDRVFramebuffer` metaclass reference (stock newer-style set_macrovision
and findFramebuffers). Our `com.local.ATIRageProLT` already subclasses
IOAccelerator and links iokit, mach, IOPCIFamily, IOGraphicsFamily and is loaded
on the G4. No additional OSBundleLibraries needed unless IONDRVFramebuffer
metacasts are wanted.

## 9. Implications for our kext
- Our current guard allow-list (`common/rage_guard.h`) is 3D-oriented and does
  not allow the 2D stream registers; B1 needs a separate 2D policy plus stream
  validation (also dest/scissor are kernel-owned, so clients must not set them).
- Src rect reach of SRC_Y_X/SRC_WIDTH1 is unclamped in stock; whether the engine
  address wraps is not established, so clamp in the validator.
- Stock does no validation at all (raw reg index into MMIO); we must not copy that.
- Our surface table has no VRAM backing; surface-mode 2D needs it. A
  display-destination-only first scope is feasible (ops 0x09/0x0d).

## 10. Not established
- Plugin consumer and ACCF type usage in CoreGraphics/WindowServer.
- Meaning of lock out1, get_config +0x9c, struct +0x10 units, set_surface mode
  bits beyond 0x800/0x400, surf+0x164/0x168/0x180 internals.
- Whether repeated type-0 maps leak or are coalesced.
- Behaviour of the (-1,-1) pad pair; macrovision vtbl slot 0x70c.
- IOProviderClass matching via metaCast.
- Engine address wrap behaviour for out-of-range source rects.
