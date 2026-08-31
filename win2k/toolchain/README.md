# win2k/toolchain — cross-building Win2000 binaries from macOS

## The CRT problem (solved)

Homebrew's `mingw-w64` (i686) targets the **Universal CRT**: normal binaries import
from `api-ms-win-crt-*.dll` / `ucrtbase`, which **do not exist on Windows 2000**.
Even the toolchain's `libmsvcrt.a` forwards to the UCRT api-sets. So a normal
CRT-using binary fails to load on Win2000 with "entry point not found".

Two ways we build Win2000-loadable binaries:

1. **CRT-free** (`-nostartfiles -nodefaultlibs -e _mainstart`, own entry, only
   kernel32/user32/advapi32/ntddk). Used by the small tools (`kmd/kmdtest`,
   `tools/mach64_peek`) and the kernel driver. No libc.

2. **Legacy msvcrt** (for Mesa / anything needing a real C runtime). Link against an
   import library built from the **target's own `msvcrt.dll`** (Win2000 ships a full
   legacy C runtime: malloc, printf, sin/cos/sqrt/pow, qsort, memcpy, ...).
   `make-msvcrt-lib.sh` builds that import lib. **Proven**: `msvcrt-smoketest.c`
   runs on the Armada importing only `msvcrt.dll` + `KERNEL32` (malloc/sqrt/sin/
   sprintf all correct).

## Files

- `make-msvcrt-lib.sh <msvcrt.dll>` — generate `libmsvcrt_win2k.a` from the target's
  msvcrt.dll (pull it: `scp armada:/cygdrive/c/WINNT/system32/msvcrt.dll .`).
- `msvcrt-smoketest.c` — the proof: calls msvcrt malloc/sqrt/sin/sprintf, prints via
  kernel32. Build:
  ```
  ./make-msvcrt-lib.sh msvcrt.dll
  i686-w64-mingw32-gcc -O2 -D_WIN32_WINNT=0x0500 -nostartfiles -nodefaultlibs \
      -e _mainstart -o smoke.exe msvcrt-smoketest.c libmsvcrt_win2k.a -lkernel32 -lgcc
  ```

## Not committed (build/vendor artifacts)

`msvcrt.dll` (Microsoft's), `libmsvcrt_win2k.a`, `msvcrt.def`, `*.exe` are gitignored
— regenerate with the script from a target `msvcrt.dll`.

## Full-CRT DLL startup (DONE — proven on Win2000)

`dllcrt0.c` is a tiny DLL entry point (`DllMainCRTStartup`) that runs gcc's global
constructors (`__CTOR_LIST__`), calls the DLL's `DllMain`, and runs destructors on
unload — replacing the stock mingw `dllcrt2.o` (which pulls the UCRT). Link it with
`crtbegin.o`/`crtend.o` (for the `.ctors`/`.dtors` brackets) and the msvcrt import lib.

**Proven** (`crtdll-test.c` + `dllload.c`, run on the Armada):
```
ctor_ran=YES  heap=YES  DllMain_alloc=005E2428  sqrt2*1000=1414
RESULT: full-CRT DLL works on Win2000 (ctors + heap + DllMain)!
```
i.e. a `LoadLibrary`'d DLL with a global constructor, a heap-allocating `DllMain`, and
msvcrt malloc/math — importing only `msvcrt.dll`.

### DLL link recipe (for the Mesa ICD)

```
CRTBEGIN=$(i686-w64-mingw32-gcc -print-file-name=crtbegin.o)
CRTEND=$(i686-w64-mingw32-gcc -print-file-name=crtend.o)
i686-w64-mingw32-gcc -O2 -D_WIN32_WINNT=0x0500 -fno-stack-protector \
    -shared -nostartfiles -nodefaultlibs -Wl,-e,_DllMainCRTStartup@12 \
    -o foo.dll  "$CRTBEGIN" dllcrt0.c <your .c/.o...> "$CRTEND" \
    libmsvcrt_win2k.a -lgcc -lkernel32
```
(crtbegin before objects, crtend after, then libs. Add -luser32/-ladvapi32 as needed.
`dllload.c` shows the CRT-free loader/harness pattern.)

## Files

- `dllcrt0.c` — reusable DLL startup (used by every msvcrt-linked DLL, incl. the ICD).
- `crtdll-test.c` / `dllload.c` — the T0 proof (test DLL + loader).

## Remaining CRT gaps to watch (fill as Mesa needs them)

`atexit`/`_cexit` flushing on DLL detach isn't wired yet (Mesa rarely needs it); some
C99 fns (snprintf, roundf, ...) may be missing from Win2000's msvcrt and will need
small compat shims when Mesa references them.
