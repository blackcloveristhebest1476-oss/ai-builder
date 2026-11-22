# Click Corrector — Geometry Dash (Geode) mod

Click a little too early and the mod holds your click back and sends it on the right frame.

You choose **Frames Early** (default 10). A click that comes **1 to 10 frames before** the right moment is moved to that moment. A click that is 11 or more frames early, on time, or late is left exactly as you made it.

Requires **Geode v5.11+** on **Geometry Dash 2.2081** (Windows, macOS, Android).

## How it knows the right moment

The mod uses **your own clicks**. The first time you beat a level with the mod installed, it saves the frame of every press and release from that run. Any completion from the start counts, including **practice mode with checkpoints**.

From then on, each of your presses is compared with the next unused press of that saved run. If yours is 1..N frames early, it is sent on the saved frame instead. Nothing is ever clicked for you: if you do not click, nothing happens.

There is no macro import. The only timings the mod knows are ones you made yourself.

## Using it

1. Install the `.geode` file (Geode → mods folder) and restart the game.
2. Open a level and beat it once (practice mode is fine).
3. Play. The counter in the top-left corner shows how many clicks were corrected this attempt.
4. Pause → **CC** to switch it on or off, change Frames Early, or delete your saved run of the level.

## Settings

| Setting | Default | What it does |
|---|---|---|
| Correct My Clicks | on | Master switch. |
| Frames Early | 10 | How early a click may be and still be corrected (1–120). |
| Correct Releases Too | on | Also holds back letting go of the button when you release early (ship, wave, robot). |
| Learn From My Completions | on | Every completion from the start replaces your saved run of the level. Turn off to keep the one saved now. |
| Safe Mode | on | An attempt with at least one corrected click does not save a new best or a completion. |
| Show Counter | on | The small counter in the corner. |

## Good to know

- **A frame is one physics tick, 1/240 s.** 10 frames is about 42 ms. This is the same unit macro bots use; it is not your monitor's frame rate.
- **Late clicks are never corrected.** By the time you click, the right frame has already passed.
- **A corrected click is delayed.** Clicking 10 frames early means your jump happens 10 frames after you pressed, which is the point.
- **Corrected runs are assisted runs.** Safe Mode is on by default so they do not count as real completions. Do not submit them to leaderboards or lists.
- A run saved in practice mode can be slightly off, because GD's checkpoints do not always restore the exact same state. A normal-mode completion is more reliable.
- Not supported: platformer levels and start positions (your saved run counts frames from the start of the level).
- Levels with random triggers play differently every attempt, so a saved run does not fit them.
- Click Between Frames / Click Between Steps give clicks sub-frame timing; corrected clicks land on whole frames.

Your saved runs are stored in the mod's save folder under `references/`, one file per level.

## Building

Push to GitHub: the **Build Geode Mod** workflow builds every platform. On `main` it also publishes a release with the `.geode` file. To build locally, install the Geode CLI and SDK, then `geode build`.
