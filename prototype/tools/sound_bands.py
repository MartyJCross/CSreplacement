"""Measures WAVs written by `crisp --dump-sounds DIR` / `--dump-played-sounds DIR` / `--dump-spatial DIR`.

    python prototype/tools/sound_bands.py "DIR/suppressed_*.wav" "DIR/rifle_shot_1.wav"
    python prototype/tools/sound_bands.py --spatial DIR

For each sound: peak, RMS, spectral centroid, how long it stays above 10% of its peak, the share of its energy in
five bands (<150 Hz "sub", 150-500 "thump", 500-1.5k "body/pop", 1.5-4k "presence", >4k "air/crack") and
"ringing" (the top 1% of 1.5-8 kHz bins' share of that band: real gun recordings ~11%; pitched, glassy layers more).
--spatial: per direction, each ear's level, the left/right delay and each ear's share above 2 kHz.
This is how sound changes get checked before handing them over (the owner judges by ear; we measure).
Needs numpy.
"""
import glob
import os
import sys
import wave

import numpy as np


def load(path):
    w = wave.open(path)
    x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float32) / 32768
    return x.reshape(-1, w.getnchannels()), w.getframerate()


def bands(x, sr):
    X = np.abs(np.fft.rfft(x)) ** 2
    fr = np.fft.rfftfreq(len(x), 1 / sr)
    t = X.sum() or 1.0
    edges = [(0, 150), (150, 500), (500, 1500), (1500, 4000), (4000, sr)]
    share = [X[(fr >= a) & (fr < b)].sum() / t * 100 for a, b in edges]
    band = X[(fr > 1500) & (fr < 8000)]
    top = np.sort(band)[::-1][:max(1, len(band) // 100)]
    ring = top.sum() / (band.sum() or 1.0) * 100
    return share, (X * fr).sum() / t, ring


def report(pattern):
    for path in sorted(glob.glob(pattern)):
        x, sr = load(path)
        m = x[:, 0]
        share, centroid, ring = bands(m, sr)
        env = np.abs(m)
        last = np.where(env > env.max() * 0.1)[0][-1] / sr if env.max() > 0 else 0
        print(f"{os.path.basename(path):30s} peak {env.max():.2f} rms {np.sqrt((m ** 2).mean()):.3f} centroid {centroid:5.0f} Hz "
              f"len {last * 1000:4.0f} ms  sub/thump/body/presence/air "
              + " ".join(f"{v:4.1f}" for v in share) + f"  ringing {ring:4.1f}%")


def spatial(folder):
    for name in ['ahead', 'left', 'right', 'behind', 'above', 'below', 'wall']:
        x, sr = load(os.path.join(folder, f'spatial_{name}.wav'))
        L, R = x[:, 0], x[:, 1]
        def hi(s):
            S = np.abs(np.fft.rfft(s)) ** 2
            fr = np.fft.rfftfreq(len(s), 1 / sr)
            return S[fr > 2000].sum() / (S.sum() or 1.0) * 100
        c = np.correlate(L, R, 'full')
        lag = np.argmax(c) - (len(R) - 1)
        rms = lambda s: np.sqrt((s ** 2).mean())
        print(f"{name:7s} L {rms(L):.4f} R {rms(R):.4f}  L-R lag {lag:+3d} samples  highs>2k L {hi(L):4.1f}% R {hi(R):4.1f}%")


if __name__ == '__main__':
    if len(sys.argv) > 2 and sys.argv[1] == '--spatial':
        spatial(sys.argv[2])
    else:
        for p in sys.argv[1:]:
            report(p)
