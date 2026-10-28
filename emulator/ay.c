/* AY-3-8912 (1 MHz, as in the CPC) rendered from 50 Hz register frames.
 *   ay psg.bin out.wav first_frame end_frame [mask]     mask: bits of channels to keep (default 7)
 * Each frame is 16 bytes: R0..R13 + flag "R13 written this frame" (envelope restart). */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

#define RATE 44100
static const double vol_table[16] = {0, 0.0105, 0.0154, 0.0222, 0.0326, 0.0462, 0.0657, 0.0954,
                                      0.1349, 0.1904, 0.2805, 0.3628, 0.4989, 0.6363, 0.8137, 1.0};

/* envelope level for shape (R13) after "pos" steps: shapes 0-7 do one ramp then stay at 0;
   8-15 repeat (pos keeps cycling through 32..47 / 16..47 patterns). */
static int envelope_value(int shape, int pos) {
    int up = (shape & 4) != 0, seg = pos / 16, i = pos & 15;
    if (!(shape & 8)) return seg == 0 ? (up ? i : 15 - i) : 0;
    switch (shape) {
    case 8: return 15 - i;
    case 12: return i;
    case 10: return (seg & 1) ? i : 15 - i;
    case 14: return (seg & 1) ? 15 - i : i;
    case 9: return seg == 0 ? 15 - i : 0;
    case 15: return seg == 0 ? i : 0;
    case 11: return seg == 0 ? 15 - i : 15;
    case 13: return seg == 0 ? i : 15;
    }
    return 0;
}

static void put16(FILE *f, int v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }
static void put32(FILE *f, int v) { put16(f, v & 0xffff); put16(f, (v >> 16) & 0xffff); }

int main(int argc, char **argv) {
    if (argc < 5) return 1;
    FILE *in = fopen(argv[1], "rb");
    fseek(in, 0, SEEK_END); long n = ftell(in) / 16; fseek(in, 0, SEEK_SET);
    uint8_t *fr = malloc(n * 16); fread(fr, 16, n, in); fclose(in);
    long a = atol(argv[3]), b = atol(argv[4]); if (b > n) b = n;
    int mask = argc > 5 ? atoi(argv[5]) : 7;
    long spf = RATE / 50, total = (b - a) * spf;
    float *L = calloc(total, sizeof(float)), *R = calloc(total, sizeof(float));
    /* AY state, stepped at 125 kHz (clock / 8) with sub-sampling */
    double tick_per_sample = 1000000.0 / 8 / RATE;
    double acc = 0;
    int tcnt[3] = {0}, tout[3] = {0}, ncnt = 0, nout = 0, ecnt = 0, estep = 0;
    unsigned lfsr = 1;
    int shape = 0, nbit = 0;
    for (long f = a; f < b; f++) {
        uint8_t *r = fr + f * 16;
        if (r[14]) { shape = r[13] & 15; estep = 0; ecnt = 0; }
        int tp[3]; for (int c = 0; c < 3; c++) { tp[c] = r[c * 2] | (r[c * 2 + 1] & 15) << 8; if (!tp[c]) tp[c] = 1; }
        int np = r[6] & 31; if (!np) np = 1;
        int ep = r[11] | r[12] << 8; if (!ep) ep = 1;
        for (long s = 0; s < spf; s++) {
            double sum[3] = {0}; int cnt = 0;
            acc += tick_per_sample;
            while (acc >= 1) {
                acc -= 1; cnt++;
                /* tone: toggles every period ticks (tick = clock/8 -> square of clock/16/period) */
                for (int c = 0; c < 3; c++) if (++tcnt[c] >= tp[c]) { tcnt[c] = 0; tout[c] ^= 1; }
                if (++ncnt >= np * 2) { ncnt = 0; nbit = ((lfsr ^ (lfsr >> 3)) & 1); lfsr = (lfsr >> 1) | (nbit << 16); nout = lfsr & 1; }
                if (++ecnt >= ep * 2) { ecnt = 0; if (++estep >= 48) estep = 16; }
                int ev = envelope_value(shape, estep);
                for (int c = 0; c < 3; c++) {
                    int ton = !((r[7] >> c) & 1), non = !((r[7] >> (c + 3)) & 1);
                    int gate = (ton ? tout[c] : 1) & (non ? nout : 1);
                    int v = r[8 + c];
                    double amp = (v & 16) ? vol_table[ev & 15] : vol_table[v & 15];
                    if ((mask >> c) & 1) sum[c] += gate * amp;
                }
            }
            if (cnt) for (int c = 0; c < 3; c++) sum[c] /= cnt;
            L[(f - a) * spf + s] = sum[0] + 0.5 * sum[1];
            R[(f - a) * spf + s] = sum[2] + 0.5 * sum[1];
        }
    }
    /* remove DC, normalise */
    double ml = 0, mr = 0, peak = 1e-9;
    for (long i = 0; i < total; i++) { ml += L[i]; mr += R[i]; }
    ml /= total; mr /= total;
    for (long i = 0; i < total; i++) { L[i] -= ml; R[i] -= mr; if (fabs(L[i]) > peak) peak = fabs(L[i]); if (fabs(R[i]) > peak) peak = fabs(R[i]); }
    FILE *o = fopen(argv[2], "wb");
    fwrite("RIFF", 1, 4, o); put32(o, 36 + total * 4); fwrite("WAVEfmt ", 1, 8, o);
    put32(o, 16); put16(o, 1); put16(o, 2); put32(o, RATE); put32(o, RATE * 4); put16(o, 4); put16(o, 16);
    fwrite("data", 1, 4, o); put32(o, total * 4);
    for (long i = 0; i < total; i++) { put16(o, (int)(L[i] / peak * 0.8 * 32767)); put16(o, (int)(R[i] / peak * 0.8 * 32767)); }
    fclose(o);
    return 0;
}
