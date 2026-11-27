# SolEngine — Videogame & Roguelike Solitaire Architecture

> **Status:** Design Proposal (Discussion Draft)  
> **Scope:** General-purpose engine primitives that enable roguelike solitaires (*Scoundrel*, *Balatro*-adjacent deckbuilders, dungeon crawlers) and adventure/puzzle solitaires (*Fairway*, *Fairytale*, *Regency*, *TriPeaks Adventures*, *Shenzhen Solitaire*) purely through `.eats` scripts — without hardcoding bespoke game assets into core C++.

---

## 1. Design Philosophy

SolEngine is a general-purpose procedural solitaire engine. Rather than baking one-off creatures, sprites, or bespoke game logic into C++, videogame solitaire features should be modeled as **small, composable, procedural primitives**:

1. **Zero-asset procedural rendering:** Overlays, seals, chains, frost, timers, and tokens are drawn through `BatchRenderer2D` in world space — 0 KB of bitmap assets, proportional `cw × ch` scaling, and full participation in 3D lighting, clouds, and gobo shadows.
2. **Strict determinism & undo safety:** Gameplay state lives in `BoardOp`-recorded card stats, tags, and blackboard entries; visual juice (shakes, flashes, floating numbers, token lerps) lives purely in the renderer and never mutates logical board state or solver search.
3. **Classic solitaires stay 100% untouched:** Games that don't invoke these hooks incur zero visual or CPU overhead.

---

## 2. What SolEngine Already Provides (~80% of the Logic Layer)

Most of the **rules and state** machinery for roguelike and videogame solitaires already exists in `ScriptRuleEngine` (`src/game/script_rule_engine.cpp`):

- **Per-card state & tags (atomic + undoable):**
  - `set_card_stat(z, i, key, val)` / `card_stat(z, i, key, default)`
  - `add_card_tag(z, i, tag)` / `remove_card_tag(z, i, tag)` / `card_has_tag(z, i, tag)`
  - Visual selection glow already wired for `card_has_tag(..., "selected")`.
- **Arbitrary spatial layouts & occlusion:**
  - `on_layout(bw, bh)`, `set_zone_pose(z, x, y, angle, layer)`, `set_card_size(cw, ch)`
  - `is_card_blocked(z, i)` / `is_zone_blocked(z)` (2D SAT oriented-bounding-box occlusion).
- **Dynamic deck & board mutation:**
  - `spawn_card(z, suit, rank, face_up, idx)`, `remove_card(z, i)`, `extract_card(fz, i, tz)`, `shuffle_zone(z)` (deterministic RNG).
- **Reactive triggers & timed sequences:**
  - `on_deal`, `on_card_click`, `on_stock_click`, `on_drop`, `on_turn_end`, `schedule_action(delay, fn, lock_input)`.
- **Diegetic tabletop UI:**
  - `set_board_card`, `add_card_row`, `add_card_bar`, `add_card_pill`, `set_zone_badge` (world-space, scene-lit, responsive).
- **Multi-stage progression metadata:**
  - `series_id`, `series_name`, `series_order` already exist on `SolitaireGameDef` and `games/manifest.json`.

---

## 3. Core Primitive 1: Procedural Card Overlays (In-Card & Overhanging)

Because `TableRenderer` renders cards in world space via `BatchRenderer2D` **without per-card scissor clipping**, an overlay attached to a card `(cx, cy, cw, ch, angle)` can sit **inside the card, wrap its border, or overhang onto the felt** (casting its own drop shadow onto both the card and the table), while still respecting card Z-order (cards stacked on top naturally cover overlays beneath).

### Proposed Data Model (on `Card` or `BoardBlackboard`)
```cpp
struct CardOverlay {
    std::string kind;   // "ice", "vine", "lock", "seal", "timer", "curse", "wild", "shield", "blocker"
    std::string label;  // optional short text (e.g. "3", "x2", "KEY")
    std::string anchor; // "full", "border", "top_right_overhang", "top_left_overhang", "bottom_overhang", "center"
    int layers = 1;     // optional multi-hit durability / stack count
    bool active = true;
};
```

### Proposed Eatscript API
- `set_card_overlay(z, i, kind, label, anchor)`
- `clear_card_overlay(z, i, kind)` (or `clear_card_overlays(z, i)`)

### High-Value Built-In Procedural Overlay Styles
| `kind` | Visual Treatment | Example Mechanics |
| :--- | :--- | :--- |
| `ice` / `frost` | Translucent crystalline rim + diagonal fracture lines (cracks deepen as `layers` drops) | Frozen cards requiring 1–2 clicks or adjacent clears to thaw |
| `vine` / `leaf` | Botanical diagonal bands wrapping over card edges onto the felt | Overgrown/covered cards that must be swept or unlocked |
| `lock` | Metallic chain band + overhanging corner padlock glyph | Locked cards opened by clearing a `"key"` card or paying a resource |
| `seal` | Embossed wax seal perched half-on-card, half-on-table with drop shadow | Cursed cards, boss cards, elite encounters, or protected relics |
| `timer` | High-contrast overhanging corner badge showing countdown number (`3`, `2`, `1`) | Bombs, expiring buffs, monster attack cooldowns |
| `wild` | Subtle iridescent/gold foil border shimmer | Wild cards, Jokers, upgraded/enchanted cards |
| `shield` / `armor` | Corner buckler badge with numeric durability | Armored monsters or weapon durability in *Scoundrel* |
| `curse` | Dark violet smoky rim + sigil | Roguelike curses, hexes, or poisoned cards |

*(Future extension: allow `.eats` vector-stamp callbacks so custom themes/games can draw bespoke procedural icons onto overlays.)*

---

## 4. Core Primitive 2: Roaming Tabletop Tokens

For mechanics where an entity sits **on top of a card** and **moves from card to card across the table** (e.g., a roaming curse, a monster aggro marker, a hopping frog/spider blocker, or a player pawn):

### Proposed Eatscript API
- `set_board_token(id, kind, target_zone, target_card_idx, anchor, label)`
- `remove_board_token(id)`

### Behavior
- Rendered in world space above the target card (with optional overhang onto the table and soft drop shadow).
- When `target_zone` / `target_card_idx` changes between turns, `TableRenderer` smoothly interpolates `(x, y)` with a slight arc/hop (`sin(pi * t)` elevation + shadow offset), giving tactile board-game movement with zero animation boilerplate in `.eats`.

---

## 5. Core Primitive 3: Roguelike Juice & Visual Feedback

All feedback effects are **fire-and-forget renderer timers** (stored in `TableRenderer`, ignored during solver simulation `suppressWidgets == true`):

### Proposed Eatscript API
1. **`shake_card(z, i, intensity, duration)`**
   - Damped horizontal + slight rotational wobble on a single card.
   - Use cases: hitting a monster, clicking a locked/frozen card, weapon durability clash, curse trigger.
2. **`shake_camera(intensity, duration)`**
   - Brief damped world-space camera jolt (respecting an optional "Reduce Screen Shake" accessibility setting).
   - Use cases: heavy HP damage, bomb detonation, boss kill, big combo/clear.
3. **`flash_card(z, i, style, duration)`**
   - Quick rim/fill color pulse (`"damage"` crimson, `"heal"` emerald, `"gold"` amber, `"curse"` violet, `"block"` steel-blue).
4. **`float_text(z, i, text, style)`**
   - Crisp world-space floating combat/score text that drifts upward and fades out over ~0.75s (e.g., `"-8 HP"`, `"+5 HP"`, `"BLOCKED!"`, `"+$25"`, `"x3!"`).

---

## 6. Core Primitive 4: Roguelike Run & Progression Patterns

Many roguelike solitaire mechanics already fit SolEngine cleanly or only need tiny additions:

| Pattern | How It Maps to SolEngine |
| :--- | :--- |
| **Relics / Passive Boons** | Display active relics as `add_card_pill` / `add_card_row` entries on a diegetic `RELICS` board card; check `get_stat("RELIC_*", 0)` inside rule hooks. |
| **Card Draft / Shop Rooms** | Deal 3 face-up choice cards into dedicated zones; `on_card_click` applies the chosen boon/card (`spawn_card`, `set_stat`) and advances to the next encounter. |
| **Multi-Floor / Boss Runs** | Either multi-stage within a single `.eats` game (like *Bowling Solitaire*'s 10-frame loop using `shuffle_zone`) OR persistent run state across `series_id` games. |
| **Custom Procedural Deck Modifiers** | Combine `set_card_stat` (e.g. `"bonus_dmg"`, `"upgraded"`) with `set_card_overlay(..., "wild", "+2")` so upgraded cards stay visually distinct even when using standard playing-card decks. |

### Ideal Flagship Vertical Slice: Upgrading *Scoundrel*
Before building a brand-new roguelike game, *Scoundrel* (`games/scoundrel.eats`) is the perfect testbed to validate every primitive in one afternoon:
- **Damage / Heal Juice:** `shake_card` + `flash_card(..., "damage")` + `float_text(..., "-9 HP")` + `shake_camera` when fighting a monster bare-handed; emerald `flash_card` + `float_text(..., "+6 HP")` on potions.
- **Weapon Cap & Durability:** `shield` / `seal` overhanging corner overlay on the equipped weapon card showing its active cap (`<= 8`).
- **Elite / Cursed Variant:** Optional *"Scoundrel: Cursed Depths"* variant using `curse` / `timer` / `lock` overlays and relic pills.

---

## 7. Suggested Implementation Phases

- **Phase A — Juice Hooks (smallest, highest immediate payoff):**
  - `shake_card`, `shake_camera`, `flash_card`, `float_text` in `TableRenderer` + `ScriptRuleEngine`.
  - Wire into *Scoundrel* (and optionally *Bowling Solitaire* strikes / *Golf* streaks).
- **Phase B — Procedural Card Overlays:**
  - `CardOverlay` list on `Card` (or keyed in `BoardBlackboard` so `Card` stays tiny/POD), `set_card_overlay` / `clear_card_overlay` in `ScriptRuleEngine`, overhanging vector rendering in `TableRenderer`.
  - Start with 5 core kinds: `ice`, `lock`, `seal`, `timer`, `curse` (+ `wild`).
- **Phase C — Roaming Board Tokens:**
  - `set_board_token` / `remove_board_token` with arc-hop interpolation in `TableRenderer`.
- **Phase D — Roguelike Showcase Game(s):**
  - Build a dedicated roguelike solitaire (or *Scoundrel* expansion / *Dungeon TriPeaks*) exercising all primitives together.

---

## 8. Open Questions for Focused Discussion

1. **Overlay ownership:** Should overlays live on `Card` (automatically traveling with the card when `move_cards` moves it — great for curses/upgrades/timers) or on `Zone` (staying on the slot — great for frozen/locked grid spaces), or support both?
2. **Accessibility & Toggle:** Should `shake_camera` have a global `Reduce Motion` toggle in Settings?
3. **First roguelike target:** Do we want to upgrade *Scoundrel* first as the vertical slice, or design a new roguelike solitaire concept (e.g., *Dungeon TriPeaks*, *Cursed Klondike*, or a deckbuilding *Poker/Cribbage* roguelike)?
4. **Cross-deal persistence:** Do we want a small `run_state` / `campaign_state` blackboard that persists across deals or `series_id` stages (also useful for cumulative Vegas bankroll!)?
