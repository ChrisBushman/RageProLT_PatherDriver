#!/bin/sh
# make-msvcrt-lib.sh - build a Windows-2000-compatible C-runtime import library.
#
# Homebrew's mingw-w64 links the Universal CRT (imports from api-ms-win-crt-*.dll /
# ucrtbase), which DOES NOT EXIST on Windows 2000 -- even its libmsvcrt.a forwards to
# the UCRT api-sets.  So a normal CRT binary won't load on Win2000 ("entry point not
# found").  Our tiny tools sidestep this by being CRT-free (-nostartfiles
# -nodefaultlibs), but Mesa needs a real C runtime (malloc / math / stdio).
#
# Win2000 ships a full legacy C runtime in msvcrt.dll (malloc, printf, sin, sqrt,
# pow, qsort, memcpy, ...).  This script builds a real import library from THAT dll so
# we can cross-link against the legacy CRT and produce Win2000-loadable binaries.
# Proven: win2k/toolchain/msvcrt-smoketest.c runs on the Armada importing only
# msvcrt.dll + KERNEL32.
#
# Usage:
#   scp armada:/cygdrive/c/WINNT/system32/msvcrt.dll .
#   ./make-msvcrt-lib.sh msvcrt.dll        # -> libmsvcrt_win2k.a (+ msvcrt.def)
#
# Link recipe (see win2k/driver/README.md for the full CRT-startup story):
#   i686-w64-mingw32-gcc -O2 -D_WIN32_WINNT=0x0500 -nostartfiles -nodefaultlibs \
#       <obj...> libmsvcrt_win2k.a -lkernel32 -lgcc -e _mainstart -o foo.exe
set -e
DLL="${1:?usage: make-msvcrt-lib.sh <msvcrt.dll from C:\\WINNT\\system32>}"
OUT="${2:-libmsvcrt_win2k.a}"
DEF="${OUT%.a}.def"; [ "$DEF" = "$OUT" ] && DEF=msvcrt.def

{ echo "LIBRARY msvcrt.dll"; echo "EXPORTS";
  # export name table: last field is the symbol; keep plain C identifiers,
  # drop the C++-mangled (?..) exports we don't need.
  i686-w64-mingw32-objdump -p "$DLL" \
    | awk '/\[Ordinal\/Name Pointer\] Table/{f=1;next} f&&/^\t\[/{print $NF}' \
    | grep -E '^[A-Za-z_][A-Za-z0-9_]*$' | sort -u ;
} > "$DEF"

i686-w64-mingw32-dlltool -d "$DEF" -D msvcrt.dll -l "$OUT"
echo "wrote $OUT ($(($(wc -l < "$DEF") - 2)) exports) from $DLL"
