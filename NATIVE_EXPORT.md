# SolEngine Native Export & Single-Header SDK — Architecture Blueprint

> **Purpose:** This document defines the long-term architecture for exporting `.eats` games and Stories as standalone, natively compilable applications (Windows, macOS, Linux, and Web/PWA) backed by a modular library of SolEngine single-header C++ runtime files and an automated GitHub Template Repository workflow.

---

## 1. Vision: The Three Pillars of SolEngine

SolEngine carries forward the core philosophy of **Eatsbits**: scripts shouldn't be trapped inside a monolithic runtime—they should be able to graduate into independent, standalone native software.

Together with [STORIES_ENGINE.md](STORIES_ENGINE.md), this completes a three-pillar engine design:
1. **Play:** Enjoy a massive, fast-starting library of classic and modern solitaire games.
2. **Create:** Author new solitaire variants, decks, themes, foley packs, and full fairytale **Stories** in self-contained `.eats` files.
3. **Ship:** Export any `.eats` game or Story into a standalone, natively compiled app (`.exe`, `.app`, Linux binary, or Web/PWA) linking only the runtime headers it actually uses.

---

## 2. Single-Header SolEngine Runtime SDK

To make standalone compilation clean, portable, and tree-shakeable, SolEngine's runtime is factored into a suite of `stb`-style single-header C++ libraries (`#define SOL_*_IMPLEMENTATION` in one translation unit).

### 2.1 Proposed Header Modules

| Header | Responsibility | Optional? |
| :--- | :--- | :--- |
| `sol_core.h` | Cards, piles, `BoardBlackboard`, `TurnTransaction`, RNG, core types | Required |
| `sol_board.h` | `SolitaireBoard`, undo/redo, layout engines, rule dispatch | Required |
| `sol_eatscript.h` | EatScript lexer/parser/evaluator/VM (`eatsbits::eatscript`) & `EatsPackageLoader` | Required (Tier 1) |
| `sol_deck.h` | Card texture cache, `evecz` binary vector loader, procedural/low-poly decks, card backs | Required |
| `sol_renderer.h` | WebGPU (`wgpu-native` / Emscripten WebGPU) 2.5D tabletop renderer, lighting, post-FX | Required |
| `sol_ui.h` | 2D batch renderer, glyph/font engine, SVG icon renderer, themed dialogs & HUD | Required |
| `sol_proctex.h` | Procedural texture & tabletop shader runtime (`proctex`, `eatsdither`) | Optional (if procedural themes used) |
| `sol_foley.h` | Physical card foley synthesizer, ambience engine, music/stinger playback | Optional (if audio enabled) |
| `sol_solver.h` | Universal `SolitaireSolver` & `AutoPlayController` | Optional (`supports_solver`) |
| `sol_stories.h` | Stories extension (`StoryRuntime`, `StorySaveStore`, dialogue/map/shop UI) | Optional (if `sol.define_story` used) |

### 2.2 Architectural Prerequisite: Decoupling `TableRenderer`
Before generating single-header distributions, the main SolEngine app's `src/render/table_renderer.cpp` should be incrementally split into clean internal subsystems (renderer, card animation/input, UI chrome, app shell). Both the main SolEngine binary and exported standalone apps then compile against the exact same headers—preventing drift between the engine and exported builds.

---

## 3. Two-Tier Export Pipeline (`solcli export`)

SolEngine already contains the key compiler building blocks in `include/eatsbits/eatscript/`:
- **`DependencyScanner` (`dispatch_scanner.hpp`):** Scans EatScript ASTs to determine which subsystems and host APIs a project actually calls.
- **`Transpiler` (`transpiler.hpp`):** AOT C++ code generator.
- **`WGSLTranspiler` (`wgsl_transpiler.hpp`):** Compiles EatScript shaders directly to WGSL.
- **`VM` (`vm.hpp` / `bytecode.hpp`):** Lightweight bytecode runtime.

Rather than blocking export on 100% static C++ transpilation of every dynamic EatScript feature, export is structured in two tiers:

### Tier 1: Selective-Header + Embedded Payload Export (Primary Target)
1. `solcli export` parses the `.eats` project and runs `DependencyScanner` to detect required modules (e.g., whether `sol_stories.h`, `sol_solver.h`, `sol_foley.h`, or `sol_proctex.h` are needed).
2. `solcli` emits a minimal generated project (`main.cpp`, `embedded_project.hpp`, `CMakeLists.txt`) containing:
   - Only the `#include`s and `#define SOL_*_IMPLEMENTATION` guards required by the project.
   - The `.eats` script (or precompiled EatScript bytecode) and bundled assets encoded as `constexpr` byte arrays.
   - App metadata (window title, icon, default resolution, target entry game or Story ID).
3. Compiling produces a **self-contained native binary or `.wasm` bundle** with zero external `.eats` files, fast startup, and 100% EatScript language compatibility.

#### Example Generated `main.cpp`
```cpp
#define SOL_CORE_IMPLEMENTATION
#define SOL_BOARD_IMPLEMENTATION
#define SOL_EATSCRIPT_IMPLEMENTATION
#define SOL_DECK_IMPLEMENTATION
#define SOL_UI_IMPLEMENTATION
#define SOL_RENDERER_IMPLEMENTATION
#define SOL_FOLEY_IMPLEMENTATION
#define SOL_STORIES_IMPLEMENTATION

#include "sol_core.h"
#include "sol_board.h"
#include "sol_eatscript.h"
#include "sol_deck.h"
#include "sol_ui.h"
#include "sol_renderer.h"
#include "sol_foley.h"
#include "sol_stories.h"

#include "embedded_project.hpp" // Generated constexpr bytes for .eats payload & icon

int main(int argc, char** argv) {
    solengine::StandaloneAppConfig cfg{};
    cfg.appTitle       = "The Clockwork Chronicles";
    cfg.entryStoryId   = "clockwork_chronicles"; // or cfg.entryGameId
    cfg.embeddedSource = kEmbeddedEatsPayload;
    cfg.embeddedSize   = sizeof(kEmbeddedEatsPayload);
    return solengine::runStandaloneApp(cfg, argc, argv);
}
```

### Tier 2: Targeted AOT Compilation for Hot Paths (Future Optimization)
Once Tier 1 is shipping reliably, `solcli export` can selectively lower performance-critical code ahead-of-time:
- **Shaders -> Static WGSL:** `WGSLTranspiler` bakes tabletop and post-FX shaders at export time (zero runtime shader transpilation).
- **Audio / DSP -> Native C++:** `Transpiler` emits native C++ for custom foley/synth graphs.
- **Tight Rule Heuristics -> Native C++:** Optional AOT lowering of `eval_heuristic()` for fast solver lookahead, while keeping high-level Story/game orchestration on the VM.

---

## 4. Zero-Setup Creator Workflow: GitHub Template Repository

Most solitaire and Story creators shouldn't have to install Visual Studio, Xcode, CMake, or Emscripten to publish a game. A dedicated **GitHub Template Repository** (e.g., `solengine-export-template`) turns GitHub Actions into a free, automated cross-platform build farm.

### 4.1 Template Repository Layout
```text
my-solitaire-story/
├── app.json                    # App title, version, entry game/story, pinned SDK version
├── icon.png                    # Optional app icon (auto-converted to .ico / .icns / favicon)
├── game/
│   └── clockwork_story.eats    # Creator's .eats file(s)
└── .github/
    └── workflows/
        └── build.yml           # Pre-configured cross-platform build & release pipeline
```

#### Example `app.json`
```json
{
  "name": "The Clockwork Chronicles",
  "slug": "clockwork-chronicles",
  "version": "1.0.0",
  "entry_file": "game/clockwork_story.eats",
  "entry_story": "clockwork_chronicles",
  "solengine_sdk": "v0.4.0",
  "targets": ["windows", "macos", "linux", "web"],
  "deploy_github_pages": true
}
```

### 4.2 Automated GitHub Actions Pipeline

```
Creator pushes .eats file (even directly in the GitHub Web UI)
                          │
                          ▼
         ┌────────────────────────────────┐
         │  1. Validate & Export Job      │
         │  • Fetch pinned solengine_sdk  │
         │  • solcli validate             │
         │  • solcli export -> generated/ │
         └────────────────┬───────────────┘
                          │
       ┌──────────────────┼──────────────────┬──────────────────┐
       ▼                  ▼                  ▼                  ▼
┌──────────────┐   ┌──────────────┐   ┌──────────────┐   ┌──────────────┐
│   Windows    │   │    macOS     │   │    Linux     │   │  Web / PWA   │
│windows-latest│   │ macos-latest │   │ubuntu-latest │   │  Emscripten  │
│  -> .zip/.exe│   │ -> .dmg/.app │   │  -> AppImage │   │ -> .wasm/PWA │
└──────┬───────┘   └──────┬───────┘   └──────┬───────┘   └──────┬───────┘
       └──────────────────┼──────────────────┴──────────────────┤
                          ▼                                     ▼
         ┌────────────────────────────────┐      ┌──────────────────────────┐
         │  GitHub Release / Artifacts    │      │  GitHub Pages (Optional) │
         │  Direct download links for all │      │  Instant playable web    │
         │  desktop & web builds          │      │  URL for the Story/game  │
         └────────────────────────────────┘      └──────────────────────────┘
```

### 4.3 Creator Experience
1. Click **"Use this template"** on `solengine-export-template` to create a new repository.
2. Upload their `.eats` file into `game/` and edit `app.json` (can be done entirely in the browser).
3. Push a commit or tag (or click **Run workflow** in the Actions tab).
4. Within a few minutes, GitHub Actions produces downloadable Windows, macOS, Linux, and Web packages—and optionally publishes a live playable link via **GitHub Pages**.

---

## 5. Practical Guardrails & Operational Details

- **SDK Version Pinning:** Every template repo pins `"solengine_sdk": "vX.Y.Z"` in `app.json` and pulls that exact versioned release tarball (`solengine-sdk-vX.Y.Z.tar.gz`) containing `solcli` and the `sol_*.h` headers. Future SolEngine updates will never silently break an existing creator's repository.
- **Fast CI Builds via Caching:** Prebuilt `wgpu-native` binaries and the Emscripten SDK should be cached via `actions/cache` keyed on `solengine_sdk` version, keeping typical builds in the 1–3 minute range.
- **Desktop Code Signing vs. Web Friction:**
  - Unsigned Windows/macOS builds trigger standard SmartScreen/Gatekeeper warnings (expected for indie/itch.io downloads).
  - The **Web / PWA build on GitHub Pages** launches instantly with zero OS warnings.
  - Creators shipping commercial desktop releases can optionally add signing certificates to their repository's GitHub Secrets; `build.yml` can detect and apply them automatically when present.
- **Minimal Binary Footprint:** Selective header inclusion ensures a simple standalone Klondike variant doesn't link the Stories runtime or unused assets, and heavy embedded art/decks can be stored compressed (`zlib`/`zstd`) and decoded lazily on startup.
- **Zero Hosting Cost:** GitHub Actions is free for public repositories (and includes free monthly minutes for private ones), allowing SolEngine to offer automated cross-platform exports without operating custom build servers.

---

## 6. Phased Implementation Roadmap

### Phase 1: Modular Runtime & Single-Header Packaging
- Split `TableRenderer` into composable rendering, input/animation, UI chrome, and app-shell modules.
- Add a build/packaging script that generates the versioned `solengine-sdk` single-header bundle (`sol_*.h`) and verifies it compiles cleanly against a minimal test harness.

### Phase 2: `solcli export` (Tier 1)
- Extend `DependencyScanner` to recognize SolEngine namespaces (`sol.*`, `story.*`, `audio.*`, `ui.*`).
- Implement `solcli export <file.eats> --out <dir>` to emit `main.cpp`, `embedded_project.hpp`, `CMakeLists.txt`, and only the required `sol_*.h` headers.
- Publish versioned `solengine-sdk` release tarballs (headers + `solcli` binaries for Win/macOS/Linux) via SolEngine's own release workflow.

### Phase 3: `solengine-export-template` Repository
- Create the public GitHub template repository with `app.json`, sample `.eats` project, and `.github/workflows/build.yml`.
- Wire up the 4-target build matrix (Windows, macOS, Linux, Web/PWA) with `wgpu-native`/Emscripten caching, GitHub Release asset uploads, and optional GitHub Pages auto-deploy.

### Phase 4: Tier 2 AOT & Local One-Click Polish
- Add ahead-of-time WGSL shader baking and DSP/heuristic C++ lowering to `solcli export`.
- Optionally integrate `zig cc` or a bundled toolchain option for creators who want one-click local native builds outside of GitHub Actions.
