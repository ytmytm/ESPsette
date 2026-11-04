import gzip
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from ota_image import ota_image, verify_ota_image, rounded, ONE_MB_FS_START


class OtaImageTests(unittest.TestCase):
    def test_deterministic_lossless_compression_and_budget(self):
        raw = b'\xe9' + bytes(range(256)) * 2300
        compressed, budget = ota_image(raw)
        self.assertEqual(gzip.decompress(compressed), raw)
        self.assertEqual(ota_image(raw)[0], compressed)
        self.assertEqual(compressed[9], 255)
        self.assertEqual(budget['margin_bytes'], ONE_MB_FS_START - rounded(len(raw)) - rounded(len(compressed)))

    def test_sector_boundary(self):
        raw = b'\xe9' + b'x' * 8191
        compressed, _ = ota_image(raw)
        limit = rounded(len(raw)) + rounded(len(compressed))
        self.assertEqual(ota_image(raw, limit)[1]['margin_bytes'], 0)
        with self.assertRaises(ValueError):
            ota_image(raw, limit - 1)

    def test_oversized_or_invalid_image_is_rejected(self):
        for data in (b'', b'not firmware', b'\xe9' + b'x' * ONE_MB_FS_START):
            with self.subTest(size=len(data)), self.assertRaises(ValueError):
                ota_image(data)

    def test_wrong_upload_is_rejected(self):
        with self.assertRaises(ValueError):
            verify_ota_image(b'\xe9correct', gzip.compress(b'\xe9different'))
