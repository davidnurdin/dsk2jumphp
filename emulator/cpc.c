/* A small Amstrad CPC 6128 made to run a disc game and look inside it while it runs.
 * See docs/emulator.md for every option and examples.
 *
 *   cpc <disk.dsk|-> <outdir> <frames> [options]
 *
 * Boot: with -rom (the machine's own ROMs) it starts like a real CPC (BASIC prompt), and -type types
 * RUN"DISC or |CPM. Without ROMs it copies the boot sector (track 0, sector #41 or the first one) to #0100
 * and jumps there, which is what |CPM does: enough for games that load themselves; -load / -pc start
 * a binary instead.
 *
 * Output in <outdir>: log.txt (events, watched writes, readers / writers), psg.bin (AY registers, one
 * 16-byte record per frame), coverage.bin (one byte per address: executed?), regs.txt (-r),
 * trace.txt (-t), screen-NNNNN.ppm (-s / -S), dump-NNNNN.bin (-d), bpXXXX-N.bin (-b).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "z80.h"

static uint8_t ram[8][16384];
static int bank_map[4] = {0, 1, 2, 3};
static Z80 cpu;
static int cur_frame;
static FILE *log_file;
static int log_fdc = 1;
static int fdc_first_byte;

static void log_event(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(log_file, fmt, ap);
    va_end(ap);
    fputc('\n', log_file);
}

#include "disk.inc"

/* ROMs (optional): lower = the OS, upper 0 = BASIC, upper 7 = AMSDOS */
static uint8_t *rom_lower, *rom_upper[256];
static int rom_lower_on, rom_upper_on, rom_select;

/* gate array */
static uint8_t ga_pen, ga_palette[17], ga_mode, ga_counter;
static int irq_pending;
/* crtc */
static uint8_t crtc_sel, crtc[18] = {63, 40, 46, 0x8e, 38, 0, 25, 30, 0, 7, 0, 0, 0x30, 0, 0, 0, 0, 0};
/* ppi / psg */
static uint8_t ppi_a, ppi_c, psg_sel, psg[16], psg_written[16];
static uint8_t keys[10];
static int vsync;

/* per-scanline captured state for rendering */
static uint8_t line_pal[312][17], line_mode[312];
static uint16_t line_start[312];
static int scan;

static uint8_t coverage[65536];
static uint16_t ring[256];
static unsigned ring_pos;
static int reg_pc[16], nreg, reg_hits;
static int refill_pc[4], refill_addr[4], refill_val[4], refill_n[4], nrefill;
static int fpoke_frame[64], fpoke_addr[64], fpoke_val[64], nfpoke;
static int poke_pc[16], poke_addr[16], poke_val[16], poke_done[16], npoke;
static FILE *reg_log;
static int reg_ix_bytes;
static int alarm_lo = -1, alarm_hi = -1, alarm_done, break_pc = -1, break_hits;

static int ur_lo = -1, ur_hi = -1;
static unsigned ur_count[65536];
static uint8_t mem_read(uint16_t a) {
    if (a >= ur_lo && a <= ur_hi && cpu.pc != a) ur_count[cpu.pc]++;
    if (a < 0x4000 && rom_lower_on && rom_lower) return rom_lower[a];
    if (a >= 0xc000 && rom_upper_on) {
        uint8_t *rom = rom_upper[rom_select] ? rom_upper[rom_select] : rom_upper[0];
        if (rom) return rom[a & 0x3fff];
    }
    return ram[bank_map[a >> 14]][a & 0x3fff];
}
#define MAX_WATCH 8
static int watch_lo[MAX_WATCH], watch_hi[MAX_WATCH], nwatch;
static int uw_lo = -1, uw_hi = -1;
static unsigned uw_count[65536];
static void mem_write(uint16_t a, uint8_t v) {
    if (fdc_first_byte) { fdc_first_byte = 0; log_event("  sector data -> %04x", a); }
    for (int q = 0; q < nwatch; q++)
        if (a >= watch_lo[q] && a <= watch_hi[q]) log_event("write %04x=%02x pc=%04x f=%d", a, v, cpu.pc, cur_frame);
    if (a >= uw_lo && a <= uw_hi) uw_count[cpu.pc]++;
    ram[bank_map[a >> 14]][a & 0x3fff] = v; /* writes always go to RAM, even under a ROM */
}

static void set_banks(uint8_t cfg) {
    static const int maps[8][4] = {
        {0, 1, 2, 3}, {0, 1, 2, 7}, {4, 5, 6, 7}, {0, 3, 2, 7},
        {0, 4, 2, 3}, {0, 5, 2, 3}, {0, 6, 2, 3}, {0, 7, 2, 3}};
    memcpy(bank_map, maps[cfg & 7], sizeof bank_map);
}

/* which code reads which keyboard row (once per 64-byte block of code): finds the input routine */
static uint8_t seen_rows[16][65536 / 64];
static uint8_t keyboard_row(void) {
    int row = ppi_c & 15;
    if (!seen_rows[row][cpu.pc / 64]) { seen_rows[row][cpu.pc / 64] = 1; log_event("key row %d pc=%04x", row, cpu.pc); }
    return row < 10 ? (uint8_t)~keys[row] : 0xff;
}

static void psg_bus(void) {
    switch (ppi_c >> 6) {
    case 3: psg_sel = ppi_a & 15; break;
    case 2: psg[psg_sel] = ppi_a; psg_written[psg_sel] = 1; break;
    }
}

#include "fdc.inc"

static uint8_t port_in(uint16_t port) {
    if (!(port & 0x0800)) {
        switch ((port >> 8) & 3) {
        case 0: /* PPI A: PSG read */
            if ((ppi_c >> 6) == 1) return psg_sel == 14 ? keyboard_row() : psg[psg_sel];
            return ppi_a;
        case 1: return 0x5e | (vsync ? 1 : 0); /* PPI B: Amstrad, 50 Hz, vsync in bit 0 */
        case 2: return ppi_c;
        }
    }
    if ((port & 0x0581) == 0x0100) return fdc_status();
    if ((port & 0x0581) == 0x0101) return fdc_read_data();
    if (!(port & 0x4000) && ((port >> 8) & 3) == 3) return crtc_sel >= 12 && crtc_sel < 18 ? crtc[crtc_sel] : 0;
    return 0xff;
}

static void port_out(uint16_t port, uint8_t v) {
    if ((port & 0xc000) == 0x4000) { /* gate array / RAM banking */
        switch (v >> 6) {
        case 0: ga_pen = v & 0x10 ? 16 : v & 15; break;
        case 1: ga_palette[ga_pen] = v & 0x1f; break;
        case 2:
            ga_mode = v & 3;
            rom_lower_on = !(v & 0x04);
            rom_upper_on = !(v & 0x08);
            if (v & 0x10) { ga_counter = 0; irq_pending = 0; }
            break;
        case 3: set_banks(v); break;
        }
    }
    if (!(port & 0x2000)) rom_select = v; /* upper ROM select (#DFxx) */
    if (!(port & 0x4000)) {
        int f = (port >> 8) & 3;
        if (f == 0) crtc_sel = v & 31;
        else if (f == 1 && crtc_sel < 18) crtc[crtc_sel] = v;
    }
    if ((port & 0x0581) == 0x0101) fdc_write_data(v);
    if (!(port & 0x0800)) {
        switch ((port >> 8) & 3) {
        case 0: ppi_a = v; psg_bus(); break;
        case 2: ppi_c = v; psg_bus(); break;
        case 3:
            if (!(v & 0x80)) { int bit = (v >> 1) & 7; if (v & 1) ppi_c |= 1 << bit; else ppi_c &= ~(1 << bit); psg_bus(); }
            break;
        }
    }
}

/* ---- keyboard: the CPC matrix (row 0-9, bit 0-7) ---- */
typedef struct { int frame, row, bit, down; } KeyEvent;
static KeyEvent *key_events;
static int nkey_events, key_events_cap;
static void add_key(int frame, int row, int bit, int down) {
    if (nkey_events == key_events_cap) {
        key_events_cap = key_events_cap ? key_events_cap * 2 : 64;
        key_events = realloc(key_events, key_events_cap * sizeof *key_events);
    }
    key_events[nkey_events++] = (KeyEvent){frame, row, bit, down};
}

#define SHIFT 0x100
/* row * 8 + bit of each character, | SHIFT when it needs SHIFT (upper case letters too) */
static int char_key(int ch) {
    static const char *letters[26] = {"8.5", "6.6", "7.6", "7.5", "7.2", "6.5", "6.4", "5.4", "4.3", "5.5", "4.5", "4.4", "4.6",
                                      "5.6", "4.2", "3.3", "8.3", "6.2", "7.4", "6.3", "5.2", "6.7", "7.3", "7.7", "5.3", "8.7"};
    static const char *digits[10] = {"4.0", "8.0", "8.1", "7.1", "7.0", "6.1", "6.0", "5.1", "5.0", "4.1"};
    const char *k = NULL;
    int shift = 0;
    if (ch >= 'a' && ch <= 'z') k = letters[ch - 'a'];
    else if (ch >= 'A' && ch <= 'Z') { k = letters[ch - 'A']; shift = 1; }
    else if (ch >= '0' && ch <= '9') k = digits[ch - '0'];
    else switch (ch) {
        case ' ': k = "5.7"; break;   case '\n': k = "2.2"; break;  case '.': k = "3.7"; break;
        case ',': k = "4.7"; break;   case ':': k = "3.5"; break;   case ';': k = "3.4"; break;
        case '-': k = "3.1"; break;   case '/': k = "3.6"; break;   case '@': k = "3.2"; break;
        case '[': k = "2.1"; break;   case ']': k = "2.3"; break;   case '\\': k = "2.6"; break;
        case '^': k = "3.0"; break;
        case '|': k = "3.2"; shift = 1; break;  case '"': k = "8.1"; shift = 1; break;
        case '!': k = "8.0"; shift = 1; break;  case '#': k = "7.1"; shift = 1; break;
        case '$': k = "7.0"; shift = 1; break;  case '%': k = "6.1"; shift = 1; break;
        case '&': k = "6.0"; shift = 1; break;  case '\'': k = "5.1"; shift = 1; break;
        case '(': k = "5.0"; shift = 1; break;  case ')': k = "4.1"; shift = 1; break;
        case '_': k = "4.0"; shift = 1; break;  case '=': k = "3.1"; shift = 1; break;
        case '*': k = "3.5"; shift = 1; break;  case '+': k = "3.4"; shift = 1; break;
        case '?': k = "3.6"; shift = 1; break;  case '>': k = "3.7"; shift = 1; break;
        case '<': k = "4.7"; shift = 1; break;
        }
    if (!k) return -1;
    return ((k[0] - '0') * 8 + (k[2] - '0')) | (shift ? SHIFT : 0);
}

/* key names usable in -k besides "row.bit" */
static int named_key(const char *name, int len) {
    static const struct { const char *name; int key; } names[] = {
        {"up", 0 * 8 + 0}, {"right", 0 * 8 + 1}, {"down", 0 * 8 + 2}, {"left", 1 * 8 + 0}, {"copy", 1 * 8 + 1},
        {"return", 2 * 8 + 2}, {"enter", 0 * 8 + 6}, {"shift", 2 * 8 + 5}, {"control", 2 * 8 + 7}, {"space", 5 * 8 + 7},
        {"esc", 8 * 8 + 2}, {"tab", 8 * 8 + 4}, {"caps", 8 * 8 + 6}, {"del", 9 * 8 + 7}, {"clr", 2 * 8 + 0},
        {"joy_up", 9 * 8 + 0}, {"joy_down", 9 * 8 + 1}, {"joy_left", 9 * 8 + 2}, {"joy_right", 9 * 8 + 3},
        {"joy_fire", 9 * 8 + 4}, {"joy_fire2", 9 * 8 + 5},
        {"f0", 1 * 8 + 7}, {"f1", 1 * 8 + 5}, {"f2", 1 * 8 + 6}, {"f3", 0 * 8 + 5}, {"f4", 2 * 8 + 4}, {"f5", 1 * 8 + 4},
        {"f6", 0 * 8 + 4}, {"f7", 1 * 8 + 2}, {"f8", 1 * 8 + 3}, {"f9", 0 * 8 + 3},
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if ((int)strlen(names[i].name) == len && !strncmp(names[i].name, name, len)) return names[i].key;
    if (len == 1) { int k = char_key(name[0]); if (k >= 0 && !(k & SHIFT)) return k; }
    return -1;
}

/* -k "frame:key+,frame:key-,...": key = row.bit or a name (joy_fire, space, a, 1...) */
static void parse_keys(const char *p) {
    while (p && *p) {
        int frame = atoi(p);
        const char *k = strchr(p, ':');
        if (!k) break;
        k++;
        const char *end = k;
        while (*end && *end != '+' && *end != '-') end++;
        if (!*end) break;
        int row, bit, key;
        if (sscanf(k, "%d.%d", &row, &bit) == 2 && k[1] == '.') key = row * 8 + bit;
        else key = named_key(k, (int)(end - k));
        if (key < 0) fprintf(stderr, "-k: unknown key '%.*s'\n", (int)(end - k), k);
        else add_key(frame, key / 8, key % 8, *end == '+');
        p = strchr(end, ',');
        if (p) p++;
    }
}

/* -type "frame:text": types text from that frame, 3 frames down / 3 up per key; "\n" is RETURN */
static void parse_type(const char *arg) {
    int frame = atoi(arg);
    const char *t = strchr(arg, ':');
    if (!t) return;
    for (t++; *t; t++) {
        int ch = *t;
        if (ch == '\\' && t[1] == 'n') { ch = '\n'; t++; }
        int key = char_key(ch);
        if (key < 0) { fprintf(stderr, "-type: no key for '%c'\n", ch); continue; }
        if (key & SHIFT) add_key(frame, 2, 5, 1);
        add_key(frame, (key & 0xff) / 8, key % 8, 1);
        add_key(frame + 3, (key & 0xff) / 8, key % 8, 0);
        if (key & SHIFT) add_key(frame + 3, 2, 5, 0);
        frame += 6;
    }
}

/* ---- video ---- */
static const uint8_t hw_rgb[32][3] = {
    {128,128,128},{128,128,128},{0,255,128},{255,255,128},{0,0,128},{255,0,128},{0,128,128},{255,128,128},
    {255,0,128},{255,255,128},{255,255,0},{255,255,255},{255,0,0},{255,0,255},{255,128,0},{255,128,255},
    {0,0,128},{0,255,128},{0,255,0},{0,255,255},{0,0,0},{0,0,255},{0,128,0},{0,128,255},
    {128,0,128},{128,255,128},{128,255,0},{128,255,255},{128,0,0},{128,0,255},{128,128,0},{128,128,255}};

static void pixel_pens(uint8_t b, int mode, int *pens) {
    if (mode == 0) {
        pens[0] = ((b >> 7) & 1) | ((b >> 2) & 2) | ((b >> 3) & 4) | ((b << 2) & 8);
        pens[1] = ((b >> 6) & 1) | ((b >> 1) & 2) | ((b >> 2) & 4) | ((b << 3) & 8);
    } else if (mode == 1) {
        for (int i = 0; i < 4; i++) pens[i] = ((b >> (7 - i)) & 1) | (((b >> (3 - i)) & 1) << 1);
    } else {
        for (int i = 0; i < 8; i++) pens[i] = (b >> (7 - i)) & 1;
    }
}

/* the displayed picture as the CRTC shows it (size from R1 / R6 / R9), mode and palette per scanline;
 * 4 PPM pixels per screen byte pair column = mode 1 resolution (mode 0 pixels come out 2 wide) */
static void save_screen(const char *dir, int frame) {
    int r1 = crtc[1], rows = crtc[6], r9 = crtc[9] + 1;
    int height = rows * r9, width = r1 * 8;
    if (width <= 0 || height <= 0 || width > 1024 || height > 312) return;
    int first = ((crtc[4] + 1) - crtc[7]) * r9 + crtc[5];
    char name[512];
    snprintf(name, sizeof name, "%s/screen-%05d.ppm", dir, frame);
    FILE *f = fopen(name, "wb");
    if (!f) return;
    fprintf(f, "P6 %d %d 255\n", width, height);
    for (int y = 0; y < height; y++) {
        int ln = ((first + y) % 312 + 312) % 312;
        int mode = line_mode[ln];
        uint16_t start = line_start[ln];
        int row = y / r9, ra = y % r9;
        for (int c = 0; c < r1; c++) {
            uint16_t ma = start + row * r1 + c;
            uint16_t addr = ((ma & 0x3000) << 2) | ((ra & 7) << 11) | ((ma & 0x3ff) << 1);
            for (int k = 0; k < 2; k++) {
                uint8_t b = ram[(uint16_t)(addr + k) >> 14][(addr + k) & 0x3fff];
                int pens[8];
                pixel_pens(b, mode, pens);
                for (int x = 0; x < 4; x++) {
                    int pen = mode == 0 ? pens[x / 2] : mode == 1 ? pens[x] : pens[x * 2];
                    fwrite(hw_rgb[line_pal[ln][pen]], 1, 3, f);
                }
            }
        }
    }
    fclose(f);
}

/* memory dump: the 8 RAM banks (128 KB), then 64 bytes of state, then the palette (17 bytes) and
 * the mode of each of the 312 scanlines (see docs/emulator.md "Dump format") */
static void dump_named(const char *dir, const char *base) {
    char name[512];
    snprintf(name, sizeof name, "%s/%s.bin", dir, base);
    FILE *f = fopen(name, "wb");
    if (!f) return;
    for (int b = 0; b < 8; b++) fwrite(ram[b], 1, 16384, f);
    uint8_t st[64] = {0};
    st[0] = cpu.a; st[1] = cpu.f; st[2] = cpu.b; st[3] = cpu.c; st[4] = cpu.d; st[5] = cpu.e; st[6] = cpu.h; st[7] = cpu.l;
    st[8] = cpu.ix; st[9] = cpu.ix >> 8; st[10] = cpu.iy; st[11] = cpu.iy >> 8; st[12] = cpu.sp; st[13] = cpu.sp >> 8;
    st[14] = cpu.pc; st[15] = cpu.pc >> 8; st[16] = ga_mode;
    memcpy(st + 17, ga_palette, 17);
    memcpy(st + 34, crtc, 18);
    for (int i = 0; i < 4; i++) st[52 + i] = bank_map[i];
    st[56] = rom_lower_on; st[57] = rom_upper_on; st[58] = rom_select;
    fwrite(st, 1, 64, f);
    fwrite(line_pal, 1, sizeof line_pal, f);
    fwrite(line_mode, 1, sizeof line_mode, f);
    fclose(f);
}
static void dump_state(const char *dir, int frame) {
    char n[64];
    snprintf(n, sizeof n, "dump-%05d", frame);
    dump_named(dir, n);
}

static int in_list(const char *list, int frame) {
    for (const char *p = list; p && *p; p = strchr(p, ',') ? strchr(p, ',') + 1 : NULL)
        if (atoi(p) == frame) return 1;
    return 0;
}

static uint8_t *read_file(const char *path, long *size) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot read %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(*size);
    if (fread(d, 1, *size, f) != (size_t)*size) { fprintf(stderr, "cannot read %s\n", path); exit(1); }
    fclose(f);
    return d;
}

static void usage(void) {
    fprintf(stderr, "usage: cpc <disk.dsk|-> <outdir> <frames> [options]   (docs/emulator.md)\n"
                    "  -rom os.rom [-urom 7:amsdos.rom]   boot with the machine's ROMs\n"
                    "  -type 'frame:RUN\"DISC\\n'            type text   -k 'frame:key+,frame:key-'  keys\n"
                    "  -pc hex  -load file@hex             no ROM: start address, raw binaries\n"
                    "  -s f1,f2  -S n                      screenshots     -d f1,f2   memory dumps\n"
                    "  -w lo-hi (x8)  -W lo-hi  -R lo-hi   watched writes, writers, readers\n"
                    "  -r pc1,pc2 [-X 32]  -t lo-hi  -b pc  registers at PCs, trace, breakpoint dumps\n"
                    "  -p pc:addr=val  -c pc:addr=val:n  -F frame:addr=val   pokes\n"
                    "  -x lo-hi                            report when execution enters lo-hi\n");
    exit(1);
}

int main(int argc, char **argv) {
    if (argc < 4) usage();
    const char *dir = argv[2];
    int frames = atoi(argv[3]);
    const char *shots = NULL, *dumps = NULL;
    int shot_every = 0, trace_lo = -1, trace_hi = -1, start_pc = 0x100, have_pc = 0;
    const char *loads[16];
    int nloads = 0;
    for (int i = 4; i < argc; i += 2) {
        if (i + 1 >= argc) usage();
        const char *o = argv[i], *v = argv[i + 1];
        if (!strcmp(o, "-k")) parse_keys(v);
        else if (!strcmp(o, "-type")) parse_type(v);
        else if (!strcmp(o, "-s")) shots = v;
        else if (!strcmp(o, "-S")) shot_every = atoi(v);
        else if (!strcmp(o, "-d")) dumps = v;
        else if (!strcmp(o, "-w") && nwatch < MAX_WATCH) {
            if (sscanf(v, "%x-%x", &watch_lo[nwatch], &watch_hi[nwatch]) == 1) watch_hi[nwatch] = watch_lo[nwatch];
            nwatch++;
        }
        else if (!strcmp(o, "-b")) sscanf(v, "%x", &break_pc);
        else if (!strcmp(o, "-W")) sscanf(v, "%x-%x", &uw_lo, &uw_hi);
        else if (!strcmp(o, "-R")) sscanf(v, "%x-%x", &ur_lo, &ur_hi);
        else if (!strcmp(o, "-r")) { for (const char *p = v; p && nreg < 16; p = strchr(p, ',') ? strchr(p, ',') + 1 : NULL) sscanf(p, "%x", &reg_pc[nreg++]); }
        else if (!strcmp(o, "-X")) reg_ix_bytes = atoi(v);
        else if (!strcmp(o, "-p") && npoke < 16) { sscanf(v, "%x:%x=%x", &poke_pc[npoke], &poke_addr[npoke], &poke_val[npoke]); npoke++; }
        else if (!strcmp(o, "-c") && nrefill < 4) { sscanf(v, "%x:%x=%x:%d", &refill_pc[nrefill], &refill_addr[nrefill], &refill_val[nrefill], &refill_n[nrefill]); nrefill++; }
        else if (!strcmp(o, "-F") && nfpoke < 64) { sscanf(v, "%d:%x=%x", &fpoke_frame[nfpoke], &fpoke_addr[nfpoke], &fpoke_val[nfpoke]); nfpoke++; }
        else if (!strcmp(o, "-t")) sscanf(v, "%x-%x", &trace_lo, &trace_hi);
        else if (!strcmp(o, "-x")) sscanf(v, "%x-%x", &alarm_lo, &alarm_hi);
        else if (!strcmp(o, "-pc")) { sscanf(v, "%x", &start_pc); have_pc = 1; }
        else if (!strcmp(o, "-load") && nloads < 16) loads[nloads++] = v;
        else if (!strcmp(o, "-rom")) {
            long n; uint8_t *d = read_file(v, &n);
            rom_lower = d;                      /* 16 KB OS, or 32 KB OS + BASIC (the usual cpc6128.rom) */
            if (n >= 32768 && !rom_upper[0]) rom_upper[0] = d + 16384;
        }
        else if (!strcmp(o, "-urom")) {
            int slot = atoi(v);
            const char *path = strchr(v, ':');
            if (!path || slot < 0 || slot > 255) usage();
            long n; rom_upper[slot] = read_file(path + 1, &n);
        }
        else { fprintf(stderr, "unknown option %s\n", o); usage(); }
    }
    if (strcmp(argv[1], "-") && !load_dsk(argv[1])) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    char name[512];
    snprintf(name, sizeof name, "%s/log.txt", dir);
    log_file = fopen(name, "w");
    if (!log_file) { fprintf(stderr, "cannot write in %s (does the folder exist?)\n", dir); return 1; }
    snprintf(name, sizeof name, "%s/psg.bin", dir);
    FILE *psg_log = fopen(name, "wb");
    FILE *trace = NULL;
    snprintf(name, sizeof name, "%s/regs.txt", dir);
    reg_log = fopen(name, "w");
    if (trace_lo >= 0) { snprintf(name, sizeof name, "%s/trace.txt", dir); trace = fopen(name, "w"); }

    cpu.read = mem_read; cpu.write = mem_write; cpu.in = port_in; cpu.out = port_out;
    z80_reset(&cpu);
    if (rom_lower) {
        /* a real start: the OS in the lower ROM, BASIC (or the selected upper ROM) above */
        rom_lower_on = rom_upper_on = 1;
        cpu.pc = 0;
    } else {
        Sector *boot = find_sector(0, 0, 0x41);
        if (!boot && disk[0][0].count) boot = &disk[0][0].s[0];
        if (boot && !nloads) memcpy(&ram[0][0x100], boot->data, boot->size < 512 ? boot->size : 512);
        for (int i = 0; i < nloads; i++) {
            char path[400]; unsigned addr;
            const char *at = strrchr(loads[i], '@');
            if (!at || sscanf(at + 1, "%x", &addr) != 1) usage();
            snprintf(path, sizeof path, "%.*s", (int)(at - loads[i]), loads[i]);
            long n; uint8_t *d = read_file(path, &n);
            for (long k = 0; k < n && addr + k < 0x10000; k++) ram[bank_map[(addr + k) >> 14]][(addr + k) & 0x3fff] = d[k];
            if (!have_pc && i == 0) start_pc = addr;
        }
        cpu.pc = start_pc;
        cpu.sp = 0xc000;
    }

    long line_t = 0;
    for (int frame = 0; frame < frames; frame++) {
        for (int q = 0; q < nkey_events; q++)
            if (key_events[q].frame == frame) {
                if (key_events[q].down) keys[key_events[q].row] |= 1 << key_events[q].bit;
                else keys[key_events[q].row] &= ~(1 << key_events[q].bit);
            }
        cur_frame = frame;
        for (int q = 0; q < nfpoke; q++) if (fpoke_frame[q] == frame) mem_write(fpoke_addr[q], fpoke_val[q]);
        memset(psg_written, 0, sizeof psg_written);
        for (scan = 0; scan < 312; scan++) {
            vsync = scan < 8;
            line_t += 256;
            while (cpu.cycles < line_t) {
                if (trace && cpu.pc >= trace_lo && cpu.pc <= trace_hi && !coverage[cpu.pc])
                    fprintf(trace, "%d %04x\n", frame, cpu.pc);
                coverage[cpu.pc] = 1;
                for (int q = 0; q < nreg; q++) if (cpu.pc == reg_pc[q] && reg_hits < 2000000) {
                    reg_hits++;
                    fprintf(reg_log, "%d %d %04x a=%02x f=%02x bc=%02x%02x de=%02x%02x hl=%02x%02x ix=%04x iy=%04x sp=%04x\n", frame, scan, cpu.pc, cpu.a, cpu.f, cpu.b, cpu.c, cpu.d, cpu.e, cpu.h, cpu.l, cpu.ix, cpu.iy, cpu.sp);
                    if (reg_ix_bytes) {
                        fprintf(reg_log, "   ix:");
                        for (int k = 0; k < reg_ix_bytes; k++) fprintf(reg_log, " %02x", mem_read(cpu.ix + k));
                        fprintf(reg_log, "\n");
                    }
                }
                for (int q = 0; q < npoke; q++) if (cpu.pc == poke_pc[q] && !poke_done[q]) {
                    poke_done[q] = 1; mem_write(poke_addr[q], poke_val[q]);
                }
                for (int q = 0; q < nrefill; q++) if (cpu.pc == refill_pc[q] && refill_n[q] > 0 && mem_read(refill_addr[q]) == 0) {
                    refill_n[q]--; mem_write(refill_addr[q], refill_val[q]);
                }
                if (cpu.pc == break_pc && break_hits < 50) {
                    char bn[64]; snprintf(bn, sizeof bn, "bp%04x-%d", break_pc, break_hits++);
                    dump_named(dir, bn);
                }
                ring[ring_pos++ & 255] = cpu.pc;
                if (!alarm_done && cpu.pc >= alarm_lo && cpu.pc <= alarm_hi) {
                    alarm_done = 1;
                    fprintf(log_file, "ALARM frame %d scan %d pc=%04x sp=%04x, last PCs:", frame, scan, cpu.pc, cpu.sp);
                    for (int i = 0; i < 256; i++) fprintf(log_file, " %04x", ring[(ring_pos + i) & 255]);
                    fprintf(log_file, "\n");
                    dump_named(dir, "alarm");
                }
                long before = cpu.cycles;
                z80_step(&cpu);
                long t = cpu.cycles - before;
                cpu.cycles = before + ((t + 3) & ~3); /* CPC wait states: 1 us steps */
                if (irq_pending && cpu.iff1 && !cpu.ei_delay) {
                    long b2 = cpu.cycles;
                    if (z80_interrupt(&cpu)) { irq_pending = 0; ga_counter &= 31; cpu.cycles = b2 + 20; }
                }
            }
            memcpy(line_pal[scan], ga_palette, 17);
            line_mode[scan] = ga_mode;
            line_start[scan] = (crtc[12] << 8) | crtc[13];
            /* HSYNC: the gate array interrupts every 52 lines, resynchronised by the VSYNC */
            if (++ga_counter == 52) { ga_counter = 0; irq_pending = 1; }
            if (scan == 1) { if (ga_counter >= 32) irq_pending = 1; ga_counter = 0; }
        }
        uint8_t rec[16];
        memcpy(rec, psg, 14);
        rec[14] = psg_written[13]; /* the envelope was restarted this frame */
        rec[15] = 0;
        fwrite(rec, 1, 16, psg_log);
        if (in_list(shots, frame) || (shot_every && frame % shot_every == 0)) save_screen(dir, frame);
        if (in_list(dumps, frame)) dump_state(dir, frame);
    }
    snprintf(name, sizeof name, "%s/coverage.bin", dir);
    FILE *cf = fopen(name, "wb");
    if (cf) { fwrite(coverage, 1, 65536, cf); fclose(cf); }
    for (int i = 0; i < 65536; i++) if (ur_count[i]) fprintf(log_file, "reader %04x %u\n", i, ur_count[i]);
    for (int i = 0; i < 65536; i++) if (uw_count[i]) fprintf(log_file, "writer %04x %u\n", i, uw_count[i]);
    fclose(psg_log); fclose(log_file); fclose(reg_log);
    if (trace) fclose(trace);
    fprintf(stderr, "frames %d, pc=%04x sp=%04x\n", frames, cpu.pc, cpu.sp);
    return 0;
}
