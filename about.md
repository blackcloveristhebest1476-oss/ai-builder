# Frame Window Analyzer

Import a macro and find out how forgiving every click really is.

- **Import** GDR2, GDR (JSON or MessagePack) replays, NaNDL calculator exports and frame-window CSV tables.
- **Measure** each click in the real level: the macro is replayed with that one input moved earlier and later, tick by tick, until the player dies. You get early frames, late frames and the total window, all labelled *Verified*, *Lower bound*, *Inconsistent* or *Not measurable*.
- **Record** your own run: beat the level and it is saved as a .gdr2 and loaded straight into the analyzer.
- **Play back** the macro with a ring on every frame-perfect click, showing its frame window.
- **Explore** the results in a sortable, searchable table, an interactive per-input graph and a histogram of frame-window ranges.
- **Estimate** the precision needed to beat the level with the NaNDL model (base, nerve, fatigue and the experimental CPS modifier), cross-checked against the reference calculator.
- **Export** JSON, CSV or a NaNDL calculator file.

Open it from the **pause menu** (chart button) or with **Alt+F** (change it in the settings).

Measurement needs the level open, practice mode off and no start position. Results are statistical estimates, not guarantees.
