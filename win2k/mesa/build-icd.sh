#!/bin/sh
# build-icd.sh - link Mesa 7.0.4 core + WGL/WMesa into a drop-in opengl32.dll
# (software backend) for Windows 2000. Run build-osmesa.sh first (compiles the core).
# T1b of the ICD bring-up.
set -u
cd "$(dirname "$0")"
MESA=Mesa-7.0.4
GCC=i686-w64-mingw32-gcc
OBJ=obj
GDI="$MESA/src/mesa/drivers/windows/gdi"

[ -f libmsvcrt_win2k.a ] || { echo "need libmsvcrt_win2k.a (see ../toolchain/make-msvcrt-lib.sh)"; exit 1; }
[ -f "$OBJ/context.o" ] || [ -f "$OBJ/driverfuncs.o" ] || { echo "run ./build-osmesa.sh first"; exit 1; }

INC="-IMesa-7.0.4/include -IMesa-7.0.4/src/mesa -IMesa-7.0.4/src/mesa/main \
 -IMesa-7.0.4/src/mesa/glapi -IMesa-7.0.4/src/mesa/drivers -IMesa-7.0.4/src/mesa/drivers/windows/gdi"

# WGL entry points + WMesa GDI software driver (-fpermissive: 2008 K&R/pointer code
# that modern gcc flags as errors). DLL startup + compat shims.
$GCC -c -O2 -w -fpermissive -D_WIN32_WINNT=0x0500 -DBUILD_GL32 $INC "$GDI/wgl.c"   -o "$OBJ/wgl.o"
$GCC -c -O2 -w -fpermissive -D_WIN32_WINNT=0x0500 -DBUILD_GL32 $INC "$GDI/wmesa.c" -o "$OBJ/wmesa.o"
$GCC -c -O2 -w -D_WIN32_WINNT=0x0500 ../toolchain/dllcrt0.c     -o "$OBJ/dllcrt0.o"
$GCC -c -O2 -w -D_WIN32_WINNT=0x0500 ../toolchain/win2kcompat.c -o "$OBJ/win2kcompat.o"

# mach64 HARDWARE backend for the DLL (win2k/driver/mach64_icd.c) + its winsys/emit deps.
HWINC="-I../../common -I../driver"
$GCC -c -O2 -w -D_WIN32_WINNT=0x0500 -DBUILD_GL32 $INC $HWINC ../driver/mach64_icd.c    -o "$OBJ/mach64_icd.o"
$GCC -c -O2 -w -D_WIN32_WINNT=0x0500 $HWINC ../driver/mach64_winsys.c  -o "$OBJ/icd_winsys.o"
$GCC -c -O2 -w -D_WIN32_WINNT=0x0500 $HWINC ../../common/mach64_emit.c -o "$OBJ/icd_emit.o"

CB=$($GCC -print-file-name=crtbegin.o)
CE=$($GCC -print-file-name=crtend.o)

# DLL object set: core + wgl + wmesa, minus the EXE-test bits and the osmesa driver.
rm -f icd-objs.tmp
for o in "$OBJ"/*.o; do
  case "$o" in
    */crt0exe.o|*/test_osmesa.o|*/osmesa.o|*/dllcrt0.o|*/win2kcompat.o|*/wgl.o|*/wmesa.o) continue;;
    */mach64_gl.o|*/mach64_emit_c.o|*/mach64_winsys.o) continue;;   # EXE + stale hw objs
    */mach64_icd.o|*/icd_winsys.o|*/icd_emit.o) continue;;          # hw backend, linked explicitly
  esac
  printf '%s\n' "$o" >> icd-objs.tmp
done

$GCC -shared -nostartfiles -nodefaultlibs -Wl,-e,_DllMainCRTStartup@12 -o opengl32.dll \
  "$CB" "$OBJ/dllcrt0.o" "$OBJ/wgl.o" "$OBJ/wmesa.o" \
  "$OBJ/mach64_icd.o" "$OBJ/icd_winsys.o" "$OBJ/icd_emit.o" \
  $(cat icd-objs.tmp) "$OBJ/win2kcompat.o" "$CE" \
  "$GDI/mesa.def" \
  libmsvcrt_win2k.a -lgcc -lgdi32 -luser32 -lkernel32 2>build-icd-errors.log
rc=$?
rm -f icd-objs.tmp
if [ "$rc" -ne 0 ]; then
  echo "link failed; errors:"; grep -viE 'stdcall-fixup|resolving _gl_dispatch|auto-import' build-icd-errors.log | head -20
  exit 1
fi
echo "linked opengl32.dll ($(stat -f%z opengl32.dll 2>/dev/null) bytes)"
$GCC -print-file-name=crtbegin.o >/dev/null
i686-w64-mingw32-objdump -p opengl32.dll | grep -iE 'DLL Name' | sed 's/^/  /'
