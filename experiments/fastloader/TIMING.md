# Final receiver timing budget

Both fast stages blank the screen. C64 disables sprite DMA and masks both CIAs;
TED still incurs refresh stalls. PAL is the slower CPU and determines minimum
delays. `analyze-timing.py --boot` assembles the actual low-RAM receiver, counts
instructions, checks table/branch page crossings and validates continuous-time
and rounded VICE-clock margins. Run without `--boot` for the direct test variant.

The final descriptor/payload wire order is b4:b5, b6:b7, b0:b1, b2:b3.
C64 reads both bits with mask `$18`; TED samples `$01` bit 6 into V with the final READ `BIT` poll and combines
it with `$FD10` bit 2. Its BVC/ORA path costs one cycle more for WRITE high;
the table and analyzer use that worst case.
A 16-entry aligned table merges the upper nibble. There is no per-byte JSR/RTS.
Y is the destination index and block counter. A partial final block adjusts
its base and initial Y so it also terminates on rollover.

| Path | C64 cycles | TED cycles |
| --- | ---: | ---: |
| Pair 1 → pair 2 | 20 | 22 |
| Pair 2 → pair 3 | 20 | 24 |
| Pair 3 → pair 4 | 20 | 22 |
| Last pair → next byte, payload | 33 | 37 |
| Last pair → next byte, descriptor | 34 | 38 |

The analyzer includes polling phase, latest data sample, branch costs and TED
refresh allowance. C64 polls direct FLAG bit 4 with `LDA #$10 / BIT $DC0D / BEQ`;
IRQ-summary bit 7 has a separate delayed path and cannot substitute for FLAG.

Default setup/gap are 3 µs, hold is 16 µs, and the extra byte pause is 19 µs on
C64 or 11 µs on TED. Thus consecutive samples are 22 µs apart within a byte,
and the last sample → next byte's first sample is 38/30 µs. The source retains
data during hold, including both TED port reads. Configured hold and gaps are
minimum delays: interrupts, loop overhead and network stalls can lengthen them.
The firmware never catches up by shortening a later interval.

At the original model's CPU-tick-rounded delays, a 4096-byte PAL payload with
256-byte MOTOR blocks measured approximately 9.25 kB/s C64 and 9.72 kB/s TED,
excluding bootstrap. There are 17 requests: descriptor plus sixteen payload
blocks. MOTOR off/on delay defaults to 1000/5 µs; it is a detection model, not
an analog RC measurement. Rounding can make NTSC's modeled throughput lower
than PAL despite its faster CPU. These figures are not ESP hardware benchmarks.

The header stub favors size over speed and uses MSB-first bit rotation, with
80 µs hold and byte pause. Its few receiver pages do not determine sustained
PRG throughput. Firmware settings permit both slower debugging and experimental
shorter delays. Values below the proven defaults may corrupt data; the accepted
configuration range is not a new validated cycle budget.

The retained tests cover actual receiver execution in VICE, all 256 byte values
through the ESP byte transmitter with changed timings, bounded setting values,
block boundaries and expected failures with unsafe hold/byte gaps. Historical
bit-layout searches and timing sweeps have been removed; the assembled-code
analyzer and regression matrices remain reproducible.
