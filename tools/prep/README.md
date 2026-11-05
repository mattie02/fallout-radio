# Content prep tool

Turns raw audio into the radio's SD card format and generates each station's weekly schedule. Python 3 standard library plus `ffmpeg` (`brew install ffmpeg`).

```bash
python3 tools/prep/prep.py                     # convert + schedule
python3 tools/prep/prep.py convert             # source/ -> content/ MP3s only
python3 tools/prep/prep.py schedule            # rebuild schedule.txt only
python3 tools/prep/prep.py schedule --station 01_crooner
python3 tools/prep/prep.py demo                # placeholder audio into source/
python3 tools/prep/make_boot_sound.py          # synthesized warm-up hum: source/system/boot.wav
python3 tools/prep/prep.py check /Volumes/CARD # is the loaded card ready?
python3 -m unittest discover -s tools/prep/tests
```

## Folders

- `source/` (not in git): raw audio in any format ffmpeg reads, mirroring the card layout, e.g. `source/stations/01_crooner/music/Blue Moon.flac`, `source/system/boot.wav`.
- `content/`: the SD card. `station.json` and `system/config.json` are hand-written and tracked; MP3s, `schedule.txt`, `clips.json` and `.prep.json` are generated and ignored by git.

## convert

Every audio file in `source/` becomes a mono, 44.1 kHz, CBR 128 kbps MP3, loudness-normalized to −16 LUFS (two-pass `loudnorm`), with no ID3 tags and no Xing header. That means the file is MP3 frames from byte 0 at exactly 16,000 bytes per second, so time *t* ms is at about byte *t* × 16.

Files inside `stations/<station>/<category>/` are named `NNN_name.mp3`: lowercase, `_` for anything else. A source named `05 Stardust.flac` keeps number 5; other files get the next free number. Numbers stay the same across runs (recorded in `.prep.json`), and names are shortened so `category/NNN_name.mp3` fits in 44 characters. Only new or changed files are converted. Deleting a source file deletes its MP3.

### Per-clip metadata

Optional `meta.json` next to the source files, keyed by source filename:

```json
{
  "Fair Day 1.wav": { "arc": "county_fair", "order": 1 },
  "Fair Day 2.wav": { "arc": "county_fair", "order": 2 },
  "Christmas.wav":  { "date": "12-25", "category": "holiday" }
}
```

- `arc` + `order`: clips in an arc always play in order, looping when the arc ends.
- `date` (`MM-DD`): kept out of the regular rotation, reserved for date-triggered broadcasts later.
- `category`: free-form label, passed through.

## schedule

`content/stations/<station>/station.json`:

```json
{
  "name": "Crooner Ballads",
  "frequency": 91.5,
  "seed": 1,
  "no_repeat_hours": { "music": 6, "*": 1 },
  "pattern": [
    { "pick": "music", "count": [2, 3] },
    { "pick": "ids" },
    { "pick": { "dj": 2, "ads": 1 }, "count": [1, 2] },
    { "silence": [20, 90], "chance": 0.1 }
  ]
}
```

- `pattern` repeats until the week is full. Each step either picks clips or inserts silence.
  - `pick`: a category folder name, a list of them (equal odds), or `{category: weight}`.
  - `count`: number of clips, either `n` or `[min, max]`.
  - `silence`: seconds of dead air, `n` or `[min, max]`.
  - `chance`: probability the step happens at all (default 1).
  - Categories with no MP3s are skipped.
- `daily` (optional): clips that start at a set time every day, e.g. `"daily": [{ "at": "18:00", "pick": "whistler", "count": 2 }]`.
  - `at` is 24-hour `HH:MM` local time; `pick` and `count` work as in `pattern`.
  - Whatever is playing at that time is cut short, and the pattern resumes when the slot's clips finish. A slot still running when the next one is due is cut short too.
  - The schedule repeats weekly, so a slot airs the same clips on the same weekday each week. To air a serial in order, number it as an arc in `meta.json`; only the first 7 × `count` parts will air.
- `no_repeat_hours`: a clip won't replay within this many hours, per category with `*` as the default. If there isn't enough audio to manage that, the least recently played clip is used (never the same clip twice in a row) and a warning is printed.
- `seed` (optional): the same content and seed always give the same schedule. Defaults to a hash of the folder name.

### schedule.txt format

Fixed-width 64-byte ASCII records, so the firmware can binary-search the file on the SD card without loading it all.

```
#VRSCHED1 008540 604800000                                     ← header: entry count, length in ms
000000000 00098038 music/002_demo_music_2.mp3                  ← start ms, duration ms, path
000098038 00173035 music/005_demo_music_5.mp3
```

- Record 0 is the header; entry *i* is at byte 64 × *i*. Each record is 63 characters plus `\n`.
- Start times are milliseconds since **Monday 00:00 local time**. The entries tile the week with no gaps, and the last one is cut off at the end of the week.
- Paths are relative to the station folder; `-` means silence.
- Playback: `ms = (now − most recent Monday 00:00) mod length`. Binary-search for the last entry with `start ≤ ms`, then seek to `ms − start` within that file.

## check

Run this after copying content to the card, before putting it in the radio. It doesn't need ffmpeg, and it exits with 1 if there are any errors. With no path it checks `content/`.

```bash
python3 tools/prep/prep.py check /Volumes/CARD
```

**Errors** mean the radio will misbehave:

- The card isn't FAT32, or `system/config.json` is missing or invalid.
- A station folder has a missing or invalid `station.json`, no frequency, a frequency off the dial, or the same frequency as another station.
- A station has audio but no `schedule.txt`, or its schedule is damaged or has gaps.
- A scheduled MP3 is missing, empty, cut short or not in the radio's format.
- `system/boot.mp3` is there but not in the radio's format.
- No station has any audio scheduled.
- More stations have schedules than the firmware has file slots for (`kSdMaxFiles`).
- A file in `content/` isn't on the card, or is a different size.

**Warnings** mean the radio will run, but something's missing:

- `system/boot.mp3` is missing, so there's no warm-up sound.
- A station has no content yet, or its schedule is all silence.
