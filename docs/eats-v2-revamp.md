# `.eats` v2 Format & API Revamp Plan

## 1. Overview & Goals

SolEngine uses `.eats` (Eatscript) across seven distinct asset categories:
- **Games** (`games/` — 28 files)
- **Themes** (`themes/` — 19 files)
- **Sounds / Foley** (`sounds/` — 9 files)
- **Ambience** (`ambience/` — 7 files)
- **Post-FX** (`postfx/` — 6 files)
- **Presets** (`presets/` — 4 files)
- **Decks** (`decks/` — 3 files)

**Target capabilities for `.eats` v2:**
1. **Self-describing assets:** Every `.eats` file declares what it defines via explicit API calls rather than relying on which directory it lives in.
2. **Single-file bundles & game series:** One `.eats` file can define a full game series (multiple games) alongside custom decks, tabletop themes, ambience, foley sound packs, post-FX, and presets without variable or function-name collisions.
3. **Seamless GitHub Gist sharing (`?gist=`):** Players can load either a single bundled `.eats` file or a multi-file Gist containing an entire themed series, and have all assets registered and ready to play immediately.
4. **Dynamic discovery across all categories:** No hardcoded UI lists; custom assets loaded from disk, drag-and-drop, or Gist appear automatically in the Game Browser and Settings UI.

---

## 2. Current Bottlenecks

- **Folder-coupled discovery:** `GameRegistry::scanDirectory` (`src/game/game_definition.cpp`) only scans `/games`; themes, ambience, sounds, and decks each look in their own fixed folders.
- **Hardcoded UI options:** `src/ui/settings_dialog.cpp` hardcodes `soundPacks[9]` and `ambOptions[8]` instead of querying runtime registries.
- **Limited drop / web routing:** `runStagedEatsDrop` (`src/render/table_renderer.cpp`) only binds `sol.set_theme`, `sol.set_background`, `sol.set_deck`, `sol.set_corner_radius`, and string-matches for `audio.` / `define_sound`. It cannot register games, ambience, post-FX, or presets.
- **Single-file Gist limit:** `loadFromGist` (`web/index.html`) breaks on the first `.eats` file in a Gist, writes it to `/decks/`, and ignores any remaining files.
- **Global-scope collisions:** Games and presets currently rely on top-level variables (`name = ...`, `tableaus = ...`, `deck_slug = ...`) and global rule functions (`def can_pick`, `def can_drop`, `def on_deal`), making it impossible to define multiple games or a game + preset in one script.

---

## 3. `.eats` v2 Unified API Design

All categories move under explicit, scoped `sol.define_*` constructors. Eatscript already provides `Evaluator::invokeCallable(const Value& callable, ...)` (`include/eatsbits/eatscript/evaluator.hpp`), allowing rule callbacks and shader functions to be passed directly as values inside definition dictionaries.

### 3.1 Games (`sol.define_game`)
```python
def dial_on_deal():
    # ...

def dial_can_pick(z, i):
    # ...

def dial_can_drop(from_z, i, to_z):
    # ...

def dial_eval():
    # ...

sol.define_game({
    id: "clockwork_dial",
    name: "Clockwork Dial",
    family: "Golf",
    category: "GOLF",
    layout: "radial_clock",
    description: "12 radial clock spokes fanning outward around a central dial.",
    decks: 1,
    tableaus: 12,
    foundations: 1,
    freecells: 0,
    tableau_deal_counts: [3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3],
    tableau_all_face_up: False,
    foundation_starter_card: True,
    stock_deal_mode: "deal_to_foundation",
    stock_deal_count: 1,
    max_redeals: 0,
    demo_seed: 2026,
    demo_seeds: [1, 7, 13, 21, 2026],
    # Recommended bundled/paired assets (optional)
    preset: "clockwork_parlor",
    # Bound rule callbacks (no global name collision)
    on_deal: dial_on_deal,
    can_pick: dial_can_pick,
    can_drop: dial_can_drop,
    eval_heuristic: dial_eval,
    doc: {
        meta: [2, "Quick", 3, "Balanced", 2, "Casual"],
        aliases: ["Radial Clock", "Sun Dial", "Watch"],
        goal: ["Clear all 12 radial clock spokes onto the center Foundation.", "No playable cards remain on the dial and the center Stock is empty."],
        zones: [
            { index: 0, name: "Clock Spokes (1-12)", desc: "12 radial piles fanning outward." },
            { index: 12, name: "Center Foundation", desc: "Build +-1 in rank regardless of suit.", examples: ["KH", "AS", "2D"] },
            { index: 13, name: "Center Stock", desc: "Click to deal a fresh card onto the center Foundation." }
        ]
    }
})
```

### 3.2 Themes (`sol.define_theme`)
Separates tabletop materials/shaders cleanly from card decks and binds the shader function explicitly:
```python
def sands_shader(u, v, nx, ny, in_r, in_g, in_b, px, py):
    # ...
    return [r, g, b]

sol.define_theme({
    id: "sands",
    name: "Sands",
    author: "Maddest Labs",
    description: "Pure procedural sand tabletops.",
    version: 1,
    procedural: true,
    lacquer: 0.2,
    relief_bump: 2.6,
    option_label: "Surface",
    options: [
        { id: "fine", name: "Fine Beach", u: [0.92, 0.79, 0.58, 0.2] }
    ],
    shader: sands_shader
})
```

### 3.3 Decks (`sol.define_deck`)
Replaces overloaded `sol.set_theme` + top-level `format` / `deck_blob` variables:
```python
sol.define_deck({
    id: "another_world",
    name: "Another World Low-Poly",
    author: "SolEngine",
    description: "Flat-shaded 1991 Another World-style pure binary vector deck.",
    corner_radius: 0.0,
    version: 1,
    format: "evecz",
    blob: "eNp0vQ14..."
})
```

### 3.4 Ambience (`sol.define_ambience`)
Standardizes `ambience.define_environment` under `sol.define_ambience`:
```python
sol.define_ambience({
    id: "rain",
    name: "Rain",
    master_gain: 1.0,
    layers: [ ... ]
})
```

### 3.5 Sound Packs (`sol.define_sound_pack`)
Replaces global `audio.set_master_volume` / `audio.set_physical_params` calls with a named pack:
```python
sol.define_sound_pack({
    id: "default",
    name: "Default",
    master_volume: 0.85,
    master_variance: 1.0,
    physical_params: {
        velvet_density: 4400.0,
        edge_snap: 0.90,
        air_boost: 5.5,
        comb_delay: 0.00018,
        felt_damping: 7500.0,
        card_rigidity: 3200.0
    }
    # Optional custom embedded/procedural sounds & sequences
})
```

### 3.6 Post-FX (`sol.define_postfx`)
Standardizes `sol.set_postfx`:
```python
sol.define_postfx({
    id: "crt_trinitron",
    name: "Trinitron CRT",
    author: "Maddest Labs",
    description: "CRT scanlines, subtle barrel curvature, and RGB phosphor mask",
    filter: "crt",
    option_label: "Tube",
    gpu_cost: "Low",
    options: [
        { id: "curved", name: "Curved Glass", param1: 0.95 },
        { id: "flat",   name: "Flat Aperture", param1: 0.35 }
    ]
})
```

### 3.7 Presets (`sol.define_preset`)
```python
sol.define_preset({
    id: "classic_parlor",
    name: "Classic Parlor",
    description: "Traditional felt tabletop with classic cards and warm lighting.",
    deck: "classic",
    card_back: "classic_sapphire",
    card_wear: 1,
    card_stock: 1,
    card_corners: 0,
    card_deviation: 0.35,
    theme: "shader_royal_velvet",
    table_tint: "auto",
    postfx: "none",
    postfx_intensity: 1.0,
    show_table_markings: true,
    sound_pack: "vintage_bicycle",
    ambience: "off",
    ambience_volume: 0.55,
    lighting: {
        preset: 6,
        angle: 0,
        shadow_gobo: 1,
        waver: 0.45,
        waver_speed: 0.30,
        flicker: 0.0,
        gobo_softness: 0.50,
        cloud_opacity: 0.15,
        god_rays: 0.25,
        dust_motes: 0.35,
        scene_darkness: 0.22,
        light_strength: 1.20
    }
})
```

### 3.8 Game Series (`sol.define_series`)
Enables bundling a curated sequence or collection of games with shared theming:
```python
sol.define_series({
    id: "clockwork_chronicles",
    name: "Clockwork Chronicles",
    author: "Maddest Labs",
    description: "A 3-part mechanical solitaire trilogy.",
    preset: "clockwork_parlor",
    games: ["clockwork_dial", "escapement_freecell", "pendulum_klondike"]
})
```

---

## 4. Phased Implementation Plan

### Phase 1: Dynamic Registries & Eatscript Callable Support
- Verify/extend Eatscript `Evaluator` so `def` functions can be stored in `Value` dictionaries and invoked via `Evaluator::invokeCallable`.
- Ensure **Ambience**, **Sound Packs**, **Post-FX**, **Presets**, **Decks**, **Themes**, and **Series** each have a singleton runtime registry (mirroring `GameRegistry`).
- Update `src/ui/settings_dialog.cpp` to populate Sound Pack and Ambience options dynamically from their registries instead of hardcoded arrays.

### Phase 2: Unified `EatsPackageLoader`
- Create a unified `EatsPackageLoader` that registers all `sol.define_*` functions in a single `Evaluator` context.
- Route startup directory scanning (`games/`, `themes/`, `decks/`, `ambience/`, `sounds/`, `postfx/`, `presets/`, `user/`), `runStagedEatsDrop`, and `solengine_load_file_from_web` through `EatsPackageLoader`.
- Temporarily keep a legacy fallback adapter during migration so the engine remains runnable at every step.

### Phase 3: Multi-File & Bundle Support in Web `?gist=`
- Update `loadFromGist` in `web/index.html` to iterate **all** `.eats` files in `gistData.files` (instead of stopping at the first `.eats` file).
- Stage downloaded files under `/user/gists/<gistId>/<filename>` in Emscripten's virtual filesystem rather than `/decks/`.
- After loading all files in a Gist (or a single bundled `.eats`), automatically apply any newly registered `preset` / `series` and deal the first game (or open the Game Browser filtered to the series).

### Phase 4: Series & Bundled Preset UX
- Add `seriesId` / `seriesName` / `seriesOrder` / `defaultPreset` support to `SolitaireGameDef` and `GameBrowserDialog`.
- Add a Series filter/badge in the Game Browser and auto-apply a game's or series's referenced preset when launching.

### Phase 5: Migrate Existing `.eats` Files (76 Total)
Migrate category-by-category, smallest to largest:
1. `decks/` (3 files)
2. `presets/` (4 files)
3. `postfx/` (6 files)
4. `ambience/` (7 files)
5. `sounds/` (9 files)
6. `themes/` (19 files)
7. `games/` (28 files)

*(Most of the structural wrapping can be scripted, followed by manual verification.)*

### Phase 6: Legacy Removal & Cleanup
- Remove old folder-specific `.eats` parsers, string-matching fallbacks in `runStagedEatsDrop`, and any remaining hardcoded asset lists.
- Update `README.md` / authoring docs with `.eats` v2 examples for creators.
