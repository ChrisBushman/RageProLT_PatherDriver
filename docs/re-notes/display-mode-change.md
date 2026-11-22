# Display mode change: how Apple's ATIRagePro handles it (and what our driver misses)

Source: stock `ATIRagePro.kext` 10.3.9 (md5 8fc97891), MH_OBJECT, `__text` file offset = vaddr + 0x33c. Disassembled with capstone,
extern calls resolved through `__text` relocations, virtual calls through `vtable for ATIRagePro` (kext vtables use the 2.95 layout:
slot `off` is at `vtable + off`, no +8). Triggered by the 640x480 switch that wedged System Preferences (GL preview open) on 2026-11-22.

## Notification path

`IOATIRProAccelerator::display_change_handler(OSObject*, void*, IOFramebuffer*, long event, void* info)` @0x20c is registered with
`IOFramebuffer::addFramebufferNotification`. It switches on `event` (jump table @0x254):

| event | IOFramebuffer meaning | action |
|---|---|---|
| 1 | display mode will change | `OSIncrementAtomic(this+0xc4)`; first one -> vslot 0x578 `ATIRagePro::display_mode_will_change(long)` |
| 2 | display mode did change | `OSDecrementAtomic(this+0xc4)`; last one -> vslot 0x57c `ATIRagePro::display_mode_did_change()` |
| 3 | will sleep | `info != 0` -> 0x580 `system_will_sleep`, else same as event 1 |
| 4 | did wake | `info != 0` -> 0x584 `system_did_wake`, else same as event 2 |
| 9 / 10 | will / did change speed | 0x588 / 0x58c |

## `ATIRagePro::display_mode_will_change(long)` @0x1290c

1. `mutex_lock(this[0x544])` -- **the accelerator mutex** (the same one `Mach64GLUserClient::accel_hold` takes).
2. `IOATIRProAccelerator::disp_mode_will_change(long)` @0x476c:
   - `waitForTimeStamp(this[0x50] - 1)` -- drain everything submitted;
   - `releaseAGPMemory` for each AGP region (`this+0xe4`, stride 0x1c, count `this[0xc8]`);
   - every texture on the VRAM-resident list (`this+0x370`): `deallocate_texture`, mark the record invalid (`+0x10 -> [0x14]=1`,
     zero `[0x28..0x32]`), move it to the other list;
   - free every tex/vertex buffer on `this+0x3e8` (`IOATIRProShared::free_texvert`);
   - every surface (ring at `this+0x5c`, 3 buffers at `+0x9c` stride 0x30): `IOATIRProSurface::move_buffer_to_backing_store`
     -- surface buffers leave VRAM;
   - `waitForTimeStamp` again; `this[0x7d] = (arg == 3)`.
3. `ATIRProMemory::dealloc` the fixed VRAM elements (`this+0x56c`, `+0x55c`, three at `+0x108` stride 0x30), then
   `ATIRProMemory::init_pool(0)` on both pools (`this[0x78]`, `this[0x584]`) -- **the whole VRAM heap is reset**.
4. `ATIRagePro::stop_engine()`.
5. `IOMemoryDescriptor::redirect(kernel_task, true)` on `this[0x548]` -- **every user mapping of the VRAM aperture is redirected**:
   user accesses block in the fault handler until it is undone. `this[0x548]` = `IOMemoryDescriptor::withPhysicalAddress(aperture
   base from CONFIG_CNTL (reg 0xDC) << 18, length this[0x9c])`, created in `set_display_mode_and_vram` @0x12f44.
6. `mutex_unlock(this[0x544])`.

## `ATIRagePro::display_mode_did_change()` @0x12aa8

`mutex_lock(this[0x544])` -> `initialize_hardware()` (re-runs `set_display_mode_and_vram`: new scanout, new VRAM reservations) ->
`IOATIRProAccelerator::disp_mode_did_change()` (`IOATIRProSurface::reset_access` on surfaces, `commitAGPMemory`) ->
`redirect(kernel_task, false)` -> `mutex_unlock`.

Apple's own clients cope because they never hold raw VRAM offsets: surface buffers are paged back in on the next access
(`reset_access` / required bits), textures are re-validated, and their CPU mappings simply block while redirected.

## What our driver (Path B gld + Mach64Accelerator) does instead

- **No notification at all.** The gld caches `target_off/target_pitch` (peeked `surf[0x12c][0x28]`), `z_off`, the texture-heap
  offsets, `cbpp`, display bounds and window origin. After step 3 those offsets point into a re-initialised heap -- quite possibly the
  new scanout -- and the next clear/triangle/present/texture upload writes there.
- **Our VRAM/register mapping is not redirected.** `Mach64Accelerator` maps BAR0 through its own `getDeviceMemoryWithRegister`
  descriptor, so the gld (and `mach64_vram_cpu_begin`) keeps touching registers and VRAM while Apple runs `stop_engine` /
  `initialize_hardware`.
- **Mutex contention.** The single-thread engine holds `this[0x544]` across a batch (`accel_hold`, 2 s watchdog); Apple's
  will/did-change block on it meanwhile.
- **Z / texture-heap records** allocated through the patched allocator (sel 10 op=ALLOC) live in Apple's heap; step 3 resets that heap,
  so our records dangle (cf. `heap-offset0-dangling-record.md`).

Which of these left System Preferences unkillable is not established; candidates are a user access to a redirected mapping that never
resumed, or a kernel call into Apple's kext on a surface whose buffer was moved to backing store.

## Implications for a fix

Mirror Apple, in our own kext + gld:
- **Kext:** register `IOFramebuffer::addFramebufferNotification` on the Rage framebuffer. On will-change: bump a mode generation in the
  shared control page (`MACH64_MEM_CONTROL`), refuse new `accel_hold`/kicks, drain, and `redirect(kernel_task, true)` our BAR0 /
  command-buffer descriptors. On did-change: `redirect(false)`, bump the generation again.
- **gld:** check the generation at frame start / present / pixel ops; on change drop every cached VRAM offset, re-run
  `mach64_pathb_setup_target`, re-allocate Z + texture heap, mark all textures for re-upload, re-resolve display bounds/cbpp/window
  origin. Until then render nothing (fail-to-blank, like `render_skip`).
