# Vintage Radio — Project Guide

A vintage-style tabletop radio, built as a Christmas gift. An ESP32-S3 plays "radio stations" from an SD card. Each station feels live: it keeps broadcasting on a schedule driven by a real-time clock, even while you're tuned away or the radio is off. Turning the dial between stations gives static, and the station fades in through the static as you tune closer.

The vibe is a 1940s–50s post-apocalyptic radio, inspired by Fallout. All station names, DJs and personas are original; nothing is copied from the game.

**Deadline:** finished and working by Christmas 2026.

---

## Stations

| # | Station | MHz | Folder | Content |
|---|---|---|---|---|
| 1 | Crooner ballads | 91.5 | `01_crooner` | Cheerful DJ, general chatter |
| 2 | Galaxy News Radio | 95.3 | `02_news` | News reels, each clip self-contained (secretly about the recipient's life) |
| 3 | Big band / jazz | 99.7 | `03_bigband` | Parody ads |
| 4 | Country / western | 103.1 | `04_country` | Gravelly DJ |
| 5 | Late Night Talk | 88.9 | `05_talk` | Recordings of the talk show Coast to Coast AM, whole episodes back to back; no DJ, IDs or ads |
| 6 | Hidden | 107.9 | `99_hidden` | Emergency broadcast / numbers station at the far end of the dial, with a personal message |

DJ clips are general chatter, not intros tied to specific songs. Frequencies live in each `station.json`.

### Late Night Talk (`05_talk`)

- **Content:** full episodes of Coast to Coast AM, put in `source/stations/05_talk/shows/` in any format. The station's on-air name, "Late Night Talk", is original; the recordings themselves are the content.
- **Rules** (`station.json`): the pattern is a single step, `{ "pick": "shows" }`, so one whole episode plays after another with no breaks. `no_repeat_hours` is 72 for shows, which needs about 72 hours of episodes to hold; with fewer, the prep tool warns and episodes come around sooner.
- **Order:** random. To play in broadcast order, number the episodes as an arc in `shows/meta.json` (`"arc"` + `"order"`).
- **Size:** episodes are long (~3 h). At 128 kbps that's ~58 MB per hour, ~173 MB per episode. An 8 GB card holds about 40 episodes plus the music. Schedule entries allow up to 27.7 h, and FAT32 allows 4 GB per file, so length is no problem.
- **Prep time:** two-pass loudness normalization takes a few minutes per episode hour; the first big batch can take an hour or so. Later runs only convert new files.
- **Weekly wrap:** the schedule restarts Monday 00:00, cutting whichever episode is playing then.
- **The Whistler:** two parts of this 1942 radio drama air every day at 18:00 (`"daily"` in `station.json`), in order from `whistler/` via its `meta.json` arc, cutting whichever episode is playing. The week holds 14 parts, so Parts 1–14 are loaded; Parts 15–22 wait in `source-unused/`. Airing them needs a schedule longer than a week, which means a format and firmware change.

---

## Hardware

- **MCU:** ESP32-S3-WROOM-1 N16R8 dev board: 16 MB flash, 8 MB octal PSRAM (has a 5VIN pin and both COM/UART and native USB ports)
- **Displays:** 1.28" GC9A01 round TFT (240×240, RGB, 4-wire SPI), the dial face; and a 0.91" SSD1306 128×32 OLED, I2C, address 0x3C, showing the audio as a live waveform. Each is optional: firmware drives whichever is connected.
- **Input:** rotary encoder with push button (KY-040 style)
- **Storage:** micro SD module (SPI, onboard regulator and level shifter, so it **must be powered from 5V**)
- **Audio:** MAX98357A I2S amp, 5-pin board (G, V, BCL, LRC, DIN) → single 4–8 Ω speaker (mono)
- **Volume / power:** 10k linear pot with a built-in on/off switch (SW1)
- **Power input:** USB-C breakout (5.1 kΩ CC resistors) from a 5V ≥2A USB-C charger
- **Clock:** DS3231 + AT24C32 (ZS-042 board) with a CR2032; the charging resistor has been removed. DS3231 at 0x68, EEPROM at 0x57 (unused)

### Pin map

| Function | GPIO | Notes |
|---|---|---|
| I2C SDA | 8 | OLED + RTC share the bus |
| I2C SCL | 9 | OLED labels this pin "SCK" |
| Encoder A (CLK) | 11 | |
| Encoder B (DT) | 12 | |
| Encoder button (SW) | 13 | active low, INPUT_PULLUP |
| SD CS | 15 | |
| SD SCK | 16 | custom SPI pins via `SPI.begin(sck, miso, mosi, cs)` |
| SD MOSI | 17 | |
| SD MISO | 18 | |
| I2S BCLK | 5 | |
| I2S LRC (WS) | 6 | |
| I2S DOUT → amp DIN | 7 | |
| Volume pot wiper | 4 | ADC1 (works with Wi-Fi on); pot ends to 3V3 and GND; 0.1 µF from wiper to GND |
| TFT SCL (SPI clock) | 39 | "SCL/SDA" on this board mean SPI, not I2C |
| TFT SDA (SPI MOSI) | 40 | |
| TFT DC | 41 | |
| TFT CS | 42 | |
| TFT RST | 21 | |
| TFT BLK (backlight) | 14 | PWM brightness via LEDC, if the board has this pin |

Power: a USB-C charger (5V, 2A or more) feeds a USB-C breakout (with 5.1 kΩ CC resistors). Its 5V goes through **SW1, the on/off switch built into the volume pot**, to the 5V rail: 5VIN, amp V and SD VCC. Ground is never switched. OLED, TFT, encoder, RTC and the volume pot run from **3V3**. All grounds are shared. The amp is a 5-pin MAX98357A board (G, V, BCL, LRC, DIN) with gain and channel mode fixed on the board. Bulk capacitors: C1 1000 µF across amp V/G, C2 100 µF across SD VCC/GND.

**Pins to avoid:** 0, 3, 45, 46 (strapping pins); 19 and 20 (native USB); 26–32 (flash); 33–37 (octal PSRAM on N8R8/N16R8 modules); 43 and 44 (UART0 / COM port); 38 and 48 (onboard RGB LED on DevKitC-1 boards).

### Controls

- **Volume pot (10k linear, with switch):** clicks the radio on at the bottom of its travel. Firmware reads GPIO 4, averages and applies hysteresis, treats the ends as hard 0% and 100%, and maps position to volume with a logarithmic (perceptual) curve. There is no other volume control.
- **Rotary encoder:** tuning only. There's no physical dial pointer; the screen is the dial. The last dial position is saved to NVS (ESP32 flash) 3 seconds after tuning stops and restored on boot; the simulator keeps it in `.pio/simulator_dial.txt`. The encoder button is free for another feature.
- **Power cycling is normal use:** the pot switch cuts all power. Never write to the SD card. Boot fast, fade audio in, and cover the boot time with a warm-up hum. Clock-based stations resume "live" automatically.

### Round TFT display

- It runs on its **own SPI bus (SPI3)**, separate from the SD card's bus, so screen redraws never block SD reads and cause audio dropouts.
- Library: LovyanGFX. `ui/RoundDial` draws into sprites in PSRAM (static face + frame) and is shared with the simulator, so the simulator shows the panel's exact pixels.
- Drawing and pushing take ~20 ms, so a FreeRTOS task on core 0 does it (`platform/esp32/Esp32Tft`); the audio loop on core 1 only hands over the latest `DisplayState`. Only rows that changed are sent.
- Panel settings that depend on the wiring or the module are in `Pins.h`: SPI speed (lower it if the image speckles) and color inversion.
- Remaining spare GPIOs: 1, 2, 10 (ADC-capable) and 47. Avoid 38 and 48 (the onboard RGB LED on DevKitC-1 boards).

---

## Architecture

The core logic is plain, hardware-independent C++17. A thin platform layer has two implementations, so the same core runs on the ESP32 and as a desktop app on macOS.

```
src/
  core/            # NO Arduino or platform headers here
    Schedule       # station schedule lookup: (station, time) -> file + offset
    Tuner          # dial position -> station + signal strength 0..1
    Mixer          # decoded station audio + generated static, mixed by signal strength
    Effects        # vintage EQ / band-pass, crackle, volume
    Radio          # ties it together; the main state machine
  ui/              # how each screen looks: DisplayState -> pixels. Shared by both targets, no hardware access
    Waveform       # the 128x32 OLED: an oscilloscope trace of the audio, on Framebuffer
    RoundDial      # the 240x240 round TFT, with LovyanGFX sprites
  platform/
    Platform.h     # interfaces: AudioOut, Input, Display, Clock, Storage
    esp32/         # I2S, encoder (ESP32Encoder / PCNT), volume pot (ADC), SSD1306, GC9A01, DS3231 (RTClib), SD, NVS
    desktop/       # audio to speakers, keyboard/mouse dial, a window showing either screen, controllable clock, local folder
tools/
  prep/            # Python content-prep and schedule generator (runs on the Mac)
content/           # the source "SD card" folder (desktop reads it directly; gets copied to the card)
```

### What runs on which core (ESP32)

- **Core 1, Arduino loop:** input, SD reads, MP3 decoding, mixing, I2S. Audio stays together here, on one thread, with no locks. Measured at about 24% of the core playing a station (decode and SD 12%, mixing 11%), and its worst stall (~15 ms) is well inside the ~139 ms output buffer. Don't split it up without a measured reason.
- **Core 0, screen tasks:** the round dial and the waveform. Both take many milliseconds per frame on their buses, so neither may run on the audio loop. The audio loop only hands them the latest state or samples, inside a very short critical section.
- The I2C bus is shared between the OLED task and the RTC on the loop; the Wire library's lock covers that.

### PlatformIO environments

- `esp32s3`: `board = esp32-s3-devkitc-1`, Arduino framework, octal PSRAM enabled.
- `desktop`: `platform = native`. Uses SDL2 for the window, input and audio (`brew install sdl2`). On Apple Silicon, add `-I/opt/homebrew/include -L/opt/homebrew/lib`.

MP3 decoding should use the same decoder on both targets (for example, the decoder used by arduino-audio-tools, or a header-only decoder such as minimp3), so audio behaves identically.

### Desktop simulator controls

- Mouse wheel / ← → : turn the dial. Click / Space : encoder button.
- ↑ ↓ : volume pot. P : power switch (simulates a full power cycle and boot).
- **Time controls** (essential for testing the schedule): pause the clock, fast-forward, and jump to a specific date and time (e.g. Dec 24 23:58).
- Display: the round TFT dial at ×2 or the OLED waveform at ×4. D switches between them; `--screen round|oled` picks one at startup.

---

## Content

### SD card layout

```
/system/
  config.json            # dial range, station frequencies, signal falloff
  boot.mp3               # optional tube warm-up sound
/stations/
  01_crooner/
    station.json         # name, frequency, break rules
    schedule.txt         # GENERATED by tools/prep — do not hand-edit
    music/  dj/  ids/  ads/
  02_news/
    station.json
    schedule.txt
    news/  ids/  music/
  03_bigband/ ...
  04_country/ ...
  05_talk/
    station.json
    schedule.txt
    shows/               # whole talk-show episodes
    whistler/            # radio drama, played by a daily slot
  99_hidden/ ...
```

Static is **generated in code** (filtered noise), not stored as files.

### Audio format (enforced by the prep tool)

- MP3, **mono**, 44.1 kHz, **CBR 128 kbps**. CBR means a time offset maps directly to a byte offset (16,000 bytes/s), which makes seeking instant.
- Loudness normalized across all files (target around −16 LUFS) so bumpers and music sit at the same level.
- Metadata / ID3 tags removed.
- Filenames: lowercase, no spaces, numeric prefix (`001_song_name.mp3`).

### Schedules: how "live" works

- The prep tool generates a long schedule for each station: **one week, aligned to Monday 00:00 local time**. It follows the station's rules: 2–3 songs, then a break (ID / DJ / ad); no song repeats within N hours; and so on.
- Each schedule line is a fixed-width record: start offset, duration and file path. Fixed width lets the firmware binary-search the file on SD without loading all of it.
- Playback position = (current local time − most recent Monday 00:00), wrapped into the schedule length. The firmware finds the entry, seeks to the offset within it and plays. No playback state is stored anywhere.
- Aligning to the week makes time-of-day and day-of-week programming possible later.
- Times are stored in **the recipient's local time** in the RTC.

---

## Conventions

- `core/` never includes Arduino, ESP-IDF or SDL headers. If something needs hardware, add an interface to `Platform.h`. Exception: ArduinoJson, which is portable C++ and used on both targets for the JSON config files.
- `core/` decides what to show (`DisplayState`, plus the audio itself for the waveform, before the volume knob so the trace doesn't shrink as you turn it down), `ui/` decides how it looks. `core/` doesn't include `ui/`.
- Never block the audio path. `Radio::update()` is called often; the display only redraws on change.
- Keep the desktop and ESP32 builds both compiling at all times.
- Prefer small, testable pieces. Schedule lookup and Tuner math should have unit tests (`pio test -e desktop`).

---

## Build plan

1. **Skeleton:** PlatformIO project, both environments, platform interfaces, the desktop window showing the 128×32 display.
2. **Prep tool:** convert and normalize content; generate `schedule.txt` from `station.json` rules.
3. **Schedule + Tuner:** time → file/offset; dial → station + signal strength. Unit tests.
4. **Audio:** MP3 playback with seeking, generated static, mixing by signal strength, vintage EQ. Tune by ear on the desktop.
5. **ESP32 platform layer:** port to hardware (the pin and wiring test sketches already work).
6. **Polish:** boot sound, display design, the hidden station, special broadcasts.
7. **Enclosure integration:** 3D-printed case (must fit a Bambu printer), USB-C power input, volume/power knob.
8. **Round display:** GC9A01 TFT dial face (done, alongside the OLED).

---

## Open decisions

- **More time-of-day programming?** e.g. slower songs late at night, the hidden station only after midnight, or Late Night Talk airing only overnight (the real show ran late at night) and dead air by day. The prep tool's `daily` slots cover fixed daily times; these would need more rules.
- **Talk episode order:** random for now; broadcast order via a `meta.json` arc if wanted.
- **News strategy:** self-contained clips for now. Possible later: news at the top of the hour, date-triggered bulletins (birthday, Christmas morning), multi-day story arcs. To keep these possible, the prep tool should accept per-clip metadata (category, optional date, optional arc + order).
- **Dial range and display style:** FM-style 88–108 vs. an AM-style range; what the OLED shows (frequency, station name, signal meter).
- **Encoder button:** what it should do (e.g. show the station name and time, or a hidden feature).
