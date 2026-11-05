"""Checks a loaded SD card (or content folder) before it goes in the radio.

Errors mean the radio will misbehave: a station is skipped, or goes silent, or
the card can't be read. Warnings mean it will run, but something is missing.
The rules mirror what the firmware does when it reads the card; no ffmpeg
needed, so a whole card takes seconds.
"""

from __future__ import annotations

import json
import plistlib
import subprocess
from dataclasses import dataclass, field
from pathlib import Path

from .schedule import HEADER_MAGIC, RECORD_SIZE, SILENCE, WEEK_MS

BYTES_PER_MS = 16  # 128 kbps CBR
SIZE_SLACK = 1024  # durations are rounded; allow a couple of MP3 frames
# Must fit kSdMaxFiles in src/platform/esp32/Esp32Platform.cpp: each scheduled
# station holds its schedule.txt open, plus the playing MP3 and one short-lived file.
SD_MAX_FILES = 12
MAX_SCHEDULED_STATIONS = SD_MAX_FILES - 2
DEFAULT_DIAL = (88.0, 108.0)  # RadioConfig's defaults, used without a config.json
NOT_COPIED = {"clips.json", ".prep.json"}  # only the prep tool needs these
MAX_LISTED = 5


@dataclass
class Report:
    errors: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)
    stations: list[str] = field(default_factory=list)  # one summary line each
    playable: int = 0  # stations with audio in their schedule


def _listed(items: list[str]) -> str:
    shown = ", ".join(items[:MAX_LISTED])
    extra = len(items) - MAX_LISTED
    return f"{shown} and {extra} more" if extra > 0 else shown


def _load_json(path: Path):
    """Returns (value, problem); problem is None if the file parsed."""
    try:
        return json.loads(path.read_text()), None
    except OSError as e:
        return None, f"can't be read ({e.strerror})"
    except ValueError as e:  # bad JSON, or not text at all
        return None, f"isn't valid JSON ({e})"


def mp3_problem(path: Path, longest_ms: int = 0) -> str | None:
    """Why this file won't play properly, or None if it looks right."""
    try:
        size = path.stat().st_size
        with path.open("rb") as f:
            head = f.read(4)
    except OSError as e:
        return f"can't be read ({e.strerror})"
    if size == 0:
        return "is empty"
    if head[:3] == b"ID3":
        return "starts with an ID3 tag (run convert)"
    # The first frame header: sync, MPEG-1 Layer III, 128 kbps, 44.1 kHz, mono.
    if (len(head) < 4 or head[0] != 0xFF or head[1] & 0xFE != 0xFA or head[2] >> 4 != 9
            or (head[2] >> 2) & 3 != 0 or head[3] >> 6 != 3):
        return "isn't mono 44.1 kHz CBR 128 kbps MP3 (run convert)"
    if size + SIZE_SLACK < longest_ms * BYTES_PER_MS:
        return (f"is cut short: {size:,} bytes, but its schedule slot needs "
                f"{longest_ms * BYTES_PER_MS:,} (copy it again)")
    return None


def parse_records(data: bytes) -> tuple[list[tuple[int, int, str]] | None, str | None]:
    """Reads schedule.txt as strictly as the firmware does: (entries, problem)."""
    if len(data) < RECORD_SIZE:
        return None, "is too short to have a header"
    head = data[:RECORD_SIZE]
    count_text, length_text = head[10:16], head[17:26]
    if (head[:10] != HEADER_MAGIC.encode() + b" " or head[16:17] != b" "
            or not count_text.isdigit() or not length_text.isdigit()
            or head[-1:] != b"\n"):
        return None, "has a bad header"
    count, length = int(count_text), int(length_text)
    if count == 0 or length == 0:
        return None, "has no entries"
    if length != WEEK_MS:
        return None, f"covers {length} ms, not one week"
    if len(data) != (count + 1) * RECORD_SIZE:
        return None, f"is {len(data):,} bytes, but its header says {(count + 1) * RECORD_SIZE:,}"
    entries = []
    expected_start = 0
    for i in range(1, count + 1):
        rec = data[i * RECORD_SIZE:(i + 1) * RECORD_SIZE]
        start_text, duration_text, path = rec[:9], rec[10:18], rec[19:63].rstrip(b" ")
        if (not start_text.isdigit() or not duration_text.isdigit() or rec[9:10] != b" "
                or rec[18:19] != b" " or rec[-1:] != b"\n" or not path):
            return None, f"has a bad record at line {i + 1}"
        start, duration = int(start_text), int(duration_text)
        if start != expected_start or duration == 0:
            return None, f"has a gap or overlap at line {i + 1}; the radio would go quiet there"
        expected_start = start + duration
        entries.append((start, duration, path.decode("ascii", "replace")))
    if expected_start != length:
        return None, f"ends at {expected_start} ms, short of the week"
    return entries, None


def _check_station(folder: Path, dial: tuple[float, float], report: Report,
                   frequencies: dict[float, str]) -> bool:
    """Checks one station. Returns True if it has a usable schedule."""
    name = folder.name
    rules, problem = _load_json(folder / "station.json")
    if problem and not (folder / "station.json").exists():
        report.errors.append(f"{name}: no station.json; the radio skips this station")
        return False
    if problem:
        report.errors.append(f"{name}: station.json {problem}; the radio skips this station")
        return False
    frequency = rules.get("frequency") if isinstance(rules, dict) else None
    if isinstance(frequency, bool) or not isinstance(frequency, (int, float)) or frequency <= 0:
        report.errors.append(f"{name}: station.json has no frequency; the radio skips this station")
        return False
    title = str(rules.get("name") or name)
    if not dial[0] <= frequency <= dial[1]:
        report.errors.append(
            f"{name}: {frequency} MHz is outside the dial ({dial[0]}-{dial[1]}), so it can't be tuned")
    tenth = round(frequency, 1)
    if tenth in frequencies:
        report.errors.append(f"{name}: shares {tenth} MHz with {frequencies[tenth]}")
    frequencies.setdefault(tenth, name)

    label = f"{frequency:5.1f}  {title:<24}"
    schedule = folder / "schedule.txt"
    if not schedule.exists():
        if any(not p.name.startswith(".") for p in folder.rglob("*.mp3")):
            report.errors.append(f"{name}: has audio but no schedule.txt (run prep.py schedule)")
        else:
            report.warnings.append(f"{name}: no audio or schedule yet, so {title} will be silent")
        report.stations.append(f"{label} no schedule")
        return False

    try:
        data = schedule.read_bytes()
    except OSError as e:
        report.errors.append(f"{name}: schedule.txt can't be read ({e.strerror})")
        report.stations.append(f"{label} BAD SCHEDULE")
        return False
    entries, problem = parse_records(data)
    if problem:
        report.errors.append(f"{name}: schedule.txt {problem}; the station will be silent")
        report.stations.append(f"{label} BAD SCHEDULE")
        return False

    longest: dict[str, int] = {}
    for _, duration, path in entries:
        if path != SILENCE:
            longest[path] = max(longest.get(path, 0), duration)
    missing = sorted(p for p in longest if not (folder / p).is_file())
    if missing:
        report.errors.append(
            f"{name}: {len(missing)} of {len(longest)} scheduled files are missing and will play "
            f"as silence: {_listed(missing)}")
    bad = []
    for path in sorted(set(longest) - set(missing)):
        why = mp3_problem(folder / path, longest[path])
        if why:
            bad.append(f"{path} {why}")
    for line in bad[:MAX_LISTED]:
        report.errors.append(f"{name}: {line}")
    if len(bad) > MAX_LISTED:
        report.errors.append(f"{name}: and {len(bad) - MAX_LISTED} more bad files")
    if longest:
        report.playable += 1
    else:
        report.warnings.append(f"{name}: its schedule is all silence, so {title} will never play anything")

    silent_ms = sum(d for _, d, p in entries if p == SILENCE)
    on_air = 100 - silent_ms * 100 // WEEK_MS
    report.stations.append(f"{label} {len(longest)} files, {on_air}% on air")
    return True


def _check_copy(card: Path, reference: Path, report: Report):
    """Every file in the reference folder should be on the card, at the same size."""
    missing, different = [], []
    for top in ("system", "stations"):
        for src in sorted((reference / top).rglob("*")):
            if not src.is_file() or src.name.startswith(".") or src.name in NOT_COPIED:
                continue
            rel = src.relative_to(reference)
            dst = card / rel
            if not dst.is_file():
                missing.append(str(rel))
            elif dst.stat().st_size != src.stat().st_size:
                different.append(str(rel))
    if missing:
        report.errors.append(
            f"{len(missing)} files in {reference.name}/ aren't on the card: {_listed(missing)}")
    if different:
        report.errors.append(
            f"{len(different)} files differ in size from {reference.name}/: {_listed(different)}")


def volume_format(path: Path) -> str | None:
    """The file system of a mounted volume, e.g. 'MS-DOS (FAT32)'; None if unknown."""
    try:
        result = subprocess.run(["diskutil", "info", "-plist", str(path)], capture_output=True)
        return plistlib.loads(result.stdout).get("FilesystemUserVisibleName")
    except (OSError, ValueError):
        return None


def check_card(card: Path, reference: Path | None = None, filesystem: str | None = None) -> Report:
    report = Report()
    if not card.is_dir():
        report.errors.append(f"{card} isn't a folder; is the card mounted?")
        return report
    if filesystem is not None and "FAT32" not in filesystem:
        report.errors.append(f"the card is formatted {filesystem}; the radio only reads FAT32")

    dial = DEFAULT_DIAL
    config, problem = _load_json(card / "system" / "config.json")
    if problem and not (card / "system" / "config.json").exists():
        report.errors.append("system/config.json is missing; the radio falls back to default settings")
    elif problem:
        report.errors.append(f"system/config.json {problem}; the radio falls back to default settings")
    elif isinstance(config, dict):
        limits = config.get("dial")
        if isinstance(limits, dict):
            dial = tuple(v if isinstance(v, (int, float)) else d
                         for v, d in zip((limits.get("min"), limits.get("max")), dial))

    boot = card / "system" / "boot.mp3"
    if not boot.exists():
        report.warnings.append("system/boot.mp3 is missing, so there's no warm-up sound at power-on")
    else:
        why = mp3_problem(boot)
        if why:
            report.errors.append(f"system/boot.mp3 {why}")

    stations = card / "stations"
    folders = sorted(p for p in stations.iterdir() if p.is_dir() and not p.name.startswith(".")) \
        if stations.is_dir() else []
    if not folders:
        report.errors.append("no stations/ folders; the radio will show no stations")

    frequencies: dict[float, str] = {}
    scheduled = sum([_check_station(f, dial, report, frequencies) for f in folders])
    if folders and not report.playable:
        report.errors.append("no station has any audio scheduled; the radio will play nothing")
    if scheduled > MAX_SCHEDULED_STATIONS:
        report.errors.append(
            f"{scheduled} stations have schedules, but the firmware has file slots for "
            f"{MAX_SCHEDULED_STATIONS}; MP3s won't open (raise kSdMaxFiles)")

    if reference is not None and reference.resolve() != card.resolve():
        _check_copy(card, reference, report)
    return report


def print_report(card: Path, report: Report, log=print) -> int:
    """Prints the result; returns the process exit code."""
    log(f"Checking {card}\n")
    for line in report.stations:
        log(f"  {line}")
    if report.stations:
        log("")
    for line in report.errors:
        log(f"error: {line}")
    for line in report.warnings:
        log(f"warning: {line}")
    if report.errors or report.warnings:
        log("")
    verdict = "NOT READY" if report.errors else "ready"
    log(f"Card {verdict}: {len(report.errors)} error(s), {len(report.warnings)} warning(s)")
    return 1 if report.errors else 0
