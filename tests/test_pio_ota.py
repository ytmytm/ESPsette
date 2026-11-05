"""Check upload selection and both image-generation hooks without a device."""
import gzip
import io
from contextlib import redirect_stdout
import json
from pathlib import Path
import runpy
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


class BuildEnvironment(dict):
    def __init__(self, build, protocol):
        super().__init__(PROJECT_DIR=str(ROOT), LINKFLAGS=['-u', '_printf_float', '-u',
                         'call_user_start', '-u', '_scanf_float', '-Wl,--gc-sections'],
                         UPLOADCMD='serial -f $SOURCE', ENV={})
        self.build, self.protocol, self.post, self.pre = build, protocol, [], []

    def subst(self, text):
        return {'$BUILD_DIR/${PROGNAME}.bin':str(self.build / 'firmware.bin'),
                '$OBJCOPY':'test-objcopy', '$UPLOAD_PROTOCOL':self.protocol}[text]

    def Replace(self, **kwargs):
        self.update(kwargs)

    def AddPostAction(self, target, action):
        self.post.append((target, action))

    def AddPreAction(self, target, action):
        self.pre.append((target, action))


class PioOtaTests(unittest.TestCase):
    def test_serial_and_ota_use_the_correct_images(self):
        for protocol in ('esptool', 'espota'):
            with self.subTest(protocol=protocol), tempfile.TemporaryDirectory() as temp:
                build = Path(temp)
                raw = b'\xe9' + b'firmware data' * 30000
                (build / 'firmware.bin').write_bytes(raw)
                (build / 'firmware.version.json').write_text(json.dumps(
                    dict(commit='a' * 40, version='a' * 12, dirty=False)))
                env = BuildEnvironment(build, protocol)
                runpy.run_path(str(ROOT / 'scripts/pio-ota.py'),
                               init_globals={'Import':lambda _:None, 'env':env})
                self.assertEqual(env['LINKFLAGS'], ['-u', 'call_user_start', '-Wl,--gc-sections'])
                self.assertEqual(len(env.post), 1)
                if protocol == 'espota':
                    self.assertIn('${PROGNAME}.bin.gz', env['UPLOADCMD'])
                    self.assertEqual(env.pre[0][0], 'upload')
                else:
                    self.assertEqual(env['UPLOADCMD'], 'serial -f $SOURCE')
                    self.assertFalse(env.pre)
                symbols = '402eb000 g *ABS* 00000000 _FS_start\n'
                with patch('subprocess.check_output', return_value=symbols):
                    captured = io.StringIO()
                    with redirect_stdout(captured):
                        env.post[0][1](None, None, env)
                    self.assertIn("Firmware image: " + "a" * 12, captured.getvalue())
                    self.assertEqual(gzip.decompress((build / 'firmware.bin.gz').read_bytes()), raw)
                    budget = json.loads((build / 'firmware.ota.json').read_text())
                    self.assertEqual(budget['filesystem_start'], 0xEB000)
                    if env.pre:
                        # Upload regenerates a deleted gzip even with a cached .bin.
                        (build / 'firmware.bin.gz').unlink()
                        env.pre[0][1](None, None, env)
                        self.assertTrue((build / 'firmware.bin.gz').exists())
