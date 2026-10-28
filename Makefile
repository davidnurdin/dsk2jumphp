# The native tools: the CPC emulator and the AY-3-8912 renderer. `make`, then `make test`.
CFLAGS = -O2 -Wall -Wextra -Wno-unused-parameter

all: emulator/cpc emulator/ay

emulator/cpc: emulator/cpc.c emulator/z80.c emulator/z80.h emulator/disk.inc emulator/fdc.inc
	$(CC) $(CFLAGS) -o $@ emulator/cpc.c emulator/z80.c

emulator/ay: emulator/ay.c
	$(CC) $(CFLAGS) -o $@ emulator/ay.c -lm

test: all
	sh tests/run.sh

clean:
	rm -f emulator/cpc emulator/ay
