# Infer sizes of the 29 sprite frames of an island and draw them.
import sys, os, struct, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools'))
from sprite import decode
from cpc import HW
def roughness(rows):
    h=len(rows); w=len(rows[0]); bad=0; n=0
    for y in range(h-1):
        for x in range(w):
            n+=1; bad+= rows[y][x]!=rows[y+1][x]
    return bad/max(1,n)
KNOWN={0xed80:(6,24),0xee10:(6,24),0xeea0:(6,24),0xef30:(6,24),0xf580:(6,24),0xf610:(6,24),0xf6a0:(6,24),0xf730:(6,24),
       0xf7c0:(4,16),0xc79e:(6,16),0xe5a0:(6,16),0xfd80:(6,16),0xfde0:(6,16)}
def frames(mem):
    p=[struct.unpack('<H',mem[0x4bfc+i*2:0x4bfe+i*2])[0] for i in range(29)]
    out=[]
    for i,a in enumerate(p):
        if a==0: out.append(None); continue
        if a in KNOWN: w,h=KNOWN[a]; out.append((a,w,h)); continue
        nxt=[q for q in p if q>a]; ln=(min(nxt)-a) if nxt else None
        best=None
        if ln and ln%6==0 and 10<=ln//6<=32: out.append((a,6,ln//6)); continue
        for w in (4,6,8,10,12,16):
            for h in ([ln//w] if ln and ln%w==0 else range(8,33)):
                if h<6 or h>48: continue
                if ln is None and h not in (16,24,32): continue
                r=roughness(decode(mem,a,w,h))
                if best is None or r<best[0]: best=(r,w,h)
        out.append((a,best[1],best[2]) if best else None)
    return out
if __name__=='__main__':
    from PIL import Image
    mem=open(sys.argv[1],'rb').read(); pal=list(mem[0x20000+17:0x20000+33])
    fr=frames(mem); print([(hex(f[0]),f[1],f[2]) if f else None for f in fr])
    W=sum((f[1]*2+3) for f in fr if f); H=max(f[2] for f in fr if f)
    im=Image.new('RGB',(W,H),(255,0,255)); px=im.load(); x0=0
    for f in fr:
        if not f: continue
        a,w,h=f
        for y,row in enumerate(decode(mem,a,w,h)):
            for x,p in enumerate(row): px[x0+x,y]=HW[pal[p]] if p else (0,0,0)
        x0+=w*2+3
    im.resize((im.width*4,im.height*4),Image.NEAREST).save(sys.argv[2])
