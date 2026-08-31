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

## Still to build (for Mesa, a real DLL with global ctors / atexit)

The smoketest uses a CRT-free entry and calls msvcrt directly (works because
malloc/sprintf/math need no CRT init). A full Mesa **DLL** needs proper C-runtime
startup: a small `crt0`/`dllcrt0` that runs global constructors (`_initterm` over the
`.ctors`/CRT init list), wires `atexit`/`_cexit`, and (for the EXE test harness)
`__getmainargs`. msvcrt.dll exports all of these (`_initterm`, `__getmainargs`,
`_cexit`, `_onexit`). That startup shim is the next toolchain task — see
`../driver/README.md`.
