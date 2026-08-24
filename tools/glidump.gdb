# Dump the gli context (r3 at sub_4530 entry, 0x509530) as 0x2000 words so a
# software run (renders) and hardware run (culls) can be diffed field-by-field.
# First hit only, then quit.
set pagination off
set width 0
set height 0
break *0x509530
commands
  silent
  printf "GLI_BASE=0x%x\n", $r3
  x/8192xw $r3
  printf "END_GLI_DUMP\n"
  quit
end
run
quit
