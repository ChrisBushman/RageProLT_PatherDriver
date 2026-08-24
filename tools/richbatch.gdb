# Rich capture at sub_4530 (GLEngine 0x505000 + foff 0x4530) -- the
# renderer-agnostic batch builder that tail-calls the flush.
#   r3 = gli context, r4 = vertex batch.
# Dump the batch and several gli-context windows so a software run (populated
# batch) can be diffed field-for-field against our hardware run (empty 0x4bc).
# Same script is used for BOTH configs so the offsets line up.
set pagination off
set width 0
set $n = 0
break *0x509530
commands
  silent
  set $n = $n + 1
  if $n <= 3
    printf "\n>>> [hit %d] sub_4530: gli=0x%x  batch=0x%x  lr=0x%x\n", $n, $r3, $r4, $lr
    printf "-- batch[0x00..0x40] (hex; look for normal floats ~0x3f/0xbf/0x43/0x44) --\n"
    x/16wx $r4
    printf "-- batch as floats --\n"
    x/16fw $r4
    printf "-- gli[0x00..0x40] --\n"
    x/16wx $r3
    printf "-- gli[0x2da0..0x2de0] (viewport scale/bias area) --\n"
    x/16wx $r3+0x2da0
    printf "-- gli[0x2da0..] as floats --\n"
    x/16fw $r3+0x2da0
    printf "-- gli[0x7880..0x78c0] (ctx-ptr / dispatch area) --\n"
    x/16wx $r3+0x7880
  end
  continue
end
run
quit
