#!/bin/sh
# Generate the native import lib for the WINAPI (stdcall) ATI3DCIF exports.
# Client objects reference _ATI3DCIF_Foo@N; the DLL exports undecorated ATI3DCIF_Foo
# (built with --kill-at). Feeding dlltool a def of the DECORATED names + --kill-at
# yields an import lib that satisfies _ATI3DCIF_Foo@N while importing "ATI3DCIF_Foo".
set -e
cd "/home/Doc/ragesdk/knight-src"
export PATH="/cygdrive/c/MinGW/bin:$PATH"
dlltool.exe -d ati3dcif_stdcall.def -D ati3dcif.dll --kill-at -l libati3dcif.a
echo "dlltool ok, lib:"
ls -l libati3dcif.a
nm.exe libati3dcif.a > nm_lib.txt
echo "nm dumped"
