#!/usr/bin/env python3
"""Vintage Radio content prep. See tools/prep/README.md.

    python3 tools/prep/prep.py                 convert + schedule (default)
    python3 tools/prep/prep.py convert         source/ -> content/ MP3s
    python3 tools/prep/prep.py schedule        content/ -> schedule.txt
    python3 tools/prep/prep.py demo            placeholder audio into source/
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from radioprep import audio  # noqa: E402
from radioprep.content import convert_tree  # noqa: E402
from radioprep.demo import make_demo  # noqa: E402
from radioprep.schedule import RuleError  # noqa: E402
from radioprep.stations import build_schedule, station_dirs  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Vintage Radio content prep")
    parser.add_argument("command", nargs="?", default="all",
                        choices=["all", "convert", "schedule", "demo"])
    parser.add_argument("--source", type=Path, default=ROOT / "source",
                        help="raw audio folder (default: source/)")
    parser.add_argument("--content", type=Path, default=ROOT / "content",
                        help="SD card folder (default: content/)")
    parser.add_argument("--station", help="only schedule this station, e.g. 01_crooner")
    parser.add_argument("-j", "--jobs", type=int, default=4, help="parallel conversions")
    args = parser.parse_args(argv)

    try:
        audio.require_tools()
        if args.command == "demo":
            make_demo(args.source, print)
            print("now run: python3 tools/prep/prep.py")
            return 0
        if args.command in ("all", "convert"):
            n = convert_tree(args.source, args.content, print, workers=args.jobs)
            print(f"{n} file(s) converted")
        if args.command in ("all", "schedule"):
            failed = 0
            for station in station_dirs(args.content, args.station):
                try:
                    n = build_schedule(station, print)
                    print(f"{station.name}: {n} entries")
                except RuleError as e:
                    print(f"error: {station.name}: {e}", file=sys.stderr)
                    failed += 1
            return 1 if failed else 0
    except (audio.AudioError, FileNotFoundError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
