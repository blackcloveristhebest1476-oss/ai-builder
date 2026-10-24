# Test fixtures and expected outputs

Run the suite with:

```
cmake -S tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests --output-on-failure
```

All values below are asserted by `test_core.cpp`.

## Macro fixtures (`fixtures/`)

The `.gdr2` files were generated with the reference writer from
[maxnut/GDReplayFormat](https://github.com/maxnut/GDReplayFormat) (commit `d8d2f0b`), so they test
compatibility with real GDR2 files rather than with our own writer.

### `basic.gdr2` — 2-player, non-platformer
| Field | Expected |
|---|---|
| author / description | `tester` / `fixture` |
| bot | `FixtureBot` v3 |
| level | `Fixture Level`, ID 128 |
| game version | `2.2081` |
| framerate | 240 |
| seed / coins | 1234 / 1 |
| stored duration | 12.5 s |
| death frames | 500, 900 |
| inputs (frame, player, press/release) | 100 P1 ↓, 130 P1 ↑, 200 P2 ↓, 250 P2 ↑, 400 P1 ↓, 460 P1 ↑, 1000 P1 ↓, 1000 P2 ↓, 1001 P1 ↑, 1100 P2 ↑ |
| presses / player-2 events | 5 / 4 |

### `platformer.gdr2` — platformer packing
Framerate 480, seed −5, level ID 4294967295, 6 inputs with buttons Right(3), Left(2) and Jump(1);
last input on frame 100000 (multi-byte varint deltas). Duration = 100000 / 480 s.

### `physics-ext.gdr2` — extensions
Input extension tag `Phys` (GDR's PhysicsInput: position/rotation/velocity per input) plus a replay
extension block. Both are skipped; inputs are frames 10 (P1), 20 (P2), 30 (P1).

### `nandl-dataset1.json` — NaNDL calculator file
Five rows (times 1.0, 2.5, 3.0, 7.25, 10.0 s; windows 4, 2, 1, 6, 3 frames at 240 Hz).
Import it in-game (**Import Macro**) and press **Precision → Calculate** with default settings:

| Modifiers | L* (σ/s) | E[T_A] (s) | P(C) | E[T_C] (s) |
|---|---|---|---|---|
| none | 24.095535278320313 | 1.2475736817905114 | 1.4439480501176256e-5 | 86400.177741 |
| nerve + fatigue | 24.311733245849609 | 1.2493357341523106 | 1.4459891698710052e-5 | 86400.075477 |
| nerve + fatigue + CPS | 18.827056884765625 | 1.2342811823258306 | 1.4285542382501375e-5 | 86400.722442 |

These are the exact numbers the reference calculator (nandl.pages.dev, calculator.js v9) returns
for the same rows, so the mod and the website can be compared directly.

## Hand-calculated NaNDL checks
- One input, N = 2 at f = 240, L = 120: s = ½·(2/240)·120 = 0.5 → p = erf(0.5/√2) = 0.3829249225480262,
  E[T_A] = t = 1 s, E[T_C] = 1/p = 2.6114 s.
- Two inputs (t = 1, N = 2) and (t = 3, N = 4), L = 120: p = 0.38292492, 0.68268949;
  E[T_A] = q₁ + 3p₁ = 1.7658498450960523 s, P(C) = 0.2614188209009449, E[T_C] = 6.7548688308297296 s.
- Nerve k_t = 0.01 at t = 1 → λ = e^−0.01; Fatigue k_u = 0.1 at i = 2 → λ = e^−0.2;
  CPS k_c = 1: c = 2 → λ = 1, c = 4 → λ = 0.5, slow (c < 0.5) → λ = 4.

## Other covered cases
Chronological sorting (out-of-order GDR 1), duplicate and redundant press/release detection, empty
macros, invalid frames/buttons/framerates, implausible frame numbers, truncated/wrong-version/corrupted
GDR2, unsupported files (PNG, unknown JSON, random text), CSV variants, NaNDL `-` rows and numeric
strings, frame-window boundaries and histogram buckets, lower-bound handling, JSON/CSV/NaNDL export
round trips, sample data determinism, P(C) underflow for 200,000 frame-perfect inputs (finite in log
space), a 1,000,000-input solve, a 200,000-input JSON import, and 6,000 randomly corrupted files
(must be rejected or parsed, never crash; run under ASan/UBSan during development).
