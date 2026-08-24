# Trace the fullscreen pixel-format request: does cglScorePixelFormat get called
# for our nodes, and what do they score?  If NO SCORE-IN lines appear, our nodes
# are disqualified BEFORE scoring (device-mask gate at cglChoosePixelFormat
# node[0x30] & device_mask, or the accel 0x100 gate).
set pagination off
set width 0
break *0x90db0f8c
commands
  silent
  printf "SCORE-IN node=0x%x [04]=0x%x [08]=0x%x [30]=0x%x\n", $r5, *(unsigned int *)($r5+4), *(unsigned int *)($r5+8), *(unsigned int *)($r5+0x30)
  continue
end
break *0x90db248c
commands
  silent
  printf "  -> loop1 score=%d best=%d\n", $r3, $r26
  continue
end
run
quit
