#!/bin/sh
# build-osmesa.sh - compile Mesa 7.0.4 core+swrast+tnl+osmesa (pure C, no x86 asm)
# to Win2000 objects with our cross toolchain. T1a of the ICD bring-up.
set -u
cd "$(dirname "$0")"
MESA=Mesa-7.0.4
GCC=i686-w64-mingw32-gcc
OBJ=obj
mkdir -p "$OBJ"

INC="-I$MESA/include -I$MESA/src/mesa -I$MESA/src/mesa/main -I$MESA/src/mesa/glapi \
 -I$MESA/src/mesa/shader -I$MESA/src/mesa/shader/slang -I$MESA/src/mesa/shader/grammar"
# pure-C software build; map the Unix ffs to the gcc builtin; quiet warnings.
# BUILD_GL32: make Mesa's GLAPI symbols plain/dllexport (consistent static linkage)
# instead of the mingw-default dllimport. ffs: Unix-ism -> gcc builtin.
DEF="-D_WIN32_WINNT=0x0500 -Dffs=__builtin_ffs -DBUILD_GL32"
CFLAGS="-O2 -w -fno-stack-protector $DEF $INC"

# osmesa.c ships Windows-style backslash #include paths; normalize to '/' so the
# cross-compiler resolves them (idempotent patch of the vendored file).
sed -i '' -E '/#[[:space:]]*include/ s/\\/\//g' "$MESA/src/mesa/drivers/osmesa/osmesa.c"

# source dirs (pure C). Exclusions:
#   t_vertex_sse.c - needs x86 asm; vsnprintf.c - fallback (msvcrt has vsnprintf);
#   grammar.c - #included by grammar_mesa.c, not compiled standalone.
dirs="src/mesa/main src/mesa/math src/mesa/glapi src/mesa/shader \
 src/mesa/shader/slang src/mesa/shader/grammar src/mesa/swrast \
 src/mesa/swrast_setup src/mesa/tnl src/mesa/vbo"

rm -f build-errors.log
n=0; fail=0
for d in $dirs; do
  for f in "$MESA/$d"/*.c; do
    case "$f" in
      *t_vertex_sse.c|*main/vsnprintf.c|*grammar/grammar.c|*grammar/grammar_crt.c) continue;;
    esac
    o="$OBJ/$(echo "$d/$(basename "$f" .c)" | tr '/' '_').o"
    if ! $GCC -c $CFLAGS "$f" -o "$o" 2>>build-errors.log; then
      echo "FAIL: $f" | tee -a build-errors.log; fail=$((fail+1))
    fi
    n=$((n+1))
  done
done
# the osmesa driver + common driver funcs
for f in "$MESA/src/mesa/drivers/common/driverfuncs.c" "$MESA/src/mesa/drivers/osmesa/osmesa.c"; do
  o="$OBJ/$(basename "$f" .c).o"
  if ! $GCC -c $CFLAGS -I"$MESA/src/mesa/drivers" "$f" -o "$o" 2>>build-errors.log; then
    echo "FAIL: $f" | tee -a build-errors.log; fail=$((fail+1))
  fi
  n=$((n+1))
done
echo "compiled $n files, $fail failed. objects in $OBJ/, errors in build-errors.log"
[ "$fail" -ne 0 ] && exit 1

# --- link the OSMesa software test EXE (T1a proof) ---
if [ ! -f libmsvcrt_win2k.a ]; then
  echo "need libmsvcrt_win2k.a -- scp armada:/cygdrive/c/WINNT/system32/msvcrt.dll ."
  echo "then: ../toolchain/make-msvcrt-lib.sh msvcrt.dll libmsvcrt_win2k.a"
  exit 1
fi
CB=$($GCC -print-file-name=crtbegin.o); CE=$($GCC -print-file-name=crtend.o)
$GCC -c -O2 -w -D_WIN32_WINNT=0x0500 ../toolchain/crt0exe.c    -o "$OBJ/crt0exe.o"
$GCC -c -O2 -w -D_WIN32_WINNT=0x0500 ../toolchain/win2kcompat.c -o "$OBJ/win2kcompat.o"
$GCC -c -O2 -w -D_WIN32_WINNT=0x0500 -DBUILD_GL32 -I"$MESA/include" test_osmesa.c -o "$OBJ/test_osmesa.o"
$GCC -nostartfiles -nodefaultlibs -e _mainstart -o test_osmesa.exe \
  "$CB" "$OBJ/crt0exe.o" "$OBJ/test_osmesa.o" "$OBJ/win2kcompat.o" \
  $(ls "$OBJ"/*.o | grep -vE 'crt0exe|test_osmesa|win2kcompat') "$CE" \
  libmsvcrt_win2k.a -lgcc -lkernel32 2>/dev/null
echo "linked test_osmesa.exe ($(stat -f%z test_osmesa.exe 2>/dev/null) bytes)"
echo "deploy+run:  scp test_osmesa.exe armada:/home/Doc/ && ssh armada 'cd /home/Doc && ./test_osmesa.exe'"
