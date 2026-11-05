"""Builds schedule.txt for each station from station.json and its MP3s."""

from __future__ import annotations

import json
import zlib
from pathlib import Path

from . import audio
from .content import META_OUT
from .schedule import WEEK_MS, Clip, format_schedule, generate, no_repeat_ms, rule_categories

STATION_FILE = "station.json"
SCHEDULE_FILE = "schedule.txt"


def station_dirs(content: Path, only: str | None = None) -> list[Path]:
    stations = sorted(p.parent for p in (content / "stations").glob(f"*/{STATION_FILE}"))
    if only:
        stations = [p for p in stations if p.name == only or p.name.split("_", 1)[-1] == only]
        if not stations:
            raise FileNotFoundError(f"no station named {only}")
    return stations


def load_clips(station: Path, categories: set[str], log) -> list[Clip]:
    clips = []
    for category in sorted(categories):
        folder = station / category
        if not folder.is_dir():
            log(f"warning: {station.name}: no {category}/ folder")
            continue
        meta_path = folder / META_OUT
        meta = json.loads(meta_path.read_text()) if meta_path.exists() else {}
        files = sorted(folder.glob("*.mp3"))
        if not files:
            log(f"warning: {station.name}: {category}/ has no MP3s")
        for f in files:
            info = audio.probe(f)
            for problem in info.problems():
                log(f"warning: {station.name}/{category}/{f.name}: {problem} (run convert)")
            m = meta.get(f.name, {})
            clips.append(Clip(
                path=f"{category}/{f.name}",
                category=category,
                duration_ms=info.duration_ms,
                arc=m.get("arc"),
                order=int(m.get("order", 0)),
                date=m.get("date"),
            ))
    return clips


def _hours_or_minutes(ms: int) -> str:
    if ms >= 3.6e6:
        return f"{ms / 3.6e6:.1f} h"
    return f"{ms / 6e4:.0f} min" if ms >= 6e4 else f"{ms / 1000:.0f} s"


def warn_small_pools(station: Path, clips: list[Clip], rules: dict, log):
    """Warns when a category has less audio than its no-repeat window."""
    for category in sorted({c.category for c in clips}):
        window_ms = no_repeat_ms(rules.get("no_repeat_hours", 0), category)
        total_ms = sum(c.duration_ms for c in clips if c.category == category and not c.date)
        if total_ms < window_ms:
            log(f"warning: {station.name}: {category}/ has {_hours_or_minutes(total_ms)} of audio, "
                f"less than its {_hours_or_minutes(window_ms)} no-repeat window; "
                "clips will repeat sooner")


def build_schedule(station: Path, log) -> int:
    rules = json.loads((station / STATION_FILE).read_text())
    clips = load_clips(station, rule_categories(rules), log)
    warn_small_pools(station, clips, rules, log)
    seed = rules.get("seed", zlib.crc32(station.name.encode()))
    entries = generate(clips, rules, seed=seed, length_ms=WEEK_MS)
    (station / SCHEDULE_FILE).write_text(format_schedule(entries, WEEK_MS))
    return len(entries)


def build_all(content: Path, log, only: str | None = None):
    audio.require_tools()
    for station in station_dirs(content, only):
        count = build_schedule(station, log)
        log(f"{station.name}: {count} entries -> {station.name}/{SCHEDULE_FILE}")
