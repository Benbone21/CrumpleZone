#!/usr/bin/env python3
# Cuts the main menu's recorded ambience into seamless mono 48 kHz 16-bit loops (the tail is crossfaded into the head, equal power).
# Usage: tools/ambience_loops.py FIRE.wav WIND.wav   (48 kHz WAVs; see assets/audio/ambience/SOURCES.txt)
import sys, wave
import numpy as np

def load(p):
    w = wave.open(p)
    ch = w.getnchannels()
    d = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, ch).astype(np.float32) / 32768
    assert w.getframerate() == 48000
    return d.mean(1)

def loop(x, start, length, fade, peak=0.85):
    r = 48000
    y = x[int(start * r): int((start + length) * r) + int(fade * r)]
    n, c = int(length * r), int(fade * r)
    out = y[:n].copy()
    t = np.linspace(0, np.pi / 2, c, dtype=np.float32)
    out[:c] = y[:c] * np.sin(t) + y[n:n + c] * np.cos(t)  # (the head fades in from the tail's own continuation)
    out -= out.mean()
    out *= peak / max(1e-6, np.abs(out).max())
    return out

def save(p, x):
    w = wave.open(p, 'wb')
    w.setnchannels(1), w.setsampwidth(2), w.setframerate(48000)
    w.writeframes((np.clip(x, -1, 1) * 32767).astype(np.int16).tobytes())
    w.close()

fire, wind = load(sys.argv[1]), load(sys.argv[2])
save('assets/audio/ambience/campfire.wav', loop(fire, 55, 40, 2.0))
save('assets/audio/ambience/wind.wav', loop(wind, 0, 26, 3.0))
