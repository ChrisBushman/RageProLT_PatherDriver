# Trace cglScorePixelFormat inputs AND return scores without using `finish`
# (which broke the command loop).  Entry 0x90db0f8c prints the node; the two
# call-return sites print r3 (score) vs r26 (best-so-far):
#   loop1 (accelerated) call at 0x2488 -> returns to 0x248c
#   loop2 (fallback)     call at 0x2580 -> returns to 0x2584
set pagination off
set width 0
break *0x90db0f8c
commands
  silent
  printf "SCORE-IN node=0x%x [04]=0x%x [08]=0x%x [10]=0x%x [14]=0x%x [18]=0x%x [1c]=0x%x [30]=0x%x\n", $r5, *(unsigned int *)($r5+4), *(unsigned int *)($r5+8), *(unsigned int *)($r5+0x10), *(unsigned int *)($r5+0x14), *(unsigned int *)($r5+0x18), *(unsigned int *)($r5+0x1c), *(unsigned int *)($r5+0x30)
  continue
end
break *0x90db248c
commands
  silent
  printf "  -> loop1 score=%d  best=%d\n", $r3, $r26
  continue
end
break *0x90db2584
commands
  silent
  printf "  -> loop2 score=%d  best=%d\n", $r3, $r26
  continue
end
run
quit
