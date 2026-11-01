"""Runs real ffmpeg; skipped when it isn't installed."""

import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from radioprep import audio  # noqa: E402


def ffmpeg(*args):
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", *args], check=True)


@unittest.skipUnless(shutil.which("ffmpeg") and shutil.which("ffprobe"), "needs ffmpeg")
class ConvertTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_mp3_with_corrupt_cover_art(self):
        # Stereo MP3 with embedded album art that doesn't decode, as found in
        # real downloads. The art must be ignored, not make ffmpeg fail.
        cover = self.dir / "cover.jpg"
        src = self.dir / "song.mp3"
        ffmpeg("-f", "lavfi", "-i", "color=c=red:s=64x64:d=1", "-frames:v", "1", str(cover))
        ffmpeg("-f", "lavfi", "-i", "sine=f=440:d=5", "-i", str(cover),
               "-map", "0:a", "-map", "1:v", "-ac", "2", "-c:a", "libmp3lame", "-c:v", "copy",
               "-id3v2_version", "3", "-disposition:v", "attached_pic", str(src))
        # Corrupt the JPEG inside the ID3 tag: garbage after its start marker.
        data = bytearray(src.read_bytes())
        soi = data.find(b"\xff\xd8")
        self.assertGreater(soi, 0)
        end = data.find(b"\xff\xd9", soi)
        data[soi + 2:end] = b"\xff\xe0\xbf\xfd" + bytes(end - soi - 6)
        src.write_bytes(data)

        dst = self.dir / "out.mp3"
        audio.convert(src, dst)

        info = audio.probe(dst)
        self.assertEqual(info.problems(), [])
        self.assertAlmostEqual(info.duration_ms, 5000, delta=100)
        self.assertEqual(dst.read_bytes()[:2], b"\xff\xfb")  # MP3 frame, no ID3 tag


if __name__ == "__main__":
    unittest.main()
