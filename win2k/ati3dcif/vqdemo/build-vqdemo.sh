#!/bin/sh
# build-vqdemo.sh - cross-build ATI's "VQ cube" Direct3D-era demo (Rproex4) as a
# Windows-2000-loadable RProEx4.exe, using the Homebrew MinGW (i686) cross toolchain
# on macOS.
#
# The EXE links against:
#   * DirectDraw            (-lddraw, stock MinGW import lib)
#   * OUR ati3dcif.dll      (-lati3dcif, import lib generated below from our .def)
#   * the TARGET's msvcrt   (libmsvcrt_win2k.a, real Win2000 C runtime - see
#                            win2k/toolchain/README.md "Legacy msvcrt" mode)
#   * kernel32/user32/gdi32
# ...and NOTHING from the Universal CRT (no api-ms-win-crt-* / ucrtbase), so it
# actually loads on Win2000.
#
# GUI-app startup: the demo has WinMain (not main), so we use our own crt0win.cpp
# entry (-nostartfiles -Wl,-e,_winmainstart) instead of the toolchain's crt0exe.c.
#
# No hardware needed - this is pure compile/link. Deploy beside the demo data:
#   scp RProEx4.exe armada:ragesdk/bin/     (needs ati3dcif.dll + mandrill.vqt there)
set -e
cd "$(dirname "$0")"

GXX=i686-w64-mingw32-g++
GCC=i686-w64-mingw32-gcc
DLLTOOL=i686-w64-mingw32-dlltool
TOOLCHAIN=../../toolchain

# -fpermissive: the 1996-era SDK relies on MSVC's lax HANDLE<->HINSTANCE/HMODULE
# implicit conversions, which ISO C++ (and g++) reject as errors; downgrade to
# warnings (silenced by -w) so the unmodified sample compiles.
# NB: we can't force __USE_MINGW_ANSI_STDIO=0 -- libstdc++'s <bits/os_defines.h>
# (pulled in via <math.h> -> <cmath>) UNCONDITIONALLY redefines it to 1, so the one
# bare sprintf() in exutil.cpp routes to libmingwex's __mingw_sprintf. vqcompat.c
# provides that symbol, forwarding to msvcrt's vsprintf.
CXXFLAGS="-O2 -w -fpermissive -fno-exceptions -fno-rtti -D_WIN32_WINNT=0x0500 -I."

# --- 1. import lib for our ati3dcif.dll (decorated linker syms, bare import names) --
$DLLTOOL -d ati3dcif_import.def -D ati3dcif.dll --kill-at -l libati3dcif.a
echo "built libati3dcif.a"

# --- 2. the msvcrt import lib (built once by make-msvcrt-lib.sh; regenerate if absent)
if [ ! -f "$TOOLCHAIN/libmsvcrt_win2k.a" ]; then
    echo "ERROR: $TOOLCHAIN/libmsvcrt_win2k.a missing." >&2
    echo "  scp armada:/cygdrive/c/WINNT/system32/msvcrt.dll $TOOLCHAIN/msvcrt.dll" >&2
    echo "  (cd $TOOLCHAIN && ./make-msvcrt-lib.sh msvcrt.dll)" >&2
    exit 1
fi

# --- 3. compile the demo (the 5 objects the original .mak lists) + startup + shims ---
# NB: parseasc.cpp is intentionally NOT compiled - the .mak omits it and no compiled
# TU references it (it is only self-included).
DEMO_SRCS="RProEx4.cpp vqlib.cpp glob.cpp geom.cpp exutil.cpp"
OBJS=""
for src in $DEMO_SRCS; do
    obj="${src%.cpp}.o"
    $GXX $CXXFLAGS -c "$src" -o "$obj"
    OBJS="$OBJS $obj"
done

# GUI WinMain startup (C++ so the WinMain symbol matches the demo's mangling).
$GXX $CXXFLAGS -c crt0win.cpp -o crt0win.o
OBJS="$OBJS crt0win.o"

# float-math + __acrt_iob_func shims for Win2000's msvcrt (shared with the ICD).
$GCC -O2 -w -D_WIN32_WINNT=0x0500 -c "$TOOLCHAIN/win2kcompat.c" -o win2kcompat.o
OBJS="$OBJS win2kcompat.o"

# demo-specific extra shims (sincos/sincosf that g++ -O2 synthesizes).
# -fno-builtin: keep sin/cos as ordinary msvcrt calls, else -O2 re-fuses the shim's
# own sin(x)+cos(x) back into sincos() -> infinite recursion (stack overflow).
$GCC -O2 -w -fno-builtin -D_WIN32_WINNT=0x0500 -c vqcompat.c -o vqcompat.o
OBJS="$OBJS vqcompat.o"

# --- 4. link ------------------------------------------------------------------------
# crtbegin.o/crtend.o bracket the .ctors/.dtors arrays that crt0win.cpp walks.
CRTBEGIN=$($GXX -print-file-name=crtbegin.o)
CRTEND=$($GXX -print-file-name=crtend.o)

# -nodefaultlibs is ESSENTIAL: without it the g++ driver auto-links the toolchain's
# own libmsvcrt.a, which forwards to the Universal CRT (api-ms-win-crt-*.dll /
# ucrtbase) that does not exist on Win2000. We supply the runtime ourselves:
# libmsvcrt_win2k.a (real Win2000 CRT) + libgcc.
# --subsystem,windows: this is a GUI (WinMain) app; keep the loader from allocating
# a console window. We still set the entry explicitly, so the loader calls our
# _winmainstart regardless of subsystem.
$GXX -O2 -nostartfiles -nodefaultlibs -Wl,-e,_winmainstart -Wl,--subsystem,windows \
    -o RProEx4.exe \
    "$CRTBEGIN" $OBJS "$CRTEND" \
    -L. -lati3dcif \
    -lddraw \
    "$TOOLCHAIN/libmsvcrt_win2k.a" \
    -lgdi32 -luser32 -lkernel32 -lgcc

echo "built RProEx4.exe"
echo "=== imported DLLs (must be only ddraw/ati3dcif/msvcrt/kernel32/user32/gdi32) ==="
i686-w64-mingw32-objdump -p RProEx4.exe | grep 'DLL Name'
