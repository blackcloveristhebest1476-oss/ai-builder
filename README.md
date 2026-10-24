# Frame Window Analyzer — Geometry Dash (Geode) mod

Import a macro, measure how many frames early or late every click can be while still surviving, and estimate the precision a human would need using the [NaNDL](https://nandl.pages.dev/) model.

Requires **Geode v5.11+** on **Geometry Dash 2.2081**. CI builds Windows, macOS and Android (32/64-bit); iOS is not built.

---

## What it does

| Area | What you get |
|---|---|
| **Import** | GDR2 (binary, current GDReplayFormat), GDR 1 (JSON or MessagePack), NaNDL calculator JSON exports, CSV / plain-text frame-window tables, and this mod's own saved analyses. Format is detected from the file contents, not the extension. |
| **Measure (Mode A)** | Inside the real level, every selected input is replayed with only that input moved −1, −2, … and +1, +2, … ticks until the player dies. Early frames, late frames, total window, asymmetry and frame-perfect classification per input. |
| **Precision (Mode B)** | NaNDL's published equations: base model plus Nerve, Fatigue and the experimental CPS modifier, solving for **L\*** (the precision at which the expected completion time equals your target, default 24 h). |
| **UI** | Dashboard, sortable/searchable input table, per-input graph (hover, click, zoom, drag-to-select region, filters, individual/cumulative views), histogram, precision calculator, settings. |
| **Export** | Full analysis JSON (re-importable), per-input CSV, and a NaNDL-calculator JSON you can load on nandl.pages.dev to cross-check. Saved analyses go to the mod's save folder; macro files are never modified. |

### Honest labels

Every window carries a status, shown in the table, detail view and exports:

| Status | Meaning |
|---|---|
| **Verified** | Measured by replaying the level: the shift just outside the window died on both sides. |
| **Lower bound** | Every tested shift survived until the search stopped (max shift reached, or the neighbouring press/release on the same button). The real window is at least this large. |
| **Inconsistent** | Re-running the edge timings gave a different result. Not trusted. |
| **Baseline failed** | The unmodified macro dies before this input's survival horizon ends, so nothing can be measured. |
| **User supplied** | Imported from a frame-window file or typed in by you. |
| **Estimated** | A default value you chose for unknown inputs, or the synthetic sample data. Never a measurement. |
| **Unknown** | No data yet. |

Precision is **never** calculated from invented windows: inputs without a window are passed to the model as NaNDL's "ignored" (`-`) rows and the result says how many there were.

---

## How measurement works (and its limits)

Geode cannot rewind GD's physics, and GD's practice checkpoints are not guaranteed to restore every bit of state. So the engine uses the most reliable method available:

1. **Baseline**: restart the level and replay the unmodified macro (inputs injected after `GJBaseGameLayer::processCommands` with `handleButton(down, button, !player2)` — the same injection point GDR bots use). Record the game mode at each input. If the baseline dies, inputs whose horizon reaches that death are marked *Baseline failed*.
2. **Determinism check** (optional, on by default): replay it again and compare player positions and velocities at every input. If they differ (random triggers, other mods), measurement stops instead of producing unreliable numbers.
3. **Search**: for each input, restart the level and replay the whole macro with only that input shifted. A trial **survives** if the player is alive `horizon` seconds after the shifted input with every later input unchanged (default 1.5 s), or reaches the end of the level. Because each trial starts from the level start, earlier inputs, velocity, gravity, speed changes, game mode, dual mode and triggers are simulated by GD itself.
4. **Edge recheck** (optional, on by default): the last surviving and first failing shift on each side are run once more; a disagreement marks the input *Inconsistent*.

The level is simulated faster than real time by stepping GD's own update many times per rendered frame (time budget setting). Long levels with many inputs take a while — the HUD shows progress, speed and an ETA; **Esc stops** and keeps everything finished so far. Measure a range (e.g. `120-180`) to focus on a hard part.

While measuring: your inputs are ignored, deaths and completions are intercepted (nothing is saved as progress or a completion), Click Between Frames is soft-toggled off and restored afterwards.

**Not supported for measurement:** practice mode, start positions, macros recorded with physics bypass (not 240 TPS), and the editor playtest. Windows are counted in whole physics ticks (1/240 s) and only contiguous windows around the original timing are reported. Platformer replays can be measured but "survival" is the only success criterion.

Drag-and-drop import is not implemented: GD's window does not expose a drop event through Geode, so the native file picker is used instead.

---

## The NaNDL model (Mode B)

With `n` counted inputs, frame window `N_i`, window rate `f`, time `t_i` (plus respawn time) and precision `L` (σ/s):

- `w_i = N_i / f`, `s_i = ½ · w_i · L`, `p_i = P(|X| ≤ s_i)` with `X ~ N(0, 1)`, `q_i = 1 − p_i`
- Modifiers multiply `s_i`: Nerve `e^(−k_t t_i)`, Fatigue `e^(−k_u i)`, CPS `(4 / max(1, 2c_i))^(k_c)` with `c_i = (i − i') / (t_i − t_i')` (**experimental**, as NaNDL says)
- `P(C) = Π p_i`, `E[T_A] = t_n P(C) + Σ t_i r_i q_i`, `E[T_C] = E[T_A] / P(C)`, and L\* solves `E[T_C](L*) = target`

The implementation follows the reference calculator's exact behaviour too (defaults: window rate 240, target 86 400 s, tolerance 1 s, 240 iterations, constants k_t = 0.0016520833717346, k_u = 0.0002727763242154, k_c = 0.2784421686721826; respawn added to every time; window 0 treated as 1; ignored windows pass unless CPS is on; same bracket-and-bisect solver). Two differences, both for numerical robustness: `P(C)` is accumulated in log space and `q_i` uses `erfc`, so very long macros give finite answers (shown as powers of ten) instead of `∞`. The test suite checks 9 L\* solutions and 6 fixed-precision results against values produced by the live calculator (`window.PrecisionCalculator`, calculator.js v9) to 1e-12 relative error.

The model is a statistical estimate under NaNDL's assumptions (independent inputs, symmetric windows, normally distributed timing error), not a prediction of a real player.

---

## Build

### Option A — GitHub Actions (no tools needed)
1. Put this folder in a GitHub repository (the workflow is already at `.github/workflows/build.yml`).
2. Push. The **Build Frame Window Analyzer** workflow runs the core tests and builds Windows, macOS and Android.
3. Download the **Build Output** artifact and unzip it: it contains `alexl.frame-window-analyzer.geode`.

### Option B — on your PC (Windows)
1. Install **Visual Studio 2022 Build Tools** ("Desktop development with C++", include *C++ Clang tools for Windows*), **CMake** and **Git**.
2. Install the Geode CLI: `winget install GeodeSDK.GeodeCLI`.
3. In a terminal:
   ```
   geode config setup
   geode sdk install
   geode sdk install-binaries
   cd path\to\frame-window-analyzer
   geode build
   ```

### Core tests (any desktop compiler, no Geode)
```
cmake -S tests -B build-tests
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```
See [tests/EXPECTED.md](tests/EXPECTED.md) for the fixtures and their documented expected outputs.

## Install
Copy `alexl.frame-window-analyzer.geode` into `Geometry Dash/geode/mods/` and restart GD (skip if `geode build` installed it for you).

## Use
1. Open the level the macro was recorded on, pause, press the **chart button** (or **Alt+F** anywhere).
2. **Import Macro** → pick a `.gdr2` / `.gdr` / `.json` file. The Dashboard shows format, inputs, duration, FPS, metadata and any warnings (**Details**).
3. **Measure** → choose `all` or a range → **Start**. Watch the HUD; Esc stops.
4. Inspect the **Inputs** table (click a row for the detail view and a timing strip), **Graphs** and the frame-window counter on the Dashboard.
5. **Precision** → *Solve L\** (or *Stats for fixed L*), toggle modifiers → **Calculate** (runs in the background).
6. **Export** JSON / CSV / NaNDL, or **Saves** to save, reload or open the saves folder.

No macro? **Sample Data** loads clearly-labelled synthetic data. Have frame windows from elsewhere? Import a NaNDL JSON or a CSV (`input,time_s,window_frames`, `-` for ignored).

## Settings
Everything is in the mod's settings (each has an explanation) and the **Settings** tab, which also has **Reset to Defaults**. Key ones:

| Setting | Default | Notes |
|---|---|---|
| Open Analyzer | Alt+F | Keybind |
| Game FPS (physics rate) | 240 | Only used when a file has no rate of its own |
| Timing-Window FPS (f) | 240 | Unit windows are counted in for NaNDL (`w = N / f`) |
| Target Completion Time | 86400 s | For L\* |
| Respawn Time | 0 s | Added to every time position |
| Nerve / Fatigue / CPS | off | Constants default to NaNDL's; CPS is experimental |
| Solver Tolerance / Max Iterations | 1 s / 240 | |
| Count Releases | off | Presses only by default |
| Use Lower-Bound Windows | on | Off = treat them as unknown |
| Frame-Perfect / Tight thresholds | 1 / 2 frames | |
| Display Ranges | 0,1,2,3,4,5,6,8,10 | Buckets `[edge, next)`, last is open (10+) |
| Max Shift / Horizon / Recheck / Determinism | 24 ticks / 1.5 s / on / on | Measurement |
| Simulation Time per Frame | 14 ms | Speed vs responsiveness |
| Color Theme, Automatic Saving, Background Calculation | Rainbow / on / on | |

## Project layout
```
src/core/      Geode-free library (tested): Json, MsgPack, Formats (parsers + registry),
               Macro (validation), Analysis (model, stats, exports, sample), Nandl (math + solver)
src/engine/    MeasurementEngine (replay trials) + Hooks (GD hooks)
src/ui/        AnalyzerPopup + Dashboard / Inputs / Graphs / Precision / Settings tabs, detail popup
src/Session.*  loaded data, background precision job, saving
src/Settings.* typed snapshot of the mod settings
tests/         standalone test suite + GDR2 fixtures
```
Adding a macro format = one new `FormatHandler` registered in `FormatRegistry` (`src/core/Formats.cpp`).
