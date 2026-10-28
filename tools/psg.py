"""The sound of a game, from the emulator's psg.bin (16 bytes per 50 Hz frame: AY registers R0-R13, then 1
when R13 was written that frame = the envelope restarted).

    python3 -I tools/psg.py segments psg.bin [--gap 25]      frame ranges where something plays, cut at silences
    python3 -I tools/psg.py show     psg.bin FROM TO         the registers frame by frame (periods, volumes, mixer)
    python3 -I tools/psg.py loop     psg.bin FROM TO         estimated length of a music loop in that range (numpy)
    python3 -I tools/psg.py render   psg.bin FROM TO out.wav [--channels 7] [--ogg]
                                                             renders it with emulator/ay (1 MHz AY), optionally .ogg (ffmpeg)

Typical use (docs/method.md, "Sound"): poke the game's "play music N" variable at a frame (-F), run the emulator
long enough, `segments` to see where it plays, `loop` to find where it repeats, `render` the intro + one loop.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
AY = os.path.join(HERE, '..', 'emulator', 'ay')


def load(path):
    d = open(path, 'rb').read()
    return [d[i:i + 16] for i in range(0, len(d) - 15, 16)]


def audible(r):
    """Something can be heard on at least one channel: volume (or envelope) with tone or noise on."""
    for c in range(3):
        vol = r[8 + c]
        tone_on = not (r[7] >> c) & 1
        noise_on = not (r[7] >> (c + 3)) & 1
        if (vol & 0x1f) and (tone_on or noise_on):
            return True
    return False


def segments(frames, gap=25):
    out, start, quiet = [], None, 0
    for i, r in enumerate(frames):
        if audible(r):
            if start is None:
                start = i
            quiet = 0
        elif start is not None:
            quiet += 1
            if quiet >= gap:
                out.append((start, i - quiet + 1))
                start, quiet = None, 0
    if start is not None:
        out.append((start, len(frames)))
    return out


def find_loop(frames, a, b, min_period=200):
    """(period, score): the most likely length of a music loop in frames a-b, by autocorrelation of what can be
    heard (volume and pitch of each channel, smoothed). Music drivers rarely tick in step with the 50 Hz frames
    and add vibrato, so registers never repeat exactly: this is an estimate (score 0-1, > 0.5 is a good sign).
    The exact loop point comes from the code: watch the writes to the driver's song pointer (docs/method.md)."""
    import numpy as np
    r = np.frombuffer(b''.join(bytes(x) for x in frames[a:b]), dtype=np.uint8).reshape(-1, 16).astype(float)
    period = np.stack([r[:, 2 * c] + 256 * (r[:, 2 * c + 1] % 16) for c in range(3)], 1)
    volume = r[:, 8:11] % 16
    feature = np.concatenate([volume, np.log2(np.maximum(period, 1)) * (volume > 0)], 1)
    kernel = np.ones(5) / 5
    feature = np.stack([np.convolve(feature[:, c], kernel, 'same') for c in range(feature.shape[1])], 1)
    feature -= feature.mean(0)
    best = None
    for p in range(min_period, len(feature) // 2):
        x, y = feature[p:], feature[:-p]
        score = float((x * y).sum() / max(1e-9, np.sqrt((x * x).sum() * (y * y).sum())))
        if best is None or score > best[1]:
            best = (p, score)
    return best


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    cmd, path = argv[1], argv[2]
    args, opts, i = [], {}, 3
    while i < len(argv):
        if argv[i] == '--ogg':
            opts['ogg'] = True
        elif argv[i].startswith('--'):
            opts[argv[i][2:]] = argv[i + 1]
            i += 1
        else:
            args.append(argv[i])
        i += 1
    frames = load(path)
    if cmd == 'segments':
        for s, e in segments(frames, int(opts.get('gap') or 25)):
            print(f'{s:6d}-{e:6d}  {e - s:5d} frames  {(e - s) / 50:6.2f} s')
    elif cmd == 'show':
        a, b = int(args[0]), int(args[1])
        print(' frame   A per vol   B per vol   C per vol  noise mixer  env per shape restart')
        for i in range(a, min(b, len(frames))):
            r = frames[i]
            per = [r[c * 2] | (r[c * 2 + 1] & 15) << 8 for c in range(3)]
            print(f'{i:6d}  ' + '  '.join(f'{per[c]:5d} {r[8 + c]:3d}' for c in range(3))
                  + f'  {r[6] & 31:5d}  {r[7]:06b}  {r[11] | r[12] << 8:6d} {r[13] & 15:5d} {"*" if r[14] else ""}')
    elif cmd == 'loop':
        found = find_loop(frames, int(args[0]), int(args[1]))
        if found:
            p, score = found
            print(f'likely loop length: {p} frames ({p / 50:.2f} s), score {score:.2f}.'
                  ' Render FROM to FROM+period and listen; the exact point: watch the song pointer (docs/method.md)')
        else:
            print('range too short: it must hold at least two loops')
    elif cmd == 'render':
        a, b, out = args[0], args[1], args[2]
        if not os.path.exists(AY):
            print('build the tools first: make')
            return 1
        wav = out if out.endswith('.wav') else out.rsplit('.', 1)[0] + '.wav'
        subprocess.run([AY, path, wav, a, b, opts.get('channels') or '7'], check=True)
        if opts.get('ogg') or out.endswith('.ogg'):
            ogg = out if out.endswith('.ogg') else wav[:-4] + '.ogg'
            subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-i', wav, '-c:a', 'libvorbis', '-q:a', '4', ogg], check=True)
            print(ogg)
        else:
            print(wav)
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
