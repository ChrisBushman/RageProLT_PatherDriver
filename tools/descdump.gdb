# At sub_4530 (0x509530) dump GLEngine's drawable descriptor gli[0x789c].
# glViewport clamps the viewport to desc[0xc]=width / desc[0x10]=height; those are
# 0 for our hw drawable -> viewport 0x0 -> geometry culled.  Confirm at the
# descriptor level and show the layout (hw vs sw) for the fix.
set pagination off
set width 0
break *0x509530
commands
  silent
  set $gli=$r3
  set $d=*(unsigned int*)($gli+0x789c)
  printf "GLI=0x%x  DESC(gli[0x789c])=0x%x\n", $gli, $d
  printf "desc[0x00..0x1c]: %08x %08x %08x %08x %08x %08x %08x %08x\n", *(unsigned int*)($d+0), *(unsigned int*)($d+4), *(unsigned int*)($d+8), *(unsigned int*)($d+0xc), *(unsigned int*)($d+0x10), *(unsigned int*)($d+0x14), *(unsigned int*)($d+0x18), *(unsigned int*)($d+0x1c)
  printf "desc[0x20..0x3c]: %08x %08x %08x %08x %08x %08x %08x %08x\n", *(unsigned int*)($d+0x20), *(unsigned int*)($d+0x24), *(unsigned int*)($d+0x28), *(unsigned int*)($d+0x2c), *(unsigned int*)($d+0x30), *(unsigned int*)($d+0x34), *(unsigned int*)($d+0x38), *(unsigned int*)($d+0x3c)
  printf ">>> WIDTH desc[0xc]=0x%x  HEIGHT desc[0x10]=0x%x\n", *(unsigned int*)($d+0xc), *(unsigned int*)($d+0x10)
  quit
end
run
quit
