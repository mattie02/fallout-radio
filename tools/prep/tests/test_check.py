import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from radioprep.check import MAX_SCHEDULED_STATIONS, check_card, print_report  # noqa: E402
from radioprep.schedule import DAY_MS, WEEK_MS, Entry, format_schedule  # noqa: E402

FRAME = bytes([0xFF, 0xFB, 0x90, 0xC4])
CLIP_MS = 1000


def week(first: Entry | None = None, gap_ms: int = 0) -> list[Entry]:
    """Fills the week with silence after an optional first entry (schedule entries max out at 27.7 h)."""
    entries = [first] if first else []
    t = (first.duration_ms if first else 0) + gap_ms
    while t < WEEK_MS:
        duration = min(DAY_MS, WEEK_MS - t)
        entries.append(Entry(t, duration, "-"))
        t += duration
    return entries


def song() -> Entry:
    return Entry(0, CLIP_MS, "music/001_song.mp3")


def write_mp3(path: Path, ms: int = CLIP_MS):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(FRAME + bytes(max(ms * 16 - 4, 0)))


def write_station(card: Path, name: str, frequency: float, with_audio: bool = True):
    folder = card / "stations" / name
    folder.mkdir(parents=True)
    (folder / "station.json").write_text(json.dumps({"name": name, "frequency": frequency}))
    if with_audio:
        write_mp3(folder / "music" / "001_song.mp3")
        (folder / "schedule.txt").write_text(format_schedule(week(song())))
    return folder


class CheckCardTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.card = Path(tmp.name) / "card"
        (self.card / "system").mkdir(parents=True)
        (self.card / "system" / "config.json").write_text('{"dial": {"min": 88, "max": 108}}')
        write_mp3(self.card / "system" / "boot.mp3")
        self.station = write_station(self.card, "01_crooner", 91.5)

    def errors(self, **kwargs):
        return "\n".join(check_card(self.card, **kwargs).errors)

    def warnings(self):
        return "\n".join(check_card(self.card).warnings)

    def test_complete_card_is_clean(self):
        report = check_card(self.card)
        self.assertEqual(report.errors, [])
        self.assertEqual(report.warnings, [])
        self.assertIn("1 files", report.stations[0])

    def test_missing_boot_sound_warns_but_is_not_an_error(self):
        (self.card / "system" / "boot.mp3").unlink()
        self.assertIn("boot.mp3 is missing", self.warnings())
        self.assertEqual(self.errors(), "")

    def test_wrong_format_boot_sound_is_an_error(self):
        (self.card / "system" / "boot.mp3").write_bytes(b"RIFFnotanmp3")
        self.assertIn("boot.mp3", self.errors())

    def test_missing_config_is_an_error(self):
        (self.card / "system" / "config.json").unlink()
        self.assertIn("config.json is missing", self.errors())

    def test_bad_config_is_an_error(self):
        (self.card / "system" / "config.json").write_text("{oops")
        self.assertIn("config.json isn't valid JSON", self.errors())

    def test_station_without_config_file_is_an_error(self):
        (self.station / "station.json").unlink()
        (self.station / "schedule.txt").unlink()
        self.assertIn("no station.json", self.errors())

    def test_missing_scheduled_file_is_an_error(self):
        (self.station / "music" / "001_song.mp3").unlink()
        self.assertIn("music/001_song.mp3", self.errors())

    def test_truncated_file_is_an_error(self):
        (self.station / "music" / "001_song.mp3").write_bytes(FRAME + bytes(100))
        self.assertIn("cut short", self.errors())

    def test_id3_tag_is_an_error(self):
        (self.station / "music" / "001_song.mp3").write_bytes(b"ID3" + bytes(20000))
        self.assertIn("ID3", self.errors())

    def test_stereo_file_is_an_error(self):
        path = self.station / "music" / "001_song.mp3"
        path.write_bytes(bytes([0xFF, 0xFB, 0x90, 0x04]) + bytes(20000))
        self.assertIn("mono", self.errors())

    def test_audio_without_schedule_is_an_error(self):
        (self.station / "schedule.txt").unlink()
        self.assertIn("no schedule.txt", self.errors())

    def test_station_with_nothing_yet_warns(self):
        write_station(self.card, "03_bigband", 99.7, with_audio=False)
        self.assertIn("03_bigband", self.warnings())
        self.assertEqual(self.errors(), "")

    def test_all_silence_schedule_warns(self):
        folder = write_station(self.card, "99_hidden", 107.9, with_audio=False)
        (folder / "schedule.txt").write_text(format_schedule(week()))
        self.assertIn("all silence", self.warnings())

    def test_card_with_nothing_to_play_is_an_error(self):
        (self.station / "schedule.txt").write_text(format_schedule(week()))
        self.assertIn("will play nothing", self.errors())

    def test_non_numeric_dial_uses_defaults(self):
        (self.card / "system" / "config.json").write_text('{"dial": {"min": "low"}}')
        self.assertEqual(self.errors(), "")

    def test_truncated_schedule_is_an_error(self):
        path = self.station / "schedule.txt"
        path.write_bytes(path.read_bytes()[:-64])
        self.assertIn("schedule.txt is", self.errors())

    def test_schedule_gap_is_an_error(self):
        (self.station / "schedule.txt").write_text(format_schedule(week(song(), gap_ms=5)))
        self.assertIn("gap or overlap", self.errors())

    def test_short_schedule_is_an_error(self):
        (self.station / "schedule.txt").write_text(format_schedule([song()]))
        self.assertIn("short of the week", self.errors())

    def test_frequency_outside_dial_is_an_error(self):
        write_station(self.card, "02_news", 120.0)
        self.assertIn("outside the dial", self.errors())

    def test_shared_frequency_is_an_error(self):
        write_station(self.card, "02_news", 91.5)
        self.assertIn("shares 91.5", self.errors())

    def test_more_stations_than_file_slots_is_an_error(self):
        for i in range(MAX_SCHEDULED_STATIONS):
            write_station(self.card, f"1{i}_extra", 92.0 + i)
        self.assertIn("kSdMaxFiles", self.errors())

    def test_limit_itself_is_fine(self):
        for i in range(MAX_SCHEDULED_STATIONS - 1):
            write_station(self.card, f"1{i}_extra", 92.0 + i)
        self.assertEqual(self.errors(), "")

    def test_wrong_file_system_is_an_error(self):
        self.assertIn("exFAT", self.errors(filesystem="exFAT"))
        self.assertEqual(self.errors(filesystem="MS-DOS (FAT32)"), "")

    def test_card_not_found(self):
        self.assertIn("mounted", "\n".join(check_card(self.card / "nope").errors))

    def test_compares_with_the_source_folder(self):
        reference = self.card.parent / "content"
        write_station(reference, "01_crooner", 91.5)
        write_mp3(reference / "stations" / "01_crooner" / "music" / "002_new.mp3")
        (reference / "stations" / "01_crooner" / "clips.json").write_text("{}")
        (reference / "stations" / "01_crooner" / "._junk").write_text("x")
        (reference / "system").mkdir()
        (reference / "system" / "config.json").write_text("{}")
        errors = self.errors(reference=reference)
        self.assertIn("stations/01_crooner/music/002_new.mp3", errors)
        self.assertIn("differ in size", errors)  # config.json
        self.assertNotIn("clips.json", errors)
        self.assertNotIn("_junk", errors)

    def test_exit_code(self):
        lines = []
        self.assertEqual(print_report(self.card, check_card(self.card), lines.append), 0)
        (self.station / "schedule.txt").unlink()
        self.assertEqual(print_report(self.card, check_card(self.card), lines.append), 1)
        self.assertIn("NOT READY", lines[-1])


if __name__ == "__main__":
    unittest.main()
