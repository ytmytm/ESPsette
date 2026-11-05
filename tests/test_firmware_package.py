"""Check release naming and integrity of downloadable firmware packages."""
import hashlib
import gzip
import json
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
            image = b'\xe9' + bytes(range(256))
            (build / 'firmware.bin').write_bytes(image)
            (build / 'firmware.bin.gz').write_bytes(gzip.compress(image,mtime=0))
            (build / 'firmware.elf').write_bytes(b'ELF fixture')
            (build / 'firmware.version.json').write_text(json.dumps(
                dict(commit='a'*40,version='a'*12,dirty=False)))
            output = root / 'dist'
            stem = firmware.package(build, output, 'v2026-11-01', 'a' * 40)
            self.assertEqual(stem, 'ESPsette-esp01-1m-v2026-11-01-' + 'a' * 12)
            metadata = json.loads((output / (stem + '.json')).read_text())
            self.assertEqual(metadata['firmware_version'], 'a' * 12)
            self.assertIn('Firmware version: ' + 'a' * 12, (output / 'FLASHING.txt').read_text())
            self.assertEqual((output / (stem + '.bin')).read_bytes(), image)
            self.assertEqual(gzip.decompress((output / (stem + '.bin.gz')).read_bytes()), image)
            self.assertTrue(json.loads((output / (stem + '.json')).read_text())['ota'])
            for line in (output / 'SHA256SUMS.txt').read_text().splitlines():
                digest, name = line.split('  ')
                self.assertEqual(digest, hashlib.sha256((output / name).read_bytes()).hexdigest())
            (build / 'firmware.version.json').write_text(json.dumps(
                dict(commit='a'*40, version='a'*12 + '-dirty', dirty=True)))
            dirty_stem = firmware.package(build, root / 'dirty-dist', '', 'a' * 40)
            self.assertEqual(dirty_stem, 'ESPsette-esp01-1m-dev-' + 'a' * 12 + '-dirty')
            with self.assertRaises(ValueError):
                firmware.package(build, output, '', 'a' * 40)

    def test_missing_build_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(ValueError):
                firmware.package(Path(temp), Path(temp) / 'dist', '', 'a' * 40)
