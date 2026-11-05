"""Weekly schedule generation. Pure Python, no ffmpeg; see tools/prep/README.md.

schedule.txt is a sequence of fixed-width 64-byte records. Record 0 is a
header; records 1..N are entries sorted by start time:

    #VRSCHED1 NNNNNN LLLLLLLLL<spaces>\n      header: entry count, length in ms
    SSSSSSSSS DDDDDDDD PATH<spaces>\n         start ms, duration ms, path

Start times are milliseconds since Monday 00:00 local time. PATH is relative
to the station folder, or "-" for silence. Entries tile the week exactly.
"""

from __future__ import annotations

import random
import re
from dataclasses import dataclass, field

DAY_MS = 24 * 3600 * 1000
WEEK_MS = 7 * DAY_MS
RECORD_SIZE = 64
PATH_WIDTH = 44  # 9 + 1 + 8 + 1 + 44 + newline = 64
SILENCE = "-"
HEADER_MAGIC = "#VRSCHED1"


class RuleError(ValueError):
    pass


@dataclass(frozen=True)
class Clip:
    path: str  # relative to the station folder, e.g. "music/001_song.mp3"
    category: str
    duration_ms: int
    arc: str | None = None  # clips in an arc play in `order`
    order: int = 0
    date: str | None = None  # "MM-DD"; reserved for special broadcasts


@dataclass(frozen=True)
class Entry:
    start_ms: int
    duration_ms: int
    path: str


# --- Rules -------------------------------------------------------------------


@dataclass
class Step:
    weights: dict[str, float] = field(default_factory=dict)  # empty = silence
    count: tuple[int, int] = (1, 1)
    silence_s: tuple[int, int] = (0, 0)
    chance: float = 1.0


def _range(value, what: str) -> tuple[int, int]:
    if isinstance(value, int):
        return value, value
    if (isinstance(value, list) and len(value) == 2
            and all(isinstance(v, int) for v in value) and 0 <= value[0] <= value[1]):
        return value[0], value[1]
    raise RuleError(f"{what} must be an int or [min, max], got {value!r}")


def parse_pattern(pattern) -> list[Step]:
    if not isinstance(pattern, list) or not pattern:
        raise RuleError("pattern must be a non-empty list")
    steps = []
    for raw in pattern:
        if not isinstance(raw, dict):
            raise RuleError(f"pattern step must be an object, got {raw!r}")
        step = Step(chance=float(raw.get("chance", 1.0)))
        if "silence" in raw:
            step.silence_s = _range(raw["silence"], "silence")
        elif "pick" in raw:
            pick = raw["pick"]
            if isinstance(pick, str):
                step.weights = {pick: 1.0}
            elif isinstance(pick, list) and pick and all(isinstance(p, str) for p in pick):
                step.weights = {p: 1.0 for p in pick}
            elif isinstance(pick, dict) and pick:
                step.weights = {str(k): float(v) for k, v in pick.items()}
            else:
                raise RuleError(f"pick must be a category, list or weights, got {pick!r}")
            step.count = _range(raw.get("count", 1), "count")
        else:
            raise RuleError(f"pattern step needs 'pick' or 'silence': {raw!r}")
        steps.append(step)
    return steps


def pattern_categories(pattern) -> set[str]:
    return {c for step in parse_pattern(pattern) for c in step.weights}


def _parse_clock(value) -> int:
    """'HH:MM' -> ms since midnight."""
    m = re.fullmatch(r"(\d{2}):(\d{2})", value) if isinstance(value, str) else None
    if not m or int(m.group(1)) > 23 or int(m.group(2)) > 59:
        raise RuleError(f"daily 'at' must be 'HH:MM', got {value!r}")
    return (int(m.group(1)) * 60 + int(m.group(2))) * 60 * 1000


def parse_daily(daily) -> list[tuple[int, Step]]:
    """The 'daily' rule: [{"at": "18:00", "pick": ..., "count": ...}], sorted by time."""
    if daily is None:
        return []
    if not isinstance(daily, list):
        raise RuleError("daily must be a list")
    slots = []
    for raw in daily:
        if not isinstance(raw, dict) or "at" not in raw or "pick" not in raw:
            raise RuleError(f"daily slot needs 'at' and 'pick': {raw!r}")
        step = parse_pattern([{k: v for k, v in raw.items() if k in ("pick", "count")}])[0]
        slots.append((_parse_clock(raw["at"]), step))
    slots.sort(key=lambda slot: slot[0])
    if len({at for at, _ in slots}) != len(slots):
        raise RuleError("two daily slots share the same time")
    return slots


def rule_categories(rules: dict) -> set[str]:
    categories = pattern_categories(rules.get("pattern"))
    for _, step in parse_daily(rules.get("daily")):
        categories |= set(step.weights)
    return categories


def no_repeat_ms(no_repeat_hours, category: str) -> int:
    if isinstance(no_repeat_hours, (int, float)):
        hours = no_repeat_hours
    else:
        rules = no_repeat_hours or {}
        hours = rules.get(category, rules.get("*", 0))
    return int(hours * 3600 * 1000)


# --- Picking -----------------------------------------------------------------


class _Picker:
    """Chooses clips: avoids repeats within a window, keeps arcs in order."""

    def __init__(self, clips: list[Clip], no_repeat_hours, rng: random.Random):
        self.rng = rng
        self.no_repeat_hours = no_repeat_hours
        self.loose: dict[str, list[Clip]] = {}
        self.arcs: dict[str, dict[str, list[Clip]]] = {}
        self.arc_pos: dict[tuple[str, str], int] = {}
        self.last_played: dict[str, int] = {}
        self.previous: str | None = None
        for clip in sorted(clips, key=lambda c: c.path):
            if clip.date:
                continue  # dated clips are for special broadcasts, not rotation
            if clip.arc:
                self.arcs.setdefault(clip.category, {}).setdefault(clip.arc, []).append(clip)
            else:
                self.loose.setdefault(clip.category, []).append(clip)
        for arcs in self.arcs.values():
            for arc_clips in arcs.values():
                arc_clips.sort(key=lambda c: (c.order, c.path))

    def has(self, category: str) -> bool:
        return bool(self.loose.get(category) or self.arcs.get(category))

    def _pool(self, category: str) -> list[Clip]:
        pool = list(self.loose.get(category, []))
        for arc, arc_clips in sorted(self.arcs.get(category, {}).items()):
            pool.append(arc_clips[self.arc_pos.get((category, arc), 0)])
        return pool

    def pick(self, category: str, now_ms: int) -> Clip:
        pool = self._pool(category)
        if len(pool) > 1:
            pool = [c for c in pool if c.path != self.previous] or pool
        window = no_repeat_ms(self.no_repeat_hours, category)
        fresh = [c for c in pool
                 if c.path not in self.last_played or now_ms - self.last_played[c.path] >= window]
        if fresh:
            clip = self.rng.choice(fresh)
        else:
            oldest = min(self.last_played[c.path] for c in pool)
            clip = self.rng.choice([c for c in pool if self.last_played[c.path] == oldest])
        self.last_played[clip.path] = now_ms
        self.previous = clip.path
        if clip.arc:
            key = (category, clip.arc)
            self.arc_pos[key] = (self.arc_pos.get(key, 0) + 1) % len(self.arcs[category][clip.arc])
        return clip


# --- Generation --------------------------------------------------------------


def generate(clips: list[Clip], rules: dict, seed: int, length_ms: int = WEEK_MS) -> list[Entry]:
    """Builds a schedule that exactly covers [0, length_ms).

    The last entry is cut short at the end of the week. Each 'daily' slot
    starts exactly on time, every day: whatever is playing then is cut short,
    and the pattern carries on when the slot's clips finish.
    """
    steps = parse_pattern(rules.get("pattern"))
    daily = parse_daily(rules.get("daily"))
    slots = [(day * DAY_MS + at, step)
             for day in range(length_ms // DAY_MS + 1)
             for at, step in daily
             if day * DAY_MS + at < length_ms]
    rng = random.Random(seed)
    picker = _Picker(clips, rules.get("no_repeat_hours", 0), rng)

    entries: list[Entry] = []
    t = 0
    next_slot = 0

    def limit() -> int:
        return slots[next_slot][0] if next_slot < len(slots) else length_ms

    def add(duration_ms: int, path: str):
        nonlocal t
        duration_ms = min(duration_ms, limit() - t)
        entries.append(Entry(t, duration_ms, path))
        t += duration_ms

    def play_clip(category: str):
        clip = picker.pick(category, t)
        if clip.duration_ms <= 0:
            raise RuleError(f"{clip.path} has no duration")
        add(clip.duration_ms, clip.path)

    def choose(weights: dict[str, float]) -> str:
        return rng.choices(list(weights), weights=list(weights.values()))[0]

    def ready() -> bool:
        """Plays any slot that is due; True if there's still time to fill."""
        nonlocal next_slot
        while next_slot < len(slots) and slots[next_slot][0] <= t:
            step = slots[next_slot][1]
            next_slot += 1
            available = {c: w for c, w in step.weights.items() if picker.has(c) and w > 0}
            if available:
                for _ in range(rng.randint(*step.count)):
                    if t >= limit():
                        break
                    play_clip(choose(available))
        return t < length_ms

    while t < length_ms:
        start_of_pass = t
        for step in steps:
            if not ready():
                break
            if step.chance < 1.0 and rng.random() >= step.chance:
                continue
            if not step.weights:
                seconds = rng.randint(*step.silence_s)
                if seconds > 0:
                    add(seconds * 1000, SILENCE)
                continue
            available = {c: w for c, w in step.weights.items() if picker.has(c) and w > 0}
            if not available:
                continue
            for _ in range(rng.randint(*step.count)):
                if not ready():
                    break
                play_clip(choose(available))
        if t == start_of_pass:
            raise RuleError("pattern produced no audio: are its categories empty?")
    return entries


# --- File format -------------------------------------------------------------


def format_entry(entry: Entry) -> str:
    if len(entry.path) > PATH_WIDTH or not entry.path.isascii() or " " in entry.path:
        raise RuleError(f"path must be ASCII, no spaces, at most {PATH_WIDTH} chars: {entry.path}")
    line = f"{entry.start_ms:09d} {entry.duration_ms:08d} {entry.path:<{PATH_WIDTH}}\n"
    assert len(line) == RECORD_SIZE
    return line


def format_schedule(entries: list[Entry], length_ms: int = WEEK_MS) -> str:
    header = f"{HEADER_MAGIC} {len(entries):06d} {length_ms:09d}"
    lines = [header.ljust(RECORD_SIZE - 1) + "\n"]
    lines += [format_entry(e) for e in entries]
    return "".join(lines)


def parse_schedule(text: str) -> tuple[int, list[Entry]]:
    """Inverse of format_schedule; returns (length_ms, entries)."""
    if len(text) % RECORD_SIZE:
        raise ValueError("schedule size is not a multiple of the record size")
    records = [text[i:i + RECORD_SIZE] for i in range(0, len(text), RECORD_SIZE)]
    magic, count, length = records[0].split()
    if magic != HEADER_MAGIC or int(count) != len(records) - 1:
        raise ValueError("bad schedule header")
    entries = []
    for rec in records[1:]:
        start, duration, path = rec.split()
        entries.append(Entry(int(start), int(duration), path))
    return int(length), entries
