# WindowServer heap corruption on display reconfiguration (Panther 10.3.9 CoreGraphics)

**Symptom.** Any display reconfiguration (`tools/mach64_arrange set rage 3840 100`, moving the Rage to the origin, ...) crashed WindowServer
within seconds: `EXC_BAD_ACCESS` in `szone_malloc` reached from `shape_difference`/`realloc`/`CGXRedrawDisplay`/`CGXOrderWindowList`, then
the crash logger spun at ~100% CPU.  The occasional crash ~5 min after login on this rig had the same signature.

**Not the RageStubFB.**  The stub reports an ordinary 640x480 16-bit mode; WindowServer classes it as depth class 3 and handles it correctly.

**Cause (measured).**  `MPSetCursor` (via `reloadCursor`/`CGXSetStaticCursors`/`MPRebuildDevice`) converts the cursor to ARGB32
(`CGSConvertBitmapToDepth`, a 1024-byte data block) and then calls `setFrame(device, hdr, ...)` for every display.  `setFrame` switches on the
device's depth class (`deviceInfo+164`): 1/2 = 8-bit, 3 = 16-bit, 4 = 32-bit.  Classes 0 and 5 take the default branch at `0x936e86dc`:

    mr   r3,r4                      ; r4 = hdr, the caller's STACK copy of the bitmap header (MPSetCursor frame, e.g. 0xbfffe620)
    bl   CGSFreeConversionBitmap    ; frees hdr->data (the shared cursor data) and tries to free the stack header
    li   r3,1000                    ; error

`MPSetCursor` later calls `CGSFreeConversionBitmap` on the same bitmap again -> double free of the data block and a free of a stack address.
WindowServer's own malloc check (`MallocScribble`, `MallocCheckHeapEach=1`, fd 2 redirected with gdb `call dup2`) printed
`Deallocation of a pointer not malloced: 0xbfffe620`, `error for object 0x1807200: Incorrect checksum for freed object`, last good check inside
`copyBitmap` for this conversion.  Breakpoints on `setFrame` showed three devices per rebuild: 7800 head B (class 4), the Rage (class 3) and
a class-0 device = the 7800's second head (`NVDA,Display-A`, `IOFBConfig` mode record all zeros, the phantom 0x0 display `0x5b81c5c1`).

**Proof.**  `set {int}0x936e86e0 = 0x60000000` (nop the `bl`) in the running WindowServer: 6 reconfigurations in a row without a crash
(every one crashed before).  The same result from a cold boot with the boot-time patcher below.

**Fix used on the rig (option B, nothing on disk in CoreGraphics changed).**
* `tools/wspatch/ws_patcher.c`: writes the `nop` into WindowServer's memory with Mach `vm_write`, only when the word there is exactly
  `0x4bffca0d` (`bl 0x936e50ec`).  `-name WindowServer` mode watches the process list and patches each WindowServer process once (WindowServer
  forks into a `-daemon` child, so the launch PID is not the running one).
* `boot/ws-launch.sh`: WindowServer launcher (`/etc/mach_init.d/WindowServer.plist` `Command` -> `/Library/RageProLT/ws-launch.sh`, original plist
  kept as `WindowServer.plist.orig`) that starts the patcher in the background and `exec`s the real WindowServer.  Opt out with
  `/Library/RageProLT/no-ws-patch`.  Log: `/Library/RageProLT/ws-patch.log`.
* Alternative without software: attach a monitor/dummy plug to the 7800's second output so that head has a real mode (class 4).

**Do not** use gdb for the patch: a gdb batch script that errors while attached kills the target (WindowServer died that way twice).
