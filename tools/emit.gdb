# The geometry-emit fn gli[0x7990]=0x84014 IS called (both gates pass) with
#   r3=drvctx r4=dispatch-table(0x22c8c8) r5=gli+0x300
# but it never dispatches to our primitive slots.  Identify its module and
# disassemble it live to see how/whether it reaches the table.
set pagination off
set width 0
set $n=0
break *0x84014
commands
  silent
  set $n=$n+1
  printf "\n>>> [%d] emit_fn @0x84014  r3(drvctx)=0x%x r4(table)=0x%x r5=0x%x\n", $n, $r3, $r4, $r5
  info symbol 0x84014
  printf "-- first 60 insns of emit fn --\n"
  x/60i 0x84014
  continue
end
run
quit
