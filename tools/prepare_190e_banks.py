#!/usr/bin/env python3
"""Cut the Mercedes 190E dyno recordings (Chippy569, Freesound pack 9658, CC0) into engine banks.

Usage: python3 tools/prepare_190e_banks.py [--source assets/source/190e] [--analyze]

Writes loop-ready WAVs and one import plan per bank under <source>/prepared/<engine-key>/,
then `python3 tools/import_engine_audio.py <source>/prepared/<key>/plan.json` makes the bank.
docs/engine-recordings.md explains the choices (mics, load states, rpm measurement).

Each loop is cut from a measured window of one take:
- The rpm is measured from the recording, not the file name: an inline-4 fires twice a turn, so
  its strongest order is the 2nd (rpm / 30 Hz). The tracker follows that order through the window
  (harmonic sum of 2nd, 4th, 6th and 8th order, 10 ms hops).
- The window is "de-chirped": resampled along the measured firing phase so the loop holds one
  steady rpm and spans a whole number of engine cycles (720 degrees), so the seam is in phase.
  This is what makes short slices of a rev sweep loopable, and it flattens the idle's hunting.
- A bank may shift formants (`shift`): the take is read slightly faster or slower and the loop is
  labelled with the rpm it now fires at, so the game still plays it at the right firing frequency
  but the exhaust "body" sounds smaller or bigger.
- Every loop is written at 48 kHz, 16-bit mono, at the same RMS; the bank's per-loop gain sets the
  level (loaded louder than coasting, louder with rpm).
Needs numpy. Never touches the originals.
"""
import argparse
import json
import math
from pathlib import Path
import wave

import numpy as np

PACK = "https://freesound.org/people/Chippy569/packs/9658/"
AUTHOR = "Chippy569 (recorded by David of Track Time Audio): The Dyno Sessions: 190E"
OUT_RATE = 48000
LOOP_RMS = 0.125  # every loop is stored at -18 dBFS RMS; the bank gain sets its level

# Freesound ids of the takes (the URL of each is https://freesound.org/s/<id>/).
TAKES = {
    "19_500": {"4062": 162507, "d6": 162506, "distant": 162505},     # loaded hold, measured 1290 rpm
    "19_2500": {"4062": 162510, "d6": 162509, "distant": 162508},    # loaded hold, 2556 rpm
    "20_6500": {"4062": 162513, "d6": 162512, "distant": 162511},    # loaded hold, 6210 rpm
    "21_500": {"4062": 162516, "d6": 162515, "distant": 162514},     # unloaded: idle, 1030 rpm
    "21_2500": {"4062": 162519, "d6": 162518, "distant": 162517},    # unloaded hold, 2430 rpm
    "17_shortsweep": {"d6": 162501, "distant": 162500},              # free rev up 1700 -> 6400
    "17_medsweep": {"d6": 162504, "distant": 162503},                # lift from 6900, coast to idle
    "18_medsweep": {"4062": 162502},                                 # lift from 6800, coast to idle
    "acceleration-sweep": {"4062": 162499},                          # loaded pull 1200 -> 7100
}

# The windows: take, start and end (s), the 2nd-order frequency at the start (a seed for the
# tracker, from a coarse look at the take), load state, and the level (output RMS) it plays at.
# Holds use the steadiest part of the take; sweep slices are where the rpm passes the wanted value.
def windows(mic):
    accel = mic == "4062"
    return [
        # coast (load 0): the idle, the unloaded 2500 hold, then the lift-off coast-down at two rpms
        ("21_500", 0.3, 3.3, 34.8, 0, 0.052),
        ("21_2500", 0.3, 3.3, 81.0, 0, 0.068),
        ("18_medsweep", 1.25, 1.85, 152.0, 0, 0.080) if accel else ("17_medsweep", 2.20, 2.90, 162.0, 0, 0.080),
        ("18_medsweep", 0.30, 0.85, 210.0, 0, 0.095) if accel else ("17_medsweep", 0.90, 1.50, 202.0, 0, 0.095),
        # throttle (load 1): the three loaded holds, and a slice of a rev-up between 2500 and 6200
        ("19_500", 0.1, 1.8, 43.2, 1, 0.125),
        ("19_2500", 0.5, 3.8, 85.2, 1, 0.160),
        ("acceleration-sweep", 2.50, 2.85, 146.0, 1, 0.190) if accel else ("17_shortsweep", 0.85, 1.30, 122.0, 1, 0.190),
        ("20_6500", 0.05, 1.55, 208.0, 1, 0.225),
    ]

# The three Honda inline-4s, all made from this one engine. Mic mix (weights; "d6_low" is the
# kick-drum mic below 250 Hz, time-aligned to the distant mic), formant shift, and a gentle tilt.
BANKS = {
    "honda_d16a6": {"mics": {"distant": 1.0}, "shift": 1.00, "lowpass": 9000,
                    "note": "stock SOHC: the distant mic, a little softened"},
    "honda_b16a": {"mics": {"4062": 1.0}, "shift": 1.05, "lowpass": None,
                   "note": "VTEC 1.6: the close 4062 mic (raspy, bright), formants 5% up"},
    "honda_b18c": {"mics": {"distant": 1.0, "d6_low": 0.6}, "shift": 0.97, "lowpass": None,
                   "note": "Type R 1.8: the distant mic with the d6 kick mic's low end, formants 3% down"},
}


def read_wav(path):
    with wave.open(str(path), "rb") as w:
        ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = np.frombuffer(w.readframes(n), dtype=np.uint8)
    if width == 3:
        b = raw.reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        v = np.where(v & 0x800000, v - 0x1000000, v) / 8388608.0
    elif width == 2:
        v = np.frombuffer(raw.tobytes(), dtype="<i2") / 32768.0
    else:
        raise ValueError(f"{path}: unsupported sample width")
    return v.reshape(-1, ch).mean(axis=1), rate


def take_path(source, take, mic):
    fid = TAKES[take][mic]
    name = f"{fid}__chippy569__{take}_{mic}.wav" if not take.startswith("acceleration") else f"{fid}__chippy569__acceleration-sweep-{mic}.wav"
    return source / name


def fft_filter(x, rate, lo=None, hi=None):
    """Zero-phase band limit with a smooth (raised-cosine) edge an octave wide."""
    n = len(x)
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(n, 1 / rate)
    g = np.ones_like(f)
    if hi:
        t = np.clip((np.log2(np.maximum(f, 1e-9)) - np.log2(hi)) / 1.0 + 0.5, 0, 1)
        g *= 0.5 + 0.5 * np.cos(np.pi * t)
    if lo:
        t = np.clip((np.log2(np.maximum(f, 1e-9)) - np.log2(lo)) / 1.0 + 0.5, 0, 1)
        g *= 0.5 - 0.5 * np.cos(np.pi * t)
    return np.fft.irfft(X * g, n)


def mix_take(source, take, mics):
    """The bank's mic mix of one take (the distant mic is the time reference)."""
    out, rate = None, None
    reference = None
    for mic, weight in mics.items():
        base = "d6" if mic == "d6_low" else mic
        if base not in TAKES[take]:
            continue
        x, rate = read_wav(take_path(source, take, base))
        x = x - x.mean()
        x = x / (np.sqrt((x ** 2).mean()) + 1e-12)
        if mic == "d6_low":
            x = fft_filter(x, rate, hi=250)
        if reference is None:
            reference = fft_filter(x, rate, hi=400)
        else:  # line the mic up with the first one (the mics stand at different distances)
            n = min(len(x), len(reference))  # (a take's mics can differ by a few samples)
            x, reference, out = x[:n], reference[:n], out[:n]
            lo = fft_filter(x, rate, hi=400)
            size = 1 << int(math.ceil(math.log2(2 * n)))
            c = np.fft.irfft(np.fft.rfft(reference, size) * np.conj(np.fft.rfft(lo, size)), size)
            lag_max = int(0.015 * rate)
            lags = np.concatenate([np.arange(0, lag_max + 1), np.arange(-lag_max, 0)])
            lag = int(lags[np.argmax(np.concatenate([c[:lag_max + 1], c[-lag_max:]]))])
            x = np.roll(x, lag)  # (the lag where the mic best matches the reference)
        out = x * weight if out is None else out + x * weight
    if out is None:  # a sweep recorded on fewer mics: use whichever the bank names first
        for mic in ("distant", "4062", "d6"):
            if mic in TAKES[take]:
                x, rate = read_wav(take_path(source, take, mic))
                out = x - x.mean()
                break
    return out, rate


def harmonic_score(seg, rate, cands, nfft):
    P = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), nfft)) ** 2
    df = rate / nfft
    score = np.zeros_like(cands)
    for k in (1, 2, 3, 4):  # the 2nd, 4th, 6th and 8th engine orders
        idx = np.clip(np.round(cands * k / df).astype(int), 0, len(P) - 1)
        score += np.log(np.maximum(np.maximum(P[idx], P[np.maximum(idx - 1, 0)]), P[np.minimum(idx + 1, len(P) - 1)]) + 1e-30)
    return score


def track(x, rate, t0, t1, f_seed):
    """2nd-order frequency through [t0, t1] (10 ms hops), each hop searched within 4% of the last."""
    hop, win = 0.01, 0.12
    nfft = 1 << 19
    times, freqs, f = [], [], f_seed
    t = t0
    while t <= t1 + 1e-9:
        a = int(max(0, (t - win / 2)) * rate)
        seg = x[a:a + int(win * rate)]
        cands = np.linspace(f * 0.96, f * 1.04, 161)
        f = float(cands[np.argmax(harmonic_score(seg, rate, cands, nfft))])
        times.append(t)
        freqs.append(f)
        t += hop
    times, freqs = np.array(times), np.array(freqs)
    coeffs = np.polyfit(times - t0, freqs, 3)  # the rpm moves smoothly; the fit removes jitter
    return times, np.polyval(coeffs, times - t0), freqs


def render(y, rate, phase, grid, start_phase, cycles, out_f2, extra):
    """Read y along the firing phase: `cycles` engine cycles from start_phase, at out_f2 Hz."""
    length = int(round(4 * cycles / out_f2 * OUT_RATE))
    step = 4 * cycles / length  # exact: a whole number of cycles in the loop
    target = start_phase + np.arange(length + extra) * step
    s = np.interp(target, phase, grid) * rate  # source sample of each output sample
    i = np.floor(s).astype(int)
    u = s - i
    p = [y[np.clip(i + k, 0, len(y) - 1)] for k in (-1, 0, 1, 2)]  # cubic (Catmull-Rom)
    out = p[1] + 0.5 * u * (p[2] - p[0] + u * (2 * p[0] - 5 * p[1] + 4 * p[2] - p[3] + u * (3 * (p[1] - p[2]) + p[3] - p[0])))
    return out[:length], out[length:]


def dechirp(x, rate, t0, t1, f_seed, shift, guide=None):
    """Resample [t0, t1] along its firing phase: a steady loop of whole engine cycles.

    Tries starts across the first engine cycle and up to two cycles fewer, and keeps the window
    whose end runs on most smoothly into its own start (the wrap the loader crossfades over 5 ms).
    `guide` is the signal to measure the rpm on, when it is clearer than x (another mic of the take).
    """
    tail = 0.06  # track a little past the window: the seam test reads what follows the loop
    times, smooth, raw = track(x if guide is None else guide, rate, t0, min(t1 + tail, len(x) / rate - 0.07), f_seed)
    grid = np.arange(int(t0 * rate), int(min(t1 + tail, len(x) / rate - 0.07) * rate)) / rate
    f2 = np.interp(grid, times, smooth)
    phase = np.concatenate([[0], np.cumsum(f2[:-1]) / rate])  # in 2nd-order cycles
    window_end = np.interp(t1, grid, phase)
    y = fft_filter(x, rate, hi=20000)  # (reads at about 2x, at most 2.1x: band-limit for 48 kHz)
    k = int(0.005 * OUT_RATE)
    best = None
    for start in np.arange(0, 4, 0.25):  # (in 2nd-order cycles: a quarter turn at a time)
        most = int((window_end - start) // 4)
        for cycles in range(max(2, most - 2), most + 1):
            if cycles < 2:
                continue
            t_end = np.interp(start + 4 * cycles, phase, grid)
            mean_f2 = 4 * cycles / (t_end - np.interp(start, phase, grid))
            out_f2 = mean_f2 * shift
            length = int(round(4 * cycles / out_f2 * OUT_RATE))
            out_f2 = 4 * cycles / length * OUT_RATE
            loop, after = render(y, rate, phase, grid, start, cycles, out_f2, k)
            err = np.sqrt(((after - loop[:k]) ** 2).mean()) / (np.sqrt((loop ** 2).mean()) + 1e-12)
            if best is None or err < best[0]:
                best = (err, loop, out_f2, cycles)
    if best is None:
        raise ValueError(f"{t0}-{t1} s holds fewer than two engine cycles")
    err, loop, out_f2, cycles = best
    inside = (times >= t0) & (times <= t1)
    rpm_raw = raw[inside] * 30
    return loop, out_f2 * 30, cycles, float(rpm_raw.min()), float(rpm_raw.max()), float(err)


def flatten(loop):
    """Divide out the loop's slow level change (RMS over 80 ms, read circularly)."""
    k = int(0.08 * OUT_RATE)
    power = np.convolve(np.concatenate([loop[-k:], loop, loop[:k]]) ** 2, np.ones(k) / k, "same")[k:-k]
    envelope = np.sqrt(power)
    return loop * (envelope.mean() / envelope)


def write_wav(path, x, rng):
    dither = (rng.random(len(x)) - rng.random(len(x))) / 32768.0  # TPDF
    pcm = np.clip(np.round((x + dither) * 32767), -32768, 32767).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(OUT_RATE)
        w.writeframes(pcm.tobytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", type=Path, default=Path("assets/source/190e"))
    parser.add_argument("--analyze", action="store_true", help="print the measurements, write nothing")
    args = parser.parse_args()
    rng = np.random.default_rng(190)
    for key, bank in BANKS.items():
        mics = bank["mics"]
        first_mic = next(m for m in mics if m != "d6_low")
        out_dir = args.source / "prepared" / key
        if not args.analyze:
            out_dir.mkdir(parents=True, exist_ok=True)
        clips = []
        print(f"{key}: {bank['note']}")
        for take, t0, t1, f_seed, load, level in windows(first_mic):
            x, rate = mix_take(args.source, take, mics)
            guide = None  # (the distant mic shows the firing most clearly: measure on it when the take has one)
            if "distant" in TAKES[take] and "distant" not in mics:
                guide, _ = read_wav(take_path(args.source, take, "distant"))
                guide = guide[:len(x)]
            loop, rpm, cycles, lo, hi, seam = dechirp(x, rate, t0, t1, f_seed, bank["shift"], guide)
            if "sweep" in take:  # a slice of a sweep swells or fades: hold its level steady
                loop = flatten(loop)
            if bank["lowpass"]:
                loop = fft_filter(loop, OUT_RATE, hi=bank["lowpass"])
            loop = loop - loop.mean()
            loop *= LOOP_RMS / np.sqrt((loop ** 2).mean())
            peak = float(np.abs(loop).max())
            if peak > 0.98:
                raise ValueError(f"{take}: crest factor too high for the stored level")
            rpm = round(rpm)
            gain = round(level / LOOP_RMS, 3)
            name = f"{'load' if load else 'coast'}-{rpm}.wav"
            mic_used = "+".join(m for m in mics if (m.replace('_low', '') in TAKES[take])) or first_mic
            print(f"  {'load ' if load else 'coast'} {take:19s} {t0:4.2f}-{t1:4.2f}s measured {lo:5.0f}-{hi:5.0f} rpm"
                  f" -> loop {rpm:5d} rpm, {cycles:3d} cycles, {len(loop) / OUT_RATE:4.2f} s, peak {peak:.2f}, seam {seam:.2f}, gain {gain}")
            if args.analyze:
                continue
            write_wav(out_dir / name, loop, rng)
            fid = TAKES[take].get(first_mic) or next(iter(TAKES[take].values()))
            clips.append({
                "file": name, "start": 0, "duration": len(loop) / OUT_RATE, "rpm": rpm, "load": load, "gain": gain,
                "from": f"{take} ({mic_used}), https://freesound.org/s/{fid}/, {t0:.2f}-{t1:.2f} s",
                "measured_rpm": [round(lo), round(hi)], "formant_shift": bank["shift"],
            })
        if args.analyze:
            continue
        plan = {
            "engine": key, "source": PACK, "author": AUTHOR, "license": "CC0-1.0",
            "note": f"Mercedes 190E inline-4 on a dyno standing in for the {key}: {bank['note']}. Made by tools/prepare_190e_banks.py.",
            "clips": clips,
        }
        (out_dir / "plan.json").write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
