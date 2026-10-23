# AI Builder — Geometry Dash mod

Select a structure in the editor, press the **AI** button, describe a design, and AI builds it.

- **Nothing selected** → builds a brand-new design near your camera (on the ground if you're looking at the floor).
- **Something selected** → uses it as a reference/anchor. Either **add** to it ("add chains hanging under this platform") or tick **Replace selection** to redesign it ("make this a neon outline version").
- The whole build is one undo step (**Ctrl+Z**). New objects come out selected, so you can move/scale them right away.
- It can set color channels for pixel art (turn off in settings).

Requires **Geode v5.11+** on **Geometry Dash 2.2081**. GD 2.209 is not supported until Geode fully supports it.

## 1. Get an API key

The mod calls an AI model over the internet, so you need a key from a provider:

| Provider setting | Where to get a key | Model examples |
|---|---|---|
| Anthropic (Claude) — default | console.anthropic.com → API Keys | `claude-sonnet-5-5` (best balance), `claude-opus-5-5`, `claude-haiku-5-5` (cheapest) |
| OpenAI-compatible | platform.openai.com, openrouter.ai, etc. | whatever that service offers |
| OpenAI-compatible + Ollama (free, local) | no key — run Ollama on your PC | Base URL `http://localhost:11434/v1` |

API usage is billed by the provider. A typical build is a few cents.

## 2. Build the mod

### Option A — GitHub Actions (no tools to install)
1. Create a new GitHub repository and upload this whole folder. Move `build-workflow.yml` to `.github/workflows/build.yml` (on GitHub: *Add file → Create new file*, name it `.github/workflows/build.yml`, and paste the contents in).
2. Open the **Actions** tab; the "Build Geode Mod" workflow runs automatically.
3. When it finishes, download **Build Output** and unzip it. Inside is `alexl.ai-builder.geode`.

### Option B — Build on your PC (Windows)
1. Install **Visual Studio 2022 Build Tools** with "Desktop development with C++" (include *C++ Clang tools for Windows*), plus **CMake** and **Git**.
2. Install the Geode CLI: `winget install GeodeSDK.GeodeCLI` (or download from github.com/geode-sdk/cli/releases).
3. In a terminal:
   ```
   geode config setup
   geode sdk install
   geode sdk install-binaries
   cd path\to\ai-builder
   geode build
   ```
   `geode build` puts the `.geode` file straight into your GD mods folder if `config setup` found your GD install.

## 3. Install & use
1. Copy `alexl.ai-builder.geode` into `Geometry Dash/geode/mods/` (skip if `geode build` already did) and restart GD.
2. Geode menu → **AI Builder** → settings (gear) → paste your **API Key**.
3. Open a level in the editor. The **AI** button sits next to undo/redo at the top-left.
4. Select something (or nothing), press **AI**, type a description, hit **Generate**.

Example prompts (the **Example** button cycles through these):
- `a medieval castle with two towers, battlements and a gate`
- `pixel art of a red mushroom with white spots, 16x16`
- `a short fair cube section with spikes, a jump pad and an orb`
- `turn this into a glowing neon version with outline blocks` (with Replace on)

## Settings
| Setting | What it does |
|---|---|
| AI Provider | Anthropic, or any OpenAI-compatible API |
| API Key / Model / Base URL | Connection details |
| Max Objects Per Build | Safety cap (default 600) |
| Max Output Tokens | Raise for huge builds (~12 tokens per object) |
| Apply Color Channels | Lets the AI set channel colors (uses channels 50+ to avoid yours) |
| Only Known Object IDs | Restricts the AI to a verified palette + IDs in your selection, so it never places garbage objects |

## Troubleshooting
- **"HTTP 401"** — wrong API key. **"HTTP 404 / model not found"** — check the Model name.
- **"cut off or malformed"** — the design was too big for the token limit; raise *Max Output Tokens* or ask for something smaller.
- Logs: enable the Geode console (Geode settings) to see the exact prompt and the AI's raw reply.

## Files
- `src/main.cpp` — adds the AI button to the editor
- `src/AIBuilderPopup.*` — the popup UI
- `src/AIClient.*` — prompt building, API calls, response parsing
- `src/Builder.*` — reads your selection and places the generated objects
- `src/Palette.hpp` — the object IDs the AI may use (cross-checked against the object catalog from the MIT-licensed EditorAI mod by entity12208)
