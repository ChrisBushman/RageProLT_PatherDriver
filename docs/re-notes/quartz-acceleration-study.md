# Quartz acceleration study (checklist B1a)

Static analysis only (Nov 26). Nothing was run or loaded on the G4 rig; the binaries were pulled with `scp` and the registry/defaults were read
with plain `ssh` (`ioreg -l`, `defaults read`, `ls`, `strings`). Disassembly: a hand-rolled Mach-O reader plus Capstone (PPC, big endian), with
the `bdnzl/mflr` PIC base and `lis/addi` constants tracked so strings and globals resolve. All addresses below are link addresses inside the named binary.

Binaries (Panther 10.3.9, build 7W98, rig `PowerMac3,6`, pulled from the rig):

| name | file on the rig | md5 |
|---|---|---|
| CG | `ApplicationServices.framework/.../CoreGraphics.framework/Versions/A/CoreGraphics` (3588040 B) | 486fb282bef19f761c1ad3966228d5be |
| WindowServer | `.../CoreGraphics.framework/Versions/A/Resources/WindowServer` (26588 B) | f16527b6141f820f1662b76d6476d667 |
| IOKit | `IOKit.framework/Versions/A/IOKit` | 0ff54c6807eadf9517c102c2a4bc6587 |
| OpenGL (CGL) | `OpenGL.framework/Versions/A/OpenGL` | 535f86e8c69decbc780d34b69bf663ce |
| GLEngine | `OpenGL.framework/Resources/GLEngine.bundle/GLEngine` | 27a063c23a6176d4a6059d690c4f0d4b |
| Apple GL drivers (for comparison) | `/System/Library/Extensions/ATIRage128GLDriver`, `ATIRadeonGLDriver`, `GeForceFXGLDriver` bundles | 5fad0d1a..., 5c9bdbae..., 9804fb2c... |
| our installed bundle | `ATIRageProGLDriver.bundle` | b85b629cef718aa7b913958f18d8f1d4 |
| `CoreGraphics.framework/Resources/Configuration.plist` | text | a0f09d063304d7acbd86f31a6dc2ec90 |

WindowServer itself is 26 KB: `main` (0x4c34) only calls `_CGXServer` in CoreGraphics (0x4c40). **All decision code is in CoreGraphics.**

## 0. Verdict (short)

* Quartz Extreme (QE, the GL window compositor) is gated by a chain of checks that are all in CoreGraphics (section 1). With the stock Rage LT Pro setup
  **it is off for four independent reasons**: the display's registry ancestry has no `IOAGPDevice` (the Lombard's chip is on PCI), 8 MB < the 16 MB
  minimum, our `gldGetRendererInfo` lacks the bit and the field CoreGraphics tests, and our pixel-format list has no node of the shape it asks for.
* All four are changeable (a plist edit, a driver edit, a driver edit, a driver edit), so QE can be **made to switch on** in principle.
  Whether it would be a **win** is doubtful: the compositor needs `EXT_texture_rectangle` (we run it on the CPU), puts every window into a texture
  (a desktop at 1024x768 needs about 3 MB at 16 bpp, 6 MB at 32 bpp, of the ~4.4 to 5.9 MB left after the scanout), and the chip's textured fill is 35-38 Mpix/s.
* **Correction to the Nov 26 addendum in `ga-2d-client-decode.md`:** WindowServer/CoreGraphics **do** use the 2D plugin (the type-2 client). The plugin's
  type UUID is built inside **IOKit.framework** (`IOPSAllocateBlitEngine`, 0x905cc5b8), not in CoreGraphics, so the earlier immediate-byte scan of
  CoreGraphics could not see it. CoreGraphics calls `IOPSAllocateBlitEngine` (0x93789e08 in `_CGXMappedDisplayStart`, 0x938eeeb8 in `_blitEngine`) and drives
  the blitter from its `MPHWFill` / `MPHWCopyRegion` / `MPHWCopy` / `MPHWFillRegion` / `MPHWSyncPixels` / `MPHWCopyPixels` hooks. So the plugin (B1b)
  accelerates **Quartz 2D** screen fills and screen-to-screen copies (scrolling, window moves), not only X11. This is the cheaper, more certain way to a
  hardware-assisted Lombard desktop than QE (section 4).

## 1. The Quartz Extreme decision chain

### 1.1 Configuration inputs

`CGXCoreGraphicsConfiguration` (0x93787480) loads `CoreGraphics.framework/Resources/Configuration.plist`. Its content on the rig:

```
GLCompositorConfiguration = { GLCompositorMinimumDisplayDepth = 16, GLCompositorMinimumVRAM = 16 (MB),
                              GLCompositorRequiredClasses = [ IOAGPDevice ], GLCompositorUseOnPortable = true,
                              tileHeight = 256, tileWidth = 256 }
```

(The key names were read from the CFString constants at 0xa369a430/…440/…450/…460/…470/…480/…4b0 and `strings`.) The **minimum VRAM
is a number in this file**, not code: 16 MB is the stock value and also the built-in default (`lis r0,0x100` = 0x1000000 at 0x9378b1f4 and 0x9378b210
when the key is absent). The code path never compares against any other VRAM constant. There is no chip or vendor whitelist/blacklist, no GL extension
string check, and no environment variable (the only `getenv` calls in CoreGraphics are `CG_FONT_LIBRARY`, `CG_FONT_SYMBOL`, `CGS_LIBRARY_PATH`).

User override: `CGXGLCompositorInitialize` (0x9378c2d0) first asks `CGXPreferenceForKey("GLCompositor")` (the WindowServer preferences,
`/Library/Preferences/com.apple.windowserver.plist`; not present on the rig) for `tileWidth`/`tileHeight`, falls back to the plist, and on a portable
(`CGSIsPortableMachine`, 0x9378c408: `IOPMCopyBatteryInfo` returns an array) honours `GLCompositorUseOnPortable` (false -> both tile sizes 0).
`CGXGLCompositorSetTileSize` (0x9378c4dc) with a zero size sets `glDefaultTileWidth/Height` = 0 and clears the per-display "GL" byte (`dev->8[0xd3]`),
which disables the compositor (checked first in 1.5). `CGSSetDebugOptions` exists as an SPI; the debug bit 0x100 disables the HW 2D path (4.1),
and a log line in `updateGLVRAMFlag` is guarded by `gCGXDisplayDebug`. Which option number sets `gCGXDisplayDebug` was not established.

### 1.2 Gate A: the display must sit under an `IOAGPDevice` (`isAccelUsable`, 0x93793434)

Called per display from `CGXGLDisplayContextInitialize` (0x93792d74) with the framebuffer service. Reads `GLCompositorRequiredClasses` and
`GLCompositorExcludedInstances`; if both are empty or missing it returns 1. Otherwise it walks from the framebuffer up the IOService plane
(`IORegistryEntryGetParentEntry`, 0x93793590..0x937935f0), compares each node's **class name** (`IOObjectGetClass`) against the required array and each
node's **registry name** against the excluded array (excluded match returns 0 at once). It returns 1 only if some ancestor's class is in the required list.
Result: **the display needs an `IOAGPDevice` ancestor.**

* Rig, the GeForce display: `AppleMacRiscAGP -> NVDA,Parent@10 <class IOAGPDevice>` (ioreg line 607). Passes.
* Rig, the Rage: `.../AppleMacRiscPCI -> pci1002,4c49@15 <class IOPCIDevice> -> RageStubFB`. No `IOAGPDevice` anywhere above it. **Fails.**
* The Lombard: `design` notes (`gl-feature-audit.md` machine line, `apple-ndrv-framebuffer.md`) record the chip on the **PCI** bus under the Grackle bridge, so the
  same result is expected. Not measured on a Lombard.

### 1.3 Gate B: VRAM (`updateGLVRAMFlag`, 0x9378b180; called from `_MPInitScreen` 0x9378abfc and `_MPEndDisplayConfiguration` 0x937990d8)

Sets or clears bit 0x02000000 of `dev+0x2c`. In order:

1. `min` = `GLCompositorMinimumVRAM << 20` (default 16 MB) (0x9378b1ec..0x9378b214).
2. Only continues if `dev->8[0xc8] != 0`, `dev->8[0xc9] != 0`, `dev->8[0xcf] == 0`, `dev->8[0xce] == 0` (0x9378b288..0x9378b2b4). From the writers:
   0xc8 = copy of bit 31 of a word in the display record (`_CGXDisplayDriverInitialize` 0x93788c24, `_reconfigureDisplays` 0x93791344), 0xc9 = bit 29 of the same word
   (0x937914b0), 0xce is cleared by `_MPSelectMode` after setting the `mirr` attribute (0x937959c8..0x937959ec) so it is a mirroring flag, 0xcf is set in
   `_CGXDisplayWillSleep` (0x938d9d68) so it is an asleep flag. The exact meaning of 0xc8/0xc9 is not established.
3. `vram = *(dev->0x80 + 0xb8)`. That field is the **length returned by `IOConnectMapMemory(fb connect, 110 = kIOFBVRAMMemory, ...)`** in `_CGXMappedDisplayStart`
   (0x93789af8, stored at 0x93789d90). So the framebuffer driver's VRAM range as seen through its user client is what counts, not `IOFBMemorySize`.
4. `vram < min` -> flag cleared ("Not OK"). `vram >= 2*min` -> flag set. `min <= vram < 2*min` -> set only if `CGXAcceleratorForDisplayDevice` succeeds **and no other active
   display shares the same accelerator** (loop 0x9378b310..0x9378b358); i.e. 16..32 MB is allowed for one display per accelerator.
5. Closed clamshell: if `GLCompositorConfiguration/AllowClosedClamshellGLWindowAccel` is not true, the machine is a portable, exactly one display device is online and
   `dev->8[0xd0] == 0`, the flag is cleared (0x9378b36c..0x9378b3a4). `dev->8[0xd0]` is set in `_getDisplayDeviceInfo` (0x9378af58) from
   `IODisplayCreateInfoDictionary`'s `IODisplayConnectFlags & 0x800`, and `IOGraphicsTypes.h` on the rig defines `kIOConnectionBuiltIn = 0x00000800`. **On the Lombard the built-in LCD
   must report `kIOConnectionBuiltIn`, or QE is refused whenever it is the only display.** Whether the Lombard NDRV sets it: not established (the rig is not a portable; its
   `AppleDisplay` shows `IODisplayConnectFlags = 0`, and `CGSIsPortableMachine` is false there, so the rule is inactive).

With stock numbers an 8 MB framebuffer fails step 4. **Minimum VRAM is therefore only a configuration value: setting `GLCompositorMinimumVRAM` to 8 (or lower) makes
8 MB pass** (and 8 MB sits in the `min..2*min` window, so one display per accelerator). The level that QE is designed for is the stock 16 MB; the code has no other reason for it
(measured need: section 5).

### 1.4 Gate C: the GL side (`CGXGLDisplayContextInitialize`, 0x93792a90)

For every display that has an accelerator (`CGXAcceleratorForDisplayDevice`) it runs `cglsInitialize` (0x9379319c: loads
`/Frameworks/OpenGL.framework/Resources/GLEngine.bundle/GLEngine` and looks up `gliChoosePixelFormat, gliDestroyPixelFormat, gliQueryRendererInfo, gliDestroyRendererInfo,
gliCreateContext, gliDestroyContext, gliAttachDrawable, gliSwapBuffers, gliSetInteger, gliGetInteger, gliGetVersion`; it requires version 2.3), then
`cglsChoosePixelFormat(&pf, attribs)` with `_attribs.0` (0x9394e2f8) = `{0x50 (kCGLPFAWindow), 0x14,8, 0x15,8, 0x16,8, 0xc,0, 0xd,0, 0}`: **window, 8/8/8 colour, depth 0, stencil 0**.
Per display index `i` (mask `1<<i`) it then calls `cglsQueryRendererInfo(&info, 1<<i)` and keeps the display only if all of this holds for the **head entry of the returned list**
(0x93792d80..0x93792d98):

| test | CoreGraphics site | meaning (from GLEngine `gliQueryRendererInfo` 0xed314 and `CGLDescribeRenderer`/`CGLDescribePixelFormat` jump tables) |
|---|---|---|
| `info+8 & 0x1000` | 0x93792d84 | a flag word the **driver** fills in `gldGetRendererInfo` (GLEngine calls the driver at `driver+0x120` and only adds `0x20000` to `+4` and the `next` pointer at `+0`). Bit 0 = Window, 1 = FullScreen, 2 = OffScreen, 4 = MPSafe, 8 = Accelerated(0x100), 10 = Compliant(0x400). **Bit 12 (0x1000) is not exposed by CGL**; it is set by the Radeon driver (`ori r9,r9,0x1000` at 0x13dd0 in its post-fill routine 0x13db8) and the GeForceFX driver (`ori r9,r9,0x5000`, 0x1bc18). **The Rage 128 driver does not set it** (its routine 0xdf78 only sets `+0x1c |= 1` and `+0x24 = 2`), so by this test the Rage 128 is not a QE renderer. |
| `info+0x30 >= GLCompositorMinimumVRAM` | 0x93792d90..0x93792d98 | `+0x30` is the CGL "video memory" property. The compare uses the raw plist number (16), so with a byte count (CGL documents bytes) it passes for any non-zero value; if a driver stores MB it must be >= 16. Apple's drivers store the **third scalar** of their kext `get_config` (0x48(r1) at 0x64b0 in the Rage 128 driver) there. Which unit Apple's kext uses: not established. |
| `isAccelUsable` | 0x93792d74 | Gate A |
| pixel-format node: `+0x30 & (1<<i)`, `+8 & 0x111`, `+0x10 in {0x8000, 0x4000, 0x400, 0x200}`, `+0x18 == 1`, `+0x1c == 1`, short `+0x22 == 0` | 0x93792dc0..0x93792e28 | the node fields as `CGLDescribePixelFormat` reads them: `+0x30` display mask, `+8` capability flags (Window 1 / MPSafe 0x10 / Accelerated 0x100), `+0x10` colour-mode bit (ARGB8888 0x8000, RGB888 0x4000, ARGB1555 0x400, RGB555 0x200), `+0x18` depth modes (1 = the **0-bit** depth mode), `+0x1c` stencil modes (1 = 0-bit), `+0x22` aux buffers. In words: it wants an accelerated window format with **no depth, no stencil, no aux buffers**, in a 32-bit or a 15/16-bit colour mode, one of each (flags `r25` bit 0 = 32-bit, bit 1 = 16-bit; stops when both found, 0x93792fec). |

The two chosen nodes become the compositor's context (`_cglsContext`, created at 0x9379308c), stored per display in `glContexts` (24-byte entries: renderer id, VRAM, context, tag 4 = 32-bit, 3 = 16-bit).
A display with a 32-bit or a 16-bit window format is enough for its own scanout depth; `glMinimumDisplayBitDepth` (plist, default 32 if the key is missing) is stored at 0x93792d00 and
`dev->8[0xd3]` is set for devices whose depth is below it (0x93792ec4).

### 1.5 Gate D: the per-display lookup every time (`__CGXGLDisplayContextForDisplayDevice`, 0x936bd51c; wrapped by `CGXGLAccelForDisplayDevice`, 0x936bdae4)

Returns the compositor context for a display only if all of: `glDefaultTileWidth/Height` non-zero (0x936bd544..0x936bd558) -> contexts exist (initialises them on first use, 0x936bd570)
-> `dev+0x2c` bit 0x10000000 clear (0x936bd580; what sets it: not established) -> `dev->8[0x1c]` (the mode description's depth, host-swapped) `>= glMinimumDisplayBitDepth` (0x936bd5a4;
15-bit modes: value not established) -> `dev->8[0xcf] == 0` (not asleep) -> **`dev+0x2c` bit 0x02000000 set (Gate B)** (0x936bd5b8) -> a context entry whose two key words match the display.
Callers: `CGXAcquireDisplayDeviceSurface` (0x9368fa60; takes `CGXGLAccelAccess` so a captured display suspends the compositor), `__CGXActivateSurfaces` (0x9370aa60), `CGXGLDisplayDeviceVRAMSize` (0x938c86c8).

### 1.6 Apple's own renderers, for reference (`gldGetRendererInfo`)

| driver | `+4` (renderer id; GLEngine ORs 0x20000) | `+8` flags | `+0x30` / `+0x34` |
|---|---|---|---|
| ATIRage128 (0x6378) | 0x1000 (0xa5cc) | 0x2513 (no 0x1000) | 3rd / 2nd scalar of kext method 3 |
| ATIRadeon (0x6594) | 0x1200 (0xe94c) | 0x2513 `\|` 0x1000 = 0x3513 | same |
| GeForceFX (0x7078) | (0x3cad4) | 0xa513 `\|` 0x5000 = 0xf513 | same |
| **ours (installed bundle, 0x2ea8)** | **0 (never set)** | **0x2540** | **`+0x30` = 0, `+0x34` = 0x770000 (usable VRAM end), `+0x38` = half** |

`gldriver/gld.h` says `video_memory` is at 0x30, but with its `_pad[0x14]` the struct puts it at **0x34** (and `texture_memory` at 0x38); Apple's layout (CGL `kCGLRPVideoMemory` reads `+0x30`,
`kCGLRPTextureMemory` `+0x34`) is one word lower. The comment "renderer_id 0x08" is also wrong: `+8` is the capability-flag word, `+4` the renderer id. The effect today: CGL reports 0 video memory for our
renderer, and CoreGraphics' test `info+0x30 >= 16` and `info+8 & 0x1000` both fail for it.

## 2. What the rig shows (read-only)

* Accelerator link works as the code needs it: framebuffer `RageStubFB` has `IOAccelIndex = 0`, `IOAccelRevision = 2`, `IOAccelTypes = "IOService:/.../pci1002,4c49@15/ATIRagePro"`; the node `ATIRagePro <class RageLTDevice>`
  has `IOMatchCategory = IOAccelerator`, `IOGLBundleName = ATIRageProGLDriver`, `IOAccelRevision = 2`, `RageLTFeatures = 255`. `IOAccelFindAccelerator` (IOKit 0x905bd450) needs exactly these (`IOAccelTypes` path, node conforms to
  `IOAccelerator`, `IOAccelIndex`), and `MPAcceleratorForDisplayDevice` (0x936ce830) caches the result in `dev->0x80+0x94/0xb0`. So `CGXAcceleratorForDisplayDevice` succeeds.
* `IOFBMemorySize = 8388608` on `RageStubFB` (a registry property; CoreGraphics uses the VRAM-memory mapping length, section 1.3, not this property).
* No `IOCFPlugInTypes` on `RageStubFB` or the accelerator; the GeForce framebuffers carry `{"ACCF0000-0000-0000-0000-000a2789904e" = "GeForceGA.plugin"}` (ioreg lines 681, 776, 889). Without it
  `IOPSAllocateBlitEngine` fails and CoreGraphics clears the hardware-2D flags (section 4.1).
* The Rage's ancestors contain no `IOAGPDevice` (the GeForce's do); the only `IOAGPDevice` in the registry is `NVDA,Parent@10`.
* `/Library/Preferences/com.apple.windowserver.plist` has only `DisplaySets` (the Rage display listed as `Unit 2, Width 640, Height 480, Depth 3, IOFlags 519`); no `GLCompositor` key; `defaults read` for
  `com.apple.windowserver` (user) and `-g CGSDebug` do not exist.
* `/var/log/windowserver.log` (where `CGSLogMessage` would write the `updateGLVRAMFlag` line, 0x9378b454: `"%s: 0x%x: %d bytes VRAM; %s for OpenGL window accel"`) exists but is root-only (`Permission denied`); the line is
  also guarded by `gCGXDisplayDebug`. So the decision result itself could not be observed. A read of that log with root, with the debug flag on, would show the VRAM byte count and OK/Not OK directly.
* `ps`: `WindowServer -daemon`, no special arguments.

## 3. What the compositor demands from the GL driver (once the gates pass)

From the tile/texture code in CoreGraphics (`CGGLCreateBackingStoreTiles` 0x936e6f34, `CGXGLCreateBackingTexture` 0x93706194, `CGXGLCreateTileTexture` 0x93758dec, `CGXGLCreateSurfaceTexture` 0x938c82b0):

* **Window contents are tiles of 256x256** (`tileWidth/tileHeight` from the plist; `adjustTileSize` 0x9378c5ac) uploaded as **`GL_TEXTURE_RECTANGLE_EXT` (0x84f5)** textures, `GL_NEAREST`/`GL_LINEAR` only (no mipmaps),
  with `GL_UNPACK_CLIENT_STORAGE_APPLE` (0x85b2) and `GL_TEXTURE_STORAGE_HINT_APPLE = GL_STORAGE_CACHED_APPLE` (0x85bc/0x85be), `GL_UNPACK_ROW_LENGTH/SKIP_ROWS/SKIP_PIXELS` (0xcf2..0xcf4), a texture parameter 0x84fe.
* Formats: 32-bit `GL_RGBA8` (0x8058) from `GL_BGRA` (0x80e1) / `GL_UNSIGNED_INT_8_8_8_8_REV` (0x8367); 16-bit `GL_RGB5` (0x8050) from `GL_BGRA` / `GL_UNSIGNED_SHORT_1_5_5_5_REV` (0x8366); `GL_ALPHA`/`GL_UNSIGNED_BYTE` tiles
  (0x1906/0x1401); `yuvs`/`2vuy` video surfaces. `CGXBackingStoreAccelerate` (0x938c94b8) only accepts 32-bit (0x20) backing stores.
* CoreGraphics imports **no** `gl*`/`CGL*` function and checks **no** extension string: it assumes the renderer it accepted in 1.4 supports all of the above. If a texture upload fails the code path was not traced (not established).
* Our driver today: `EXT_texture_rectangle` is advertised but **shaded on the CPU, level 0 only** (`docs/gl-feature-audit.md`, "ARB_texture_cube_map, texture_rectangle, NPOT | tested (CPU shaded)"); the hardware needs power-of-two textures
  (`standalone-driver-design.md` L222-223: non-POT "Fails (garbage). Sources must be padded"). 256x256 tiles are POT, edge tiles are not.

## 4. CoreGraphics' surface and blitter paths (question 3)

### 4.1 Software-drawn windows do not go through the type-0 surface client; the 2D plugin does the hardware part

* CoreGraphics' "MappedDisplay" driver table `gMPProcs` (0xa369a5e8) holds `MPHWCopy` (0x938d4fe0), `MPHWFill` (0x9376c278), `MPHWCopyRegion` (0x937076d0), `MPHWFillRegion` (0x938d520c), `MPHWSyncPixels` (0x938d54ec),
  `MPHWCopyPixels` (0x938d55e8), `MPHWPurgePixels` (0x936c7edc), `MPMapFramebuffer` (0x9373a934), `MPAcceleratorForDisplayDevice`, `MPAccessSurfaceForDisplayDevice`, `MPFinishSurfaceForDisplayDevice`.
* `_CGXMappedDisplayStart` (0x93789940) maps the framebuffer (`IOConnectMapMemory`, types 110 and the aperture, 0x93789af8/0x93789b20), reads the pixel format into `mp+0xa4`
  (2 = other, 3 = 15/16 bit, 4 = 32 bit), sets `mp+0x178` = (`mp+0xa4 >= 2`) (0x93789d60; so **not for 8 bpp**; cleared by debug bit 0x100, 0x938d4894) and calls
  `IOPSAllocateBlitEngine(fb service, &mp+0x168, ...)` (0x93789e08). On failure `mp+0x168 = 0` and the hardware bits `0x60` of `mp+0xa0` are cleared (0x93789e24, 0x93789e44).
* `IOPSAllocateBlitEngine` (IOKit 0x905cc5b8) is `IOCreatePlugInInterfaceForService(fb service, type ACCF0000-...-000a2789904e, interface 6766E94A-...-000a2789904e)` followed by four `GetBlitter` calls:
  (kind 1, mode 0) copy -> `blit+4`; (0, 0x6000) fill -> `+8`; (4, 0x1000) copy region -> `+0x78`; (1, 0x2000) -> `+0xc`. These are exactly the rows of the stock plugin's `GetBlitter` table
  (`ga-2d-client-decode.md` section 5: copy, copy region, fill; **mode 0x2000 is not in the table and returns 0x2c7 = kIOReturnUnsupported**).
* Users: `MPHWFill` -> `IOPSBlitFill` (0x9376c318), `MPHWFillRegion` -> `IOPSBlitFill` (0x938d537c), `MPHWCopyRegion` -> `IOFBBlitSurfaceCopy` / `IOFBBlitVRAMCopy` / `IOPSBlitIdle` (0x93707a0c, 0x93707ba8, 0x93707c70),
  `MPHWCopy` -> `IOFBBlitVRAMCopy` (0x938d5148), `MPHWCopyPixels` -> `IOFBBlitSurfaceSurfaceCopy` / `IOFBBlitMemorySurfaceCopy` (0x938d57a4, 0x938d5998), `MPHWSyncPixels` -> `IOFBBlitMemorySurfaceCopy` + `IOFBSynchronize` (0x938d5594, 0x938d55bc),
  `MPPing`/`MPRebuildDevice` -> `IOPSBlitIdle`/`IOPSBlitReset`. When a blit returns `kIOReturnUnsupported` (0xe00002c7), `MPHWFill` logs "unsupported" and clears the capability bit (0x9376c344..) and the CPU path takes over.
* `_blitEngine` (0x938eee38) is the per-process variant (applications that map the display, e.g. captured-display drawing): same call, skipped when the application flavour is 1.
* With the stock plugin the **supported set is therefore: fills, screen-to-screen copies, copy region**; **memory-to-screen (the software window flush, mode 0x2000) is declined**, so pushing a software window's pixels to VRAM stays a CPU
  copy through the mapped framebuffer (`CGSCopyDRAMToVRAM` / `vecCGSCopyDRAMToVRAM`, 0x936ea028 / 0x936fb090, AltiVec; the only caller found is `CGSSwapClassicBuffer`/`CGSFillClassicBuffer`; the composition of ordinary windows into the mapped
  framebuffer was not traced function by function: not established).
* Our rig: no `IOCFPlugInTypes` -> no blit engine -> CoreGraphics does everything with the CPU. On a real Lombard with the stock accelerator the plugin is loaded, so **stock Quartz 2D there should already
  use the blitter for fills and copies** (not run; static).

### 4.2 The type-0 surface selectors (IOKit stubs) and who calls them in CoreGraphics

Selector numbers read from the IOKit user-side stubs (the `li r4, N` before `io_connect_method_*`) and cross-checked against `kext/RageLTSurfaceClient.cpp`'s table:

| IOAccel call | selector (IOKit) | CoreGraphics callers | our client now |
|---|---|---|---|
| `IOAccelCreateSurface` (0x905bd908; opens a new connection, method 7 `ScalarI(2) ScalarO`) | 7 `SetIDMode` | `allocateBackingSurface` (0x938c9320), `MPAccessSurfaceForDisplayDevice`, `CGXBindSurface` | ok |
| `IOAccelSetSurfaceFramebufferShape` (0x905a35a4) | 9 `SetShape` | `_CGXActivateSurfaces`, `CGXAcquireDisplayDeviceSurface`, `_CGXSynchronizeSurfaceVisibility`, `MPFinishSurfaceForDisplayDevice` | ok (stores bounds/rects) |
| `IOAccelSetSurfaceFramebufferShapeWithBacking` (0x905d68a8) | 6 `SetShapeBacking` | `updateBackingSurface` (0x938c9134) | stores the shape, ignores the backing |
| `IOAccelSetSurfaceScale` (0x905d686c) | 8 | `CGXAcquireDisplayDeviceSurface` | accepted, ignored |
| `IOAccelFlushSurfaceOnFramebuffers` (0x905bdaa8) | 10 `Flush(doSwap, x)` | `_CGXFlushSurface` (0x936adb9c), `MPFinishSurfaceForDisplayDevice` (0x936bdb5c) | returns success, does nothing (our bundle presents through the GL client) |
| `IOAccelQueryLockSurface` (0x905ccc1c) | 11 | `CGXAcquireDisplayDeviceSurface` | success |
| `IOAccelReadLockSurface` / `ReadUnlockSurface` | 12 / 13 | `_CGXNextSurface` (surface enumeration) | **Unsupported** / success |
| `IOAccelWriteLockSurface` / `WriteUnlockSurface` | 14 / 15 | `CGXAcquireDisplayDeviceSurface`, `CGXReleaseDisplayDeviceSurface` | **Unsupported** / success |
| `IOAccelReadLockSurfaceWithOptions` / `ReadUnlock...` | 0 / 1 | `_CGXBackingStoreDataLock`, `_synchronizeBackingSurface` | **Unsupported** / success |
| `IOAccelWriteLockSurfaceWithOptions` / `WriteUnlock...` | 3 / 4 | `lockBackingSurface`, `discardBackingSurface`, `_CGXBackingStoreDataLock` | **Unsupported** / success |
| `IOAccelReadSurface` (0x905d6a1c) | 5 | `_CGXBackingStoreAlphaTest` (0x936e4938) | **Unsupported** |
| `IOAccelDestroySurface` (0x905bd9b4) | (returns `kIOReturnUnsupported` locally; the connection is closed instead) | various | n/a |
| `IOAccelWaitForSurface` (0x905bd9d4) | (returns 0 locally) | `_CGXOrderSurface`, `_CGXShapeSurface`, `_CGXUpdateSurfaceVisibility` | n/a |

What these surfaces are: GL window surfaces (each GL drawable creates a CGS surface; `CGXFlushSurface` flushes them) and, optionally, **accelerated window backing stores** (`CGXBackingStoreAccelerate`, 0x938c94b8: 32-bit backings only;
reached only from `_CGXSynchronizeWindowBackingStore` (0x936ac148) with the client's option 4/5, and only when a global session byte (`gCGXCurrentSession+0x2f`, meaning not established) is set; the user-side API is
`CGSLockWindowAccelerator`/`CGSSynchronizeWindowAccelerator`, used by `libOpenGLContext`'s hardware-accelerated window contexts). A window that WindowServer draws in software **never reaches the type-0 client**.

### 4.3 Could the CPU copy be taken over with the blitter / DMA?

* **Fills and screen copies: yes, through the plugin (B1b).** CoreGraphics already routes them to the plugin when it exists; the type-2 client, `GetBlitter` rows copy / fill / copy region, and the 2D kernel engine are what is needed; nothing in the type-0 client changes.
* **Memory to screen (the window flush): not through the stock interface.** The memory-surface copy (`IOFBBlitMemorySurfaceCopy`, 0x936fc468) pins the source pages (page-size rounded, a cached table `lastMemory`) and asks the plugin for mode 0x2000/0x80 blits; the stock Rage Pro plugin
  declines that mode. A DMA host-to-VRAM copy engine exists in our kernel client (`docs/re-notes/dma-upload-design.md`, `texture-dma-upload-standalone.md`); offering `GetBlitter(kind 1, mode 0x2000)` through the plugin, backed by that, is **the one place where taking over the
  CPU copy is possible**, but it needs the plugin's memory-descriptor protocol decoded (`IOFBBlitMemorySurfaceCopy` and plugin selectors) first. Not decoded here.
* The remaining per-pixel drawing and compositing into the mapped framebuffer by WindowServer itself (AltiVec) is outside the driver's reach.

## 5. VRAM budget for QE at 1024x768 (question 4)

Facts used: usable VRAM end 0x770000 = 7,798,784 B = 7.44 MiB (`RAGE_VRAM_USABLE_END`, kernel `selInfo`); the NDRV places the scanout at 0x800 (16 bpp) / 0x1000 (32 bpp) because `[0, fb_base)` holds the hardware-cursor image (`apple-ndrv-framebuffer.md` D1-D3);
measured on the G4: "free VRAM at 1024x768x32 is about 4.4 MB" (`apple-ndrv-framebuffer.md`, Chess incident). The compositor draws straight into the scanout (the compositor's `attribs` have no double-buffer flag), so no extra colour buffer.

| item | 16 bpp | 32 bpp |
|---|---|---|
| scanout 1024x768 | 1,572,864 (1.50 MiB) | 3,145,728 (3.00 MiB) |
| cursor block + base offset | 2,048 | 4,096 |
| **free for textures + GL surfaces** | **6,223,872 (5.94 MiB)** | **4,648,960 (4.43 MiB)** |
| a full-screen window as 256x256 tiles (12 tiles) | 1,572,864 | 3,145,728 |
| light desktop (full-screen desktop window, 1024x22 menu bar, 600x70 Dock, two 640x480 windows): exact pixels | 2,930,720 (2.79 MiB) | 5,861,440 (5.59 MiB) |
| same, each partial tile padded to power-of-two (hardware needs POT) | 3,112,960 (2.97 MiB) | 6,225,920 (5.94 MiB) |
| same, every tile a full 256x256 | 4,063,232 (3.88 MiB) | 8,126,464 (7.75 MiB) |
| GL window on top (640x480 target+Z, 16/16 / 32/16 / 32/32) | 1,228,800 | 1,843,200 / 2,457,600 |

Reading: **at 32 bpp QE does not fit** (a light desktop is larger than all free VRAM before any GL window; and the stock compositor has no texture paging beyond what the driver does). **At 16 bpp it fits** with about 3 MiB left for
GL targets, which is roughly one 640x480 16/16 GL window plus slack (the Chess case showed two GL windows of 740x680 and 630x418 already exceed 4.4 MiB at 32 bpp). A busy desktop (more or larger windows) overflows 6 MiB even at 16 bpp.
These are estimates from window sizes I chose; the real window set, whether CoreGraphics keeps textures for hidden or minimised windows, and the tile padding are not established. A texture manager that evicts (host copy kept by `GL_UNPACK_CLIENT_STORAGE_APPLE`, the data
stays in WindowServer memory) is the safety net; our kernel allocator is per-context and has no eviction.

Levers, cheapest first:
1. **`GLCompositorMinimumVRAM` in `Configuration.plist`** (16 -> 8 or less) and **drop `IOAGPDevice` from `GLCompositorRequiredClasses`** (or add `IOPCIDevice`): the two stock-config gates. System-file edit, easy to ship and to revert; a Lombard-only installer step. `GLCompositorMinimumDisplayDepth` stays 16.
2. **Run the desktop at 16 bpp** (15-bit 1555 on this chip) whenever QE is wanted: halves both the scanout and the tiles. CoreGraphics itself picks the 16-bit tile format for 16-bit displays (the `GL_RGB5` / `1_5_5_5_REV` branch of `CGXGLCreateBackingTexture`, 0x93706230..0x93706238); whether it is chosen from the display depth or from the pixel-format tag (3 vs 4, 1.4) was not
   traced to the end. Lower resolution (800x600: 0.92 MiB / 1.83 MiB scanout) buys another 0.6 / 1.3 MiB but the Lombard's panel scaler makes that look soft (`apple-ndrv-framebuffer.md` L161).
3. **Smaller tiles** (`tileWidth/Height` 128, plist or `GLCompositor` preference): cuts padding waste (edge tiles) at the cost of more textures and draw calls; with POT padding in the driver it wastes less.
4. **Window-count / hidden-window limits**: not available as a setting; would be a driver-side eviction of textures of hidden windows (re-upload from client storage).
5. **Texture format**: `GL_RGB5` is already the smallest the compositor asks for; compressed textures are not used by CoreGraphics.
6. Keep the **NDRV layout** and the top reservation `[0x770000, 0x7ff800)` as they are (the 0.5 MiB at the top is the chip's registers/scratch, not free).

## 6. Verdict, requirements, risks

**Can we make the Lombard's Quartz hardware accelerated?** In two different senses:

* **Quartz 2D hardware assist (fills, screen copies): yes, and the stock plugin already does it** where it loads. Needs the type-2 client (B1b): `IOCFPlugInTypes` on the accelerator and the framebuffer (`foundFramebuffer` copies it),
  the plugin bundle (`ATIRageProGA.plugin`, ships on the system; it talks to the type-2 context client in our kext), `GetBlitter` rows copy / fill / copy-region.
  The earlier statement that this is "X11 only" was wrong. Static; **runtime not tested**, and whether the stock plugin actually accelerates these on a Lombard is not measured.
* **Quartz Extreme (GL compositor): technically yes, practically not recommended yet.** It needs all of:
  1. `Configuration.plist`: drop (or satisfy) `GLCompositorRequiredClasses = IOAGPDevice`; lower `GLCompositorMinimumVRAM` to <= 8 (the code needs `vram >= min`; and it sits in the single-display `min..2*min` window).
  2. A framebuffer whose `kIOFBVRAMMemory` mapping length is >= that minimum (Lombard NDRV value: not established), a display depth >= 16, and on a portable `IODisplayConnectFlags & kIOConnectionBuiltIn (0x800)` for the LCD or `AllowClosedClamshellGLWindowAccel = true`.
  3. `gldGetRendererInfo`: `+8 |= 0x1000`, `+0x30` = video memory (non-zero; MB vs bytes of Apple's drivers not established), fix the struct layout (`+4` renderer id, `+0x30/+0x34` memory), and make sure our renderer is the **head** entry returned for its display mask (the order of GLEngine's driver list was not traced).
  4. `gldChoosePixelFormat`: add nodes with depth mode exactly 1, stencil mode exactly 1, aux 0 (our four nodes have depth modes 0x400/0x1000), flags with Window (0x1) and Accelerated (0x100), colour mode 0x8000/0x4000 (32-bit) and 0x400/0x200 (16-bit), display mask covering the display index.
  5. GL: **hardware `GL_TEXTURE_RECTANGLE_EXT`** (pad to POT, scale coordinates), BGRA/8888_REV and 1_5_5_5_REV uploads, client-storage semantics, NEAREST/LINEAR, blend for window alpha. Today rectangle textures are CPU-shaded.
  6. 16 bpp desktop (section 5), or a texture eviction scheme at 32 bpp.
* **Expected benefit is uncertain.** The compositor adds a GL pass over every visible window per frame; with 35-38 Mpix/s textured fill (`gl-feature-audit.md`) a three-layer 1024x768 recompose is about 2.4 Mpix = about 60 ms, plus every window update still needs a CPU/DMA upload to VRAM (the data cannot stay in system memory for a PCI Rage, no AGP). The existing CPU path writes only the changed pixels. No measurement was made; this is arithmetic from numbers already in the repo.

Risks: (a) the plist edit touches an Apple system file (revertible, but a system-update would restore it); (b) QE routes every window through our GL driver: a driver fault takes the whole desktop with it, and the hot path (rectangle textures) is new;
(c) gate B/D read `dev->8` fields we cannot see from the registry, so a successful pass is only provable by the WindowServer log or by `CGDisplayUsesOpenGLAcceleration` (0x937825d8, `CGSDisplayStatusQuery(display, 0x10)`; not run);
(d) the installed bundle's `gldGetRendererInfo` layout bug (1.6) makes CGL report wrong video memory today, independent of QE.

### Not established (do not rely on these)

* What sets `dev+0x2c` bit 0x10000000 (a "no GL" bit tested at 0x936bd580), what `dev->8[0xc8]/[0xc9]` mean exactly, what `dev->8[0x1c]` holds for a 15-bit mode, what `gCGXCurrentSession+0x2f` is, which option number of `CGSSetDebugOptions` sets `gCGXDisplayDebug` or the 0x08000000 bit tested in `CGXAcquireDisplayDeviceSurface` (0x936902d8).
* Unit of Apple's `+0x30` (bytes vs MB), and the order of GLEngine's driver list (which renderer is the head entry).
* What `IONDRVFramebuffer` returns for `kIOFBVRAMMemory` on a Lombard, whether the Lombard NDRV publishes `kIOConnectionBuiltIn`, and the Lombard's registry ancestry (the PCI statement comes from earlier notes).
* Whether the stock plugin really accelerates Quartz 2D fills/copies on a Lombard in practice, and which ordinary-window drawing paths bypass `MPHW*`.
* Any runtime behaviour: nothing was run.

## 7. Proposed new checklist items (not added to `remaining-work.md`)

* **B1c (reworded B1b scope)**: 2D client / `GetBlitter` plugin path for **Quartz 2D** (fills, copy, copy region), not X11 only; add the `IOCFPlugInTypes` plumbing; measure window drag and scroll with and without. Correct the addendum in `ga-2d-client-decode.md`.
* **B1d**: fix `gld_renderer_info` (layout: `+4` id, `+8` flags with Window/FullScreen/MPSafe/Accelerated/Compliant and 0x1000, `+0x30/+0x34` memory, the short/byte fields Apple fills at `+0x20..+0x2c`) and compare with `CGLDescribeRenderer` output; host test on the struct offsets.
* **B1e**: QE enable experiment, only if B1c shows the 2D path is not enough: Configuration.plist variant + pixel-format nodes (depth 0/stencil 0) + hardware rectangle textures; success criterion `CGDisplayUsesOpenGLAcceleration` and the `updateGLVRAMFlag` log line; 16 bpp only first.
* **B1f**: read the rig's `/var/log/windowserver.log` with root (needs the user's decision) and, with `CGSSetDebugOptions`, the `updateGLVRAMFlag` line to confirm the VRAM byte count our `RageStubFB` presents (kIOFBVRAMMemory length).
* **B1g**: decode `IOFBBlitMemorySurfaceCopy` and the plugin's mode 0x2000 interface; decide whether a DMA-backed memory-to-screen blit is worth offering.
