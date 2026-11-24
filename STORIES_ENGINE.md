# SolEngine Stories Engine — Architecture & Extension Blueprint

> **Purpose:** This document defines the architecture, EatScript API, and Core-vs-Extension boundaries for **Stories** in SolEngine—enabling creators to author rich, multi-game narrative experiences (fairytale adventures, RPG campaigns, roguelite runs, historical solitaire anthologies, and interactive tutorials) packaged into single `.eats` files.

---

## 1. Vision & Terminology

Internally and externally, these experiences are called **Stories** (`sol.define_story`, `StoryRuntime`, `StoryRegistry`, `story.*`).

A **Story** is a self-contained `.eats` package that chains together **any existing built-in SolEngine game** (e.g., Klondike, Golf, Pyramid, Scoundrel) and/or **bundled custom games** defined in the same file, unified by:
- A persistent outer state machine (chapters, quests, branching choices, hub maps).
- Cross-game progression (currency, inventory, unlocked nodes, star ratings, story flags, save slots).
- Game mutators and active abilities that twist standard solitaire rules without duplicating game scripts.
- Declarative narrative UI (dialogue modals, chapter maps, shops, script-driven HUDs).
- Bundled visual and audio assets (themes, decks, card backs, portraits, icons, foley, ambience, music).

### Relationship to Existing SolEngine Systems
SolEngine's `.eats` v2 architecture already provides the building blocks for single-file multi-asset bundles:
- **`EatsPackageLoader` (`src/game/eats_package_loader.cpp`):** Parses a single `.eats` file and registers games, themes, decks, ambience, sound packs, post-FX, presets, and `GameSeriesDef` (`sol.define_series`) without global namespace collisions.
- **`ScriptRuleEngine` (`src/game/script_rule_engine.cpp`):** Executes per-game EatScript hooks with atomic, undoable `BoardOp` transactions, `BoardBlackboard` stats/strings, per-card tags/stats, custom layouts (`on_layout`), and 3D victory choreography.
- **`GameSeriesDef` (`include/solengine/game_definition.hpp`):** Currently provides static grouping and Game Browser filtering (`SERIES` category), but has no runtime progression, persistence, or flow control.

Stories build directly on top of these foundations as a dedicated **meta-layer** that outlives any single `SolitaireBoard` deal.

---

## 2. Core vs. Extension Architecture

Because Stories span narrative UI, persistence, node graphs, asset caching, and audio/music orchestration, the Stories Engine should be built as a **SolEngine Extension** (e.g., `solengine-stories`, registered via `EatsModuleDescriptor` / `EatsHostInterface` in `include/eatsbits/abi/eats_host_abi.h` or an optional CMake module).

Keeping Stories modular preserves SolEngine's fast-startup, minimalist core while letting the Stories runtime load lazily only when a user opens a Story or browses the Stories tab.

```
+-------------------------------------------------------------------+
|                      SolEngine Core (Lean)                        |
|  SolitaireBoard | ScriptRuleEngine | TableRenderer | AudioFoley   |
|  EatsPackageLoader | GameRegistry  | SessionManager | Host ABI    |
+-------------------------------+-----------------------------------+
                                |  Thin Extension Seam
                                |  (Events, Mutators, UI Slot,
                                |   Package Hook, Audio/Overlay API)
+-------------------------------v-----------------------------------+
|                   SolEngine Stories Extension                     |
|  StoryRegistry | StoryRuntime (Persistent EatScript Evaluator)    |
|  SaveSlotStore | Node Graph Engine | Mutator & Ability Manager    |
|  Declarative UI (Dialogue, Map, Shop, Story HUD) | Asset Cache    |
+-------------------------------------------------------------------+
```

### 2.1 What Belongs in SolEngine Core (Thin Enablers)
These are small, general-purpose improvements that also benefit standalone games and keep the extension cleanly decoupled:
1. **`EatsPackageLoader` Extension Hook:** Allow registered extensions to bind additional `sol.define_*` handlers (e.g., `sol.define_story`, `sol.define_image`, `sol.define_vector`, `sol.define_card_back`, `sol.define_music`) during package load.
2. **Shared Package Source Storage:** Replace per-definition `std::string scriptSource` copies in `EatsPackageLoader` with a shared immutable package reference (`std::shared_ptr<const std::string>` or package ID table) so large multi-game + art `.eats` bundles do not duplicate memory.
3. **Board Lifecycle Event Hooks:** Emit clean callbacks/events from `SolitaireBoard` / `TableRenderer` on `onDealStart`, `onTurnCommitted`, `onVictory`, `onDefeat`, and `onAbort`, returning a structured summary (score, moves, elapsed time, remaining cards, `BoardBlackboard` snapshot).
4. **Rule Middleware / Mutator Chain:** Allow an active extension to register pre/post rule hooks alongside `ScriptRuleEngine` (e.g., `on_deal_post`, `can_pick_override`, `can_drop_override`, `on_turn_end_post`, `get_status_override`) so any existing game can be modified at runtime.
5. **Script-Triggered Audio (`audio.play`):** Replace the `noopFn` audio stubs in `ScriptRuleEngine` with live `AudioFoleyEngine` dispatch so both standalone games and Stories can play foley/SFX by ID.
6. **Generalized Tag-Driven Card Overlays:** Extend card rendering beyond hardcoded RPG stat badges (`hp`, `attack`, `durability`) to support generic tag-driven overlays (icon badge, corner counter, frame/tint, lock/chain/ice overlay).
7. **Declarative UI Overlay Slot:** Provide a layered UI render/input hook in `TableRenderer` (below card-drag animations for HUD elements; above the board for modal dialogs/maps/shops) so the Stories extension can render themed UI without duplicating the renderer.

### 2.2 What Belongs in the Stories Extension
- `StoryRegistry` & `StoryDef` metadata (title, author, cover art, chapter list, tags, default preset).
- `StoryRuntime`: A persistent EatScript `Evaluator` instance that stays alive across deals and drives the Story state machine.
- `StorySaveStore`: Per-story variable persistence, save slots, chapter unlock/star records, and auto-save on node transitions.
- Declarative Story UI renderers (Dialogue/Choices modal, Node/Overworld Map view, Shop/Inventory modal, Story HUD & Ability Bar, Banners/Toasts).
- Story Browser / Title Screen UI ("Continue", "New Story", "Chapters", progress percentage).
- Story-scoped asset cache (lazy-loaded portraits, map backgrounds, icons, music tracks—evicted when exiting the Story).

---

## 3. EatScript Authoring API (`sol.define_story`)

A single `.eats` file can define custom assets, bundle one or more custom games, reference any built-in SolEngine game, and wire them together with `sol.define_story`.

```python
# 1. Optional bundled assets (decks, themes, card backs, art, presets, games)
sol.define_card_back({
    id: "clockwork_crest",
    name: "Clockwork Crest",
    format: "svg",
    data: "<svg ...>...</svg>"
})

sol.define_image({
    id: "portrait_archivist",
    format: "svg", # or "evecz", "png_b64", "proctex"
    data: "<svg ...>...</svg>"
})

sol.define_preset({
    id: "clockwork_parlor",
    name: "Clockwork Parlor",
    deck: "classic",
    card_back: "clockwork_crest",
    theme: "shader_arcane_parchment",
    ambience: "fireplace",
    sound_pack: "default"
})

# Optional bundled custom game (or reference any built-in game ID)
sol.define_game({
    id: "brass_vault_golf",
    name: "Brass Vault",
    # ... standard .eats v2 game definition ...
})

# 2. Story logic & lifecycle callbacks
def on_story_start():
    story.set_var("gold", 25)
    story.set_var("keys", 0)
    story.set_var("chapter", 1)
    story.goto_node("intro")

def on_node_enter(node_id):
    if node_id == "intro":
        ui.show_dialogue({
            speaker: "The Archivist",
            portrait: "portrait_archivist",
            text: "Three brass seals guard the Grand Orrery. Clear the first seal to earn a key.",
            choices: [
                { id: "begin", label: "Approach the First Seal" },
                { id: "tavern", label: "Visit the Tinker's Bench (Shop)" }
            ]
        })
    elif node_id == "seal_1":
        # Reuse a built-in SolEngine game with a Story preset and a quest mutator
        story.launch_game("klondike_draw_1", {
            title: "Chapter 1: The Sunlit Seal",
            preset: "clockwork_parlor",
            seed: 4201,
            mutators: ["golden_aces"],
            abilities: ["peek_stock", "brass_wildcard"],
            blackboard_in: { GOLD: story.get_var("gold", 0) }
        })
    elif node_id == "seal_2":
        # Chain directly into a bundled custom game from this same .eats file
        story.launch_game("brass_vault_golf", {
            title: "Chapter 2: The Brass Vault",
            preset: "clockwork_parlor"
        })

def on_choice(node_id, choice_id):
    if choice_id == "begin":
        story.goto_node("seal_1")
    elif choice_id == "tavern":
        story.goto_node("shop_1")

def on_game_victory(node_id, result):
    reward = 50 + result["score"] // 10
    story.set_var("gold", story.get_var("gold", 0) + reward)
    story.set_var("keys", story.get_var("keys", 0) + 1)
    audio.play("quest_fanfare")
    ui.show_toast("+" + str(reward) + " Gold!")
    if node_id == "seal_1":
        story.unlock_node("seal_2")
        story.goto_node("overworld")
    elif node_id == "seal_2":
        story.complete("true_ending")

def on_game_defeat(node_id, result):
    ui.show_dialogue({
        speaker: "The Archivist",
        portrait: "portrait_archivist",
        text: "The gears jammed before the seal broke. Shall we wind the spring again?",
        choices: [
            { id: "retry", label: "Retry Chapter" },
            { id: "map", label: "Return to Map" }
        ]
    })

# 3. Story registration
sol.define_story({
    id: "clockwork_chronicles",
    name: "The Clockwork Chronicles",
    author: "Maddest Labs",
    version: 1,
    description: "A 3-chapter fairytale solitaire quest through the Grand Orrery.",
    cover_image: "portrait_archivist",
    default_preset: "clockwork_parlor",
    nodes: [
        { id: "intro",     type: "dialogue", title: "Prologue" },
        { id: "overworld", type: "map",      title: "The Grand Orrery" },
        { id: "seal_1",    type: "game",     title: "The Sunlit Seal",  game: "klondike_draw_1" },
        { id: "seal_2",    type: "game",     title: "The Brass Vault",  game: "brass_vault_golf" },
        { id: "shop_1",    type: "shop",     title: "Tinker's Bench" }
    ],
    on_start: on_story_start,
    on_node_enter: on_node_enter,
    on_choice: on_choice,
    on_game_victory: on_game_victory,
    on_game_defeat: on_game_defeat
})
```

---

## 4. Detailed Subsystem Specifications

### 4.1 Story Meta-Runtime & Node Graph (`StoryRuntime`)
- **Persistent Evaluator:** Unlike `ScriptRuleEngine` (which resets per deal), `StoryRuntime` keeps one live `Evaluator` for the active Story session.
- **Node Types:** `dialogue`, `game`, `map`, `shop`, `cutscene` (or arbitrary script-controlled states).
- **Flow Control API (`story.*`):**
  - `story.goto_node(node_id)` — Transition to another node and invoke `on_node_enter(node_id)`.
  - `story.launch_game(game_id, opts)` — Deal a built-in or bundled game with optional overrides (`seed`, `preset`, `theme`, `deck`, `ambience`, `postfx`, `title`, `mutators`, `abilities`, `blackboard_in`).
  - `story.apply_preset(preset_id)` — Switch visual/audio atmosphere between story beats.
  - `story.unlock_node(node_id)` / `story.set_node_stars(node_id, stars)` — Track progression on chapter maps.
  - `story.complete(ending_id)` — Mark the Story finished and record completion/ending metadata.

### 4.2 Cross-Game Persistence & Save Slots (`StorySaveStore`)
- **Serializable Variables:**
  - `story.get_var(key, default)` / `story.set_var(key, value)` supporting `int`, `float`, `bool`, `string`, and nested `list`/`dict` (serialized as JSON).
  - Scoped by `(story_id, slot_id)` so multiple Stories never collide.
- **Storage Backends:**
  - **Desktop:** JSON files under the user data directory (e.g., `saves/stories/<story_id>/slot_<n>.json`), cleanly separate from `SessionManager`'s quick-resume board snapshot.
  - **Web / PWA:** `localStorage` or IndexedDB keyed by `solengine.story.<story_id>.slot.<n>`.
- **Story <-> Board Bridge:**
  - `blackboard_in` seeds initial `BoardBlackboard` stats/strings before `on_deal()`.
  - `on_game_victory(node_id, result)` and `on_game_defeat(node_id, result)` receive a dictionary containing `score`, `moves`, `seconds`, `undo_count`, `seed`, `cards_remaining`, and a copy of final `stats` / `strings` from `BoardBlackboard`.

### 4.3 Game Mutators & Active Abilities
To make chaining existing solitaire variants feel fresh without copy-pasting entire rule scripts:
- **Mutators (`sol.define_mutator`):** Lightweight rule overlays attached to any game deal via `mutators: [...]`:
  - `on_deal_post()` — Tag specific cards (e.g., `"locked"`, `"cursed"`, `"gold_key"`), set move limits, or swap cards.
  - `can_pick(z, i)` / `can_drop(from_z, i, to_z)` — Add restrictions (e.g., locked cards cannot be picked until adjacent cards are cleared) or relax rules (e.g., wildcards).
  - `on_turn_end()` — Decrement move counters, tick hazards, or grant combo bonuses.
  - `get_status()` — Override or augment win/lose conditions (e.g., *"Clear 3 Gold Cards within 40 moves"*).
- **Active Abilities / Power-Ups (`sol.define_ability`):**
  - Rendered as clickable action slots in the Story HUD bar (icon, label, cost/charges, tooltip).
  - Clicking an ability checks `can_activate()` and invokes `on_activate()` with live `ScriptBoardContext` access (e.g., shuffle stock, burn a blocking card, spawn a wildcard, peek face-down cards) inside a normal undoable `TurnTransaction`.

### 4.4 Declarative Story UI Primitives (`ui.*`)
All Story UI is **data-driven and engine-rendered** so it automatically matches SolEngine's UI theme, scales cleanly across desktop and web viewports, and supports keyboard/controller/touch input:
- **`ui.show_dialogue({ speaker, portrait, text, pages, choices, backdrop })`:** Multi-page story beats, character portraits, optional typewriter reveal, and branching choice buttons (with optional `enabled` / `cost_label` fields).
- **`ui.show_map({ background, nodes, edges })`:** Interactive chapter/overworld node graph with coordinates (`x`, `y` normalized `0..1`), node status (`"locked"`, `"available"`, `"completed"`), star/medal badges, and click-to-enter dispatch (`on_node_select(node_id)`).
- **`ui.show_shop({ title, currency_label, currency_value, items })`:** Declarative grid/list of purchasable relics, abilities, card backs, or themes (`on_shop_buy(item_id)`).
- **`ui.set_hud(items)` / `ui.show_banner(title, subtitle)` / `ui.show_toast(text)`:** Lightweight in-game quest tracker (objectives, move counter, currency, active ability bar) rendered cleanly in the HUD layer without clashing with card animations.

### 4.5 Bundled Visual Assets & Card Overlays
Extending single-file `.eats` packaging for fairytale presentation:
- **`sol.define_image` / `sol.define_vector` / `sol.define_proctex`:** Register named visual assets (portraits, map backgrounds, item icons, cover art) using SVG, `evecz` binary vector blobs, base64 PNG/WebP, or procedural `proctex` graphs—decoded lazily when a Story is opened.
- **`sol.define_card_back`:** First-class custom card back definitions (procedural, SVG, or `evecz`) that Stories can apply per chapter or grant as unlockable rewards.
- **Tag-Driven Card Overlays (`sol.define_card_overlay`):** Map card tags (e.g., `"frozen"`, `"golden"`, `"cursed"`, `"wild"`) to visual treatments (tint/glow, border frame, corner icon, badge counter) rendered in `TableRenderer` / `CardCanvas`.

### 4.6 Script-Triggered Audio & Music
- **Live Foley / SFX API:** Expose `audio.play(sound_id, { gain, pitch })` to both `ScriptRuleEngine` and `StoryRuntime` (aligning with the foley API goal in `todo-local.md`).
- **Music & Atmospheric Transitions:** Add `sol.define_music` (embedded compressed audio or procedural/sequenced tracks) alongside `audio.play_music(id, { fade_sec, loop })`, `audio.stop_music({ fade_sec })`, and `audio.set_ambience(id, { fade_sec })` so chapter shifts feel cinematic.

---

## 5. Packaging, Performance & Tooling

- **Fast Startup Preservation:** Startup directory scanning (`stories/` or `user/`) should only parse lightweight Story headers/metadata for the browser list; heavy embedded assets (portraits, maps, bundled decks, music) should decode lazily when the player actually launches a Story.
- **Memory Deduplication:** `EatsPackageLoader` should store each `.eats` file's source buffer once (`std::shared_ptr<const std::string>`) and reference it from games, themes, and stories instead of copying the full file text per definition.
- **CLI Validation (`solcli`):** Add a `solcli validate-story <file.eats>` command that checks:
  - Syntax and callable resolution for all Story, game, mutator, and ability hooks.
  - Referential integrity of all referenced `game` IDs, `preset` IDs, `node` targets, and asset IDs.
  - Reachability of nodes from `on_start` / initial nodes.
- **Dev Inspector (Debug Build / Toggle):** An optional overlay panel for Story authors to jump directly to any `node_id`, inspect/edit `story.*` variables live, simulate victory/defeat, and hot-reload the `.eats` file without restarting SolEngine.
- **Web & `?gist=` Sharing:** Opening a `.eats` Story via drag-and-drop or `?gist=` should auto-register all bundled assets and open directly onto the Story's Title / Continue screen.

---

## 6. Phased Implementation Roadmap

### Phase 0: Core Engine Enablers (Small, Standalone-Valuable Changes)
- Deduplicate `scriptSource` storage in `EatsPackageLoader`.
- Add extension registration hooks to `EatsPackageLoader` and `SolitaireBoard` lifecycle events.
- Expose `audio.play` inside `ScriptRuleEngine`.
- Add `sol.define_card_back` and generalized tag-driven card overlays.

### Phase 1: Stories Extension MVP (Playable Linear & Branching Stories)
- Create the `solengine-stories` extension module (`StoryRegistry`, `StoryRuntime`, `StorySaveStore`).
- Implement `sol.define_story`, `story.get_var`/`set_var`, `story.goto_node`, `story.launch_game`, and `story.complete`.
- Implement `ui.show_dialogue` (text + portraits + branching choices) and basic **Stories** tab in the Game Browser.
- *Outcome:* Creators can already ship full branching multi-game Stories chaining built-in and custom games in a single `.eats` file.

### Phase 2: Mutators, Abilities & Tactile Depth
- Add `sol.define_mutator` (rule middleware wrapping existing games) and `sol.define_ability` (HUD power-up bar).
- Add `ui.set_hud`, `ui.show_banner`, and `ui.show_toast`.
- Wire tag-driven card overlays (locks, gold cards, curses, wildcards) to mutators.

### Phase 3: Full Fairytale Presentation & Authoring Polish
- Add `ui.show_map` (interactive chapter/overworld graph) and `ui.show_shop` (inventory/upgrades).
- Add `sol.define_image` / `sol.define_vector` / `sol.define_proctex` and `sol.define_music` with crossfade transitions.
- Add `solcli validate-story` and the in-engine Story Dev Inspector with hot-reload.
