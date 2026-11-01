import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from radioprep.schedule import (  # noqa: E402
    RECORD_SIZE, SILENCE, WEEK_MS, Clip, RuleError, format_schedule, generate, parse_pattern,
    parse_schedule,
)

HOUR = 3600 * 1000


def songs(n, minutes=3, category="music"):
    return [Clip(f"{category}/{i:03d}_s.mp3", category, minutes * 60 * 1000) for i in range(1, n + 1)]


class GenerateTest(unittest.TestCase):
    def assert_tiles(self, entries, length=WEEK_MS):
        t = 0
        for e in entries:
            self.assertEqual(e.start_ms, t)
            self.assertGreater(e.duration_ms, 0)
            t += e.duration_ms
        self.assertEqual(t, length)

    def test_covers_week_exactly(self):
        rules = {"pattern": [{"pick": "music"}]}
        entries = generate(songs(5, minutes=7), rules, seed=1)
        self.assert_tiles(entries)
        self.assertLess(entries[-1].duration_ms, 7 * 60 * 1000 + 1)

    def test_deterministic_for_seed(self):
        rules = {"pattern": [{"pick": "music", "count": [1, 3]}]}
        a = generate(songs(10), rules, seed=7, length_ms=6 * HOUR)
        b = generate(songs(10), rules, seed=7, length_ms=6 * HOUR)
        c = generate(songs(10), rules, seed=8, length_ms=6 * HOUR)
        self.assertEqual(a, b)
        self.assertNotEqual(a, c)

    def test_no_repeat_window_respected_when_possible(self):
        # 40 three-minute songs = 2 h of music; a 1.5 h window is satisfiable.
        rules = {"pattern": [{"pick": "music"}], "no_repeat_hours": {"music": 1.5}}
        entries = generate(songs(40), rules, seed=3, length_ms=24 * HOUR)
        last = {}
        for e in entries[:-1]:
            if e.path in last:
                self.assertGreaterEqual(e.start_ms - last[e.path], 1.5 * HOUR)
            last[e.path] = e.start_ms

    def test_small_pool_falls_back_without_back_to_back(self):
        rules = {"pattern": [{"pick": "music"}], "no_repeat_hours": 6}
        entries = generate(songs(3), rules, seed=1, length_ms=2 * HOUR)
        for a, b in zip(entries, entries[1:]):
            self.assertNotEqual(a.path, b.path)

    def test_pattern_blocks_and_breaks(self):
        clips = songs(20) + songs(3, minutes=1, category="ids") + songs(4, minutes=1, category="dj")
        rules = {"pattern": [
            {"pick": "music", "count": [2, 3]},
            {"pick": "ids"},
            {"pick": "dj"},
        ]}
        entries = generate(clips, rules, seed=2, length_ms=12 * HOUR)
        cats = "".join(e.path[0] for e in entries[:-3])  # m, i, d
        for block in cats.split("i")[1:]:
            self.assertTrue(block.startswith("d"), cats)
            music = block[1:]
            if music:  # trailing partial block is fine
                self.assertIn(len(music), (2, 3), cats)

    def test_weighted_pick(self):
        clips = songs(5, minutes=1, category="dj") + songs(5, minutes=1, category="ads")
        rules = {"pattern": [{"pick": {"dj": 9, "ads": 1}}]}
        entries = generate(clips, rules, seed=4, length_ms=24 * HOUR)
        dj = sum(e.path.startswith("dj/") for e in entries)
        self.assertGreater(dj / len(entries), 0.8)

    def test_missing_category_is_skipped(self):
        rules = {"pattern": [{"pick": "music"}, {"pick": "ads"}]}
        entries = generate(songs(5), rules, seed=1, length_ms=HOUR)
        self.assertTrue(all(e.path.startswith("music/") for e in entries))

    def test_silence_steps(self):
        rules = {"pattern": [{"pick": "broadcast"}, {"silence": [10, 20]}]}
        entries = generate(songs(2, minutes=1, category="broadcast"), rules, seed=1, length_ms=HOUR)
        silences = [e for e in entries if e.path == SILENCE]
        self.assertTrue(silences)
        for s in silences[:-1]:
            self.assertTrue(10_000 <= s.duration_ms <= 20_000)

    def test_arcs_play_in_order_and_dated_clips_skipped(self):
        clips = [
            Clip("news/001_a.mp3", "news", 60_000),
            Clip("news/002_b.mp3", "news", 60_000),
            Clip("news/010_fair3.mp3", "news", 60_000, arc="fair", order=3),
            Clip("news/011_fair1.mp3", "news", 60_000, arc="fair", order=1),
            Clip("news/012_fair2.mp3", "news", 60_000, arc="fair", order=2),
            Clip("news/020_xmas.mp3", "news", 60_000, date="12-25"),
        ]
        entries = generate(clips, {"pattern": [{"pick": "news"}]}, seed=5, length_ms=4 * HOUR)
        fair = [e.path for e in entries if "fair" in e.path]
        expected = ["news/011_fair1.mp3", "news/012_fair2.mp3", "news/010_fair3.mp3"]
        self.assertEqual(fair[:6], expected * 2)
        self.assertFalse(any("xmas" in e.path for e in entries))

    def test_long_shows_play_whole(self):
        # Talk station: 25 three-hour shows back to back, none repeated within 72 h.
        shows = songs(25, minutes=180, category="shows")
        rules = {"pattern": [{"pick": "shows"}], "no_repeat_hours": {"shows": 72}}
        entries = generate(shows, rules, seed=1)
        self.assert_tiles(entries)
        for e in entries[:-1]:
            self.assertEqual(e.duration_ms, 180 * 60 * 1000)
        last = {}
        for e in entries[:-1]:
            if e.path in last:
                self.assertGreaterEqual(e.start_ms - last[e.path], 72 * HOUR)
            last[e.path] = e.start_ms
        self.assertEqual(len(format_schedule(entries)), RECORD_SIZE * (len(entries) + 1))

    def test_empty_pattern_errors(self):
        with self.assertRaises(RuleError):
            generate([], {"pattern": [{"pick": "music"}]}, seed=1, length_ms=HOUR)
        with self.assertRaises(RuleError):
            parse_pattern([{"count": 2}])
        with self.assertRaises(RuleError):
            parse_pattern([{"pick": "music", "count": [3, 1]}])


class FormatTest(unittest.TestCase):
    def test_round_trip_and_fixed_width(self):
        entries = generate(songs(4), {"pattern": [{"pick": "music"}]}, seed=1, length_ms=HOUR)
        text = format_schedule(entries, HOUR)
        self.assertEqual(len(text), RECORD_SIZE * (len(entries) + 1))
        self.assertTrue(all(len(line) == RECORD_SIZE - 1 for line in text.split("\n")[:-1]))
        self.assertEqual(parse_schedule(text), (HOUR, entries))

    def test_rejects_long_path(self):
        clip = Clip("music/" + "x" * 40 + ".mp3", "music", 1000)
        entries = generate([clip], {"pattern": [{"pick": "music"}]}, seed=1, length_ms=1000)
        with self.assertRaises(RuleError):
            format_schedule(entries, 1000)


if __name__ == "__main__":
    unittest.main()
