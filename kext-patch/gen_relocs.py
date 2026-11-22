import struct, pickle
d=open('ATIRagePro.orig','rb').read()
out=[]
for i in range(5533):
    e=0x1aca4+8*i; w0,w1=struct.unpack('>II',d[e:e+8]); sc=bool(w0&0x80000000)
    if sc: addr=w0&0xffffff; typ=(w0>>24)&0xf
    else: addr=w0; typ=w1&0xf
    out.append((e,w0,w1,sc,addr,typ))
pickle.dump(out,open('_relocs.pkl','wb')); print(len(out),'relocs; pairs:',sum(1 for t in out if t[5]==1))
