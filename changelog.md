# v1.0.0
- Fast Measurement: trials restart from a verified checkpoint just before the input, surviving trials stop as soon as the movement is back to the original run, and several ticks run per update. All three are verified and fall back automatically (setting to turn it off).
- Default playback sound "Death Bong": 1-frame clicks play it at normal pitch, wider windows play it higher (adjustable pitch rise per range).
- Sounds tab: choose a sound (built-in or your own file) for each frame-window range; playback plays it on every click in that range.
- Live playback counter (top left) showing how many clicks of each range have been played.
- New "Counter" color theme and integer range labels (1, 2, 3, 4, 5-6, 7-8, 9-12, 13-16, 17+), now the default ranges; Settings → Counter Style applies both.
- Record mode: records your own run (normal or practice), saves it as .gdr2 and loads it into the analyzer when you finish the level.
- Playback mode: replays the macro and marks frame-perfect (or tight / all measured) clicks with a ring and their frame window.
- Initial release: macro import (GDR2, GDR JSON/MessagePack, NaNDL JSON, CSV), in-game frame-window measurement, dashboard, input table, graphs, NaNDL precision solver, JSON/CSV/NaNDL export and saved analyses.
