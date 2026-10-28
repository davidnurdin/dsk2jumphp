# dsk2jumphp

Les outils pour **désosser un jeu Amstrad CPC sur disquette** (`.dsk`) et le refaire en PHP avec
[JumPHP](https://github.com/davidnurdin/jumPhp) : un petit émulateur CPC sans écran qui fait tourner le jeu et
note tout ce qu'il voit, et des outils pour en sortir les graphismes, les cartes, les textes et la musique.

Ils ont servi à refaire *Rainbow Islands* (CPC, 1989) : https://github.com/davidnurdin/rainbow-island —
l'extraction complète est dans `examples/rainbow-islands/`.

## Installation

```bash
git clone https://github.com/davidnurdin/dsk2jumphp.git
cd dsk2jumphp
make          # compile l'émulateur (emulator/cpc) et le rendu sonore (emulator/ay) : il faut un compilateur C
make test     # vérifie tout (Python 3 avec Pillow et numpy ; ffmpeg pour les .ogg)
```

## En bref

```bash
python3 -I tools/dsk.py info jeu.dsk                     # ce qu'il y a sur la disquette
mkdir run && emulator/cpc jeu.dsk run 1500 -S 50         # 30 s de jeu, une capture d'écran par seconde
emulator/cpc jeu.dsk run 1500 -rom cpc6128.rom -urom 7:amsdos.rom -type '100:RUN"DISC\n' -S 50   # avec les ROM
emulator/cpc jeu.dsk run 1500 -d 500,1400                # deux images de la mémoire...
python3 -I tools/cpc.py diff run/dump-00500.bin run/dump-01400.bin --from 3 --to 2   # ... où sont les vies ?
python3 -I tools/cpc.py gfx run/dump-01400.bin ed80 6 24 4 sprites.png               # des sprites en PNG
python3 -I tools/psg.py render run/psg.bin 500 1500 musique.ogg                       # la musique
```

| Dossier | Contenu |
|---|---|
| `emulator/` | l'émulateur CPC 6128 (Z80, gate array, CRTC, AY, contrôleur de disquette, 128 Ko) et `ay.c` |
| `tools/` | `dsk.py` (disquettes), `cpc.py` (images mémoire : écran, sprites, textes, différences), `z80dis.py`, `psg.py` (son) |
| `docs/` | `method.md` la méthode pas à pas, `emulator.md` toutes les options, `cpc.md` le CPC en bref |
| `examples/rainbow-islands/` | un jeu entier : adresses trouvées, scripts d'extraction, commandes |
| `.claude/skills/dsk2jumphp/` | la compétence (skill) pour Claude Code |
| `AGENTS.md` | les consignes pour les agents IA |

## Avec un agent IA (Claude Code...)

Le dépôt contient un `AGENTS.md` et un skill : un agent sait s'en servir sans réécrire d'émulateur. Pour qu'il
soit disponible dans un projet JumPHP :

```bash
git clone https://github.com/davidnurdin/dsk2jumphp.git ~/dsk2jumphp && make -C ~/dsk2jumphp
mkdir -p .claude/skills && ln -s ~/dsk2jumphp/.claude/skills/dsk2jumphp .claude/skills/dsk2jumphp
```

Puis : « refais le jeu de /chemin/jeu.dsk avec dsk2jumphp ».

## Ce qui n'est pas fourni

Ni disquettes, ni ROM du CPC (sous droits). Les ROM ne servent qu'aux jeux lancés depuis le BASIC : prends
celles d'un émulateur installé sur ta machine (`cpc6128.rom`, `amsdos.rom`). Ne publie que les scripts et les
notes ; les éléments extraits d'un jeu appartiennent à ses auteurs.

Licence MIT (les outils).
