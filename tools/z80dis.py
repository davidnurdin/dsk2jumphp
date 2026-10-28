# Z80 disassembler (documented opcodes and the usual undocumented ones); see USAGE below.
import sys
R=['b','c','d','e','h','l','(hl)','a']; RP=['bc','de','hl','sp']; RP2=['bc','de','hl','af']
CC=['nz','z','nc','c','po','pe','p','m']; ALU=['add a,','adc a,','sub ','sbc a,','and ','xor ','or ','cp ']
ROT=['rlc','rrc','rl','rr','sla','sra','sll','srl']
def dis(mem, pc, base=0):
    start=pc
    def b():
        nonlocal pc
        v=mem[pc-base] if 0<=pc-base<len(mem) else 0; pc+=1; return v
    def w(): lo=b(); hi=b(); return lo|hi<<8
    def sb(v): return v-256 if v>127 else v
    op=b(); ix=None
    if op in (0xdd,0xfd):
        ix='ix' if op==0xdd else 'iy'; op=b()
    def r(i,d=None):
        if ix and i==6: return f'({ix}{d:+d})'
        if ix and i in (4,5) : return ix+('h' if i==4 else 'l')
        return R[i]
    def hl(): return ix or 'hl'
    x=op>>6; y=(op>>3)&7; z=op&7; p=y>>1; q=y&1
    if op==0xcb:
        if ix:
            d=sb(b()); op2=b()
            x2=op2>>6;y2=(op2>>3)&7
            m=f'({ix}{d:+d})'
        else:
            op2=b(); x2=op2>>6;y2=(op2>>3)&7; m=R[op2&7]
        s=[f'{ROT[y2]} {m}',f'bit {y2},{m}',f'res {y2},{m}',f'set {y2},{m}'][x2]
    elif op==0xed:
        op=b(); x=op>>6; y=(op>>3)&7; z=op&7; p=y>>1; q=y&1
        if x==1:
            if z==0: s=f'in {R[y] if y!=6 else "f"},(c)'
            elif z==1: s=f'out (c),{R[y] if y!=6 else "0"}'
            elif z==2: s=('sbc hl,' if q==0 else 'adc hl,')+RP[p]
            elif z==3:
                n=w(); s=f'ld (#{n:04x}),{RP[p]}' if q==0 else f'ld {RP[p]},(#{n:04x})'
            elif z==4: s='neg'
            elif z==5: s='retn' if y!=1 else 'reti'
            elif z==6: s=f'im {[0,0,1,2,0,0,1,2][y]}'
            else: s=['ld i,a','ld r,a','ld a,i','ld a,r','rrd','rld','nop','nop'][y]
        elif x==2 and z<=3 and y>=4:
            s=[['ldi','cpi','ini','outi'],['ldd','cpd','ind','outd'],['ldir','cpir','inir','otir'],['lddr','cpdr','indr','otdr']][y-4][z]
        else: s=f'db #ed,#{op:02x}'
    else:
        d=None
        if ix and (x==1 and (y==6 or z==6) and not (y==6 and z==6) or (x==0 and z in(4,5,6) and y==6) or (x==2 and z==6)):
            d=sb(b())
        if x==0:
            if z==0:
                if y==0: s='nop'
                elif y==1: s="ex af,af'"
                elif y==2: e=sb(b()); s=f'djnz #{(pc+e)&0xffff:04x}'
                elif y==3: e=sb(b()); s=f'jr #{(pc+e)&0xffff:04x}'
                else: e=sb(b()); s=f'jr {CC[y-4]},#{(pc+e)&0xffff:04x}'
            elif z==1:
                if q==0: n=w(); s=f'ld {hl() if p==2 else RP[p]},#{n:04x}'
                else: s=f'add {hl()},{hl() if p==2 else RP[p]}'
            elif z==2:
                if q==0:
                    if p==0: s='ld (bc),a'
                    elif p==1: s='ld (de),a'
                    elif p==2: n=w(); s=f'ld (#{n:04x}),{hl()}'
                    else: n=w(); s=f'ld (#{n:04x}),a'
                else:
                    if p==0: s='ld a,(bc)'
                    elif p==1: s='ld a,(de)'
                    elif p==2: n=w(); s=f'ld {hl()},(#{n:04x})'
                    else: n=w(); s=f'ld a,(#{n:04x})'
            elif z==3: s=('inc ' if q==0 else 'dec ')+(hl() if p==2 else RP[p])
            elif z==4: s='inc '+r(y,d)
            elif z==5: s='dec '+r(y,d)
            elif z==6: n=b(); s=f'ld {r(y,d)},#{n:02x}'
            else: s=['rlca','rrca','rla','rra','daa','cpl','scf','ccf'][y]
        elif x==1:
            if y==6 and z==6: s='halt'
            else:
                a=r(y,d); c=r(z,d)
                if ix and (y==6 or z==6):
                    a=R[y] if z==6 else a; c=R[z] if y==6 else c
                s=f'ld {a},{c}'
        elif x==2: s=ALU[y]+r(z,d)
        else:
            if z==0: s='ret '+CC[y]
            elif z==1:
                if q==0: s='pop '+(hl() if p==2 else RP2[p])
                else: s=['ret','exx',f'jp ({hl()})',f'ld sp,{hl()}'][p]
            elif z==2: n=w(); s=f'jp {CC[y]},#{n:04x}'
            elif z==3:
                if y==0: n=w(); s=f'jp #{n:04x}'
                elif y==2: n=b(); s=f'out (#{n:02x}),a'
                elif y==3: n=b(); s=f'in a,(#{n:02x})'
                elif y==4: s=f'ex (sp),{hl()}'
                elif y==5: s='ex de,hl'
                elif y==6: s='di'
                else: s='ei'
            elif z==4: n=w(); s=f'call {CC[y]},#{n:04x}'
            elif z==5:
                if q==0: s='push '+(hl() if p==2 else RP2[p])
                else: n=w(); s=f'call #{n:04x}'
            elif z==6: n=b(); s=ALU[y]+f'#{n:02x}'
            else: s=f'rst #{y*8:02x}'
    return pc-start, s
USAGE = """Z80 disassembler.

    python3 -I tools/z80dis.py dump.bin FROM TO [--coverage coverage.bin]   an emulator dump (the CPU's 64 KB)
    python3 -I tools/z80dis.py file.bin FROM TO --org ADDR                   a raw file loaded at ADDR

FROM / TO are hex addresses. With --coverage (written by every emulator run), executed instructions get a
'*': code and data are told apart, and the routines the game really uses stand out.
"""

if __name__ == '__main__':
    import os
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    args = [a for a in sys.argv[1:]]
    opts = {}
    for name in ('--coverage', '--org'):
        if name in args:
            i = args.index(name)
            opts[name] = args[i + 1]
            del args[i:i + 2]
    if len(args) < 3:
        print(USAGE)
        sys.exit(1)
    path, start, end = args[0], int(args[1].lstrip('#&$'), 16), int(args[2].lstrip('#&$'), 16)
    if '--org' in opts:
        mem, org = open(path, 'rb').read(), int(opts['--org'].lstrip('#&$'), 16)
    else:
        from cpc import Dump
        mem, org = Dump(path).mem, 0
    cov = open(opts['--coverage'], 'rb').read() if '--coverage' in opts else None
    pc = start
    while pc <= end:
        length, text = dis(mem, pc, org)
        mark = ('*' if cov[pc] else ' ') if cov else ''
        print(f'{mark}{pc:04x}  {bytes(mem[pc - org:pc - org + length]).hex():10s} {text}')
        pc += length
