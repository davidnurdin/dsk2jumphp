#include "z80.h"

#define FC 0x01
#define FN 0x02
#define FP 0x04
#define FX 0x08
#define FH 0x10
#define FY 0x20
#define FZ 0x40
#define FS 0x80

static uint8_t szp[256], sz[256];
static int tables_ready;

static void init_tables(void) {
    for (int i = 0; i < 256; i++) {
        int p = 0;
        for (int b = 0; b < 8; b++) p ^= (i >> b) & 1;
        sz[i] = (i & 0x80) | (i == 0 ? FZ : 0) | (i & (FX | FY));
        szp[i] = sz[i] | (p ? 0 : FP);
    }
    tables_ready = 1;
}

#define RD(a) z->read((uint16_t)(a))
#define WR(a, v) z->write((uint16_t)(a), (uint8_t)(v))
#define BC ((uint16_t)(z->b << 8 | z->c))
#define DE ((uint16_t)(z->d << 8 | z->e))
#define HL ((uint16_t)(z->h << 8 | z->l))
#define SETBC(v) do { uint16_t _v = (v); z->b = _v >> 8; z->c = _v; } while (0)
#define SETDE(v) do { uint16_t _v = (v); z->d = _v >> 8; z->e = _v; } while (0)
#define SETHL(v) do { uint16_t _v = (v); z->h = _v >> 8; z->l = _v; } while (0)

static uint8_t fetch(Z80 *z) { return RD(z->pc++); }
static uint16_t fetch16(Z80 *z) { uint16_t lo = fetch(z); return lo | (fetch(z) << 8); }
static uint16_t rd16(Z80 *z, uint16_t a) { return RD(a) | (RD(a + 1) << 8); }
static void wr16(Z80 *z, uint16_t a, uint16_t v) { WR(a, v & 0xff); WR(a + 1, v >> 8); }
static void push(Z80 *z, uint16_t v) { z->sp -= 2; wr16(z, z->sp, v); }
static uint16_t pop(Z80 *z) { uint16_t v = rd16(z, z->sp); z->sp += 2; return v; }

void z80_reset(Z80 *z) {
    if (!tables_ready) init_tables();
    z->a = z->f = 0xff; z->sp = 0xffff; z->pc = 0; z->i = z->r = 0; z->im = 0;
    z->iff1 = z->iff2 = 0; z->halted = 0; z->ei_delay = 0; z->cycles = 0;
    z->ix = z->iy = 0xffff;
}

/* ---- ALU ---- */
static void add8(Z80 *z, uint8_t v, int carry) {
    int c = carry ? (z->f & FC) : 0;
    int r = z->a + v + c;
    z->f = sz[r & 0xff] | ((r >> 8) & FC) | ((z->a ^ v ^ r) & FH) |
           ((((z->a ^ ~v) & (z->a ^ r)) & 0x80) ? FP : 0);
    z->a = r;
}
static void sub8(Z80 *z, uint8_t v, int carry, int store) {
    int c = carry ? (z->f & FC) : 0;
    int r = z->a - v - c;
    uint8_t f = sz[r & 0xff] | ((r >> 8) & FC) | FN | ((z->a ^ v ^ r) & FH) |
                ((((z->a ^ v) & (z->a ^ r)) & 0x80) ? FP : 0);
    if (store) { z->f = f; z->a = r; }
    else z->f = (f & ~(FX | FY)) | (v & (FX | FY));
}
static void alu(Z80 *z, int op, uint8_t v) {
    switch (op) {
    case 0: add8(z, v, 0); break;
    case 1: add8(z, v, 1); break;
    case 2: sub8(z, v, 0, 1); break;
    case 3: sub8(z, v, 1, 1); break;
    case 4: z->a &= v; z->f = szp[z->a] | FH; break;
    case 5: z->a ^= v; z->f = szp[z->a]; break;
    case 6: z->a |= v; z->f = szp[z->a]; break;
    case 7: sub8(z, v, 0, 0); break;
    }
}
static uint8_t inc8(Z80 *z, uint8_t v) {
    uint8_t r = v + 1;
    z->f = (z->f & FC) | sz[r] | ((v ^ r) & FH) | (v == 0x7f ? FP : 0);
    return r;
}
static uint8_t dec8(Z80 *z, uint8_t v) {
    uint8_t r = v - 1;
    z->f = (z->f & FC) | sz[r] | FN | ((v ^ r) & FH) | (v == 0x80 ? FP : 0);
    return r;
}
static uint16_t add16(Z80 *z, uint16_t a, uint16_t b) {
    uint32_t r = a + b;
    z->f = (z->f & (FS | FZ | FP)) | ((r >> 16) & FC) | (((a ^ b ^ r) >> 8) & FH) | ((r >> 8) & (FX | FY));
    return r;
}
static uint16_t adc16(Z80 *z, uint16_t a, uint16_t b) {
    uint32_t r = a + b + (z->f & FC);
    z->f = ((r >> 16) & FC) | (((a ^ b ^ r) >> 8) & FH) | ((r >> 8) & (FS | FX | FY)) |
           ((r & 0xffff) ? 0 : FZ) | ((((a ^ ~b) & (a ^ r)) & 0x8000) ? FP : 0);
    return r;
}
static uint16_t sbc16(Z80 *z, uint16_t a, uint16_t b) {
    uint32_t r = a - b - (z->f & FC);
    z->f = ((r >> 16) & FC) | FN | (((a ^ b ^ r) >> 8) & FH) | ((r >> 8) & (FS | FX | FY)) |
           ((r & 0xffff) ? 0 : FZ) | ((((a ^ b) & (a ^ r)) & 0x8000) ? FP : 0);
    return r;
}
static uint8_t rot(Z80 *z, int op, uint8_t v) {
    uint8_t c;
    switch (op) {
    case 0: c = v >> 7; v = (v << 1) | c; break;
    case 1: c = v & 1; v = (v >> 1) | (c << 7); break;
    case 2: c = v >> 7; v = (v << 1) | (z->f & FC); break;
    case 3: c = v & 1; v = (v >> 1) | ((z->f & FC) << 7); break;
    case 4: c = v >> 7; v <<= 1; break;
    case 5: c = v & 1; v = (v >> 1) | (v & 0x80); break;
    case 6: c = v >> 7; v = (v << 1) | 1; break;
    default: c = v & 1; v >>= 1; break;
    }
    z->f = szp[v] | c;
    return v;
}
static int cond(Z80 *z, int cc) {
    switch (cc) {
    case 0: return !(z->f & FZ);
    case 1: return z->f & FZ;
    case 2: return !(z->f & FC);
    case 3: return z->f & FC;
    case 4: return !(z->f & FP);
    case 5: return z->f & FP;
    case 6: return !(z->f & FS);
    default: return z->f & FS;
    }
}

/* register access with optional IX/IY replacement of H/L (idx: 0 = HL, 1 = IX, 2 = IY) */
static uint8_t *reg8(Z80 *z, int r, int idx) {
    static uint8_t dummy;
    switch (r) {
    case 0: return &z->b; case 1: return &z->c; case 2: return &z->d; case 3: return &z->e;
    case 4: if (idx == 1) return ((uint8_t *)&z->ix) + 1; if (idx == 2) return ((uint8_t *)&z->iy) + 1; return &z->h;
    case 5: if (idx == 1) return (uint8_t *)&z->ix; if (idx == 2) return (uint8_t *)&z->iy; return &z->l;
    case 7: return &z->a;
    }
    return &dummy;
}
static uint16_t get_rp(Z80 *z, int p, int idx) {
    switch (p) {
    case 0: return BC; case 1: return DE;
    case 2: return idx == 1 ? z->ix : idx == 2 ? z->iy : HL;
    default: return z->sp;
    }
}
static void set_rp(Z80 *z, int p, int idx, uint16_t v) {
    switch (p) {
    case 0: SETBC(v); break; case 1: SETDE(v); break;
    case 2: if (idx == 1) z->ix = v; else if (idx == 2) z->iy = v; else SETHL(v); break;
    default: z->sp = v;
    }
}

static int exec_cb(Z80 *z, int idx, uint16_t addr) {
    uint8_t op = fetch(z);
    int x = op >> 6, y = (op >> 3) & 7, r = op & 7;
    uint8_t v;
    if (idx) v = RD(addr); else v = (r == 6) ? RD(HL) : *reg8(z, r, 0);
    int t = idx ? 23 : (r == 6 ? 15 : 8);
    switch (x) {
    case 0: v = rot(z, y, v); break;
    case 1:
        z->f = (z->f & FC) | FH | (sz[v & (1 << y)] & ~(FX | FY)) | (v & (FX | FY));
        if (!(v & (1 << y))) z->f |= FP;
        return idx ? 20 : (r == 6 ? 12 : 8);
    case 2: v &= ~(1 << y); break;
    case 3: v |= (1 << y); break;
    }
    if (idx) { WR(addr, v); if (r != 6) *reg8(z, r, 0) = v; }
    else if (r == 6) WR(HL, v);
    else *reg8(z, r, 0) = v;
    return t;
}

static int exec_ed(Z80 *z) {
    uint8_t op = fetch(z);
    int x = op >> 6, y = (op >> 3) & 7, zz = op & 7, p = y >> 1, q = y & 1;
    z->r = (z->r & 0x80) | ((z->r + 1) & 0x7f);
    if (x == 1) {
        switch (zz) {
        case 0: { uint8_t v = z->in(BC); z->f = (z->f & FC) | szp[v]; if (y != 6) *reg8(z, y, 0) = v; return 12; }
        case 1: z->out(BC, y == 6 ? 0 : *reg8(z, y, 0)); return 12;
        case 2: { uint16_t hl = HL; SETHL(q ? adc16(z, hl, get_rp(z, p, 0)) : sbc16(z, hl, get_rp(z, p, 0))); return 15; }
        case 3: { uint16_t a = fetch16(z); if (q) set_rp(z, p, 0, rd16(z, a)); else wr16(z, a, get_rp(z, p, 0)); return 20; }
        case 4: { uint8_t v = z->a; z->a = 0; sub8(z, v, 0, 1); return 8; }
        case 5: z->pc = pop(z); z->iff1 = z->iff2; return 14;
        case 6: z->im = (y & 3) == 0 ? 0 : (y & 3) == 2 ? 1 : (y & 3) == 3 ? 2 : 0; return 8;
        case 7:
            switch (y) {
            case 0: z->i = z->a; return 9;
            case 1: z->r = z->a; return 9;
            case 2: z->a = z->i; z->f = (z->f & FC) | sz[z->a] | (z->iff2 ? FP : 0); return 9;
            case 3: z->a = z->r; z->f = (z->f & FC) | sz[z->a] | (z->iff2 ? FP : 0); return 9;
            case 4: { uint8_t m = RD(HL); WR(HL, (z->a << 4) | (m >> 4)); z->a = (z->a & 0xf0) | (m & 0x0f); z->f = (z->f & FC) | szp[z->a]; return 18; }
            case 5: { uint8_t m = RD(HL); WR(HL, (m << 4) | (z->a & 0x0f)); z->a = (z->a & 0xf0) | (m >> 4); z->f = (z->f & FC) | szp[z->a]; return 18; }
            default: return 8;
            }
        }
    }
    if (x == 2 && zz <= 3 && y >= 4) {
        int dir = (y & 1) ? -1 : 1, rep = y >= 6;
        switch (zz) {
        case 0: { /* LDI/LDD */
            uint8_t v = RD(HL); WR(DE, v);
            SETHL(HL + dir); SETDE(DE + dir); SETBC(BC - 1);
            uint8_t n = v + z->a;
            z->f = (z->f & (FS | FZ | FC)) | (BC ? FP : 0) | (n & FX) | ((n << 4) & FY);
            if (rep && BC) { z->pc -= 2; return 21; }
            return 16;
        }
        case 1: { /* CPI/CPD */
            uint8_t v = RD(HL); uint8_t r = z->a - v;
            SETHL(HL + dir); SETBC(BC - 1);
            z->f = (z->f & FC) | FN | (sz[r] & ~(FX | FY)) | ((z->a ^ v ^ r) & FH) | (BC ? FP : 0);
            if (rep && BC && r) { z->pc -= 2; return 21; }
            return 16;
        }
        case 2: { /* INI/IND */
            uint8_t v = z->in(BC); WR(HL, v); SETHL(HL + dir); z->b--;
            z->f = FN | (z->b ? 0 : FZ);
            if (rep && z->b) { z->pc -= 2; return 21; }
            return 16;
        }
        case 3: { /* OUTI/OUTD: B is decremented before the port write */
            uint8_t v = RD(HL); z->b--; z->out(BC, v); SETHL(HL + dir);
            z->f = FN | (z->b ? 0 : FZ);
            if (rep && z->b) { z->pc -= 2; return 21; }
            return 16;
        }
        }
    }
    return 8;
}

int z80_step(Z80 *z) {
    if (z->ei_delay) z->ei_delay--;
    if (z->halted) { z->cycles += 4; z->r = (z->r & 0x80) | ((z->r + 1) & 0x7f); return 4; }
    int idx = 0, t = 0;
    uint8_t op;
    for (;;) {
        op = fetch(z);
        z->r = (z->r & 0x80) | ((z->r + 1) & 0x7f);
        if (op == 0xdd) { idx = 1; t += 4; continue; }
        if (op == 0xfd) { idx = 2; t += 4; continue; }
        break;
    }
    uint16_t *ir = idx == 1 ? &z->ix : &z->iy;
    int x = op >> 6, y = (op >> 3) & 7, zz = op & 7, p = y >> 1, q = y & 1;

    /* memory operand (HL) or (IX+d) */
#define MADDR() (idx ? (uint16_t)(*ir + (int8_t)fetch(z)) : HL)

    if (op == 0xcb) {
        if (idx) { uint16_t a = *ir + (int8_t)fetch(z); t += exec_cb(z, idx, a); }
        else t += exec_cb(z, 0, 0);
        z->cycles += t; return t;
    }
    if (op == 0xed) { t += exec_ed(z); z->cycles += t; return t; }

    switch (x) {
    case 0:
        switch (zz) {
        case 0:
            switch (y) {
            case 0: t += 4; break;
            case 1: { uint8_t a = z->a, f = z->f; z->a = z->a_; z->f = z->f_; z->a_ = a; z->f_ = f; t += 4; break; }
            case 2: { int8_t e = fetch(z); if (--z->b) { z->pc += e; t += 13; } else t += 8; break; }
            case 3: { int8_t e = fetch(z); z->pc += e; t += 12; break; }
            default: { int8_t e = fetch(z); if (cond(z, y - 4)) { z->pc += e; t += 12; } else t += 7; break; }
            }
            break;
        case 1:
            if (!q) set_rp(z, p, idx, fetch16(z)), t += 10;
            else { uint16_t r = get_rp(z, 2, idx); set_rp(z, 2, idx, add16(z, r, get_rp(z, p, idx))); t += 11; }
            break;
        case 2:
            switch (op) {
            case 0x02: WR(BC, z->a); t += 7; break;
            case 0x12: WR(DE, z->a); t += 7; break;
            case 0x22: wr16(z, fetch16(z), get_rp(z, 2, idx)); t += 16; break;
            case 0x32: WR(fetch16(z), z->a); t += 13; break;
            case 0x0a: z->a = RD(BC); t += 7; break;
            case 0x1a: z->a = RD(DE); t += 7; break;
            case 0x2a: set_rp(z, 2, idx, rd16(z, fetch16(z))); t += 16; break;
            case 0x3a: z->a = RD(fetch16(z)); t += 13; break;
            }
            break;
        case 3: set_rp(z, p, idx, get_rp(z, p, idx) + (q ? -1 : 1)); t += 6; break;
        case 4: case 5:
            if (y == 6) {
                uint16_t a = MADDR(); uint8_t v = RD(a);
                WR(a, zz == 4 ? inc8(z, v) : dec8(z, v)); t += idx ? 19 : 11;
            } else {
                uint8_t *r = reg8(z, y, idx); *r = zz == 4 ? inc8(z, *r) : dec8(z, *r); t += 4;
            }
            break;
        case 6:
            if (y == 6) { uint16_t a = MADDR(); WR(a, fetch(z)); t += idx ? 15 : 10; }
            else { *reg8(z, y, idx) = fetch(z); t += 7; }
            break;
        case 7:
            t += 4;
            switch (y) {
            case 0: { uint8_t c = z->a >> 7; z->a = (z->a << 1) | c; z->f = (z->f & (FS | FZ | FP)) | c | (z->a & (FX | FY)); break; }
            case 1: { uint8_t c = z->a & 1; z->a = (z->a >> 1) | (c << 7); z->f = (z->f & (FS | FZ | FP)) | c | (z->a & (FX | FY)); break; }
            case 2: { uint8_t c = z->a >> 7; z->a = (z->a << 1) | (z->f & FC); z->f = (z->f & (FS | FZ | FP)) | c | (z->a & (FX | FY)); break; }
            case 3: { uint8_t c = z->a & 1; z->a = (z->a >> 1) | ((z->f & FC) << 7); z->f = (z->f & (FS | FZ | FP)) | c | (z->a & (FX | FY)); break; }
            case 4: { /* DAA */
                uint8_t a = z->a, corr = 0, c = z->f & FC;
                if ((z->f & FH) || (a & 0x0f) > 9) corr |= 0x06;
                if (c || a > 0x99) { corr |= 0x60; c = FC; }
                if (z->f & FN) z->a -= corr; else z->a += corr;
                z->f = (z->f & FN) | szp[z->a] | c | ((a ^ z->a) & FH);
                break;
            }
            case 5: z->a = ~z->a; z->f = (z->f & (FS | FZ | FP | FC)) | FH | FN | (z->a & (FX | FY)); break;
            case 6: z->f = (z->f & (FS | FZ | FP)) | FC | (z->a & (FX | FY)); break;
            case 7: z->f = ((z->f & (FS | FZ | FP | FC)) | ((z->f & FC) ? FH : 0) | (z->a & (FX | FY))) ^ FC; break;
            }
            break;
        }
        break;
    case 1:
        if (op == 0x76) { z->halted = 1; t += 4; break; }
        if (y == 6) { uint16_t a = MADDR(); WR(a, *reg8(z, zz, 0)); t += idx ? 19 : 7; }
        else if (zz == 6) { uint16_t a = MADDR(); *reg8(z, y, 0) = RD(a); t += idx ? 19 : 7; }
        else { *reg8(z, y, idx) = *reg8(z, zz, idx); t += 4; }
        break;
    case 2:
        if (zz == 6) { uint16_t a = MADDR(); alu(z, y, RD(a)); t += idx ? 19 : 7; }
        else { alu(z, y, *reg8(z, zz, idx)); t += 4; }
        break;
    case 3:
        switch (zz) {
        case 0: if (cond(z, y)) { z->pc = pop(z); t += 11; } else t += 5; break;
        case 1:
            if (!q) {
                uint16_t v = pop(z);
                if (p == 3) { z->a = v >> 8; z->f = v; } else set_rp(z, p, idx, v);
                t += 10;
            } else switch (p) {
                case 0: z->pc = pop(z); t += 10; break;
                case 1: { uint8_t s;
                    s = z->b; z->b = z->b_; z->b_ = s; s = z->c; z->c = z->c_; z->c_ = s;
                    s = z->d; z->d = z->d_; z->d_ = s; s = z->e; z->e = z->e_; z->e_ = s;
                    s = z->h; z->h = z->h_; z->h_ = s; s = z->l; z->l = z->l_; z->l_ = s; t += 4; break; }
                case 2: z->pc = get_rp(z, 2, idx); t += 4; break;
                case 3: z->sp = get_rp(z, 2, idx); t += 6; break;
            }
            break;
        case 2: { uint16_t a = fetch16(z); if (cond(z, y)) z->pc = a; t += 10; break; }
        case 3:
            switch (y) {
            case 0: z->pc = fetch16(z); t += 10; break;
            case 2: { uint8_t n = fetch(z); z->out((z->a << 8) | n, z->a); t += 11; break; }
            case 3: { uint8_t n = fetch(z); z->a = z->in((z->a << 8) | n); t += 11; break; }
            case 4: { uint16_t v = rd16(z, z->sp); wr16(z, z->sp, get_rp(z, 2, idx)); set_rp(z, 2, idx, v); t += 19; break; }
            case 5: { uint16_t d = DE; SETDE(HL); SETHL(d); t += 4; break; }
            case 6: z->iff1 = z->iff2 = 0; t += 4; break;
            case 7: z->iff1 = z->iff2 = 1; z->ei_delay = 1; t += 4; break;
            }
            break;
        case 4: { uint16_t a = fetch16(z); if (cond(z, y)) { push(z, z->pc); z->pc = a; t += 17; } else t += 10; break; }
        case 5:
            if (!q) { push(z, p == 3 ? (z->a << 8 | z->f) : get_rp(z, p, idx)); t += 11; }
            else if (p == 0) { uint16_t a = fetch16(z); push(z, z->pc); z->pc = a; t += 17; }
            break;
        case 6: alu(z, y, fetch(z)); t += 7; break;
        case 7: push(z, z->pc); z->pc = y * 8; t += 11; break;
        }
        break;
    }
    z->cycles += t;
    return t;
}

int z80_interrupt(Z80 *z) {
    if (!z->iff1 || z->ei_delay) return 0;
    if (z->halted) { z->halted = 0; }
    z->iff1 = z->iff2 = 0;
    z->r = (z->r & 0x80) | ((z->r + 1) & 0x7f);
    push(z, z->pc);
    if (z->im == 2) { z->pc = rd16(z, (z->i << 8) | 0xff); z->cycles += 19; return 19; }
    z->pc = 0x38; z->cycles += 13; return 13;
}
