import sys, struct
def unpack(src, size=0x2500):
    out=bytearray(); i=0
    while len(out)<size:
        c=src[i]; out.append(c); i+=1
        same=1
        while len(out)<size:
            a=src[i]
            if a!=c: break
            out.append(a); i+=1; same+=1
            if same==3:
                n=src[i]; i+=1
                out.extend([c]*(n-1)); break
        else: break
        if same==3: continue
    return bytes(out[:size])
def unpack2(src,size=0x2500):
    # literal port of #7135
    out=bytearray(); i=0
    while True:
        a=src[i]; c=a; out.append(a); i+=1
        if len(out)>=size: return bytes(out)
        while True:
            b=2; restart=False
            while b:
                a=src[i]
                if a!=c:
                    out.append(a); c=a; i+=1
                    if len(out)>=size: return bytes(out)
                    restart=True; break
                out.append(a); i+=1
                if len(out)>=size: return bytes(out)
                b-=1
            if restart: continue
            n=src[i]; i+=1; n-=1
            if n:
                for _ in range(n):
                    out.append(c)
                    if len(out)>=size: return bytes(out)
            break
def blocks(u):
    lens=struct.unpack('<8H',u[:16])
    tiles=u[0x10:0x210]; pal=u[0x210:0x250]; b=u[0x250]
    p=0x251; bl=[]
    for n in lens[:6]:
        bl.append(u[p:p+n]); p+=n
    return dict(header=lens,gfx512=tiles,data64=pal,byte=b,blocks=bl,rest=u[p:])
if __name__=='__main__':
    src=open(sys.argv[1],'rb').read()
    u=unpack2(src)
    ref=open(sys.argv[2],'rb').read()[0x7300:0x7300+0x2500] if len(sys.argv)>2 else None
    if ref: print('match', u==ref)
    b=blocks(u)
    print([hex(x) for x in b['header']], hex(b['byte']), [len(x) for x in b['blocks']], len(b['rest']))
