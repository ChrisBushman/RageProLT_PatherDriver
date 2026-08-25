# Read-site trace: find WHERE GLEngine sources GL_MAX_TEXTURE_SIZE.
# The app does `GLint mx = -1; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &mx);`, so at
# the glGetIntegerv entry r3=0xD33 (pname), r4=&mx, *r4 == -1.  We single-step
# from the call and stop at the FIRST instruction that writes *r4 (the store of
# the answer).  Disassembling around that store + dumping the base registers
# reveals the load that produced the value -> the limits struct / gli field the
# texture-size cap lives in.  From there we know exactly what to populate.
#   app ~0x1000-0x8000 | our bundle ~0x80000 | GLEngine base 0x505000 | com.apple.opengl 0x90db0000
set pagination off
set width 0
set height 0
set env DYLD_BIND_AT_LAUNCH 1
break main
run
# conditional bp: only the GL_MAX_TEXTURE_SIZE query
break glGetIntegerv if $r3 == 0xd33
commands
  silent
  printf "=== glGetIntegerv(GL_MAX_TEXTURE_SIZE) params=0x%x init=0x%x ===\n", $r4, *(int*)$r4
  set $pp = $r4
  set $init = *(int*)$pp
  set $n = 0
  set $done = 0
  while $n < 120000 && $done == 0
    stepi
    if *(int*)$pp != $init
      printf ">>> STORE at pc=0x%x  newval=0x%x (%d)\n", $pc, *(int*)$pp, *(int*)$pp
      printf "--- disasm around store ---\n"
      x/10i $pc-24
      printf "--- regs at store ---\n"
      printf " r3=0x%x  r4=0x%x  r5=0x%x  r6=0x%x  r7=0x%x\n", $r3,$r4,$r5,$r6,$r7
      printf " r8=0x%x  r9=0x%x r10=0x%x r11=0x%x r12=0x%x\n", $r8,$r9,$r10,$r11,$r12
      printf "r27=0x%x r28=0x%x r29=0x%x r30=0x%x r31=0x%x\n", $r27,$r28,$r29,$r30,$r31
      printf "--- backtrace ---\n"
      bt 12
      set $done = 1
    end
    set $n = $n + 1
  end
  printf "=== steps=%d done=%d ===\n", $n, $done
  kill
  quit
end
continue
quit
