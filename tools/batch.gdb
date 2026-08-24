# Break sub_4530 (runtime 0x509530 = GLEngine base 0x505000 + foff 0x4530) -- the
# render-time function that tail-calls our flush.  r4 = the vertex batch (r29).
# bt shows the real (non-tailcall) caller chain; dump r4 to see if the batch is
# populated (geometry) or empty (culled upstream).
set pagination off
set width 0
break *0x509530
commands
  silent
  printf ">>> sub_4530: r3(gli)=0x%x  r4(batch)=0x%x  lr=0x%x\n", $r3, $r4, $lr
  printf "    batch[0..7]: %08x %08x %08x %08x %08x %08x %08x %08x\n", *(unsigned int*)$r4, *(unsigned int*)($r4+4), *(unsigned int*)($r4+8), *(unsigned int*)($r4+0xc), *(unsigned int*)($r4+0x10), *(unsigned int*)($r4+0x14), *(unsigned int*)($r4+0x18), *(unsigned int*)($r4+0x1c)
  bt 8
  continue
end
run
quit
