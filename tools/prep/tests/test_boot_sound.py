import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import make_boot_sound as boot  # noqa: E402


class BootSoundTest(unittest.TestCase):
    def rms(self, samples, start, end):
        part = samples[int(start * boot.RATE):int(end * boot.RATE)]
        return (sum(s * s for s in part) / len(part)) ** 0.5

    def test_swells_up_then_dies_away(self):
        s = boot.synthesize()
        self.assertEqual(len(s), int(boot.RATE * boot.SECONDS))
        self.assertLessEqual(max(abs(x) for x in s), boot.PEAK + 1e-9)
        start = self.rms(s, 0, 0.3)
        full = self.rms(s, 3.0, 3.4)
        end = self.rms(s, 4.8, 5.0)
        self.assertLess(start, full * 0.2)
        self.assertLess(end, full * 0.1)

    def test_same_every_run(self):
        self.assertEqual(boot.synthesize()[:2000], boot.synthesize()[:2000])


if __name__ == "__main__":
    unittest.main()
