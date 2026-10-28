# Rebuild an absolute round map from the emulator's "write" log.
# The game keeps the map in a ring of 64 rows x 32 tiles at #0800; #FEFB is the camera (top row, 8 bits).
import sys, re
def build(lines, start_frame):
    ring=bytearray(2048); cam=-1; first=None; started=False; going_up=True
    rows={}; bottom=None
    for l in lines:
        m=re.match(r'write (\w+)=(\w+) pc=(\w+) f=(\d+)',l)
        if not m or int(m[4])<start_frame: continue
        a=int(m[1],16); v=int(m[2],16)
        if a==0xfefb:
            c=v if v<128 else v-256
            c=cam+((c-cam+128)%256-128)
            if first is None: first=c
            if started and c>cam: going_up=False
            if cam-c==1: started=True
            cam=c
            if not going_up and c==first and bottom is None: bottom=bytes(ring)
            continue
        ring[a-0x800]=v
        if not going_up: continue
        r=(a-0x800)//32; x=(a-0x800)%32
        k=(r-cam)%64
        if k>=40: k-=64
        rows.setdefault(cam+k,[0]*32)[x]=v
    if bottom:
        for y in range(first-6,0):
            r=y&63; rows[y]=list(bottom[r*32:r*32+32])
    return rows
