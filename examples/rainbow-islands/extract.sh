#!/usr/bin/env bash
# The whole extraction of Rainbow Islands (CPC, 1989): 28 rounds run in the emulator, then the assets written
# into a JumPHP game folder.
#   examples/rainbow-islands/extract.sh /path/to/rainbow.dsk /path/to/jumphp/games/rainbow-island [work dir]
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
DSK="$1"; GAME="$2"; WORK="${3:-$(mktemp -d)}"
make -C "$ROOT" >/dev/null
for round in $(seq 0 27); do
  python3 -I "$HERE/extract_round.py" "$ROOT/emulator/cpc" "$DSK" "$WORK/rounds" "$round"
done
python3 -I "$HERE/export.py" "$DSK" "$WORK/rounds" "$GAME"
