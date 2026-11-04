"""Replay real ROM SAVE pin captures without requiring VICE or ROMs in CI."""
import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[1]


class WriteViceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="espsette-write-")
        cls.replay = Path(cls.temp.name) / "replay"
        subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I" + str(REPO / "src"),
                        str(REPO / "experiments/write/replay.cpp"), str(REPO / "src/write_decoder.cpp"),
                        "-o", str(cls.replay)], check=True)
        cls.captures = []
        for path in sorted((REPO / "tests/fixtures/write").glob("*.json.gz")):
            fixture = json.loads(gzip.decompress(path.read_bytes()))
            tick, events = 0, []
            for delta, signal, high in fixture.pop("events"):
                tick += delta
                events.append((tick, signal, high))
            fixture["events"] = events
            cls.captures.append(fixture)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    @staticmethod
    def csv(fixture, events):
        return f'hz,{fixture["hz"]}\n' + "".join(f"{t},{s},{h}\n" for t, s, h in events)

    def decode(self, fixture, events=None):
        text = self.csv(fixture, fixture["events"] if events is None else events)
        return json.loads(subprocess.check_output([str(self.replay)], input=text, text=True))

    def check_header(self, fixture, result, count=1):
        command = fixture["name"].startswith("ESP:")
        start = 0x0801 if fixture["machine"] == "c64" else 0x1001
        self.assertEqual(result, dict(name=fixture["name"], command="TEST" if command else "",
                                     machine="c64" if fixture["machine"] == "c64" else "c16",
                                     status="command_received_no_action" if command else "header_ignored",
                                     headers=count, commands=count if command else 0, errors=0,
                                     start=start, end=start + 24 + len(fixture["name"]), type=1))

    def first_marker(self, fixture):
        # Locate the first byte using the proven physical edge for this CPU.
        level = int(fixture["machine"] == "c64")
        minimum = 615 if level else 1600
        last = None
        for i, (tick, signal, high) in enumerate(fixture["events"]):
            if signal == "W" and high == level:
                if last is not None and (tick - last) * 1000000 / fixture["hz"] > minimum:
                    return i
                last = tick
        self.fail("capture contains no header marker")

    def test_rom_save_matrix(self):
        self.assertEqual(len(self.captures), 12)
        for fixture in self.captures:
            with self.subTest(machine=fixture["machine"], ntsc=fixture["ntsc"], name=fixture["name"]):
                # Delta encoding must reconstruct every original physical event.
                self.assertEqual(hashlib.sha256(self.csv(fixture, fixture["events"]).encode()).hexdigest(),
                                 fixture["trace_sha256"])
                self.check_header(fixture, self.decode(fixture))

    def test_next_save_after_completed_payload(self):
        for fixture in self.captures:
            events = fixture["events"]
            # Start another SAVE with only 10 ms of silence after the trace.
            offset = events[-1][0] + fixture["hz"] // 100 - events[0][0]
            joined = events + [(t + offset, s, h) for t, s, h in events]
            with self.subTest(machine=fixture["machine"], ntsc=fixture["ntsc"], name=fixture["name"]):
                self.check_header(fixture, self.decode(fixture, joined), count=2)

    def test_motor_off_rejects_write(self):
        for fixture in self.captures:
            events = [(t, s, h if s == "W" else 0) for t, s, h in fixture["events"]]
            result = self.decode(fixture, events)
            self.assertEqual((result["headers"], result["commands"], result["errors"]), (0, 0, 0))

    def test_wrong_polarity_does_not_publish(self):
        for fixture in self.captures:
            events = [(t, s, 1 - h if s == "W" else h) for t, s, h in fixture["events"]]
            result = self.decode(fixture, events)
            self.assertEqual((result["headers"], result["commands"]), (0, 0))

    def test_missing_header_edges_do_not_publish(self):
        for fixture in self.captures:
            events = fixture["events"]
            first = self.first_marker(fixture)
            damaged = events[:first + 70] + events[first + 200:]
            result = self.decode(fixture, damaged)
            self.assertEqual((result["headers"], result["commands"]), (0, 0))

    def test_partial_header_does_not_publish(self):
        for fixture in self.captures:
            result = self.decode(fixture, fixture["events"][:self.first_marker(fixture) + 2000])
            self.assertEqual((result["headers"], result["commands"]), (0, 0))

    def test_ten_microsecond_capture_jitter(self):
        for fixture in self.captures:
            events = [(t + ((i * 17) % 21 - 10) * fixture["hz"] // 1000000 if s == "W" else t, s, h)
                      for i, (t, s, h) in enumerate(fixture["events"])]
            self.check_header(fixture, self.decode(fixture, events))

    def test_stray_edges_before_pilot(self):
        for fixture in self.captures:
            events = fixture["events"]
            # The old detector could interpret one stray long period as a
            # byte marker. No family is selected until a complete short pilot.
            tick, _, _ = events[0]
            noise = []
            for i, us in enumerate([350, 350, 270, 270, 200, 50, 100, 800, 30, 500]):
                tick += us * fixture["hz"] // 1000000
                noise.append((tick, "W", i % 2))
            self.check_header(fixture, self.decode(fixture, events[:1] + noise + events[1:]))

    def test_micros_wraparound(self):
        for fixture in self.captures:
            offset = ((1 << 32) - 2000000) * fixture["hz"] // 1000000
            events = [(t + offset, s, h) for t, s, h in fixture["events"]]
            self.check_header(fixture, self.decode(fixture, events))

    def test_ted_motor_pause_missed_by_main_loop(self):
        for fixture in self.captures:
            if fixture["machine"] == "c64":
                continue
            events = fixture["events"]
            off = next(i for i, (_, s, h) in enumerate(events) if s == "M" and not h)
            on = next(i for i in range(off + 1, len(events)) if events[i][1:] == ("M", 1))
            # Main-loop polling can miss the very brief TED header/data pause.
            joined = events[:off] + events[on + 1:]
            self.check_header(fixture, self.decode(fixture, joined))

    def test_aborted_save_rearms_after_a_second_off(self):
        for fixture in self.captures:
            events = fixture["events"]
            off = next(i for i, (_, s, h) in enumerate(events) if s == "M" and not h)
            prefix = events[:off + 1]  # verified header, no program record
            offset = prefix[-1][0] + 2 * fixture["hz"] - events[0][0]
            joined = prefix + [(t + offset, s, h) for t, s, h in events]
            self.check_header(fixture, self.decode(fixture, joined), count=2)


if __name__ == "__main__":
    unittest.main()
