import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('build_version', ROOT / 'scripts/build-version.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class BuildVersionTests(unittest.TestCase):
    def test_commit_identity_and_dirty_tree(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            subprocess.run(['git', 'init', '-q', temp], check=True)
            source = root / 'source.txt'
            source.write_text('original\n')
            subprocess.run(['git', 'add', 'source.txt'], cwd=root, check=True)
            subprocess.run(['git', '-c', 'user.name=Build Test', '-c', 'user.email=test@example.invalid',
                            '-c', 'commit.gpgsign=false', 'commit', '-qm', 'fixture'], cwd=root, check=True)
            identity = builder.version(root)
            self.assertEqual(len(identity['commit']), 40)
            self.assertEqual(identity['version'], identity['commit'][:12])
            self.assertFalse(identity['dirty'])
            source.write_text('modified\n')
            changed = builder.version(root)
            self.assertTrue(changed['dirty'])
            self.assertEqual(changed['commit'], identity['commit'])
            self.assertEqual(changed['version'], identity['version'] + '-dirty')
