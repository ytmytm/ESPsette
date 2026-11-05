"""Feed original VICE pin events through the production periodic sampler."""
import gzip
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[1]


class WriteSamplerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='espsette-sampler-')
        cls.replay = Path(cls.temp.name) / 'sample-replay'
        subprocess.run(['c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Isrc',
                        'experiments/write/sample-replay.cpp', 'src/write_sampler.cpp',
                        'src/write_decoder.cpp', '-o', str(cls.replay)], cwd=REPO, check=True)
        cls.captures = []
        for path in sorted((REPO / 'tests/fixtures/write').glob('*.json.gz')):
            fixture = json.loads(gzip.decompress(path.read_bytes()))
            tick, events = 0, []
            for delta, signal, high in fixture.pop('events'):
                tick += delta
                events.append((tick, signal, high))
            fixture['events'] = events
            fixture['csv'] = f'hz,{fixture["hz"]}\n' + ''.join(f'{t},{s},{h}\n' for t, s, h in events)
            cls.captures.append(fixture)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def decode(self, fixture, *options, text=None):
        return json.loads(subprocess.check_output([str(self.replay), *map(str, options)],
                                                  input=fixture['csv'] if text is None else text, text=True))

    def check_header(self, fixture, result):
        self.assertEqual(result['name'], fixture['name'])
        command = fixture['name'].startswith('ESP:')
        self.assertEqual(result['command'], 'TEST' if command else '')
        self.assertEqual((result['headers'], result['commands'], result['errors'], result['gaps']),
                         (1, int(command), 0, 0))
        self.assertEqual(result['status'], 'command_received_no_action' if command else 'header_ignored')
        start = 0x0801 if fixture['machine'] == 'c64' else 0x1001
        self.assertEqual((result['start'], result['end']), (start, start + 24 + len(fixture['name'])))

    def test_real_save_matrix_and_sampling_phases(self):
        self.assertEqual(len(self.captures), 12)
        for fixture in self.captures:
            for phase in range(40):
                with self.subTest(machine=fixture['machine'], ntsc=fixture['ntsc'], name=fixture['name'], phase=phase):
                    result = self.decode(fixture, '--phase', phase)
                    self.check_header(fixture, result)
                    self.assertEqual(result['maxGapUs'], 40)

    def test_sampler_jitter(self):
        for fixture in self.captures:
            self.check_header(fixture, self.decode(fixture, '--jitter', 5))

    def test_suspended_capture(self):
        for fixture in self.captures:
            result = self.decode(fixture, '--suspended')
            self.assertEqual((result['headers'], result['commands'], result['errors']), (0, 0, 0))

    def test_large_sampling_gap_cannot_publish_a_partial_header(self):
        for fixture in self.captures:
            high = int(fixture['machine'] == 'c64')
            minimum = 615 if high else 1600
            last = None
            marker = None
            for tick, signal, level in fixture['events']:
                if signal == 'W' and level == high:
                    if last is not None and (tick - last) * 1000000 / fixture['hz'] > minimum:
                        marker = tick * 1000000 // fixture['hz']
                        break
                    last = tick
            self.assertIsNotNone(marker)
            result = self.decode(fixture, '--gap-at', marker + 150000, '--gap-us', 500)
            self.assertEqual((result['headers'], result['commands']), (0, 0))
            self.assertGreater(result['gaps'], 0)

    def test_counter_wraparound(self):
        for fixture in self.captures:
            # Move the recording across the 32-bit microsecond rollover.
            # Skip the 71-minute prelude with the sampler's phase option.
            offsetUs = (1 << 32) - 2000000
            offset = offsetUs * fixture['hz'] // 1000000
            text = f'hz,{fixture["hz"]}\n' + ''.join(f'{t+offset},{s},{h}\n' for t, s, h in fixture['events'])
            self.check_header(fixture, self.decode(fixture, '--phase', offsetUs, text=text))
