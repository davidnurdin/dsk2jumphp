/* Small Z80 core: documented opcodes + the common undocumented ones (IXH/IXL, SLL, DDCB copies). */
#ifndef Z80_H
#define Z80_H
#include <stdint.h>

typedef struct Z80 {
    uint8_t a, f, b, c, d, e, h, l;
    uint8_t a_, f_, b_, c_, d_, e_, h_, l_;
    uint16_t ix, iy, sp, pc;
    uint8_t i, r, im, iff1, iff2, halted;
    int ei_delay;
    long cycles;                  /* T-states */
    uint8_t (*read)(uint16_t);
    void (*write)(uint16_t, uint8_t);
    uint8_t (*in)(uint16_t);
    void (*out)(uint16_t, uint8_t);
} Z80;

void z80_reset(Z80 *z);
int z80_step(Z80 *z);             /* executes one instruction, returns T-states */
int z80_interrupt(Z80 *z);        /* IM1/IM2 maskable interrupt; returns T-states, 0 if refused */
#endif
