#!/usr/bin/env python3
"""Cut documented CC0 PCM recordings into a bank for the game's startup loader.

Usage: python3 tools/import_engine_audio.py bank-plan.json [--root assets/audio/engines]
The JSON plan contains engine, source, author, license (CC0-1.0), and clips.
Each clip: file, start (seconds), duration (0.1-4 seconds), rpm, load (0/1), gain.
This does not infer RPM, synthesize missing recordings, or establish reuse rights.
"""
import argparse
import json
import math
import os
from pathlib import Path
import tempfile
import wave

KEYS = {
    "honda_d16a6", "honda_b16a", "honda_b18c", "jesko_5l",
    "ford_27_ecoboost", "ford_23_ecoboost", "ford_30_raptor",
    "toyota_5vz", "toyota_5vz_supercharged", "toyota_1gr", "toyota_1gr_supercharged",
    "gm_ls3", "gm_ls7", "gm_ls3_supercharged", "rotax_900_turbo",
    "toyota_2gr", "toyota_2gr_supercharged",
}

def finite(value, name, lo, hi):
    number = float(value)
    if not math.isfinite(number) or not lo <= number <= hi:
        raise ValueError(f"{name} must be between {lo} and {hi}")
    return number

def metadata(plan, name):
    value = plan.get(name)
    if not isinstance(value, str) or not value.strip() or any(c in value for c in "\r\n\0"):
        raise ValueError(f"a nonempty single-line {name} is required")
    return value.strip()

def import_bank(plan_path, root):
    plan_path = Path(plan_path).resolve()
    plan = json.loads(plan_path.read_text(encoding="utf-8"))
    if not isinstance(plan, dict):
        raise ValueError("the plan must be a JSON object")
    key = plan.get("engine")
    if key not in KEYS:
        raise ValueError("engine must be one of the engine keys in docs/engine-recordings.md")
    source, author, license_id = (metadata(plan, name) for name in ("source", "author", "license"))
    if license_id != "CC0-1.0":
        raise ValueError("the repository currently permits CC0 assets only; license must be CC0-1.0")
    clips = plan.get("clips")
    if not isinstance(clips, list) or not 4 <= len(clips) <= 16:
        raise ValueError("provide 4-16 clips, including two distinct RPMs for each load state")
    jobs, seen, decoded_bytes = [], set(), 0
    for clip in clips:
        if not isinstance(clip, dict):
            raise ValueError("each clip must be a JSON object")
        rpm = finite(clip["rpm"], "RPM", 300, 18000)
        load = clip["load"]
        if type(load) is not int or load not in (0, 1):
            raise ValueError("load must be the integer 0 (coast) or 1 (throttle)")
        if (rpm, load) in seen:
            raise ValueError("duplicate RPM in one load state")
        seen.add((rpm, load))
        start = finite(clip.get("start", 0), "start", 0, 86400)
        duration = finite(clip["duration"], "duration", 0.1, 4)
        gain = finite(clip.get("gain", 0.5), "gain", 0.001, 2)
        original = (plan_path.parent / clip["file"]).resolve()
        with wave.open(str(original), "rb") as recording:
            channels, width, rate = recording.getnchannels(), recording.getsampwidth(), recording.getframerate()
            if recording.getcomptype() != "NONE" or channels not in (1, 2) or width not in (2, 3, 4) or not 8000 <= rate <= 192000:
                raise ValueError(f"{original}: expected mono/stereo PCM16/24/32, 8-192 kHz")
            first, count = round(start * rate), round(duration * rate)
            if first + count > recording.getnframes():
                raise ValueError(f"{original}: requested clip extends beyond the recording")
            recording.setpos(first)
            data = recording.readframes(count)
            if len(data) != count * width * channels:
                raise ValueError(f"{original}: truncated PCM data")
        decoded_bytes += count * 4 * 1.875  # mono floats, including four mip levels
        jobs.append((rpm, load, gain, channels, width, rate, data))
    if any(sum(load == state for _, load in seen) < 2 for state in (0, 1)):
        raise ValueError("each load state needs two distinct RPM clips")
    if decoded_bytes > 16 * 1024 * 1024:
        raise ValueError("clips exceed the game's 16 MB decoded bank budget")
    root = Path(root).resolve()
    destination = root / key
    if destination.exists():
        raise ValueError(f"{destination} already exists; an existing bank will not be overwritten")
    root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".import-", dir=root) as temporary:
        staging = Path(temporary) / key
        staging.mkdir()
        lines = [f"source {source}", f"author {author}", f"license {license_id}"]
        for index, (rpm, load, gain, channels, width, rate, data) in enumerate(sorted(jobs)):
            filename = f"{'load' if load else 'coast'}-{rpm:g}-{index}.wav"
            with wave.open(str(staging / filename), "wb") as output:
                output.setnchannels(channels); output.setsampwidth(width); output.setframerate(rate)
                output.writeframes(data)
            lines.append(f"loop {rpm:g} {load} {gain:g} {filename}")
        (staging / "bank.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
        (staging / "import-plan.json").write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
        os.rename(staging, destination)
    print(f"Imported {key}: {len(jobs)} documented clips into {destination}")
    print("Restart the game; the runtime validates the whole bank before using it.")
    return destination

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("plan", type=Path)
    parser.add_argument("--root", type=Path, default=Path("assets/audio/engines"))
    args = parser.parse_args()
    try:
        import_bank(args.plan, args.root)
    except (OSError, ValueError, TypeError, KeyError, wave.Error) as error:
        parser.exit(1, f"Cannot import engine bank: {error}\n")

if __name__ == "__main__":
    main()
