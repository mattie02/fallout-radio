import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from radioprep.content import MANIFEST, META_OUT, numbered_name, plan_folder, sanitize  # noqa: E402
from radioprep.schedule import PATH_WIDTH  # noqa: E402


class NamingTest(unittest.TestCase):
    def test_sanitize(self):
        self.assertEqual(sanitize("Moonlight Serenade (1939)"), "moonlight_serenade_1939")
        self.assertEqual(sanitize("Café Rêverie"), "cafe_reverie")
        self.assertEqual(sanitize("!!!"), "clip")

    def test_numbered_name_fits_record(self):
        name = numbered_name("music", 7, "a" * 100)
        self.assertTrue(name.startswith("007_") and name.endswith(".mp3"))
        self.assertEqual(len("music/" + name), PATH_WIDTH)


class PlanFolderTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.rel = Path("stations/01_crooner/music")
        self.src = root / "source" / self.rel
        self.dst = root / "content" / self.rel
        self.src.mkdir(parents=True)
        self.log = []

    def tearDown(self):
        self.tmp.cleanup()

    def touch(self, name, data=b"x"):
        (self.src / name).write_bytes(data)

    def plan(self):
        jobs = plan_folder(self.src, self.dst, self.rel, self.log.append)
        for job in jobs:  # pretend conversion succeeded
            job.dst.parent.mkdir(parents=True, exist_ok=True)
            job.dst.write_bytes(b"mp3")
        return {j.src.name: j.dst.name for j in jobs}

    def test_numbers_new_files_and_keeps_given_prefixes(self):
        self.touch("Blue Moon.wav")
        self.touch("05 Stardust.flac")
        self.touch("notes.txt")
        self.assertEqual(self.plan(), {
            "05 Stardust.flac": "005_stardust.mp3",
            "Blue Moon.wav": "006_blue_moon.mp3",
        })

    def test_names_stable_and_unchanged_files_skipped(self):
        self.touch("b.wav")
        self.plan()
        self.touch("a.wav")  # sorts first, but must not renumber b
        self.assertEqual(self.plan(), {"a.wav": "002_a.mp3"})
        self.assertEqual(self.plan(), {})
        self.touch("b.wav", b"changed")
        self.assertEqual(self.plan(), {"b.wav": "001_b.mp3"})

    def test_removed_source_deletes_output(self):
        self.touch("a.wav")
        self.plan()
        (self.src / "a.wav").unlink()
        self.plan()
        self.assertFalse((self.dst / "001_a.mp3").exists())
        self.assertEqual(json.loads((self.dst / MANIFEST).read_text()), {"sources": {}})

    def test_metadata_rekeyed_to_output_names(self):
        self.touch("Fair Day 1.wav")
        (self.src / "meta.json").write_text(json.dumps({
            "Fair Day 1.wav": {"arc": "fair", "order": 1, "mood": "x"},
            "missing.wav": {"date": "12-25"},
        }))
        self.plan()
        meta = json.loads((self.dst / META_OUT).read_text())
        self.assertEqual(meta, {"001_fair_day_1.mp3": {"arc": "fair", "order": 1}})
        self.assertTrue(any("missing.wav" in line for line in self.log))
        self.assertTrue(any("mood" in line for line in self.log))

    def test_system_files_not_numbered(self):
        rel = Path("system")
        src, dst = self.src.parents[2] / "system", self.dst.parents[2] / "system"
        src.mkdir()
        (src / "Boot.wav").write_bytes(b"x")
        jobs = plan_folder(src, dst, rel, self.log.append)
        self.assertEqual([j.dst.name for j in jobs], ["boot.mp3"])


if __name__ == "__main__":
    unittest.main()
