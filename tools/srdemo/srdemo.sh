#!/bin/sh
# srdemo.sh STAGE ["ENV.."] : run the SDL demo at that stage on the MDD, scan the screen at 10 s, kill by PID, print painted bbox, save ../../stale/sd.bin
S=/tmp/claude-1000/-var-home-sam/42b40b94-f4fd-4664-ba66-f9b8e0758d82/scratchpad
"$S/sonicr/killsr.sh" >/dev/null 2>&1
ssh -o ConnectTimeout=8 MDD "cd ~/RageProLT/tools; rm -f ~/mach64gld.log ~/scan_D.bin; (~/rungui.sh /tmp/sd.out 60 env DEMO_STAGE=$1 DEMO_FRAMES=100000 $2 /Users/sam/sonicr/srdemo > /tmp/sd.run 2>&1 &); sleep 10; sudo ./mach64_scanshot save ~/scan_D.bin >/dev/null; for p in \$(ps -axww -o pid,command | grep '[s]onicr/srdemo' | grep -v 'bash\|rungui\|env' | awk '{print \$1}'); do kill \$p; done; sleep 1; head -n 3 /tmp/sd.out"
scp -q MDD:scan_D.bin $S/stale/sd.bin
python3 - <<P
import struct
d=open('$S/stale/sd.bin','rb').read()
def px(x,y):
    v=struct.unpack_from('<H',d,y*1280+x*2)[0]; return ((v>>10)&31,(v>>5)&31,v&31)
# inside the window area only (rows 44..423 to stay clear of the shadow/dock): bbox of non-black
xs=[];ys=[]
for y in range(44,424):
    for x in range(0,640,2):
        if px(x,y)!=(0,0,0): xs.append(x); ys.append(y)
print('non-black bbox in window rows 44..423: x %d..%d  y %d..%d (%d px sampled)'%(min(xs),max(xs),min(ys),max(ys),len(xs)) if xs else 'nothing painted')
P
