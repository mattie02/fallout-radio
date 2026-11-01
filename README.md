# Vintage Radio

ESP32-S3 tabletop radio with "live" stations played from an SD card. See [CLAUDE.md](CLAUDE.md) for the full project guide.

## Build

```bash
pio run -e esp32s3            # firmware
pio run -e desktop            # macOS simulator (needs `brew install sdl2`)
pio run -e desktop -t exec    # build and run the simulator
pio test -e desktop           # unit tests (RADIO_DUMP=1 prints the display as ASCII)
```

## Content

Put raw audio in `source/stations/<station>/<category>/` (e.g. `source/stations/01_crooner/music/`), then:

```bash
python3 tools/prep/prep.py        # convert to content/ and build each schedule.txt
python3 tools/prep/prep.py demo   # or: generate placeholder audio first
```

Station rules live in `content/stations/*/station.json`. See [tools/prep/README.md](tools/prep/README.md).

## Simulator

```
.pio/build/desktop/program [--content DIR] [--time "2026-12-24 23:58"]
```

| Control | Action |
|---|---|
| Mouse wheel, ← → | Turn the dial 0.1 MHz (Shift + arrow: 1 MHz) |
| Click, Space | Encoder button |
| P | Pause / resume the clock |
| ] / [ | Clock faster / slower (1×, 10×, 60×, 600×, 3600×, 86400×) |
| R | Back to real time |
| Esc | Quit |

The window title shows the frequency and the simulated date and time. `--content` defaults to `./content`.
