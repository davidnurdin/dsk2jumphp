"""Builds tests/out/test.dsk: a self-booting disc (system format) whose boot sector, run at #0100 like |CPM
does, sets mode 0, pen 0 black / pen 1 red, fills the screen with pen 1 and plays a note on AY channel A.
Hand-assembled Z80, so the tests need no commercial disc."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
from dsk import make_system_disc


def psg(reg, value):
    """Writes an AY register through the PPI: select (#F6C0), write (#F680), inactive (#F600)."""
    return bytes([0x06, 0xf4, 0x3e, reg, 0xed, 0x79,          # ld b,#f4 / ld a,reg / out (c),a
                  0x01, 0xc0, 0xf6, 0xed, 0x49,               # ld bc,#f6c0 / out (c),c
                  0x01, 0x00, 0xf6, 0xed, 0x49,               # ld bc,#f600 / out (c),c
                  0x06, 0xf4, 0x3e, value, 0xed, 0x79,        # ld b,#f4 / ld a,value / out (c),a
                  0x01, 0x80, 0xf6, 0xed, 0x49,               # ld bc,#f680 / out (c),c
                  0x01, 0x00, 0xf6, 0xed, 0x49])              # ld bc,#f600 / out (c),c


code = bytes([0xf3,                                           # di
              0x01, 0x8c, 0x7f, 0xed, 0x49,                   # ld bc,#7f8c / out (c),c : mode 0, ROMs off
              0x01, 0x00, 0x7f, 0xed, 0x49, 0x0e, 0x54, 0xed, 0x49,   # pen 0 = hardware 20 (black)
              0x01, 0x01, 0x7f, 0xed, 0x49, 0x0e, 0x4c, 0xed, 0x49,   # pen 1 = hardware 12 (red)
              0x21, 0x00, 0xc0, 0x11, 0x01, 0xc0, 0x01, 0xff, 0x3f,   # ld hl,#c000 / ld de,#c001 / ld bc,#3fff
              0x36, 0xc0, 0xed, 0xb0])                        # ld (hl),#c0 (2 pixels of pen 1) / ldir
code += psg(0, 0x00) + psg(1, 0x01) + psg(7, 0x3e) + psg(8, 0x0f)  # channel A: period #100, tone on, volume 15
code += bytes([0x18, 0xfe])                                   # jr $
os.makedirs(os.path.join(os.path.dirname(__file__), 'out'), exist_ok=True)
make_system_disc(os.path.join(os.path.dirname(__file__), 'out', 'test.dsk'), code)
