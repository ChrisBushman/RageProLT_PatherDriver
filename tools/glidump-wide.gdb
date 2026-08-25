# Dump the gli context (r3 at sub_4530 entry, 0x509530) as 0x8000 words = 128 KB,
# wide enough to cover past the bound-texture channel at gli+0x15748 in case the
# texture-env-mode field lives high like that one did.  First hit only, then quit.
set pagination off
set width 0
set height 0
break *0x509530
commands
  silent
  printf "GLI_BASE=0x%x\n", $r3
  x/32768xw $r3
  printf "END_GLI_DUMP\n"
  quit
end
run
quit
