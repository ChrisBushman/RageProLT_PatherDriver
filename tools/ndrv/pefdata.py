import struct,sys
def vcount(b,i):
    v=0
    while True:
        x=b[i]; i+=1; v=(v<<7)|(x&0x7f)
        if not x&0x80: return v,i
def unpack(b):
    out=bytearray(); i=0
    while i<len(b):
        op=b[i]>>5; cnt=b[i]&0x1f; i+=1
        if cnt==0: cnt,i=vcount(b,i)
        if op==0: out+=bytes(cnt)
        elif op==1: out+=b[i:i+cnt]; i+=cnt
        elif op==2:
            rc,i=vcount(b,i); blk=b[i:i+cnt]; i+=cnt; out+=blk*(rc+1)
        elif op==3 or op==4:
            common=cnt; cust,i=vcount(b,i); rc,i=vcount(b,i)
            if op==3: com=b[i:i+common]; i+=common
            else: com=bytes(common)
            out+=com
            for _ in range(rc):
                out+=b[i:i+cust]; i+=cust; out+=com
        else: raise Exception('op%d'%op)
    return bytes(out)
def load(path):
    c=open(path,'rb').read()
    n=struct.unpack('>H',c[32:34])[0]
    secs=[]
    for k in range(n):
        o=40+k*28
        nm,dd,tl,pl,ul,co=struct.unpack('>iIIIII',c[o:o+24]); kind=c[o+24]
        secs.append(dict(def_=dd,total=tl,packed=pl,unp=ul,off=co,kind=kind))
    return c,secs
if __name__=='__main__':
    c,secs=load(sys.argv[1]); print(secs)
