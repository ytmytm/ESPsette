import importlib.util
from pathlib import Path
import re
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('build_demo', ROOT / 'scripts/build-demo.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class DemoSiteTest(unittest.TestCase):
    def test_demo_has_local_assets_and_always_simulates_esp(self):
        original = (ROOT / 'web/player.html').read_bytes()
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            builder.build_demo(output)
            page = (output / 'index.html').read_text(encoding='utf-8')
            self.assertIn('const preview = true;', page)
            self.assertNotIn("const preview = location.protocol", page)
            self.assertIn('<h2>Demo mode</h2>', page)
            for control in ('previewFastload', 'previewPauseReceiver', 'previewSaveName', 'previewWriteCommand'):
                self.assertIn(f'id="{control}"', page)
            self.assertIn('Fastload PRG demo', page)
            self.assertIn('Simulate ESP:TEST', page)
            self.assertIn('SAVE "ESP:TEST",1', page)
            self.assertIn('data:video/mp4;base64,',page)
            self.assertIn('data:video/webm;base64,',page)
            for asset in set(re.findall(r'assets/[A-Za-z0-9_./-]+\.(?:webp|png|woff2|mp4|webm|js)', page)):
                self.assertEqual((output / asset).read_bytes(), (ROOT / 'web' / asset).read_bytes())
            self.assertTrue((output / 'assets/fonts/caveat-OFL.txt').is_file())
            for name in ('ui-test.tap', 'ui-test.idx'):
                self.assertEqual((output / 'samples' / name).read_bytes(),
                                 (ROOT / 'tests/fixtures' / name).read_bytes())
            builder.build_demo(output)
            self.assertEqual((output / 'index.html').read_text(encoding='utf-8'), page)
        self.assertEqual((ROOT / 'web/player.html').read_bytes(), original)


if __name__ == '__main__':
    unittest.main()
