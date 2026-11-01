"""ffmpeg / ffprobe wrappers: convert to the radio's MP3 format and probe it."""

from __future__ import annotations

import json
import re
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path

SAMPLE_RATE = 44100
BITRATE = 128_000
TARGET_LUFS = -16.0
TRUE_PEAK = -1.5
LRA = 11.0

AUDIO_EXTENSIONS = {".wav", ".flac", ".mp3", ".m4a", ".aac", ".aif", ".aiff", ".ogg", ".opus"}


class AudioError(RuntimeError):
    pass


def require_tools():
    for tool in ("ffmpeg", "ffprobe"):
        if not shutil.which(tool):
            raise AudioError(f"{tool} not found: run `brew install ffmpeg`")


def _run(cmd: list[str]) -> subprocess.CompletedProcess:
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        tail = "\n".join(result.stderr.strip().splitlines()[-5:])
        raise AudioError(f"{cmd[0]} failed:\n{tail}")
    return result


def _loudnorm(extra: str = "") -> str:
    return f"loudnorm=I={TARGET_LUFS}:TP={TRUE_PEAK}:LRA={LRA}{extra}"


def measure_loudness(src: Path) -> dict:
    """First loudnorm pass: returns ffmpeg's measured values."""
    result = _run([
        "ffmpeg", "-hide_banner", "-nostats", "-i", str(src),
        "-map", "0:a:0", "-vn",  # skip embedded cover art
        "-af", "aformat=channel_layouts=mono," + _loudnorm(":print_format=json"),
        "-f", "null", "-",
    ])
    match = re.search(r"\{[^{}]*\"input_i\"[^{}]*\}", result.stderr, re.S)
    if not match:
        raise AudioError(f"no loudness measurement for {src}")
    return json.loads(match.group(0))


def convert(src: Path, dst: Path):
    """Converts to mono 44.1 kHz CBR 128 kbps MP3 at -16 LUFS, no tags.

    No ID3 or Xing/Info header either: the file is MP3 frames from byte 0 at
    16,000 bytes per second, so a time offset of t ms is near byte t * 16
    (within one frame; the decoder resyncs to the next frame header).
    """
    m = measure_loudness(src)
    if m["input_i"] in ("-inf", "inf"):
        norm = "volume=0"  # digital silence; nothing to normalize
    else:
        norm = _loudnorm(
            f":measured_I={m['input_i']}:measured_TP={m['input_tp']}"
            f":measured_LRA={m['input_lra']}:measured_thresh={m['input_thresh']}"
            f":offset={m['target_offset']}:linear=true")
    dst.parent.mkdir(parents=True, exist_ok=True)
    tmp = dst.with_suffix(".tmp.mp3")
    _run([
        "ffmpeg", "-hide_banner", "-nostats", "-y", "-i", str(src),
        "-map", "0:a:0", "-vn",
        "-af", f"aformat=channel_layouts=mono,{norm},aresample={SAMPLE_RATE}",
        "-ac", "1", "-ar", str(SAMPLE_RATE),
        "-c:a", "libmp3lame", "-b:a", "128k",
        "-map_metadata", "-1", "-id3v2_version", "0", "-write_id3v1", "0", "-write_xing", "0",
        str(tmp),
    ])
    tmp.replace(dst)


@dataclass(frozen=True)
class Probe:
    duration_ms: int
    codec: str
    channels: int
    sample_rate: int
    bit_rate: int

    def problems(self) -> list[str]:
        out = []
        if self.codec != "mp3":
            out.append(f"codec {self.codec}, expected mp3")
        if self.channels != 1:
            out.append(f"{self.channels} channels, expected mono")
        if self.sample_rate != SAMPLE_RATE:
            out.append(f"{self.sample_rate} Hz, expected {SAMPLE_RATE}")
        if abs(self.bit_rate - BITRATE) > 1000:
            out.append(f"{self.bit_rate // 1000} kbps, expected {BITRATE // 1000}")
        return out


def probe(path: Path) -> Probe:
    result = _run([
        "ffprobe", "-v", "error", "-select_streams", "a:0",
        "-show_entries", "stream=codec_name,channels,sample_rate,bit_rate:format=duration",
        "-of", "json", str(path),
    ])
    data = json.loads(result.stdout)
    stream = (data.get("streams") or [{}])[0]
    return Probe(
        duration_ms=int(round(float(data.get("format", {}).get("duration", 0)) * 1000)),
        codec=stream.get("codec_name", "?"),
        channels=int(stream.get("channels", 0)),
        sample_rate=int(stream.get("sample_rate", 0)),
        bit_rate=int(stream.get("bit_rate", 0) or 0),
    )
