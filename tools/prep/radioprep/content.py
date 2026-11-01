"""Mirrors source/ into content/: converts audio, names files, keeps metadata.

Inside source/stations/<station>/<category>/ each audio file becomes
content/stations/<station>/<category>/NNN_name.mp3. A file whose name already
starts with digits and "_" keeps that number; others get the next free one.
Numbers stay stable across runs via a .prep.json manifest in each output
folder. Outside station categories (e.g. source/system/boot.wav) files keep
their sanitized name.

Optional per-clip metadata: source/.../<category>/meta.json, keyed by source
filename, e.g. {"bulletin.wav": {"category": "local", "date": "12-25",
"arc": "fair", "order": 2}}. It is written next to the MP3s as clips.json,
keyed by output filename.
"""

from __future__ import annotations

import json
import re
import unicodedata
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

from . import audio
from .schedule import PATH_WIDTH

MANIFEST = ".prep.json"
META_IN = "meta.json"
META_OUT = "clips.json"
META_KEYS = {"category", "date", "arc", "order"}


def sanitize(stem: str) -> str:
    ascii_stem = unicodedata.normalize("NFKD", stem).encode("ascii", "ignore").decode()
    return re.sub(r"[^a-z0-9]+", "_", ascii_stem.lower()).strip("_") or "clip"


def numbered_name(category: str, number: int, stem: str) -> str:
    """'NNN_stem.mp3', with stem cut so 'category/NNN_stem.mp3' fits a schedule record."""
    prefix = f"{number:03d}_"
    room = PATH_WIDTH - len(category) - 1 - len(prefix) - len(".mp3")
    if room < 1:
        raise ValueError(f"category name too long: {category}")
    return f"{prefix}{stem[:room].rstrip('_') or 'clip'}.mp3"


def _load_json(path: Path, default):
    if not path.exists():
        return default
    with path.open() as f:
        return json.load(f)


def _write_json(path: Path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w") as f:
        json.dump(data, f, indent=2, sort_keys=True)
        f.write("\n")


@dataclass
class Job:
    src: Path
    dst: Path


def _is_category_dir(rel: Path) -> bool:
    # stations/<station>/<category>
    return len(rel.parts) == 3 and rel.parts[0] == "stations"


def plan_folder(src_dir: Path, dst_dir: Path, rel: Path, log) -> list[Job]:
    """Updates names and metadata for one folder; returns conversions needed."""
    sources = sorted(p for p in src_dir.iterdir()
                     if p.is_file() and p.suffix.lower() in audio.AUDIO_EXTENSIONS)
    manifest_path = dst_dir / MANIFEST
    manifest = _load_json(manifest_path, {}).get("sources", {})
    numbered = _is_category_dir(rel)
    category = rel.parts[-1] if numbered else ""

    # Drop outputs whose source is gone.
    for name in list(manifest):
        if name not in {p.name for p in sources}:
            out = dst_dir / manifest[name]["out"]
            if out.exists():
                out.unlink()
                log(f"removed {out}")
            del manifest[name]

    used = {int(m["out"][:3]) for m in manifest.values() if m["out"][:3].isdigit()}
    used |= {int(p.name[:3]) for p in dst_dir.glob("[0-9][0-9][0-9]_*.mp3")} if dst_dir.exists() else set()
    jobs = []
    for src in sources:
        stat = src.stat()
        entry = manifest.get(src.name)
        if entry is None:
            if numbered:
                m = re.match(r"(\d+)[ _-]+(.*)", src.stem)
                if m and int(m.group(1)) not in used:
                    number, stem = int(m.group(1)), sanitize(m.group(2))
                else:
                    number = max(used, default=0) + 1
                    stem = sanitize(m.group(2) if m else src.stem)
                used.add(number)
                out_name = numbered_name(category, number, stem)
            else:
                out_name = sanitize(src.stem) + ".mp3"
            entry = manifest[src.name] = {"out": out_name}
        dst = dst_dir / entry["out"]
        if not dst.exists() or entry.get("mtime") != stat.st_mtime_ns or entry.get("size") != stat.st_size:
            jobs.append(Job(src, dst))
        entry["mtime"], entry["size"] = stat.st_mtime_ns, stat.st_size

    if sources or manifest_path.exists():
        _write_json(manifest_path, {"sources": manifest})

    meta_in = _load_json(src_dir / META_IN, {})
    meta_out = {}
    for name, meta in meta_in.items():
        if name not in manifest:
            log(f"warning: {src_dir / META_IN} lists missing file {name}")
            continue
        unknown = set(meta) - META_KEYS
        if unknown:
            log(f"warning: {src_dir / META_IN}: unknown keys {sorted(unknown)} for {name}")
        meta_out[manifest[name]["out"]] = {k: v for k, v in meta.items() if k in META_KEYS}
    if meta_out:
        _write_json(dst_dir / META_OUT, meta_out)
    elif (dst_dir / META_OUT).exists():
        (dst_dir / META_OUT).unlink()
    return jobs


def convert_tree(source: Path, content: Path, log, workers: int = 4) -> int:
    """Converts everything new or changed under source/. Returns files converted."""
    audio.require_tools()
    if not source.is_dir():
        raise FileNotFoundError(f"source folder not found: {source}")
    jobs: list[Job] = []
    for src_dir in sorted([source, *(p for p in source.rglob("*") if p.is_dir())]):
        rel = src_dir.relative_to(source)
        jobs += plan_folder(src_dir, content / rel, rel, log)

    def run(job: Job):
        # The manifest already records the new source mtime, so remove the
        # stale output first: if conversion fails, the next run retries it.
        job.dst.unlink(missing_ok=True)
        audio.convert(job.src, job.dst)
        return job

    with ThreadPoolExecutor(max_workers=workers) as pool:
        for job in pool.map(run, jobs):
            log(f"converted {job.src.relative_to(source)} -> {job.dst.relative_to(content)}")
    return len(jobs)
