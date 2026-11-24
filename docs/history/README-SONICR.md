# SonicR-G3-RageDriver on the Lombard: findings and changes

This note covers what we found after installing the third-party **SonicR-G3-RageDriver** package on the Lombard, and what this repo
changed in response. The work is on branch `sonicr-driver-import`, starting 2026-11-22.

**Test machine:** PowerBook G3 "Lombard", ATI Rage LT Pro (mach64) with 8 MB of VRAM, Mac OS X 10.3.9, reached with `ssh lombard`.

## 1. What the package is

SonicR-G3-RageDriver is a fork of this repo. It was developed on a G4 MDD with a PCI Rage LT Pro, using its own stand-in
framebuffer (`RageStubFB`). It installs three things:

| Component | md5 | Notes |
|---|---|---|
| Patched `ATIRagePro.kext` | `e64ddc65` | Apple's kext plus the descriptor-initialisation fix and DMA/texture handlers (`kext-patch/`) |
| `Mach64Accelerator.kext` | `36b94f15` | `com.local.Mach64Accelerator`; maps BAR0 and offers DMA texture upload |
| `ATIRageProGLDriver.bundle` | `7a39275f` | The Path-B GL plugin, with a worker-thread engine (`gld_engine.c`) and DMA texture upload |

Their source was imported unchanged in **37de1f3**. Files that exist only in this repo were left alone.

To undo the install:
- Their backups are in `/Library/RageProLT-GL/stock`.
  The "stock" GL bundle there is actually our older `dcce1e` build; Apple's original bundle no longer exists on the machine.
- Our previous gld is saved as `~/ATIRageProGLDriver.bundle.ours` and our v7 kext as `~/ATIRagePro.v7`.
- To return to our own driver, run their `uninstall-rage-gl.sh`, then `install-kext-v7.sh`.

## 2. Findings

### 2.1 Kernel panics left no record
- The first OpenGL game run and a screen saver run each ended in the "Please restart" panic screen.
- No `panic.log` was written and NVRAM held nothing. The only traces were an unclean shutdown and a journal replay.
- Setting `boot-args="debug=0x100"` did not change this.
- The Lombard's clock was in 2006 until `ntpdate` fixed it on 2026-11-22, so log timestamps from before that date are unreliable.

### 2.2 Chess crashed in `glReadPixels`
- The crash was `KERN_INVALID_ADDRESS` in `mach64_ctl08`, reading the Z buffer at `regs+0x301000`.
- **Cause:** the pixel paths (`glReadPixels` ctl08, `glDrawPixels` ctl0c, `glBitmap` ctl14) treated `ctx->regs` as a CPU view of VRAM.
  `ctx->regs` is actually Apple's type-1 **command buffer**, an IOKit map of about 768 KB:
  - Offsets past its end fault.
  - Small offsets silently overwrite queued engine commands.
- The real CPU view of VRAM is BAR0, which `Mach64Accelerator` maps through `MACH64_MEM_SURFACE`. In that mapping:
  - VRAM is little-endian, and a CPU offset equals the engine's VRAM offset.
  - The register window starts at `0x7FF800`, so usable VRAM is `[0, 0x7FF800)`.
- **Fixed in 3dad95c** (§3.1).

### 2.3 The windowed Chess board turned white
- The board appeared for a moment, then its view went white.
- Some garbage also appeared on the left of the screen until something repainted it. The old and new drivers both showed this.
- **Cause:** the windowed present (`surface_flush`, Apple's `build_swap`) blits the frame directly to scanout. The WindowServer later
  repaints the window from its own backing store, which is white. Chess only redraws on events, so its frame was painted over and
  never came back.
- **Fixed in 47f0ae2** (§3.2).

### 2.4 Full screen is limited by 8 MB of VRAM
- At **32-bit** depth, a full-screen 1024x768 surface gets no buffer: `target_off` is 0, so the driver sets `render_skip`.
  The result is a **white screen** for the Flurry screen saver and for games.
- At **16-bit** depth ("thousands of colours"), full-screen Flurry **renders** (target `0x478000`, pitch 1024, Z `0x180800`).
- The import dropped this repo's opt-in `MACH64_FS_SHRINK` path, which made a smaller surface fit. Porting it back is the likely fix
  for 32-bit full screen. Not done yet.

### 2.5 Display mode changes wedged the machine
- Switching to 640x480 while the System Preferences screen saver preview (GL) was open left a black screen.
- System Preferences was stuck in the kernel at about 80% CPU. `kill -9`, `ps` and `sample` all hung, while `uptime` and `tail`
  still worked. Only a power-cycle recovered the machine.
- **Apple's ATIRagePro handles mode changes; this driver did not.** Disassembly is in `docs/re-notes/display-mode-change.md`.
  Apple's kext:
  - registers `display_change_handler` with `IOFramebuffer::addFramebufferNotification`;
  - **on "will change":** takes the accelerator mutex; drains; releases AGP memory; deallocates textures; moves surface buffers to
    their backing store; **resets the whole VRAM heap** (`init_pool`); stops the engine; and calls
    `IOMemoryDescriptor::redirect(kernel_task, true)` on the VRAM aperture, so user mappings block;
  - **on "did change":** re-initialises the hardware; calls `reset_access` on surfaces; recommits AGP memory; undoes the redirect.
- The driver, by contrast:
  - cached VRAM offsets (target, Z, texture heap) across the change, so after the heap reset they could point into the new scanout;
  - kept its own BAR0 mapping live while Apple reprogrammed the chip;
  - kept Z and texture records that pointed into the reset heap.
- **Fixed in ca5d788** (§3.3). After the fix:
  - mode changes with no GL app, and with Chess open, complete;
  - Chess re-validates its context (mode generation 8, then 10).
- `tools/mach64_redirtest` showed that a user mapping of the aperture **survives** the redirect, so the redirect is not what wedged
  the preview.
- The preview case still **wedged once more** after the fix, right after running `sample` on System Preferences. **Cause unknown.**

### 2.6 Snowball Run: no textures, no background, then a full lockup
- The game switches the display to 640x480 32-bit by itself. It runs double-buffered in a window-backed surface: target `0x12cc00`,
  pitch 640, Z `0x259c00`.
- The driver state read from the running game:
  - texture heap `0x44b000`–`0x74b000`, with `next == base` (nothing was ever allocated) and 0 evictions;
  - mode generation **0**, while the context had seen generation 1;
  - the frame tick stuck at 102, and about 50% CPU in the R state.
- What appeared on screen: only the cursor. There were no textures and no background, and then the **whole machine locked up**, with
  no panic screen.
- **Working hypothesis:** the game's own mode switch makes Apple reinitialise `BUS_CNTL`. The DMA path then hits its bus-mastering
  check (bit 6 set or bit 27 clear) and calls `dma_disable()`, which turns it off **for the life of the process**. That breaks:
  - DMA texture upload, so no texture ever reaches the heap;
  - the mode-generation query, which shared the DMA connection and so returned 0;
  - possibly the single-thread engine, leaving a fallback to spin until the machine wedges.
- **Not confirmed yet.** The traced build (§3.4) is meant to settle it.

## 3. Changes made in this repo

| Commit | Change |
|---|---|
| 37de1f3 | Imported the SonicR driver source unchanged |
| 3dad95c | Pixel ops use real VRAM (BAR0), not the command buffer |
| 47f0ae2 | Idle repaint keeps windowed frames visible |
| ca5d788 | Display mode change handling (kext + gld) |
| 23737dd | Tools: `mach64_modeswitch` depth argument, `mach64_redirtest` |
| *(uncommitted)* | Mode generation on its own connection; opt-in tracing; `mach64_peekproc`, `mach64_threadpc` |

### 3.1 Pixel ops on real VRAM (3dad95c)
- New `mach64_vram_cpu_begin()` (`gldriver/gld_texture.c`):
  1. drains queued and in-flight engine work (`mach64_dma_fence`);
  2. waits up to 1 s for the FIFO to empty and `GUI_STAT` to go idle;
  3. returns the BAR0 mapping. It returns 0 on failure, and the pixel op is then skipped.
- ctl08, ctl0c and ctl14 span-check their surface with `mach64_vram_span_ok()` and clamp rows and columns to the surface.

### 3.2 Idle repaint (47f0ae2)
- After each real windowed present, the driver sets `repaint_ok` and arms a `CFRunLoopTimer` on the presenting thread's run loop.
- While nothing new is drawn, the timer re-presents the buffer at most every 150 ms through `mach64_pathb_present`.
- `repaint_ok` is cleared by any draw (`mach64_frame_start`), a re-target, a reshape or a detach.
- `gldDestroyContext` removes the timer. Threads without a run loop never fire it.
- Result: the Chess board stays visible.

### 3.3 Display mode changes (ca5d788)
- **Kext (`kext/Mach64Accelerator.*`):**
  - A publish notification finds `IOFramebuffer`s on our PCI device and calls `addFramebufferNotification` on each.
  - Will/did events are counted across heads, mapped the way Apple maps them (events 3/4 without info are treated as 1/2).
  - **Will change:** the mode generation becomes odd, and the `fFB` (BAR0) mapping is redirected.
  - **Did change:** the redirect is undone, and the generation becomes even.
  - `stop()` removes the notifiers and undoes any redirect.
  - New user-client selector **19 `get_mode_gen`** (`common/mach64_abi.h`). `accel_hold` returns `NotReady` during a change.
  - `Info.plist` now depends on `com.apple.iokit.IOGraphicsFamily`.
- **gld:** `mach64_mode_check()` runs at render_ctl, at the first draw of a frame, in readback and in the idle-repaint timer.
  - **Generation odd:** set `render_skip` / `present_unsafe` and draw nothing.
  - **Generation changed:**
    1. fence;
    2. drop the Z and texture-heap records *without freeing them*, since Apple reset that heap;
    3. evict every texture (`mach64_tex_invalidate_all`);
    4. discard the batch and re-run `mach64_pathb_setup_target`.
  - With an older kext the generation reads 0 and the feature stays off.
- **Tools:**
  - `tools/mach64_modeswitch W H [bpp]` switches resolution from the command line. Run it as the console user.
  - `tools/mach64_redirtest` checks that an aperture mapping survives a mode change.

### 3.4 Uncommitted work (for the Snowball Run lockup)
- **`mach64_mode_gen()` has its own connection** to `Mach64Accelerator`, independent of DMA state. Mode tracking now keeps working
  after `dma_disable()`.
- **Opt-in tracing:**
  - **Enable:** `MACH64_TRACE=1`, or create the file `/tmp/gld-trace-on`. A reboot deletes that file, so a reboot always turns
    tracing off.
  - **Output:** `/tmp/gld-trace-<pid>.log`, with an `fsync` after every line so the end of the log survives a hang or power-cycle.
  - **Logged:**
    - every place DMA is disabled, with its location and `BUS_CNTL`;
    - DMA transfer bus-off and timeout errors, and DMA map/lock setup;
    - mode-generation changes and re-validations;
    - texture uploads, heap set and allocation failures;
    - `setup_target` results and attach calls;
    - each step of a present;
    - frame flush and finish;
    - engine `wait_done` timeouts, refused `accel_hold` calls, and the engine never going idle.
- **Inspection tools** that work without stopping the target process. `gdb -batch` kills the target whenever a script command errors:
  - `tools/mach64_peekproc.c`: `pid [*]addr[+off][:count]` reads words from another process with `task_for_pid` + `vm_read`.
  - `tools/mach64_threadpc.c`: samples each thread's PC, LR and run state.

## 4. Current state on the Lombard

**Installed:**
- `Mach64Accelerator.kext` `701e84b7` and gld `f4d28188`, both from ca5d788, installed with `~/RageProLT-sonicr/install-modechange.sh`.
  Running it with `revert` restores their kext and gld.
- The patched `ATIRagePro.kext` `e64ddc65`, from the package.

**Built but not installed:** the traced gld, `bf855a20`. To install it:

```
ssh -t lombard sudo sh RageProLT-sonicr/install-gld.sh      # backup: ~/ATIRageProGLDriver.bundle.sonicr; `revert` arg restores it
```

**Desktop:** 1024x768 at 32-bit.

## 5. Open issues and next steps

1. **Snowball Run:**
   1. Install the traced gld.
   2. Create `/tmp/gld-trace-on` and run the game until it fails.
   3. Read `/tmp/gld-trace-<pid>.log`; the log survives a power-cycle.
   4. Confirm or rule out the `dma_disable` hypothesis and find what spins before the lockup.
2. **32-bit full screen:** port the `MACH64_FS_SHRINK` smaller-surface path back from this repo's pre-import driver.
3. **Screen saver preview wedge during a mode change:** still unexplained. It may be triggered by `sample`.
4. Commit the uncommitted tracing and tools once the Snowball Run trace has been read.

## 6. Working on the Lombard: practical notes

- **Wedged process:** `ps`, `sample` and `vmmap` hang on a process stuck in the kernel. Use `mach64_peekproc` and
  `mach64_threadpc`, or just `tail` a log file. A wedge needs a power-cycle.
- **gdb:** do not use `gdb -batch` to attach to a GL app. It killed Chess twice and probably Snowball Run once.
- **Mode switches:** do not switch modes while a GL app is running unless that is the test.
- **sudo steps** are run by the user, for example `ssh -t lombard sudo ...`.
- **Screenshots:** Panther's `screencapture` writes PDF whatever the file extension.
- **Install scripts:** a kext must be owned by `root:wheel` before `kextload -n -t` will validate it. The install scripts stage it in
  `/tmp` for this.
