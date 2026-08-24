---
name: osx-gl-framework-integration
description: Debug and satisfy the private contract between a 3rd-party driver and Apple's OpenGL framework on OS X 10.3/10.4 (the "gld" GLDriver plugin ABI, GLEngine, CGL). Use when your driver is loaded by the framework but something silently doesn't work — pixel format rejected (npix=0), context falls back to software, geometry never dispatches, no error anywhere. Covers the discovery/load path, the entry-point contracts, and the runtime-tracing method that turns "silently wrong" into "exact instruction".
---

# Interfacing a driver with Apple's OpenGL framework (Panther/Tiger)

Hard-won from writing an ATIRageProGLDriver.bundle from scratch and getting a
stock GL app to render accelerated triangles through OpenGL.framework. The
framework is undocumented, version-specific, and fails **silently** — it just
uses its software fallback and reports success. The whole game is making the
invisible visible.

## The stack, and who calls whom

```
app → OpenGL.framework (CGLChoosePixelFormat/CGLCreateContext)
    → com.apple.opengl (Versions/A/OpenGL): cglChoosePixelFormat, glcAddFormatToCache, cglScorePixelFormat
    → GLEngine.bundle (gli*): gliChoosePixelFormat, gliCreateContext, the renderer registry
    → your GLDriver.bundle (gld*): the ~55 gld* entry points you export
    → your kext (IOAccelerator user client)
```

- **The framework loads your bundle by NAME, not Info.plist.** GLEngine reads the
  string property `IOGLBundleName` off the IOAccelerator IOKit service, builds
  `/System/Library/Extensions/<name>.bundle/Contents/MacOS/<name>`, and loads it
  with **NSCreateObjectFileImageFromFile / NSLinkModule** (not dlopen/CFBundle),
  then `NSLookupSymbolInModule` per gld* symbol. **Your executable name must equal
  `IOGLBundleName` exactly.** Often the kext already declares a bundle name for a
  driver Apple never shipped — fill that slot, no kext change needed.

## The silent-failure gates, in the order you'll hit them

Each of these makes the framework quietly pick software with no error:

1. **gldInitializeLibrary must return arg1** (the interface ptr), not 0. Returning
   0 = "init failed" → your driver is dropped.
2. **gldGetVersion is a hidden gate.** GLEngine looks it up right after Init and
   bails (no enumeration) if the symbol is absent — and it's stored in GLEngine's
   `__data`, so a `strings | grep ^_gld` on the reference MISSES it. Fill 4 out
   params, return 1.
3. **The pixel-format node needs the accelerated bit.** The CGL scorer
   (`cglScorePixelFormat`) skips any config node without `node[0x08] & 0x100`
   (hardware marker) — its whole scoring loop `andi. 0x100 / beq skip`. Software
   configs lack it; that's why copying them yields npix but never a *win*.
4. **The node's renderer-type must be YOURS.** `gliCreateContext` instantiates a
   renderer from a type-keyed registry by matching `node[0x04]` bits 8-15 against
   `[renderer+0x104]`. Copy the wrong type → it builds a *software* renderer that
   forwards your pixel format but rasterizes in software (GL_RENDERER still shows
   your name — misleading!).
5. **The ctx ABI must be exact.** Your per-context struct is shared with GLEngine
   at fixed offsets (connect@0x04, fmt_caps@0x2c, device_info@0x34, mapped
   regions @0xb8/0xc0/0xd0/0xdc, the gli-context ptr @0x10, drawable/target/dims
   in the tail). A struct without padding puts your fields on GLEngine's fields
   and corrupts render state. Compile-assert `sizeof==0x200`.
6. **gldUpdateDispatch must RETURN a meaningful value** (0). GLEngine compares the
   return to a cached flag: equal → keep hardware dispatch, not-equal → reconfigure
   to software. A `void` function leaves garbage in r3 → software every draw.
7. **gldInitDispatch installs the render engine, not just primitive slots.** The
   dispatch table's control slots (`table[0x00..0x14]`) are the driver's
   flush/vertex functions GLEngine calls to actually render; `table[0x04]` is THE
   flush entry. Stub them and geometry never reaches your primitive slots.

## The method that beats silent failure: log every entry point, then gdb the framework

1. **Instrument first.** Give every gld* entry point a `gld_log()` to a file. The
   *sequence* of calls (and where it stops) localizes the failure to one entry
   point before you reason about anything.
2. **Find the caller with `__builtin_return_address(0)`.** Log it from inside a
   gld* function to get the exact framework call site; map runtime→file offset
   with the crash log's "Binary Images" load base (for a bundle, `__text`
   foff==vaddr, so offset = runtime − base). Now you can disassemble the caller.
3. **Crash logs are gold.** `/Library/Logs/CrashReporter/<app>.crash.log` gives a
   *symbolicated* backtrace (named framework functions!), the faulting PC, and the
   fault address — turns "crashes somewhere" into an exact instruction + which
   struct field was mis-shaped.
4. **gdb the framework's decision, don't guess it.** When the framework silently
   diverges, breakpoint the deciding instruction and read the registers:
   - dump a live data structure the framework built (e.g. break in
     `glcPluginChoosePixelFormat` with the pf-list head in a register, walk it) —
     this hands you the exact byte format to replicate.
   - break right after your function returns and read what the framework does with
     the return value (that's how you find "it wanted a return value").
   - `set $x = *(unsigned int*)(addr)` + `while`/`printf` in a `-batch -x` command
     file dumps whole structs non-interactively.
5. **Install a logging stub at an unknown slot, run, read the args.** When you
   don't know what the framework passes a callback, install a stub that logs its
   registers and dereferences pointer-looking args. One run tells you the contract.

## Traps specific to this interface

- **GL_RENDERER lies.** It reflects your *context's* string, which you return even
  when GLEngine is software-rasterizing behind it. Don't trust it as proof of
  hardware; prove hardware with a kext-side fence/stamp that only increments when
  DMA actually ran.
- **The node encoding is shared across three consumers** — the CGL scorer (npix),
  `gliCreateContext` (renderer selection), and `gldCreateContext`'s caps decode.
  Changing one field to fix one consumer can break another. Reverse the encoder
  (how the reference builds nodes from color/depth sizes), don't hand-tune bytes.
- **Two paths load your bundle**: the CGL pixel-format path (gets gldChoosePixelFormat)
  and the renderer-registry path (gets the render engine). Working in one doesn't
  mean working in the other.

See also [[ppc-macho-reverse-engineering]], [[osx-binary-decomp-for-abi]],
[[driver-dev-test-loop]].
