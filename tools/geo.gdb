# Breakpoint GLEngine's primitive-dispatch functions + the triangle (tbl+0x28)
# dispatch sites to see how far our glDrawArrays gets before geometry is dropped.
# Base 0x505000.  Functions containing tbl+0x28/prim dispatch:
#   0x5b1f74 (fn@acf74, has tbl+0x28@ad358)
#   0x5db3ec (fn@d63ec, prim cluster 0x18/1c/20/40/44)
#   0x5dcd4c (fn@d7d4c, has tbl+0x28@d8008)
#   0x594ef0 (fn@8fef0, tbl+0x1c/44)
#   0x5ba328 (fn@b5328, tbl+0x24)
# actual triangle bctrl sites: 0x5b236c, 0x5dd020
set pagination off
set width 0
break *0x5b1f74
commands
  silent
  printf ">>> ENTER fn@0xacf74 (r3=0x%x r4=0x%x r5=0x%x)\n", $r3,$r4,$r5
  continue
end
break *0x5db3ec
commands
  silent
  printf ">>> ENTER fn@0xd63ec prim-cluster (r3=0x%x r4=0x%x r5=0x%x)\n", $r3,$r4,$r5
  continue
end
break *0x5dcd4c
commands
  silent
  printf ">>> ENTER fn@0xd7d4c (r3=0x%x r4=0x%x r5=0x%x)\n", $r3,$r4,$r5
  continue
end
break *0x594ef0
commands
  silent
  printf ">>> ENTER fn@0x8fef0 (r3=0x%x r4=0x%x r5=0x%x)\n", $r3,$r4,$r5
  continue
end
break *0x5b236c
commands
  silent
  printf ">>> *** TRIANGLE DISPATCH @0xad36c fires! fn=0x%x r3=0x%x r4=0x%x\n", $r12,$r3,$r4
  continue
end
break *0x5dd020
commands
  silent
  printf ">>> *** TRIANGLE DISPATCH @0xd8020 fires! fn=0x%x r3=0x%x r4=0x%x\n", $r12,$r3,$r4
  continue
end
run
quit
