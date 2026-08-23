# OpenGL.framework integration — full reversal + where it stops

Goal: get a stock GL app to render through our driver via the framework's own
renderer discovery (vs us driving the gld* lifecycle by hand, which works — see
gld-layer.md / first-light.txt).  This documents the complete reverse-engineered
path, how far it works, and exactly what remains (a large subsystem).

## Status: the framework discovers, loads, and DRIVES our driver

A real accelerated CGL app (`tools/mach64_gltest.c`, `kCGLPFAAccelerated`) makes
OpenGL.framework find, load, and call into our bundle through 5 stages, no crash
until deep in pixel-format matching.  Trace (from /tmp/mach64gld.log, our
gld_log):

```
gldInitializeLibrary(iface, iface2, 3, cb1, cb2)   -> return iface (success)
gldGetVersion()                                     -> (2,4,0xb,0x1000), return 1
gldChoosePixelFormat(attribs = [8=color 32, 12=depth 16, 4, 0])  -> a pf struct
  ... framework reads the pf, matches configs -> npix ...
```

## Discovery mechanism (works)

1. GLEngine (`OpenGL.framework/Resources/GLEngine.bundle/GLEngine`, loaded into
   every GL process) enumerates IOKit services of `IOMatchCategory=IOAccelerator`.
2. For each it reads the string property **`IOGLBundleName`**.
3. It builds the path by concatenation:
   `/System/Library/Extensions/` + `<IOGLBundleName>` + `.bundle/Contents/MacOS/`
   + `<IOGLBundleName>`  and loads it via **NSCreateObjectFileImageFromFile /
   NSLinkModule** (NOT dlopen/CFBundle), then **NSLookupSymbolInModule** per gld*.
4. **The executable name MUST equal `<IOGLBundleName>`** (path is built by name,
   not read from Info.plist) -- ours is `ATIRageProGLDriver`.

**The empty slot was already ours:** Apple's `ATIRagePro.kext` Info.plist
declares `IOGLBundleName = "ATIRageProGLDriver"` -> a bundle that was never
shipped.  Installing our bundle as
`/System/Library/Extensions/ATIRageProGLDriver.bundle` fills it; no kext change.

## The gld* lifecycle gates (reversed)

- **gldInitializeLibrary(a1..a5)**: framework passes 5 interface pointers
  (iface data seg, iface2, version=3, 2 callbacks in code seg).  Reference SAVES
  them and RETURNS a1.  Returning 0 makes the framework treat init as failed.
- **gldGetVersion(v0,v1,v2,v3)** -- THE hidden gate.  GLEngine looks it up right
  after Init (`gliChoosePixelFormat`-adjacent loader) and BAILS with no
  enumeration if the symbol is absent.  It is stored in GLEngine's __data, so a
  `strings | grep ^_gld` list MISSES it.  Fills 4 outputs (2,4,0xb,0x1000),
  returns 1.  Found via runtime caller trace: `__builtin_return_address(0)` in
  gldInitializeLibrary = 0x5df4e8 -> GLEngine base 0x505000 (from crash-log image
  list) -> file offset 0xda4e4 (GLEngine __text: foff==vaddr) -> the post-Init
  `NSLookupSymbolInModule(module, "_gldGetVersion"); if NULL bail`.
- **gldChoosePixelFormat(pf_out, attribs)**: attribs = 0-terminated int32 GLD
  tokens ([8]=color+val, [11]=alpha, [12]=depth+val, [4]=? boolean, ...).  Must
  return a pixel-format object.

## The pixel-format object layout (partially reversed) -- the wall

Call chain (from the crash log backtrace):
```
CGLChoosePixelFormat -> cglChoosePixelFormat -> glcPluginChoosePixelFormat
  (com.apple.opengl, 0x90db0000) -> gliChoosePixelFormat (GLEngine, 0xed120)
```
`gliChoosePixelFormat(out_list, attribs)` loops the renderer list, calls each
renderer's gldChoosePixelFormat (ptr at renderer+0x118), and links the returned
pf structs into a list.  It reads/writes:
- **pf[0x00] = linked-list "next" pointer** (0 = single; it DEREFS if nonzero --
  our first layout put color_size here and bus-errored: crash log EXC_BAD_ACCESS
  at 0x24, PC GLEngine+0xed1c4).
- pf[0x04] |= 0x20000, pf[0x08] |= 0x80  (framework decorates buffer-mode flags).

Then `glcPluginChoosePixelFormat` passes the list to an imported helper that
builds the CGLPixelFormatObj and computes **npix** by matching each pf against
the request.  Our zeroed pf matches nothing -> npix=0.

The reference `gldChoosePixelFormat` builds the pf as a **linked list of 0x34-byte
config nodes**, each a `memcpy` (sub_6883c) of an entry from the **renderer's
config table** (`[renderer+8]`), with node[0]=next.  Node fields encode
renderer index (node[0x04] high byte), caps (sub_a5cc of a config bitmask),
buffer flags (node[0x08]), the config bitmask (node[0x30]), and the copied
config template (color/depth/etc.).

**Why this is the wall:** the renderer config table (global at
gldChoosePixelFormat PIC-base 0x5ad8 + 0x63600 = 0x690d8 -> 0x69510) is **all
zeros in the file -- populated at RUNTIME** via the framework callbacks saved in
gldInitializeLibrary.  So producing an acceptable pf requires reimplementing
Apple's renderer/config-enumeration subsystem (build the 0x34-byte config table
in the framework's exact private format, via undocumented callbacks) -- not a
struct fill.  And this is only the first of several remaining subsystems:
gldCreateContext's full contract, gldAttachDrawable's CGS window-surface format
(routes through a kext set_surface selector, see gld-layer.md), and the windowed
render path.

## RE techniques that worked here (see also the ppc-macho RE skill)

- Runtime caller trace: log `__builtin_return_address(0)` from a gld* to get the
  exact framework call site; map runtime addr -> file offset using the crash
  log's "Binary Images" load addresses (image is loaded at its listed base;
  GLEngine __text foff==vaddr, so offset = runtime - base).
- Crash logs are gold: /Library/Logs/CrashReporter/<app>.crash.log gives the
  symbolicated backtrace (named framework functions!), the faulting PC, and the
  fault address -- turns "it crashes somewhere" into an exact instruction.
- gld_log -> /tmp/mach64gld.log inside the bundle traces how the framework
  drives us call-by-call; log every entry point (incl stubs) to see the sequence.

## Reproduce

`tools/gltest-install.sh` (sudo): installs the bundle as ATIRageProGLDriver.bundle,
loads the kext, runs the accelerated CGL app, dumps the trace.  NOTE: while
installed, the incomplete bundle makes EVERY accelerated GL app crash (bus error
in the pf path) -- remove it when not testing:
`sudo rm -rf /System/Library/Extensions/ATIRageProGLDriver.bundle`.
