import gzip
import hashlib
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class EmbeddedAssetsTests(unittest.TestCase):
    def generate(self, script):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'generated.h'
            subprocess.run(['python3', str(ROOT / 'scripts' / script), '--out', str(output)], check=True)
            first = output.read_bytes()
            subprocess.run(['python3', str(ROOT / 'scripts' / script), '--out', str(output)], check=True)
            self.assertEqual(first, output.read_bytes())
            return bytes(int(x, 16) for x in re.findall(rb'0x([0-9a-f]{2})', first))

    def test_demo_is_the_original_prg(self):
        data = (ROOT / 'assets/demo/circles.prg').read_bytes()
        self.assertEqual(hashlib.sha256(data).hexdigest(),
                         'ca0b6572b36473588e51d14017781ccee89c45656ad0a04875e0728abe4dcc3c')
        self.assertEqual(self.generate('embed-demo.py'), data)

    def test_gzip_page_retains_assets_licenses_and_ui(self):
        data = self.generate('embed-player.py')
        page = gzip.decompress(data).decode()
        self.assertIn('<title>ESPsette</title>', page)
        self.assertIn('music Shogoon', page)
        self.assertIn('code YTM', page)
        self.assertIn('SIL OPEN FONT LICENSE', page)
        for kind in ('font/woff2', 'image/webp', 'image/png', 'video/mp4', 'video/webm'):
            self.assertIn('data:' + kind + ';base64,', page)
        self.assertTrue('root.createPlaybackAwake = factory' in page)
        self.assertIn('const preview = location.protocol', page)
        self.assertLess(len(data), len(page.encode()) * 0.6)
