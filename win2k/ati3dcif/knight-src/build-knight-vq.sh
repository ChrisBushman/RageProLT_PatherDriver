#!/bin/sh
# Build ATI's Knight "VQ" demo NATIVELY on the Win2000 Armada with MinGW gcc 3.4.5.
# Run on the target (login cwd = /home/Doc):
#   ssh armada 'cd ragesdk/knight-src && sh build-knight-vq.sh'
# The minimal Cygwin sh here has NO basename/wc/dirname/grep, so this uses only
# shell builtins + the MinGW tools.
set -e

export PATH="/cygdrive/c/MinGW/bin:$PATH"
GXX=/cygdrive/c/MinGW/bin/g++.exe
DLLTOOL=/cygdrive/c/MinGW/bin/dlltool.exe
OBJDUMP=/cygdrive/c/MinGW/bin/objdump.exe

INCS="-I VQ/inc -I common/inc"
# The ddraw.h installed on this box is a mingw-w64 header that uses __MSABI_LONG();
# classic MinGW (gcc 3.4.5) has no _mingw.h defining it, so provide it ourselves.
# -fno-operator-names: the code uses "or" (and possibly and/not) as ordinary variable
# names; ISO C++ makes those alternative operator tokens, which gcc honors by default.
CXXFLAGS="-c -O2 -w -fpermissive -fno-operator-names -D_WIN32_WINNT=0x0500 -DWIN32 -DNDEBUG -D_WINDOWS -D__MSABI_LONG(n)=n $INCS"

echo "=== [1/3] native import lib for ati3dcif.dll ==="
# The ATI3DCIF header is WINAPI (stdcall), so client objects reference _ATI3DCIF_Foo@N.
# Our DLL exports UNDECORATED names (built with --kill-at). Feed dlltool a def of the
# DECORATED @N names + --kill-at: the import lib then satisfies _ATI3DCIF_Foo@N while
# importing the undecorated "ATI3DCIF_Foo" the DLL actually exports.
$DLLTOOL -d ati3dcif_stdcall.def -D ati3dcif.dll --kill-at -l libati3dcif.a
ls -l libati3dcif.a

echo "=== [2/3] compile ==="
rm -f *.o
for f in common/*.cpp VQ/*.cpp; do
    base=${f##*/}          # strip dir
    obj=${base%.cpp}.o     # swap extension
    echo "  CC $f -> $obj"
    $GXX $CXXFLAGS "$f" -o "$obj"
done
echo "objects:"; ls *.o

echo "=== [3/3] link ==="
# gcc 3.4.5 MinGW links libstdc++/libgcc STATICALLY by default (there is no
# libstdc++-6.dll / libgcc_s*.dll in this era), so VQ.exe has no C++-runtime DLL
# dependency. (The gcc 4.5+ flags -static-libstdc++/-static-libgcc don't exist here.)
$GXX *.o -o VQ.exe -mwindows \
    -L. -lati3dcif -lddraw -lwinmm -lgdi32 -luser32
ls -l VQ.exe
echo "=== BUILD OK (DLL imports checked on the Mac via objdump) ==="
