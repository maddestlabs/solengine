---
id: ["klondike_draw_1", "klondike_draw_3"]
name: "Klondike"
aka: ["Classic Solitaire", "Patience", "Canfield (historical British/early US confusion)", "Fascination", "Triangle", "Seven-Card Klondike"]
family: "Klondike"
ancestor: null
decks: 1
info_type: "half_open"
mechanic: "building"
status: "documented"
eatscript: "../../../games/klondike.eats"
video: "Y7oD9qvcFKw"
---

# Klondike (Draw 1 & Draw 3)

> **One-line summary:** The quintessential single-deck patience game — build 4 suit foundations Ace-to-King by unpacking a 7-column triangular tableau (alternating colors, descending) and cycling a 24-card stock.

| Field | Value |
| :--- | :--- |
| **Game IDs** | `klondike_draw_1`, `klondike_draw_3` |
| **Family** | [Klondike Family](./README.md) |
| **Decks** | 1 (52 cards) |
| **Information Model** | Half-Open (21 face-down tableau cards + 24 face-down stock cards at deal) |
| **Status** | `documented` · `implemented` |
| **Eatscript Source** | [games/klondike.eats](../../../games/klondike.eats) |
| **Play Online** | [Play Draw 1](https://solitaireengine.org/?game=klondike_draw_1) · [Play Draw 3](https://solitaireengine.org/?game=klondike_draw_3) |
| **Video Walkthrough** | [YouTube (`Y7oD9qvcFKw`)](https://www.youtube.com/watch?v=Y7oD9qvcFKw) |

### Video Walkthrough

[![Watch Klondike Walkthrough](https://img.youtube.com/vi/Y7oD9qvcFKw/hqdefault.jpg)](https://www.youtube.com/watch?v=Y7oD9qvcFKw)

*[Watch the SolEngine Klondike Walkthrough on YouTube](https://www.youtube.com/watch?v=Y7oD9qvcFKw)*

---

## 1. History & Lineage

- **Early Print Documentation & The Great Name Swap:** Despite being the world's best-known solitaire today, Klondike does not appear in the earliest 19th-century English anthologies such as `[Cadogan1870]` or the first edition of `[Dick1883]`. One of the earliest documented printings of the 7-column game appears in the 1907 *Hoyle's Games* under the name **"Seven-Card Klondike"** — while plain **"Klondike"** in that same volume referred to the 13-card reserve game now called *Canfield* `[Hoyle1907]`, `[Parlett1979]`.
- **Standardization (1913):** In the 1913 edition of *The Official Rules of Card Games*, the U.S. Playing Card Company standardized the modern American names: the 7-column triangular game became **Klondike**, and the Saratoga casino reserve game became **Canfield** `[USPC1913]`. However, because early 20th-century revisions of *Dick's Games of Patience* (e.g., 1908/1912) and British manuals continued printing the 7-column game under the name "Canfield", the two names remained swapped in British usage for decades `[Dick1898]`, `[KellerSolLab]`, `[Parlett1979]`.
- **20th-Century Codification & Digital Era:** `[MoreheadMottSmith1949]` cemented Klondike's standard American ruleset (including Turn-3 and Turn-1 variants) and popularized its strategic terminology. Its inclusion as the bundled *Solitaire* (`sol.exe`, written by Wes Cherry) in Microsoft Windows 3.0 (1990) made Draw 1 and Draw 3 Klondike globally synonymous with the word "Solitaire".
- **Lineage:** Klondike spawned a vast family of mechanical descendants — including *Easthaven* (replacing the waste pile with Spider-style row deals), *Thoughtful Solitaire* (all cards dealt face-up for perfect information), *Whitehead* (same-color building), and *Yukon* (removing the stock entirely and allowing arbitrary face-up substack picks) `[Parlett1979]`.

> [!NOTE]
> **Unverified / Folklore Claims:** Popular lore attributes Klondike's invention to prospectors during the **Klondike Gold Rush** (1896–1899) in the Yukon Territory. While the game's rise in popularity coincides with the Gold Rush era, card-game historians note there is **no primary print evidence** directly linking its creation to gold-rush miners `[Parlett1979]`, `[KellerSolLab]`.

---

## 2. Formal Rules

### Setup & Deal
- **Deck:** 1 standard 52-card deck.
- **Tableau (7 columns):** Dealt in a left-to-right triangle of `1, 2, 3, 4, 5, 6, 7` cards (28 cards total). Only the top card of each column is dealt face-up; the remaining 21 tableau cards begin face-down.
- **Stock & Waste:** The remaining 24 cards form the face-down Stock; the Waste pile starts empty.
- **Foundations (4 piles):** Start empty.

### Play Mechanics
- **Foundations:** Build **up in suit** from Ace (`1`) to King (`13`). Only single cards may be placed onto a Foundation.
- **Tableau:** Build **down in rank** in **alternating colors** (Red on Black, Black on Red).
  - Any face-up card in a tableau column (together with all cards stacked on top of it) may be moved as a unit onto another tableau column whose top card is one rank higher and of opposite color.
  - Exposing a face-down tableau card automatically flips it face-up at turn end.
  - Only a **King** (or a face-up sequence led by a King) may be placed into an empty tableau column.
- **Stock & Waste:**
  - **Draw 1 (`klondike_draw_1`):** Clicking Stock turns 1 card face-up onto the Waste. Only the top Waste card is playable.
  - **Draw 3 (`klondike_draw_3`):** Clicking Stock turns up to 3 cards face-up onto the Waste. Only the top Waste card is playable.
  - **Redeal:** Clicking an empty Stock turns the entire Waste pile back over into the Stock, preserving order.

### Win & Loss Conditions
- **Victory:** All 52 cards are moved to the 4 Foundations.
- **Loss / Deadlock:** No legal moves remain that can progress the board state.

### Rule Ambiguities & SolEngine Conventions

| Rule Point | Historical / Regional Variations | SolEngine Convention |
| :--- | :--- | :--- |
| **Redeals (Draw 1)** | Traditional print rules (`[MoreheadMottSmith1949]`) allow **1 pass only** (0 redeals) for Draw 1, whereas modern digital clients allow unlimited redeals. | **Unlimited redeals** (`max_redeals: -1`) for both Draw 1 and Draw 3, matching modern digital expectations. |
| **Redeals (Draw 3)** | Unlimited redeals (standard) vs. 3-pass Vegas scoring. | **Unlimited redeals** (`max_redeals: -1`). |
| **Worrying Back** (Foundation → Tableau) | Allowed in Windows Solitaire and Blake & Gent's solver model `[BlakeGent2019]`; disallowed in some strict print rules. | **Currently disallowed** in `klondike_can_pick()` (picks are permitted only from Waste `z == 1` and face-up Tableau `z in 6..12`). *Note: worth revisiting if strict Windows/Thoughtful parity is desired.* |
| **Empty Columns** | Kings only (standard) vs. any card (relaxed variants). | **Kings only** (`empty_tableau: "king"`). |

---

## 3. Eatscript Implementation Mapping

Defined in [games/klondike.eats](../../../games/klondike.eats).

### Zone Map
| Zone Index | Role | Count | Initial State |
| :---: | :--- | :---: | :--- |
| `0` | Stock | 1 | 24 cards face-down |
| `1` | Waste | 1 | Empty |
| `2..5` | Foundations | 4 | Empty |
| `6..12` | Tableau Columns | 7 | `[1, 2, 3, 4, 5, 6, 7]` cards, top card face-up |

### Callbacks & Shared Helpers
- `klondike_can_pick(z, i)` ([klondike.eats:L25-L33](../../../games/klondike.eats#L25-L33)): Permits picking the top card of Waste (`z == 1`) or any face-up card in Tableau (`6..12`). Because tableau drops enforce alternating-color descending builds, every face-up substack in standard Klondike is already a valid ordered run.
- `klondike_can_drop(from_z, i, to_z)` ([klondike.eats:L48-L70](../../../games/klondike.eats#L48-L70)): Enforces single-card Ace-to-King same-suit drops on `2..5`, and King-on-empty / alternating-color descending drops on `6..12`.
- `klondike_draw_stock(count)` ([klondike.eats:L72-L87](../../../games/klondike.eats#L72-L87)): Shared by `k1_stock` (`count = 1`) and `k3_stock` (`count = 3`). Recycles Waste back to Stock in reverse pop order when Stock is empty, preserving deal order on the next pass.
- `klondike_on_turn_end()` ([klondike.eats:L106-L113](../../../games/klondike.eats#L106-L113)): Flips any exposed face-down top card on columns `6..12`.
- `klondike_status()` ([klondike.eats:L115-L119](../../../games/klondike.eats#L115-L119)): Returns `"victory"` when `zone_size(2..5)` sums to `52`.

---

## 4. Solver Considerations

### Search & Information Profile
- **Information Visibility:** Standard Klondike is a **hidden-information** game (45 of 52 cards start unseen). For **Guaranteed Solvable (`Shift + N`)** deal generation and offline analysis, the solver operates in **Thoughtful Klondike** (perfect-information) mode. For live Hints / Auto-Play, the engine must either respect hidden-card uncertainty or explicitly operate with omniscient lookahead.
- **Branching Factor:** Moderate (typically 3–12 legal moves), but Draw 1 and Draw 3 with unlimited redeals introduce large **stock-cycling loops** and **redundant substack splits** (splitting a face-up run mid-stack rarely helps unless it exposes a card needed for a foundation or allows a King to vacate a column).
- **Cycles & Transpositions:**
  - *Tableau ping-pong:* Moving a card/run back and forth between two opposite-color parents of the same rank.
  - *Empty stock-cycle:* Recycling the waste pile without playing any card from Waste or changing Tableau state is a pure cycle and must be pruned via state hashing (transposition table).
  - *Foundation symmetry:* Empty foundation slots are interchangeable (`2..5`); canonicalizing foundation order avoids `4!` duplicate states `[BlakeGent2019]`.
  - *Safe auto-foundation dominance:* Promoting an Ace or Two (or any card of rank `r` when both opposite-color foundations are already at `>= r - 1`) is a **dominant move** that never sacrifices solvability `[BlakeGent2019]`.

### Heuristic (`eval_heuristic`) Design
- **Current Eatscript Heuristic (`klondike_eval`, [klondike.eats:L8-L23](../../../games/klondike.eats#L8-L23)):**
  - `+120` per card on Foundations (`2..5`)
  - `-25` per face-down card remaining in Tableau (`6..12`)
- **Strengths:** Simple, fast, and strongly prioritizes foundation progress and uncovering hidden cards.
- **Known Limitations & Future Tuning Opportunities:**
  - *Equal weighting of foundation cards:* `+120` treats promoting a 7 the same as an Ace, which can tempt a greedy search into prematurely promoting a mid-rank card needed as a tableau bridge (especially since worrying back is disabled in SolEngine).
  - *No column-depth or mobility term:* Uncovering a card in a tall face-down column (e.g., column 7 with 6 hidden cards) is dramatically more valuable than uncovering a card in a short column; weighting hidden cards quadratically by depth or rewarding empty columns when a King is available improves search guidance `[Bjarnason2007]`.
  - *Draw 3 waste alignment:* In Draw 3, playing a card from Waste shifts the modulo-3 accessibility of every subsequent card in the Stock/Waste cycle; rewarding newly unlocked Waste cards helps break Draw 3 plateaus.

### Solvability & Win Rates

| Variant & Ruleset | Information Model | Winnability | Source |
| :--- | :--- | :---: | :--- |
| **Klondike Draw 1** (unlimited passes, worrying back allowed) | Thoughtful (perfect info) | **90.480% ± 0.116%** | `[BlakeGent2019]` |
| **Klondike Draw 3** (unlimited passes, worrying back allowed) | Thoughtful (perfect info) | **81.945% ± 0.084%** | `[BlakeGent2019]` |
| **Klondike Draw 3** (unlimited passes) | Thoughtful (lower bound) | ≥ 79% (earlier bound) | `[Bjarnason2009]` |
| **Klondike Draw 3** (standard hidden info) | Blind AI (Monte-Carlo planning) | ~35%–40%+ | `[Bjarnason2009]` |

> [!IMPORTANT]
> **Impact of SolEngine's "No Worrying Back" Rule:** Because `klondike_can_pick()` currently disallows moving cards back from Foundations to Tableau, SolEngine's true thoughtful solvability rate is slightly lower than Blake & Gent's figures above (which permit worrying back). Keep this in mind when comparing Guaranteed Solvable rejection rates against published benchmarks.

- **Guaranteed Solvable (`Shift + N`) Notes:** Because >80% of random Draw 3 deals and >90% of Draw 1 deals are solvable in thoughtful mode, random seed rejection rates are low — most seeds succeed on the first or second attempt if the solver applies transposition hashing and safe-foundation dominance pruning.

---

## 5. References

- `[Cadogan1870]` — Baseline 19th-century English patience anthology (predates Klondike's print appearance). See [REFERENCES.md](../REFERENCES.md).
- `[Dick1883]` — First edition of *Dick's Games of Patience* (predates Klondike). See [REFERENCES.md](../REFERENCES.md).
- `[Dick1898]` — Second series and later early-20th-century revisions documenting the Klondike/Canfield naming overlap. See [REFERENCES.md](../REFERENCES.md).
- `[Hoyle1907]` — Earliest documented print rules for "Seven-Card Klondike" alongside 13-card "Klondike" (Canfield). See [REFERENCES.md](../REFERENCES.md).
- `[USPC1913]` — 1913 standardization separating *Klondike* (7-column tableau) from *Canfield* (13-card reserve). See [REFERENCES.md](../REFERENCES.md).
- `[MoreheadMottSmith1949]` — Classic American codification of Klondike Draw 1 (1-pass) and Draw 3 rules. See [REFERENCES.md](../REFERENCES.md).
- `[Parlett1979]` — Historical lineage, nomenclature analysis, and taxonomy of the Klondike family. See [REFERENCES.md](../REFERENCES.md).
- `[KellerSolLab]` — Historical notes on *Dick's* editions, Canfield/Klondike confusion, and Gold Rush folklore. See [REFERENCES.md](../REFERENCES.md).
- `[Bjarnason2007]` — Real-time search and heuristic features for Klondike. See [REFERENCES.md](../REFERENCES.md).
- `[Bjarnason2009]` — Monte-Carlo planning lower bounds for thoughtful and hidden-information Klondike. See [REFERENCES.md](../REFERENCES.md).
- `[BlakeGent2019]` — *Solvitaire* DFS solver, symmetry/dominance reductions, and precision winnability bounds for Thoughtful Draw 1 (90.480% ± 0.116%) and Draw 3 (81.945% ± 0.084%). See [REFERENCES.md](../REFERENCES.md).
