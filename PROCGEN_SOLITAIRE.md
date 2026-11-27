# Procedural Solitaire Layouts (`PROCGEN_SOLITAIRE.md`)

> **Core Breakthrough:** SolEngine separates **solitaire mechanics** from **card geometry**, turning seeded procedural layouts into playable level design. In aesthetic games like **Memory Engine**, procedural layouts deliver endless spatial variety; in occlusion-driven games like **Golf Engine** and **Card Mahjong Engine**, every procedural shape generates a genuinely different dependency graph and tactical puzzle.

---

## 1. Why This Is Seminal for SolEngine

Traditional solitaire collections grow by duplicating rules into dozens of near-identical variants (*Clockwork Dial* vs. *Black Hole*, *Memory Figure-8* vs. *Clockwork Memory*). SolEngine already pioneers procedural cards, backgrounds, audio foley, and ambience — **procedural layouts** complete the picture:

1. **Zero Library Clutter:** Instead of shipping 15 slightly different TriPeaks or Memory clones, SolEngine provides seminal **Engine** games (*Memory Engine*, *Golf Engine*, *Card Mahjong Engine*) that generate infinite, valid, seeded boards.
2. **Geometry as Gameplay:** Because `ScriptRuleEngine::isCardBlocked()` evaluates 2D Oriented Bounding Box (SAT) occlusion directly from card poses and `zIndex` layers, *any* procedural shape automatically produces accurate blocking, uncovering, and auto-flip rules — no hardcoded DAG tables required.
3. **Aspect-Ratio Invariance:** All procedural layouts are authored in **card-unit space** (1 card = `1.0 × 1.4` units) and uniformly auto-fitted into the viewport's safe area, ensuring the exact same overlap/blocking graph on desktop widescreen, ultrawide, and mobile portrait.
4. **Decoupled Deal & Layout Seeds (`deal:layout`):** Players who discover a procedural board they love can **Lock Current Layout** in the Seed Dialog (`#`). Locking pins `layoutSeed_` while **Random / New Deal** reshuffles the cards (`seed_`), and dual seeds (`9104:4821`) can be copied, pasted, and replayed from Seed History.

---

## 2. The 3-Layer Procedural Layout Architecture

Every procedural layout flows through three composable stages:

```text
[1. Generators]                [2. Symmetry Operators]       [3. Constraints & Auto-Fit]
  - Parametric Curves            - layout_mirror_x()           - layout_prune_overlaps(pad, even)
    (Clockwork, Figure-8,        - layout_mirror_y()           - layout_trim_to(maxN)
     Archway, Wave, Rose)        - layout_mirror_xy() (4x)     - Game-specific deck/pair parity
  - Proven-Part Motifs           - layout_rotate_n(N)            via on_configure(layout_seed)
    (Quadrant clusters)          (Preserves explicit zIndex!)  - layout_apply(...) uniform fit
  - ASCII Layout DSL
    layout_from_ascii("""...""")
```

### EatScript Procedural Layout API

| Function | Purpose |
| --- | --- |
| `on_configure(layout_seed)` | Pre-deal hook that configures `tableaus`, `tableau_deal_counts`, and deck filters before deck construction. |
| `config_tableaus(n, per_tab)` / `config_deal_counts(list)` | Sets dynamic zone count and cards-per-zone per seed. |
| `config_deck(decks, suits, exclude_ranks)` | Guarantees exact card counts (e.g., exact rank pairs for Memory and Card Mahjong). |
| `config_all_face_up(bool)` | Toggles initial face-up state dynamically. |
| `get_seed()` / `get_layout_seed()` | Returns deal seed and effective layout seed (locked or coupled). |
| `seed_hash(s, i)` / `seed_int(s, i, lo, hi)` / `seed_float(s, i)` | Pure stateless deterministic mixing (safe across window resizes; never mutates `rngState_`). |
| `layout_begin()` / `layout_add(ux, uy, deg, z)` | Builds slots in card-width units (`1.0 × 1.4`). |
| `layout_mirror_x()` / `layout_mirror_y()` / `layout_mirror_xy()` | Mirrors motifs across axes while preserving `zIndex` layers. |
| `layout_rotate_n(n)` | Replicates a motif radially $N$ times while preserving `zIndex` layers. |
| `layout_preset(name, seed)` | Built-in seeded generator (`"random"`, `"clockwork"`, `"figure8"`, `"archway"`, `"wave"`, `"diamond"`, `"symmetry"`, `"grid"`). |
| `layout_from_ascii(diagram, col_step, row_step, z, ox, oy)` | Draws layouts directly from raw multiline ASCII art (`\|`, `-`, `/`, `\`, `(`, `)`, and `1`–`9` for z-layers). |
| `layout_prune_overlaps(pad, enforce_even)` | Removes OBB-colliding slots and optionally enforces even pair counts. |
| `layout_apply(first_zone, bw, bh, pad_top, pad_bot, pad_x)` | Computes exact rotated bounding box and uniformly scales/centers all zones. |
| `check_obb_overlap(...)` | Exposes native 2D SAT OBB overlap test directly to EatScript. |

### Companion UX Systems
- **Native Bitmask Clearing Solver:** [solitaire_solver.cpp](file:///c:/git/solengine/src/game/solitaire_solver.cpp) precomputes a 64-bit `blockingMask[64]` from `ScriptRuleEngine::isCardBlockedByPile`, enabling millisecond-speed solvable deal verification and full-length Game Browser previews across arbitrary procedural clearing layouts.
- **Smart Empty-Space Stat Cards:** [table_renderer.cpp](file:///c:/git/solengine/src/render/table_renderer.cpp) scores 9 open-space candidates around the active piles to place stat cards in empty felt pockets, with hover fade/pass-through and a unified collapse toggle (`−` header button, hotkey `I`, and Command Palette `"Cycle Stat Cards"`).

---

## 3. Proven Implementations

### Memory Engine (`games/memory.eats`) — *Aesthetic & Spatial Procgen*
- Replaced standalone *Memory (Figure-8)* and *Clockwork Memory* variants.
- Calls `layout_preset("random", lseed)` across 7 families (*Clockwork*, *Figure-8*, *Archway*, *Twin Wave*, *Diamond Palace*, *Symmetry*, *Grid*) with asymmetric cutouts and guaranteed zero-overlap even pairs (`12–36` cards).
- Proved `on_configure(layout_seed)`, stateless seed hashing, card-unit auto-fit, and Lock Layout (`deal:layout`).

### Golf Engine (`games/golf.eats`) — *Mechanical Occlusion Procgen*
- Replaced standalone *Clockwork Dial* while keeping classic *Golf*, *TriPeaks*, and *Black Hole*.
- Combines **layered planar shapes** (1 card/zone across 2–4 `zIndex` layers: *Peaks*, *Archway*, *Cascading Wave*, *Diamond Fortress*, *Symmetry*, and layered *ASCII Maps* — *Hourglass*, *Crown*, *Twin Towers*, *Butterfly*) with **outward-fanned radial spokes** (*Clockwork*).
- Proved that procedural layouts genuinely transform solitaire strategy: every shape creates a unique blocking DAG, auto-flips its front layer on deal, and solves natively via the 64-bit OBB blocking bitmask.

### Windmill (`games/windmill.eats`) — *ASCII Layout DSL Showcase*
- Classic 2-deck Windmill (*Propeller*) authored with two overlaid `layout_from_ascii(...)` diagrams (`\ / #` angled corner foundations + `| -` radiating windmill sails) — zero manual trig or pixel math.

### Card Mahjong Engine (`games/mahjong.eats`) — *Multi-Story Stacked Pairing*
- Bridges **Memory** (equal-rank pair matching) and **Golf** (multi-layer blocking stacks).
- Deals 36–52 face-up cards (always multiples of 4 so every rank appears as 2 pairs) across 3–4 stacked layers (*Turtle*, *Ziggurat*, *Fortress*, *Twin Peaks*, *Archway*, *Diamond Temple*, *Clockwork Stack*).
- Uses `can_target` / `on_target` + `is_card_blocked` so players peel exposed matching pairs from the top layers down to unlock buried cards.

---

## 4. Future Procedural Solitaire Roadmap

### A. Pyramid / Pairing Engine *(Sums to 13)*
- **Concept:** Apply the Golf Engine layered procedural pipeline to Pyramid solitaire (pair unblocked cards summing to 13; Kings clear solo) with Stock/Waste or open *Giza* mode.
- **Mechanical Payoff:** Twin Pyramids, Ziggurats, Hourglasses, Archways, and ASCII Temples dramatically alter which complementary ranks are trapped beneath each other.
- **Engine Fit:** Can share the exact same 64-bit `blockingMask` native solver extension for fast solvable deals.

### B. Adjacency / Neighbor Engine *(Procedural Monte Carlo)*
- **Concept:** Pair equal-rank cards that are **physically adjacent** on the felt (within `~1.6 × cw` center distance), rather than hardcoded 5×5 grid indices.
- **Mechanical Payoff:** The layout literally defines the adjacency graph — *Rings* wrap around, *Figure-8s* form high-traffic central hubs, *Diamonds* create tight diagonal clusters, and *Waves* create narrow chokepoints.

### C. Open-Information Pure-Skill Engine *(Procedural Black Hole / Giza)*
- **Concept:** Zero Stock, all 52 cards face-up from frame 1 across procedural layered stars, spirals, dual rings, or fanned clusters.
- **Mechanical Payoff:** Turns every procedural seed into a deterministic, zero-luck tactical puzzle where the entire clearing sequence can be planned from move 1.

### D. Procedural Dungeon & Campaign Solitaire *(Fairytale / RPG Solitaire)*
- **Concept:** Combine procedural layered layouts with SolEngine's card tags (`add_card_tag`, `card_has_tag`, `set_card_stat`) to turn each board into a **themed dungeon chamber**:
  - **Shape = Room Archetype:** *Archway* = Castle Gate, *Diamond Fortress* = Vault, *Cascading Wave* = River Crossing, *ASCII Map* = Boss Chamber.
  - **Mechanical Twists:** Clearing one wing uncovers a **Key** card that unlocks a **Sealed/Locked** card on the opposite wing; **Wild/Torch** cards clear any rank; **Hazard/Curse** cards require adjacent clearing; **Treasure** cards grant bonus streaks or relics across a multi-room run.
  - **Progression:** Chain 3–5 seeded procedural rooms into a roguelike solitaire run where the player's remaining Stock or score carries forward.
