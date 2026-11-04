#!/usr/bin/env python3
"""Check negative-Y final blocks against a linear address oracle (no VICE)."""
import itertools


def verify(address, length, block_bytes):
    base, left, written, blocks = address, length, 0, 0
    while left:
        count = min(left, block_bytes)
        left -= count
        blocks += 1
        y = 0
        if count < 256:
            # Model the actual eight-bit ADC and conditional high decrement,
            # independently of the desired linear output-address sequence.
            low = (base & 255) + count
            high = (base >> 8) - (low < 256)
            base = ((high << 8) | (low & 255)) & 65535
            y = (-count) & 255
        block_writes = 0
        while True:
            assert (base + y) & 65535 == (address + written) & 65535
            written += 1
            block_writes += 1
            y = (y + 1) & 255
            if y == 0:
                base = (base + 256) & 65535
                break
        assert block_writes == count
        assert base == (address + written) & 65535
    assert written == length
    assert blocks == (length + block_bytes - 1) // block_bytes


if __name__ == "__main__":
    cases = set(itertools.product((0x1000, 0x10fe, 0x10ff, 0x7fff, 0xfffe), range(512)))
    cases.update((0x1000 + low, length) for low in range(256)
                 for length in (1, 255, 256, 257, 511, 512, 513, 8192))
    cases.update(itertools.product((0x1000, 0xfffe), (12288, 28672, 32768, 65535)))
    for address, length in sorted(cases):
        for block_bytes in (1, 256):
            verify(address, length, block_bytes)
    print(f"{2 * len(cases)} address/length/block-size cases: PASS")
