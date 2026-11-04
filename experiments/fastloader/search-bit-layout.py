#!/usr/bin/env python3
"""Search wire permutations and verify byte assembly without starting VICE.

The cost comparison covers the listed decoder families, not every possible
6502 program. Routes are tuples (WRITE0,SENSE0,...,WRITE3,SENSE3).
"""
import itertools
import json
from functools import partial


def c64_serial(samples):
    result = 0
    for value in samples:
        result = (result << 2) | (((value >> 3) & 1) << 1) | ((value >> 4) & 1)
    return result


def c64_parallel(samples):
    high = (((samples[0] & 0x18) >> 2) | (samples[1] & 0x18)) >> 1
    low = (((samples[2] & 0x18) >> 2) | (samples[3] & 0x18)) >> 1
    return (high << 4) | low


def c64_eor(samples):
    high = ((((samples[0] & 0x18) >> 2) ^ samples[1]) >> 1) & 15
    low = ((((samples[2] & 0x18) >> 2) ^ samples[3]) >> 1) & 15
    # CPU port bits 1/2 remain high. They invert the low two bits of each
    # nibble; the upper lookup table and final EOR #3 undo that inversion.
    return ((high ^ 3) << 4) | (low ^ 3)


def c16_parallel(samples):
    pairs = [(cpu & 2) | (sense & 4) for cpu, sense in samples]
    return (((pairs[0] >> 1) | (pairs[1] << 1)) << 4) | (pairs[2] >> 1) | (pairs[3] << 1)


def search_rotations(write_position, sense_position):
    """Enumerate OR plus ROL/ROR through the nine-bit A/C register.

    CLC is required initially. Symbols must enter two empty bit positions;
    after the fourth insertion carry must contain no data bit.
    """
    best = None

    def visit(state, stage, cost, rotations):
        nonlocal best
        if stage == 4:
            if state[8] is None and (best is None or cost < best[0]):
                best = cost, rotations, tuple(state.index(bit) for bit in range(8))
            return
        if state[write_position] is not None or state[sense_position] is not None:
            return
        bits = state.copy()
        bits[write_position], bits[sense_position] = 2 * stage, 2 * stage + 1
        for amount in range(9):
            extra = min(amount, 9 - amount)
            if best is not None and cost + extra >= best[0]:
                continue
            visit([bits[(index - amount) % 9] for index in range(9)],
                  stage + 1, cost + extra, [*rotations, amount])

    visit([None] * 9, 0, 0, [])
    assert best is not None
    return best


def rotate_decoder(samples, *, c16, rotations):
    accumulator = carry = 0
    for sample, amount in zip(samples, rotations):
        pair = ((sample[0] & 2) | (sample[1] & 4)) if c16 else sample & 0x18
        value = (accumulator | pair) | (carry << 8)
        rotated = ((value << amount) | (value >> (9 - amount))) & 511
        accumulator, carry = rotated & 255, rotated >> 8
    assert carry == 0
    return accumulator


def pins(byte, route, c16=False):
    values = []
    for index in range(4):
        write = (byte >> route[2 * index]) & 1
        sense = (byte >> route[2 * index + 1]) & 1
        values.append((write << 1, sense << 2) if c16 else 0x07 | (write << 3) | (sense << 4))
    return values


def routes_for(decoder, c16=False):
    # Each decoder is affine over bits. Zero and the eight basis vectors
    # suffice to select routes; exhaustive byte/noise checks follow below.
    probes = (0, *(1 << bit for bit in range(8)))
    return [route for route in itertools.permutations(range(8))
            if all(decoder(pins(byte, route, c16)) == byte for byte in probes)]


def verify(decoder, route, c16=False):
    checks = 0
    for byte in range(256):
        reference = pins(byte, route, c16)
        assert decoder(reference) == byte
        checks += 1
        # Vary unrelated pins of each individual sample, independently of
        # every other sample. All relevant operations are bitwise/affine.
        for sample in range(4):
            for noise in range(256):
                values = reference.copy()
                if c16:
                    cpu, sense = values[sample]
                    values[sample] = (cpu | (noise & ~2), sense)
                    assert decoder(values) == byte
                    values[sample] = (cpu, sense | (noise & ~4))
                else:
                    # Only the EOR candidate needs HIRAM/CHAREN fixed high.
                    mask = 0x1e if decoder is c64_eor else 0x18
                    values[sample] ^= noise & ~mask
                assert decoder(values) == byte
                checks += 2 if c16 else 1
    return checks


if __name__ == "__main__":
    c64_rotations = search_rotations(3, 4)
    c16_rotations = search_rotations(1, 2)
    candidates = [
        ("c64-serial-pha-pla", c64_serial, False, 152),
        ("c64-serial-tax-txa-unrolled", c64_serial, False, 140),
        ("c64-mask-or-nibbles", c64_parallel, False, 50),
        # The 40-cycle EOR variant additionally needs A-preserving polling.
        # It is not selected: direct FLAG polling loads the mask into A.
        ("c64-eor-needs-a-preserving-poll", c64_eor, False, 40),
        ("c16-mask-or-nibbles", c16_parallel, True, 94),
        ("c64-or-rol-ror", partial(rotate_decoder, c16=False, rotations=c64_rotations[1]),
         False, 38 + 2 * c64_rotations[0] + 2),
        ("c16-or-rol-ror", partial(rotate_decoder, c16=True, rotations=c16_rotations[1]),
         True, 86 + 2 * c16_rotations[0] + 2),
    ]
    report = []
    for name, decoder, c16, cycles in candidates:
        routes = routes_for(decoder, c16)
        assert len(routes) == 1, (name, routes)
        route = routes[0]
        checks = verify(decoder, route, c16)
        report.append(dict(candidate=name, routes_searched=40320, route=route,
                           production_selected=name in ("c64-mask-or-nibbles", "c16-mask-or-nibbles"),
                           data_assembly_cycles_per_byte=cycles, byte_and_noise_checks=checks))
    assert report[2]["route"] == report[3]["route"] == report[4]["route"] == (4, 5, 6, 7, 0, 1, 2, 3)
    print(json.dumps(report, indent=2))
