"""Builds prototype/assets/ from the downloaded CC0 packs (see asset-downloads/SOURCES.md).

    python prototype/tools/prep_assets.py [path/to/asset-downloads]

Sounds: the firearm recordings (96 kHz, several shots per file) are cut into single shots with their echo,
resampled to 48 kHz mono 16-bit WAV and normalised; short single sounds (footsteps, impacts, clicks) are
copied as they are (the game converts and levels everything when it loads them). Textures: each source is
resized to 512 px and turned into a "detail" map (divided by its average colour, 128 = 1.0x), so in the game
it only adds surface detail on top of the map's own colours. Only what's used is written, plus CREDITS.md.
Needs numpy and Pillow.
"""
import glob
import os
import shutil
import struct
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
SRC = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, '..', 'asset-downloads'))
OUT = os.path.join(REPO, 'prototype', 'assets')
RATE = 48000


# ---------------------------------------------------------------------------------------------- sounds
def read_wav(path):
    data = open(path, 'rb').read()
    i, fmt, raw = 12, None, b''
    while i + 8 <= len(data):
        cid, size = data[i:i + 4], struct.unpack('<I', data[i + 4:i + 8])[0]
        if cid == b'fmt ':
            fmt = struct.unpack('<HHIIHH', data[i + 8:i + 24])
        if cid == b'data':
            raw = data[i + 8:i + 8 + size]
        i += 8 + size + (size & 1)
    _, ch, rate, _, _, bits = fmt
    if bits == 24:
        b = np.frombuffer(raw[:len(raw) // 3 * 3], dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        v = (np.where(v >= 1 << 23, v - (1 << 24), v) / float(1 << 23)).astype(np.float32)
    elif bits == 16:
        v = np.frombuffer(raw[:len(raw) // 2 * 2], dtype=np.int16).astype(np.float32) / 32768.0
    else:
        raise ValueError(f'{path}: {bits}-bit not handled')
    return v[:len(v) // ch * ch].reshape(-1, ch).mean(axis=1), rate


def resample(x, rate):
    if rate == RATE:
        return x
    n = int(round(len(x) * RATE / rate))
    spec = np.fft.rfft(x)
    out = np.fft.irfft(spec[:n // 2 + 1] if n < len(x) else np.pad(spec, (0, n // 2 + 1 - len(spec))), n)
    return (out * (n / len(x))).astype(np.float32)


def write_wav(path, x):
    pcm = (np.clip(x, -1, 1) * 32767).astype('<i2').tobytes()
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 36 + len(pcm)) + b'WAVEfmt ')
        f.write(struct.pack('<IHHIIHH', 16, 1, 1, RATE, RATE * 2, 2, 16))
        f.write(b'data' + struct.pack('<I', len(pcm)) + pcm)


def onsets(x):
    hop = RATE // 100
    env = np.array([np.abs(x[k:k + hop]).max() for k in range(0, len(x) - hop, hop)])
    out, last = [], -100
    for k in range(1, len(env)):
        if env[k] > 0.35 * env.max() and env[k] > 3 * env[max(0, k - 3)] and k - last > 30:
            # back up to the first sample above 10% of this hop's peak: the true start of the shot
            s = k * hop
            seg = np.abs(x[max(0, s - hop):s + hop])
            first = int(np.argmax(seg > 0.1 * seg.max()))
            out.append(max(0, s - hop + first))
            last = k
    return out


def cut_shots(files, length):
    """Single shots, each `length` s with its echo fading out; none that clip or overlap the next one."""
    shots = []
    for f in files:
        x, r = read_wav(f)
        x = resample(x, r)
        on = onsets(x)
        for i, s in enumerate(on):
            start = max(0, s - int(0.001 * RATE))  # 1 ms lead-in: the bang is instant
            end = start + int(length * RATE)
            if i + 1 < len(on) and on[i + 1] < end:
                continue  # another shot inside this one
            clip = x[start:end].copy()
            if len(clip) < int(0.6 * length * RATE):
                continue
            clipped = int((np.abs(clip) > 0.995).sum())
            fade = np.ones(len(clip), dtype=np.float32)
            n = int(len(clip) * 0.45)
            fade[-n:] = np.linspace(1, 0, n) ** 2
            clip *= fade
            shots.append((clipped, -float(np.abs(clip).max()), os.path.basename(f), clip))
    shots.sort(key=lambda s: (s[0], s[1]))  # least clipping first, then loudest
    return [s[3] / max(1e-6, np.abs(s[3]).max()) * 0.9 for s in shots]


def lowpass(x, hz):
    a = 1 - np.exp(-2 * np.pi * hz / RATE)
    y, out = 0.0, np.empty_like(x)
    for i, v in enumerate(x):
        y += a * (v - y)
        out[i] = y
    return out


def sounds():
    d = os.path.join(OUT, 'sounds')
    os.makedirs(d, exist_ok=True)
    lib = os.path.join(SRC, 'sounds', 'opengameart', 'firearm-library', 'Prepared SFX Library')
    made = {}
    for name, folders, length, count in (('rifle_shot', ['AK-47'], 0.9, 6),
                                         ('pistol_shot', ['1911', 'Walther PPQ'], 0.75, 4),
                                         ('sniper_shot', ['Tikka', 'Mosin Nagant'], 1.4, 3)):
        files = [f for fo in folders for f in sorted(glob.glob(os.path.join(lib, fo, '*.wav')))]
        shots = cut_shots(files, length)[:count]
        for k, clip in enumerate(shots):
            write_wav(os.path.join(d, f'{name}_{k + 1}.wav'), clip)
        made[name] = len(shots)
        if name == 'rifle_shot':  # heard from far away: muffled, the echo louder relative to the crack
            for k, clip in enumerate(shots[:3]):
                far = lowpass(clip, 900.0)
                write_wav(os.path.join(d, f'rifle_shot_far_{k + 1}.wav'), far / np.abs(far).max() * 0.8)
            made['rifle_shot_far'] = min(3, len(shots))

    def copy(name, pattern, count):
        files = sorted(glob.glob(os.path.join(SRC, 'sounds', pattern)))[:count]
        for k, f in enumerate(files):
            shutil.copyfile(f, os.path.join(d, f'{name}_{k + 1}{os.path.splitext(f)[1].lower()}'))
        made[name] = len(files)

    copy('footstep', 'opengameart/fantozzi-footsteps/**/Fantozzi-Sand*.ogg', 6)
    copy('footstep_wood', 'kenney/**/footstep_wood_*.ogg', 4)
    copy('impact_metal', 'kenney/kenney_impact-sounds/**/impactMetal_light_*.ogg', 4)
    copy('impact_wood', 'kenney/kenney_impact-sounds/**/impactWood_light_*.ogg', 4)
    copy('impact_stone', 'kenney/kenney_impact-sounds/**/impactMining_*.ogg', 4)
    copy('ui_click', 'kenney/kenney_interface-sounds/**/click_00[12].ogg', 2)
    return made


# glob ** needs recursive=True: patch the helper above to use it
_glob = glob.glob
glob.glob = lambda p, recursive=True: _glob(p, recursive=recursive)


# ---------------------------------------------------------------------------------------------- textures
TEXTURES = [  # game name, Poly Haven source
    ('sandstone', 'old_sandstone_02'),
    ('plaster', 'clay_plaster'),
    ('sand', 'dense_sand'),
    ('paving', 'red_sandstone_pavement'),
    ('planks', 'brown_planks_03'),
    ('shutter', 'painted_metal_shutter'),
    ('plate', 'metal_plate_02'),
]


def textures():
    d = os.path.join(OUT, 'textures')
    os.makedirs(d, exist_ok=True)
    for name, src in TEXTURES:
        im = Image.open(os.path.join(SRC, 'textures', f'{src}_Diffuse_1k.jpg')).convert('RGB').resize((512, 512), Image.LANCZOS)
        a = np.asarray(im).astype(np.float32)
        detail = a / a.reshape(-1, 3).mean(axis=0) * 128.0  # 128 = the map's own colour, unchanged
        Image.fromarray(np.clip(detail, 0, 255).astype(np.uint8)).save(os.path.join(d, f'{name}.jpg'), quality=92)
    return len(TEXTURES)


CREDITS = """# Credits

All assets here are CC0 (public domain): free for any use, no attribution required. Thank you to:

**Textures** - Poly Haven (polyhaven.com), resized and turned into detail maps:
old_sandstone_02 (sandstone), clay_plaster (plaster), dense_sand (sand), red_sandstone_pavement (paving),
brown_planks_03 (planks), painted_metal_shutter (shutter), metal_plate_02 (plate).

**Sounds**
- *The Free Firearm Sound Library* (opengameart.org/content/the-free-firearm-sound-library): rifle, pistol,
  sniper and distant shots, cut into single shots.
- Fantozzi's footsteps (opengameart.org/content/fantozzis-footsteps-grasssand-stone): footsteps.
- Kenney (kenney.nl): Impact Sounds (impacts, wooden footsteps), Interface Sounds (menu clicks).

Anything not listed (hit sounds, explosions, flashbang, bomb, reloads...) is synthesized in src/audio.cpp.
Rebuild this folder with `python prototype/tools/prep_assets.py` (see asset-downloads/SOURCES.md).
"""

if __name__ == '__main__':
    if not os.path.isdir(SRC):
        sys.exit(f'no downloads at {SRC}')
    made = sounds()
    n = textures()
    open(os.path.join(OUT, 'CREDITS.md'), 'w', encoding='utf-8').write(CREDITS)
    print('sounds:', ', '.join(f'{k} {v}' for k, v in made.items()))
    print('textures:', n)
