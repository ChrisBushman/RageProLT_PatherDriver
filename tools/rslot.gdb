# Is our render slot (mach64_render_triangles, runtime 0x800d0 in the current
# build) ever called during the draw?  Definitive check that geometry isn't
# reaching us (vs a logging red herring).
set pagination off
break *0x800d0
commands
  silent
  printf ">>> RENDER SLOT HIT: r3(ctx)=0x%x r4(v)=0x%x r5(n)=%d\n", $r3, $r4, $r5
  continue
end
run
quit
