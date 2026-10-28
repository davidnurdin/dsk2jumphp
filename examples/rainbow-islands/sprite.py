# CPC sprite frames of this game: w bytes x h lines, row-major; odd lines are stored mirrored
# (bytes reversed, the two pixels of each byte swapped) because the blitter zig-zags.
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools'))
from cpc import HW, mode0_pixels
def decode(mem, base, w, h):
    rows=[]
    for y in range(h):
        bs=mem[base+y*w:base+y*w+w]
        px=[p for b in bs for p in mode0_pixels(b)]
        if y&1: px=px[::-1]
        rows.append(px)
    return rows
if __name__=='__main__':
    from PIL import Image
    d=open(sys.argv[1],'rb').read(); pal=list(d[0x20000+17:0x20000+33])
    strips=[]
    for spec in sys.argv[3:]:
        a,n,w,h=spec.split(':'); a=int(a,16); n=int(n); w=int(w); h=int(h)
        im=Image.new('RGB',(n*(w*2+2),h),(255,0,255)); px=im.load()
        for f in range(n):
            for y,row in enumerate(decode(d,a+f*w*h,w,h)):
                for x,p in enumerate(row): px[f*(w*2+2)+x,y]=HW[pal[p]] if p else (0,0,0)
        strips.append(im)
    W=max(s.width for s in strips); H=sum(s.height+2 for s in strips)
    from PIL import Image as I
    out=I.new('RGB',(W,H),(60,60,60)); y=0
    for s in strips: out.paste(s,(0,y)); y+=s.height+2
    out.resize((out.width*6,out.height*3),I.NEAREST).save(sys.argv[2])
