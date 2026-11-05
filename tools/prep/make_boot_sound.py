#!/usr/bin/env python3
"""Synthesizes the tube warm-up sound the radio plays at power-on.

    python3 tools/prep/make_boot_sound.py [out.wav]    default: source/system/boot.wav
    python3 tools/prep/prep.py convert                 makes content/system/boot.mp3

A hum that swells up with a little hiss and crackle, then dies away while the
radio fades in over it. It lives in the mains-harmonic range rather than at
50/60 Hz itself: a small speaker can't reproduce that, and the radio's vintage
filter cuts below 250 Hz anyway. Same output every run. To use a real recording
instead, just put it at source/system/boot.wav.
"""

from __future__ import annotations

import math
import random
import struct
import sys
import wave
from pathlib import Path

RATE = 44100
SECONDS = 5.0
SWELL_END = 3.2      # hum reaches full level
DIE_AWAY = 1.5       # last seconds, matching WarmUp::kFadeSamples in the firmware
PEAK = 0.8
# (Hz, level): harmonics of 60 Hz mains, weighted to what a small speaker plays.
HARMONICS = [(120, 0.30), (180, 0.20), (240, 0.45), (300, 0.20), (360, 0.40),
             (480, 0.22), (600, 0.12), (720, 0.06)]
SEED = 1945


def smoothstep(x: float) -> float:
    x = min(max(x, 0.0), 1.0)
    return x * x * (3 - 2 * x)


def synthesize() -> list[float]:
    rng = random.Random(SEED)
    samples = []
    count = int(RATE * SECONDS)
    phases = [rng.random() * 2 * math.pi for _ in HARMONICS]
    hiss = 0.0
    pop = 0.0
    wobble = 1.0
    for i in range(count):
        t = i / RATE
        swell = smoothstep(t / SWELL_END)
        die = smoothstep((SECONDS - t) / DIE_AWAY)
        # The pitch settles as it warms, and the level never sits quite still.
        drift = 0.97 + 0.03 * swell
        wobble += 0.0004 * (rng.uniform(0.82, 1.0) - wobble)
        hum = 0.0
        for k, (hz, level) in enumerate(HARMONICS):
            phases[k] += 2 * math.pi * hz * drift / RATE
            hum += level * math.sin(phases[k])
        hiss += 0.35 * (rng.uniform(-1, 1) - hiss)
        # Crackle comes and goes as things heat up, most of it in the middle.
        if rng.random() < (6 * math.sin(math.pi * min(t / SECONDS, 1)) + 1) / RATE:
            pop = rng.choice((-1, 1)) * rng.uniform(0.25, 0.7)
        crackle = pop
        pop *= 0.72
        samples.append((hum * swell * wobble + hiss * 0.10 * swell + crackle * (0.3 + 0.7 * swell)) * die)
    peak = max(abs(s) for s in samples) or 1.0
    return [s / peak * PEAK for s in samples]


def main(argv: list[str]) -> int:
    root = Path(__file__).resolve().parents[2]
    out = Path(argv[1]) if len(argv) > 1 else root / "source" / "system" / "boot.wav"
    out.parent.mkdir(parents=True, exist_ok=True)
    frames = b"".join(struct.pack("<h", int(s * 32767)) for s in synthesize())
    with wave.open(str(out), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(frames)
    print(f"wrote {out} ({SECONDS:g} s)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
