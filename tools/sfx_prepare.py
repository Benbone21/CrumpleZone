#!/usr/bin/env python3
# Cuts the recorded sound effects (Pixabay; see assets/audio/sfx/SOURCES.txt) into mono 48 kHz 16-bit files: tight one-shots and
# seamless crossfaded loops.  Usage (from the repo root): python3 tools/sfx_prepare.py /path/to/wav/   (the folder of 48 kHz WAVs)
import sys, wave, os
import numpy as np

SRC = sys.argv[1].rstrip('/') + '/'
R = 48000

def load(n):
    w = wave.open(SRC + n + '.wav')
    assert w.getframerate() == R
    d = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, w.getnchannels()).astype(np.float32) / 32768
    return d.mean(1)

def save(p, x):
    os.makedirs(os.path.dirname(p), exist_ok=True)
    w = wave.open(p, 'wb')
    w.setnchannels(1), w.setsampwidth(2), w.setframerate(R)
    w.writeframes((np.clip(x, -1, 1) * 32767).astype(np.int16).tobytes())
    w.close()
    print(p, round(len(x) / R, 2), 's', os.path.getsize(p) // 1024, 'KB')

def norm(x, peak):
    x = x - x.mean()
    return x * (peak / max(1e-6, np.abs(x).max()))

def loop(x, start, length, fade, peak=0.85):  # the tail is crossfaded (equal power) into the head
    y = x[int(start * R): int((start + length + fade) * R)]
    n, c = int(length * R), int(fade * R)
    out = y[:n].copy()
    t = np.linspace(0, np.pi / 2, c, dtype=np.float32)
    out[:c] = y[:c] * np.sin(t) + y[n:n + c] * np.cos(t)
    return norm(out, peak)

def shot(x, peak=0.9, thr=0.01, tail=0.02, start=0.0, end=None):  # trimmed where it falls under thr (of its peak), short fades
    x = x[int(start * R): None if end is None else int(end * R)]
    e = np.abs(x)
    idx = np.nonzero(e > thr * e.max())[0]
    a, b = max(0, idx[0] - int(0.003 * R)), min(len(x), idx[-1] + int(tail * R))
    y = x[a:b].copy()
    f = min(len(y) // 4, int(0.004 * R))
    y[:f] *= np.linspace(0, 1, f)
    y[-int(0.01 * R):] *= np.linspace(1, 0, int(0.01 * R))
    return norm(y, peak)

S = 'assets/audio/sfx/'
SHOP_ONLY = len(sys.argv) > 2 and sys.argv[2] == 'shop'
if SHOP_ONLY:
    save('assets/audio/ambience/shop.wav', loop(load('freesound_community-auto-shop-soundscape-68816'), 2.0, 15.4, 0.6, 0.8))
    sys.exit(0)
# winch: the motor's hum (a loop) and a bank of pawl clicks (8 of the strongest, 70 ms each, back to back)
save(S + 'winch_motor.wav', loop(load('freesound_community-motor3-58848'), 20, 6.0, 0.5, 0.8))
rat = load('freesound_community-toy-ratchet-71580')
env = np.convolve(np.abs(rat), np.ones(48) / 48, 'same')  # (16 s: fine)
clicks, i = [], 0
while i < len(env):
    if env[i] > 0.35 * env.max():
        clicks.append((env[i:i + 480].max(), i)); i += int(0.05 * R)
    else: i += 1
clicks = sorted(clicks, reverse=True)[:8]
bank = []
for _, at in sorted(clicks, key=lambda c: c[1]):
    c = rat[max(0, at - 150): max(0, at - 150) + 3360].copy()
    c *= np.linspace(1, 0, len(c)) ** 0.5
    bank.append(c / max(1e-6, np.abs(c).max()) * 0.9)
save(S + 'winch_click.wav', np.concatenate(bank))
save(S + 'door_open.wav', shot(load('dragon-studio-open-car-door-372469')))
save(S + 'door_shut.wav', shot(load('musicnation782-car-door-shut-297266')))
save(S + 'clunk.wav', shot(load('freesound_community-mechanical1-107614')))
save(S + 'toolbox_shut.wav', shot(load('tool box-clicking-shut-94753')))
tb = load('freesound_community-toolbox-search-57850')
w = int(2.6 * R); cs = np.concatenate([[0], np.cumsum(tb.astype(np.float64) ** 2)]); r2 = np.sqrt((cs[w:] - cs[:-w]) / w)
a = int(np.argmax(r2[: R * 100]))
save(S + 'toolbox_rummage.wav', shot(tb[a: a + w], 0.9, 0.03, 0.05))
save(S + 'compressor.wav', loop(load('u_1eebrv7h0x-compressor-machine-sound-337021'), 2.0, 5.0, 0.5, 0.8))
save(S + 'air_blow.wav', loop(load('freesound_community-air-blowing-4-38952'), 0.1, 2.6, 0.5, 0.85))
save(S + 'air_hiss.wav', shot(load('freesound_community-air-hiss-102406'), 0.9, 0.03, 0.05))
wr = load('freesound_community-ratchet-socket-wrench-tool-95253')  # three runs of ratchet ticks; cut six 160 ms slots out of them
slots = []
for at in (0.11, 0.25, 0.55, 0.68, 0.95, 1.08):
    c = wr[int((at - 0.005) * R): int((at - 0.005) * R) + 7680].copy()
    c *= np.minimum(1, np.linspace(1, 0, len(c)) * 6)
    slots.append(c / max(1e-6, np.abs(c).max()) * 0.9)
save(S + 'wrench_ratchet.wav', np.concatenate(slots))
save(S + 'ui_click.wav', shot(load('universfield-computer-mouse-click-02-383961'), 0.7, 0.03, 0.01))
# the menu's stream and, held back for later: the horn, a river and a forest bed
save('assets/audio/ambience/stream.wav', loop(load('soundreality-stream-nature-445380'), 30, 24, 2.0, 0.8))
save(S + 'horn.wav', shot(load('universfield-double-car-honk-352443')))
save('assets/audio/ambience/river.wav', loop(load('hasin2004-nature-sound-near-river-or-waterfall-247409'), 2, 20, 2.0, 0.8))
save('assets/audio/ambience/forest.wav', loop(load('soundreality-nature-forest-sound-537925'), 40, 24, 2.0, 0.8))

# the repair shop's ambience (a loop under every repair station / shop): 2 s to 17.4 s of the soundscape (its fade-out tail is left off), 0.6 s crossfade.
# (Run as: python3 tools/sfx_prepare.py <wav folder> shop   to make only this one.)
save('assets/audio/ambience/shop.wav', loop(load('freesound_community-auto-shop-soundscape-68816'), 2.0, 15.4, 0.6, 0.8))
