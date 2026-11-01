"""Synthesizes placeholder source audio so the pipeline can run before real
content exists. Music is a steady chord, speech-like clips are pulsing
filtered noise, station IDs are beeps; each kind sounds distinct by ear."""

from __future__ import annotations

import random
import subprocess
from pathlib import Path

from . import audio

# station -> category -> (count, min seconds, max seconds)
LAYOUT = {
    "01_crooner": {"music": (6, 90, 180), "dj": (3, 15, 30), "ids": (2, 5, 8), "ads": (3, 25, 35)},
    "02_news": {"news": (5, 30, 60), "ids": (2, 5, 8), "music": (3, 90, 150)},
    "03_bigband": {"music": (6, 90, 180), "ids": (2, 5, 8), "ads": (3, 25, 35)},
    "04_country": {"music": (6, 90, 180), "ids": (2, 5, 8), "dj": (3, 15, 30)},
    "99_hidden": {"broadcast": (2, 20, 40)},
}

SPEECH = {"dj", "ads", "news", "broadcast"}


def _source_filter(category: str, rng: random.Random, seconds: int) -> str:
    if category in SPEECH:
        rate = rng.uniform(2.5, 4.5)  # syllable-ish pulsing
        return (f"anoisesrc=color=pink:duration={seconds}:seed={rng.randrange(1 << 30)},"
                f"bandpass=f={rng.randint(500, 1200)}:width_type=o:w=1.5,"
                f"tremolo=f={rate:.2f}:d=0.9")
    if category == "ids":
        f = rng.choice([660, 784, 880])
        return f"sine=f={f}:duration={seconds},tremolo=f=2:d=1"
    root = rng.choice([196, 220, 247, 262, 294])
    tones = "+".join(f"0.25*sin(2*PI*{root * r:.1f}*t)" for r in (1, 1.25, 1.5, 2))
    return f"aevalsrc='{tones}':s=44100:d={seconds},tremolo=f=0.5:d=0.3"


def make_demo(source: Path, log, seed: int = 1):
    audio.require_tools()
    rng = random.Random(seed)
    for station, categories in LAYOUT.items():
        for category, (count, lo, hi) in categories.items():
            folder = source / "stations" / station / category
            folder.mkdir(parents=True, exist_ok=True)
            for i in range(1, count + 1):
                out = folder / f"Demo {category.title()} {i}.wav"
                if out.exists():
                    continue
                seconds = rng.randint(lo, hi)
                subprocess.run(
                    ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi",
                     "-i", _source_filter(category, rng, seconds), "-ac", "2", str(out)],
                    check=True)
                log(f"made {out.relative_to(source)} ({seconds}s)")
    boot = source / "system" / "boot.wav"
    if not boot.exists():
        boot.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
             "aevalsrc='0.3*sin(2*PI*60*t)*min(t/2,1)':s=44100:d=3", str(boot)],
            check=True)
        log(f"made {boot.relative_to(source)}")
