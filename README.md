# Vintage Radio

ESP32-S3 tabletop radio with "live" stations played from an SD card. See [CLAUDE.md](CLAUDE.md) for the full project guide.

## Build

```bash
pio run -e esp32s3            # firmware
pio run -e desktop            # macOS simulator (needs `brew install sdl2`)
pio run -e desktop -t exec    # build and run the simulator
pio test -e desktop           # unit tests (RADIO_DUMP=DIR writes both screens as images)
```

## Content

Put raw audio in `source/stations/<station>/<category>/` (e.g. `source/stations/01_crooner/music/`), then:

```bash
python3 tools/prep/prep.py        # convert to content/ and build each schedule.txt
python3 tools/prep/prep.py demo   # or: generate placeholder audio first
```

Station rules live in `content/stations/*/station.json`. See [tools/prep/README.md](tools/prep/README.md).

## Hardware

1. **SD card:** format it FAT32, then copy `content/` onto it so `stations/` and `system/` sit at the card's root. The extras rsync skips are only needed by the prep tool. The `._` files macOS adds are ignored by the radio.

   ```bash
   rsync -av --delete --exclude='.prep.json' --exclude='clips.json' content/ /Volumes/<CARD>/
   ```

2. **Flash:** plug into the board's COM/UART port.

   ```bash
   pio run -e esp32s3 -t upload
   pio device monitor -e esp32s3
   ```

3. **Set the clock:** the monitor prints a boot report (time, stations found). If the RTC battery was ever removed, the time starts at the firmware's build time. Set it in the monitor (recipient's local time):

   ```
   time 2026-12-24 23:58
   status
   ```

If the dial moves two stations per click, or turns the wrong way, change `kEncoderCountsPerStep` or `kEncoderReverse` in `src/platform/esp32/Pins.h`.

## Simulator

```
.pio/build/desktop/program [--content DIR] [--time "2026-12-24 23:58"] [--screen round|oled]
```

| Control | Action |
|---|---|
| Mouse wheel, ← → | Turn the dial 0.1 MHz (Shift + arrow: 1 MHz) |
| Click, Space | Encoder button |
| D | Switch between the round TFT and the OLED |
| P | Pause / resume the clock |
| ] / [ | Clock faster / slower (1×, 10×, 60×, 600×, 3600×, 86400×) |
| R | Back to real time |
| C | Reload `system/config.json` (tune the sound without restarting) |
| Esc | Quit |

### Tuning the sound

The `audio` section of `content/system/config.json` sets the sound. Edit it while the simulator runs and press C to hear the change:

| Key | Default | What it does |
|---|---|---|
| `volume` | 0.7 | Master volume, 0–1 |
| `static_level` | 0.5 | Static loudness between stations |
| `hiss` | 0.03 | Static left when fully tuned |
| `crackle_per_sec` | 6 | Pops per second in the static |
| `eq_low_hz` / `eq_high_hz` | 250 / 3800 | The "old speaker" band-pass |
| `drive` | 1.4 | Warm saturation on peaks (1 = clean) |

`signal.lock_mhz` and `signal.fade_mhz` set how close you need to be to a station, and how quickly it fades into static.

The window title shows what the radio is doing: frequency, the station in range and its signal strength, the file playing on it with the position (e.g. `music/012_blue_moon.mp3 1:23/3:05`), and the simulated date, time and speed. Use it with the clock keys to check schedules. `--content` defaults to `./content`.
