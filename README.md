# Extreme Picker (Geode mod)

Recommends your next Extreme Demon from the AREDL, based on the tags of the extremes you've already beaten. See [about.md](about.md) for what it does in game.

Targets Geode 5.11.2 / Geometry Dash 2.2081 (Windows, macOS, Android).

## Build option 1: GitHub (no tools to install)

1. Open the **Actions** tab. The "Build Geode Mod" workflow runs on every push.
2. When it finishes, download **Build Output** from the run page. Inside is the `.geode` file.
3. Builds of the `main` branch are also published under **Releases**.

## Build option 2: on your PC

Install the [Geode CLI and SDK](https://docs.geode-sdk.org/getting-started/), then in this folder:

```
geode build
```

The CLI installs the mod into your game automatically.

## Installing a built .geode file

Put it in `Geometry Dash/geode/mods/` and restart the game.

## How picks are scored

- **Tag match**: cosine similarity between a level's tags and your average tag profile. Tags that are rare on the list count for more than tags nearly every level has. `NONG` and `2P` are ignored.
- **Difficulty fit**: harder levels must be within "Max jump" places of your hardest, and smaller jumps score higher. Easier levels score higher the closer they are to your hardest.
- **Enjoyment** (optional): up to a 15% boost from the list's enjoyment rating.

Final score = match x fit x enjoyment. The logic lives in `src/Recommender.cpp` and has a standalone test in `tests/`.

## Files

- `src/main.cpp`: adds the button to the main menu
- `src/PickerPopup.cpp`: downloads the list, finds your completions, draws the popup
- `src/Aredl.cpp`: parses the AREDL API response
- `src/Recommender.cpp`: the scoring
