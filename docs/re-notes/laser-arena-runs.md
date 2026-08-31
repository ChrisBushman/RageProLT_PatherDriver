# Milestone: Laser Arena runs on our Mesa ICD (ATI crash fixed)

**2026-08-31.** The headline goal of the Win2000 port is met: `LA_GL.exe` — the
OpenGL build of Laser Arena that instantly crashed on ATI's stock ICD
(`atioglxx.dll`, NULL-deref at `DrvValidateVersion+0x7a060`, see
`laser-arena-failure-mode` memory) — now **runs past the crash to its main menu**
using our Mesa 7.0.4 `opengl32.dll`.

## How
- Built the software `opengl32.dll` via `win2k/mesa/build-icd.sh` (Mesa core +
  WGL/WMesa GDI driver, CRT-free/msvcrt link — imports only GDI32/KERNEL32/
  USER32/msvcrt, all present on Win2000).
- Proved the WGL contract first with `win2k/driver/wgltest.exe` (LoadLibrary our
  DLL; ChoosePixelFormat/SetPixelFormat/CreateContext/MakeCurrent/clear+triangle/
  ReadPixels/SwapBuffers): center red, corner blue — PASS on the Armada.
- Dropped `opengl32.dll` into `C:\Program Files\Trainwreck Studios\Laser Arena\`
  next to `LA_GL.exe`. Windows' app-dir DLL search loads ours; being a full
  drop-in (not a registry-installed ICD) it never enters ATI's `Drv*` path.
- User launched `LA_GL.exe` at the physical console: reached the main menu,
  rendering via software Mesa. **Dr. Watson log clean — no crash recorded.**

This validates the whole reuse strategy: `common/` mach64 code + Mesa front-end
give a working OpenGL on hardware ATI abandoned.

## Open follow-ups (polish, not blockers)
1. **Display fit.** 640x480 fullscreen looked "too tall for the screen" on the
   1024x768 LCD. Almost certainly an LCD mode-set/centering artifact (the game
   ChangeDisplaySettings to 640x480; WMesa just BitBlts its DIB to the game
   window — no vertical offset of ours). Verify it looks the same under the
   game's own software mode / v1minigl before touching our code.
2. **Performance.** Software Mesa at 640x480x16 is a slideshow. The mach64
   hardware backend is already proven standalone end-to-end (render + depth +
   texture + present, `win2k/driver/mach64_gl.c`). Next real work: fold that
   backend + a hardware `SwapBuffers` (32bpp back buffer -> RGB565 -> scanout)
   into the DLL's WGL path so LA_GL renders on the GPU. Note: with hw hooks
   installed, geometry goes to VRAM, so `glReadPixels` no longer reflects the
   frame — verification must read VRAM / the scanout, and the target must be
   sized to the window (mach64_gl.c currently hardcodes 32x32).
