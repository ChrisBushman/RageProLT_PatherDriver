# At sub_4530 (draw/flush time) check whether our attach-time descriptor write
# survived AND whether glViewport applied it to the actual viewport state.
#   desc = gli[0x789c]; desc[0xc]/[0x10] = clamp maxes (we wrote 0x800)
#   gli+0x4e58/0x4e5c = int viewport (glViewport output; 0 => still culled)
#   gli+0xf0/0xf4     = float drawable dims
set pagination off
set width 0
break *0x509530
commands
  silent
  set $gli=$r3
  set $d=*(unsigned int*)($gli+0x789c)
  printf "GLI=0x%x DESC=0x%x\n", $gli, $d
  printf "  desc[0xc]=0x%x desc[0x10]=0x%x  (we wrote 0x800)\n", *(unsigned int*)($d+0xc), *(unsigned int*)($d+0x10)
  printf "  gli+0x4e58(vpW)=0x%x gli+0x4e5c(vpH)=0x%x  (0 => viewport still culled)\n", *(unsigned int*)($gli+0x4e58), *(unsigned int*)($gli+0x4e5c)
  printf "  gli+0xf0=0x%x gli+0xf4=0x%x\n", *(unsigned int*)($gli+0xf0), *(unsigned int*)($gli+0xf4)
  quit
end
run
quit
