# extract_round.py emu dsk workdir round -> workdir/round-NN/{log,dump,map.json}
# Runs one round: the round number is poked when the game picks it (#2761: #FEE0 = round, #FEDF = #80 so the
# island is reloaded), the intro pan is refilled so the camera rises to the top of the round (#2986), the
# joystick inserts a credit (up) and starts (fire). The writes to the camera (#FEFB) and to the map ring
# (#0800-#0FFF) are logged; mapbuild.py rebuilds the whole map from them. -pc 110: the boot code starts after
# the 16-byte parameter block of the boot sector.
import sys, os, subprocess, json, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mapbuild import build
emu,dsk,work,n=sys.argv[1],sys.argv[2],sys.argv[3],int(sys.argv[4])
d=f'{work}/round-{n:02d}'; os.makedirs(d,exist_ok=True)
for f in os.listdir(d): os.remove(os.path.join(d,f))
subprocess.run([emu,dsk,d,'2201','-d','2200','-S','400','-w','fefb-fefb','-w','0800-0fff',
    '-p',f'2761:fee0={n:02x}','-p','2761:fedf=80','-c','2986:fefe=81:2','-k','300:9.0+,320:9.0-,400:9.4+,420:9.4-','-pc','110'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
lines=open(d+'/log.txt').read().splitlines()
# the round starts with the big ring clear after frame 400
start=min(int(m[1]) for l in lines if (m:=re.match(r'write 0\w+=\w+ pc=\w+ f=(\d+)',l)) and int(m[1])>400)
rows=build(lines,start)
cams=[int(m[1],16) for l in lines if (m:=re.match(r'write fefb=(\w+) pc=\w+ f=(\d+)',l)) and int(m[2])>=start]
un=[]
for c in cams:
    c=c-256 if c>127 else c
    if un: c=un[-1]+((c-un[-1]+128)%256-128)
    un.append(c)
cams=un
top=min(cams); rest=cams[-1]
mem=open(d+'/dump-02200.bin','rb').read()
out={'round':n,'top':top,'start_camera':rest,'rows':[rows[y] for y in range(top,0)]}
json.dump(out,open(d+'/map.json','w'))
print(n,'top',top,'start',rest,'height',-top)
