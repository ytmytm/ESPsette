"""Check release naming and integrity of downloadable firmware packages."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    'firmware_package', Path(__file__).resolve().parents[1] / 'scripts/package-firmware.py')
firmware = importlib.util.module_from_spec(spec)
spec.loader.exec_module(firmware)


class FirmwarePackageTest(unittest.TestCase):
    def test_release_dates(self):
        self.assertEqual(firmware.validate_tag('v2026-11-01'), 'v2026-11-01')
        for tag in ('2026-11-01', 'v2026-2-01', 'v2026-02-30', 'v1.0', 'v2026-11-01-extra'):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                firmware.validate_tag(tag)

    def test_package_preserves_binary_and_checksums(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            build = root / 'build'
            build.mkdir()
            image = bytes(range(256))
            (build / 'firmware.bin').write_bytes(image)
            (build / 'firmware.elf').write_bytes(b'ELF fixture')
            output = root / 'dist'
            stem = firmware.package(build, output, 'v2026-11-01', 'a' * 40)
            self.assertEqual((output / (stem + '.bin')).read_bytes(), image)
            for line in (output / 'SHA256SUMS.txt').read_text().splitlines():
                digest, name = line.split('  ')
                self.assertEqual(digest, hashlib.sha256((output / name).read_bytes()).hexdigest())
            with self.assertRaises(ValueError):
                firmware.package(build, output, '', 'a' * 40)

    def test_missing_build_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(ValueError):
                firmware.package(Path(temp), Path(temp) / 'dist', '', 'a' * 40)
