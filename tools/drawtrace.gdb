# Live trace of glDrawArrays: single-step from the app's call and log every real
# function call (LR changes to a far target -> a bl/bctrl, not PIC bcl/bdnzl).
# Reveals the path GLEngine takes: hardware T&L + submit, vs software raster
# fallback, and which module/function geometry ends up in.
#   app ~0x1000-0x8000 | our bundle ~0x80000 | GLEngine base 0x505000 | com.apple.opengl 0x90db0000
set pagination off
set width 0
set height 0
break glDrawArrays
commands
  silent
  printf "=== at glDrawArrays, tracing calls ===\n"
  set $n = 0
  set $lastlr = $lr
  set $calls = 0
  while $n < 120000 && $calls < 700
    stepi
    if $lr != $lastlr
      set $dp = $pc - $lr
      if $dp > 0x200 || $dp < -0x200
        printf "%d pc=0x%x lr=0x%x\n", $calls, $pc, $lr
        set $calls = $calls + 1
      end
      set $lastlr = $lr
    end
    set $n = $n + 1
  end
  printf "=== DONE steps=%d calls=%d ===\n", $n, $calls
  quit
end
run
quit
