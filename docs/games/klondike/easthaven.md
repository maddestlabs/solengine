---
id: "easthaven"
name: "Easthaven"
aka: ["Easthaven Klondike", "Aces Up (Morehead & Mott-Smith 1949, deprecated)"]
family: "Klondike"
ancestor: "klondike_draw_1"
decks: 1
info_type: "half_open"
mechanic: "building"
status: "documented"
eatscript: "../../../games/klondike.eats"
---

# Easthaven

> **One-line summary:** A mechanical hybrid of Klondike and Spider — build 4 suit foundations Ace-to-King from a 7×3 tableau, dealing fresh rows of 7 cards directly onto the columns from the stock.

| Field | Value |
| :--- | :--- |
| **Game ID** | `easthaven` |
| **Family** | [Klondike Family](./README.md) |
| **Decks** | 1 (52 cards) |
| **Information Model** | Half-Open (14 face-down tableau cards + 31 face-down stock cards at deal) |
| **Status** | `documented` · `implemented` |
| **Eatscript Source** | [games/klondike.eats](../../../games/klondike.eats) |
| **Play Online** | [Play Easthaven](https://solitaireengine.org/?game=easthaven) |

---

## 1. History & Lineage

- **Victorian Ancestor (*Westcliff*, 1911):** Easthaven descends directly from **Westcliff**, first published by Mary Whitmore Jones in *New Games of Patience* (1911) `[Jones1911]`, `[Parlett1979]`. Classic Westcliff dealt a 21-card tableau (7 columns of 3 cards each, top card face-up) with Klondike-style alternating-color descending builds, but did not deal subsequent rows from the stock.
- **Morehead & Mott-Smith (1949) & The "Aces Up" Name Collision:** In *The Complete Book of Solitaire and Patience Games* (1949), Albert H. Morehead and Geoffrey Mott-Smith adapted the 21-card Westcliff layout by adding Spider-style row deals from the stock (dealing 1 card onto each of the 7 tableau columns) `[MoreheadMottSmith1949]`, `[Parlett1979]`. They originally printed this variant under the title **"Aces Up"** — creating a collision with the unrelated, well-known discard game *Aces Up* — while reassigning the name *Westcliff* to a 30-card variant `[MoreheadMottSmith1949]`. Later editions and patience compilers resolved the collision by renaming the row-dealing game **Easthaven** `[Parlett1979]`.
- **Mechanical Hybridity:** Easthaven sits at the intersection of two major families:
  - From **Klondike**, it inherits the 7-column tableau, alternating-color descending builds, and 4 Ace-to-King suit foundations.
  - From **Spider**, it inherits the **stock-to-tableau row deal** (no waste pile, no redeals), which forces the player to manage buried, out-of-sequence cards after every deal.

---

## 2. Formal Rules

### Setup & Deal
- **Deck:** 1 standard 52-card deck.
- **Tableau (7 columns):** 3 cards dealt to each of the 7 columns (`[3, 3, 3, 3, 3, 3, 3]` = 21 cards total). In each column, the bottom 2 cards are face-down and the top card is face-up (14 hidden tableau cards).
- **Stock:** The remaining 31 cards form the face-down Stock. There is **no Waste pile**.
- **Foundations (4 piles):** Start empty.

### Play Mechanics
- **Foundations:** Build **up in suit** from Ace (`1`) to King (`13`). Single cards only.
- **Tableau:** Build **down in rank** in **alternating colors** (Red on Black, Black on Red).
  - Only **properly ordered alternating-color descending sequences** (or single top cards) may be picked and moved as a unit. Because stock deals drop arbitrary cards onto existing columns, a face-up stack is *not* guaranteed to be a valid movable run.
  - Exposing a face-down tableau card automatically flips it face-up at turn end.
  - Empty columns accept **Kings only** in SolEngine's ruleset (see Rule Ambiguities below).
- **Stock Deals:** Clicking the Stock deals **1 face-up card onto each of the 7 tableau columns** (4 full rows of 7 cards = 28 cards). The final click deals the remaining **3 cards** onto the first 3 tableau columns (`6`, `7`, `8`). No redeals (`max_redeals: 0`).

### Win & Loss Conditions
- **Victory:** All 52 cards are moved to the 4 Foundations.
- **Loss / Deadlock:** Stock is exhausted and no legal moves remain to reach victory.

### Rule Ambiguities & SolEngine Conventions

| Rule Point | Historical / Regional Variations | SolEngine Convention |
| :--- | :--- | :--- |
| **Empty Tableau Columns** | Many sources (including several modern digital clients and `[BlakeGent2019]`'s *Solvitaire* model) allow **any card or valid sequence** in an empty column; stricter print variants (following `[MoreheadMottSmith1949]`'s King restriction) restrict empty columns to **Kings only**. | **Kings only** (`empty_tableau: "king"`, via shared `klondike_can_drop()`). This makes SolEngine's Easthaven significantly tighter than "any card" implementations. |
| **Dealing with Empty Columns** | Spider requires all columns to be non-empty before dealing a row; Easthaven traditionally allows dealing even when columns are empty. | **Allowed with empty columns** (`easthaven_stock()` deals directly to columns `6..12` regardless of whether any column is empty). |
| **Worrying Back** (Foundation → Tableau) | Permitted in some digital versions; disallowed in others. | **Disallowed** (`easthaven_can_pick()` only permits picks from tableau zones `6..12`). |

---

## 3. Eatscript Implementation Mapping

Defined in [games/klondike.eats](../../../games/klondike.eats#L215-L257).

### Zone Map
| Zone Index | Role | Count | Initial State |
| :---: | :--- | :---: | :--- |
| `0` | Stock | 1 | 31 cards face-down |
| `1` | *(Unused Waste slot)* | — | Unused in Easthaven (stock deals directly to tableau) |
| `2..5` | Foundations | 4 | Empty |
| `6..12` | Tableau Columns | 7 | `[3, 3, 3, 3, 3, 3, 3]` cards, top card face-up |

### Callbacks & Shared Helpers
- `easthaven_can_pick(z, i)` ([klondike.eats:L35-L46](../../../games/klondike.eats#L35-L46)): Unlike `klondike_can_pick()`, this **must walk the substack** from `i` to `sz - 1` verifying `card_is_red(z, k) != card_is_red(z, k + 1)` and `card_rank(z, k) == card_rank(z, k + 1) + 1`. This prevents picking disordered face-up stacks created by stock row-deals.
- `klondike_can_drop(from_z, i, to_z)` ([klondike.eats:L48-L70](../../../games/klondike.eats#L48-L70)): Shared with Klondike; enforces Ace-to-King suit foundations (`2..5`) and King-on-empty / alternating-color descending tableau drops (`6..12`).
- `easthaven_stock()` ([klondike.eats:L95-L104](../../../games/klondike.eats#L95-L104)): Deals 1 face-up card from Stock (`0`) to each tableau column `6..12` while Stock is non-empty (cleanly handling the final 3-card partial row).
- `klondike_on_turn_end()` & `klondike_status()` ([klondike.eats:L106-L119](../../../games/klondike.eats#L106-L119)): Shared with Klondike.

---

## 4. Solver Considerations

### Search & Information Profile
- **Information Visibility:** Half-open (45 of 52 cards start face-down: 14 in tableau, 31 in stock). Because there are **no redeals** and only **5 stock clicks total** (`7 + 7 + 7 + 7 + 3`), the game has a strict monotonic horizon on stock usage.
- **Branching Factor:** Lower than Spider (7 columns instead of 10, alternating-color strictness), but **stock-deal timing** is a critical branching decision: dealing too early buries playable sequences and breaks column runs; dealing too late misses cards needed to clear columns.
- **Cycles & Transpositions:**
  - *Tableau ping-pong:* Moving an ordered run back and forth between two valid opposite-color parents.
  - *No stock-cycle loops:* Unlike Klondike, the stock cannot recycle, so every stock click strictly advances the state (making state-space termination cleaner).
  - *Safe auto-foundation dominance:* Promoting Aces, Twos, and safe ranks (`<= min(opposite_color_foundations) + 1`) remains a dominant move `[BlakeGent2019]`.

### Heuristic (`eval_heuristic`) Design
- **Current Eatscript Heuristic:** Shares `klondike_eval()` ([klondike.eats:L8-L23](../../../games/klondike.eats#L8-L23)): `+120` per foundation card, `-25` per face-down tableau card.
- **Easthaven-Specific Tuning Opportunities:**
  - *Disorder / Break-Count Penalty:* Because each stock deal drops 7 arbitrary cards on top of existing columns, a strong Easthaven heuristic should penalize **sequence breaks** (adjacent face-up pairs that are *not* alternating-color descending) — similar to Spider solvers. Right now `klondike_eval()` does not distinguish a clean ordered column from a tangled stack of 6 disordered face-up cards.
  - *Pre-Deal Compression Bonus:* Rewarding fewer total runs (or empty columns when a King is ready) encourages the solver to consolidate columns *before* clicking Stock.
  - *Empty-Column Trap under Kings-Only:* Because SolEngine uses Kings-only empty columns, vacating a column without a King in play yields zero immediate benefit *until* the next stock deal drops a fresh card into it (which is actually helpful — an empty column absorbs a single stock card cleanly without burying an existing run!).

### Solvability & Win Rates

| Variant & Ruleset | Information Model | Winnability | Source |
| :--- | :--- | :---: | :--- |
| **Easthaven** (standard *Solvitaire* model — any card/run in empty column) | Thoughtful (perfect info) | **82.844% ± 0.100%** *(2,000,000 deals)* | `[BlakeGent2019]` |

> [!IMPORTANT]
> **SolEngine Kings-Only Impact:** Blake & Gent's 82.844% benchmark permits filling empty columns with any card/sequence `[BlakeGent2019]`. Because SolEngine's `easthaven` currently shares `klondike_can_drop()` (Kings-only on empty columns and no worrying back), its true thoughtful winnability rate is **lower** than 82.844%. If we later want higher solvability or a "Classic Easthaven" option, adding an `any`-card empty-column rule is a 2-line change in `games/klondike.eats`.

- **Guaranteed Solvable (`Shift + N`) Notes:** Because stock deals are irreversible and Kings-only empty spaces tighten mobility, the solver benefits heavily from a disorder-penalty term in `eval_heuristic` so it doesn't waste budget exploring early stock deals before exhausting tableau consolidations.

---

## 5. References

- `[Jones1911]` — Earliest print publication of *Westcliff* (the 21-card 7×3 ancestor). See [REFERENCES.md](../REFERENCES.md).
- `[MoreheadMottSmith1949]` — First publication of the row-dealing Easthaven variant (originally titled "Aces Up"). See [REFERENCES.md](../REFERENCES.md).
- `[Parlett1979]` — Historical lineage linking *Westcliff*, *Aces Up* (1949), and *Easthaven*. See [REFERENCES.md](../REFERENCES.md).
- `[BlakeGent2019]` — *Solvitaire* thoughtful winnability benchmark for Easthaven (`82.844% ± 0.100%` over 2,000,000 deals). See [REFERENCES.md](../REFERENCES.md).
