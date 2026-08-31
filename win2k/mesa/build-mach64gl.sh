#!/bin/sh
# build-mach64gl.sh - T2 proof: OSMesa GL pipeline + hardware render hooks.
# Links Mesa OSMesa objects (build-osmesa.sh must have run) + the mach64 winsys +
# common/mach64_emit into an msvcrt EXE. Run mach64kmd.sys on the target first.
set -u
cd "$(dirname "$0")"
GCC=i686-w64-mingw32-gcc
OBJ=obj
D=../driver
C=../../common
[ -f libmsvcrt_win2k.a ] || { echo "need libmsvcrt_win2k.a"; exit 1; }
[ -f "$OBJ/osmesa.o" ] || { echo "run ./build-osmesa.sh first"; exit 1; }

MI="-IMesa-7.0.4/include -IMesa-7.0.4/src/mesa -IMesa-7.0.4/src/mesa/main -IMesa-7.0.4/src/mesa/glapi"

$GCC -c -O2 -w -fpermissive -D_WIN32_WINNT=0x0500 -DBUILD_GL32 $MI -I$D -I$C "$D/mach64_gl.c"     -o "$OBJ/mach64_gl.o"
$GCC -c -O2 -w -D_WIN32_WINNT=0x0500 -I$D -I$C "$D/mach64_winsys.c" -o "$OBJ/mach64_winsys.o"
$GCC -c -O2 -w -I$C "$C/mach64_emit.c" -o "$OBJ/mach64_emit_c.o"
$GCC -c -O2 -w -D_WIN32_WINNT=0x0500 ../toolchain/crt0exe.c     -o "$OBJ/crt0exe.o"
$GCC -c -O2 -w -D_WIN32_WINNT=0x0500 ../toolchain/win2kcompat.c -o "$OBJ/win2kcompat.o"

CB=$($GCC -print-file-name=crtbegin.o); CE=$($GCC -print-file-name=crtend.o)

# Mesa core (incl osmesa.o) minus the harness/driver objects.
rm -f gl-objs.tmp
for o in "$OBJ"/*.o; do
  case "$o" in
    */test_osmesa.o|*/crt0exe.o|*/win2kcompat.o|*/dllcrt0.o|*/wgl.o|*/wmesa.o|*/mach64_gl.o|*/mach64_winsys.o|*/mach64_emit_c.o) continue;;
  esac
  printf '%s\n' "$o" >> gl-objs.tmp
done

$GCC -nostartfiles -nodefaultlibs -e _mainstart -o mach64_gl.exe \
  "$CB" "$OBJ/crt0exe.o" "$OBJ/mach64_gl.o" "$OBJ/mach64_winsys.o" "$OBJ/mach64_emit_c.o" \
  $(cat gl-objs.tmp) "$OBJ/win2kcompat.o" "$CE" \
  libmsvcrt_win2k.a -lgcc -lkernel32 -luser32 2>gl-link.log
rc=$?
rm -f gl-objs.tmp
if [ "$rc" -ne 0 ]; then echo "link failed:"; grep -viE 'stdcall|resolving _gl_dispatch' gl-link.log | head -20; exit 1; fi
echo "built mach64_gl.exe ($(stat -f%z mach64_gl.exe 2>/dev/null) bytes)"
i686-w64-mingw32-objdump -p mach64_gl.exe | grep -iE 'DLL Name' | sed 's/^/  /'
