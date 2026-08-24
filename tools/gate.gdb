# Break GLEngine sub_4530 (runtime 0x505000+0x4530=0x509530), the render handler.
# It gates the geometry-emit dispatch (gli[0x7990]) on:
#   gate1 @0x5095d0: gli[0x78a1] != 0
#   gate2 @0x5096b4: combined gli[0x300..0x310] state != 0
# and emits geometry at the bctrl @0x5096cc.  Find which gate skips us to the
# state-flush-only path (0x509780) and dump the deciding gli state.
set pagination off
set width 0
set $n=0
break *0x509530
commands
  silent
  set $n=$n+1
  set $gli=$r3
  printf "\n>>> [%d] sub_4530 gli=0x%x flags=0x%x\n", $n, $gli, $r4
  printf "    gli[0x78a1](gate1 byte) = 0x%x\n", *(unsigned char*)($gli+0x78a1)
  printf "    gli+0x300 state: %08x %08x %08x %08x %08x\n", *(unsigned int*)($gli+0x300), *(unsigned int*)($gli+0x304), *(unsigned int*)($gli+0x308), *(unsigned int*)($gli+0x30c), *(unsigned int*)($gli+0x310)
  printf "    gli[0x7990](emit fn) = 0x%x   gli[0x7898](drvctx) = 0x%x\n", *(unsigned int*)($gli+0x7990), *(unsigned int*)($gli+0x7898)
  printf "    gli[0x789c] = 0x%x  gli[0x7878] = 0x%x\n", *(unsigned int*)($gli+0x789c), *(unsigned char*)($gli+0x7878)
  continue
end
# did we reach the geometry-emit dispatch?
break *0x5096cc
commands
  silent
  printf ">>> REACHED geometry-emit bctrl: emit_fn=0x%x drvctx=0x%x table=0x%x\n", $r12, $r3, $r4
  continue
end
# ... or did we skip to the state-flush-only tail?
break *0x509780
commands
  silent
  printf ">>> SKIPPED to state-flush-only (no geometry) flags=0x%x\n", $r29
  continue
end
run
quit
