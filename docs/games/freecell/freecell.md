---
id: "freecell"
name: "FreeCell"
aka: ["Baker's Alternating", "Eight-Column FreeCell"]
family: "FreeCell"
ancestor: "bakers_game"
decks: 1
info_type: "open"
mechanic: "building"
status: "documented"
eatscript: "../../../games/freecell.eats"
video: null
---

# FreeCell

> **One-line summary:** The landmark open-information solitaire of pure skill — 8 face-up columns, 4 temporary free cells, alternating-color building, and ~99.999% solvability.

| Field | Value |
| :--- | :--- |
| **Game ID** | `freecell` |
| **Family** | [FreeCell Family](./README.md) |
| **Decks** | 1 (52 cards) |
| **Information Model** | Open (perfect information — all 52 cards visible at deal) |
| **Status** | `documented` · `implemented` |
| **Eatscript Source** | [games/freecell.eats](../../../games/freecell.eats#L171-L211) |
| **Play Online** | [Play FreeCell](https://solitaireengine.org/?game=freecell) |

---

## 1. History & Lineage

- **Invention on PLATO (1978):** Inspired by Martin Gardner's June 1968 column on *Baker's Game* `[Gardner1968]`, **Paul Alfille** (then a medical student at the University of Illinois) modified the tableau build rule from same-suit to **alternating colors** and implemented the first computerized **FreeCell** in **1978** on the **PLATO** networked computer system in the TUTOR language `[KellerFreeCell]`. Alfille's PLATO version already supported customizable column/cell counts, streak tracking, and tournaments.
- **Microsoft Windows Era (1991–1995):** Microsoft developer **Jim Horne** encountered FreeCell on PLATO, wrote a DOS port in 1988, and brought it to Windows in the *Microsoft Entertainment Pack* (1991) and **Windows 95** `[KellerFreeCell]`. Windows FreeCell introduced the famous 32,000 numbered deals — of which **Deal #11982** became legendary as the sole unsolvable deal in the original 32,000 set `[KellerFreeCell]`.
- **Lineage:** Direct descendant of [Baker's Game](bakers-game.md); sibling to [Eight Off](eight-off.md) and [Seahaven Towers](seahaven-towers.md).

---

## 2. Formal Rules

### Setup & Deal
- **Deck:** 1 standard 52-card deck, dealt completely face-up.
- **Tableau (8 columns):** `[7, 7, 7, 7, 6, 6, 6, 6]` cards (52 total).
- **FreeCells (4 cells):** Start empty; each holds at most 1 card.
- **Foundations (4 piles):** Start empty.

### Play Mechanics
- **Foundations:** Build **up in suit** from Ace (`1`) to King (`13`). Single cards only.
- **Tableau:** Build **down in rank** in **alternating colors** (Red on Black, Black on Red).
  - Strictly a single-card game; ordered alternating-color runs move atomically as a **supermove** up to:
    $$\text{max\_cards} = (1 + \text{empty\_cells}) \times 2^{\text{empty\_non\_target\_columns}}$$
  - **Any card** (or valid supermove sequence) may fill an empty tableau column.
- **FreeCells:** Hold any single exposed card temporarily.

### Win & Loss Conditions
- **Victory:** All 52 cards reach the 4 Foundations.
- **Loss / Deadlock:** No legal moves remain.

### Rule Ambiguities & SolEngine Conventions

| Rule Point | Historical / Regional Variations | SolEngine Convention |
| :--- | :--- | :--- |
| **Supermoves** | Single-card rule vs. atomic shortcut up to `(1 + fc) * 2^(empty_cols)`. | **Atomic supermoves enabled** via `fc_max_supermove()`, excluding destination column `to_z` when dropping into an empty column. |
| **Foundation-to-Tableau** | Disallowed in Microsoft FreeCell and standard digital rules. | **Disallowed** (`fc_can_pick()` permits picks only from `0..3` and `8..15`). |

---

## 3. Eatscript Implementation Mapping

Defined in [games/freecell.eats](../../../games/freecell.eats#L121-L133) and [L171-L211](../../../games/freecell.eats#L171-L211).

### Zone Map
| Zone Index | Role | Count | Initial State |
| :---: | :--- | :---: | :--- |
| `0..3` | FreeCells | 4 | Empty |
| `4..7` | Foundations | 4 | Empty |
| `8..15` | Tableau Columns | 8 | `[7, 7, 7, 7, 6, 6, 6, 6]` face-up |

### Callbacks & Shared Helpers
- `freecell_eval()` → `fc_eval(3, 4, 7, 8, 15, False, 25, 30, 20)`
- `freecell_pick(z, i)` → `fc_can_pick(3, 8, 15, False, z, i)`
- `freecell_drop(fz, i, tz)` → `fc_can_drop(3, 4, 7, 8, 15, False, False, fz, i, tz)`
- `fc4_status()` → `fc_status(4, 7)`

---

## 4. Solver Considerations

### Search & Information Profile
- **Computational Complexity:** Generalized FreeCell (arbitrary deck sizes) was proven **NP-complete** by Malte Helmert `[Helmert2003]`. For a standard 52-card deck, however, heuristic best-first search (e.g., weighted A* / BeFS with transposition tables) solves almost every deal in milliseconds.
- **Branching Factor:** Higher than Baker's Game because every card has 2 opposite-color parents, plus up to 4 FreeCells and empty columns.
- **Cycles & Transpositions:**
  - *Cell & empty-column symmetry:* Canonicalizing the multiset of FreeCells and sorting empty columns eliminates huge swaths of equivalent states `[BlakeGent2019]`.
  - *Horne's Safe Auto-Foundation Rule:* Unlike same-suit Baker's Game, promoting a card of rank `r` to Foundation in FreeCell is **only unconditionally safe** when `r <= 2` OR both opposite-color foundations are already at `>= r - 1` (or `r - 2` if the same-color partner foundation is also at `>= r - 1` — the classic Jim Horne / Michael Keller safe-move criterion) `[KellerFreeCell]`, `[BlakeGent2019]`.

### Heuristic (`eval_heuristic`) Design
- **Current Eatscript Heuristic (`freecell_eval`, [freecell.eats:L122-L123](../../../games/freecell.eats#L122-L123)):**
  - `+150` per foundation card
  - `+25` per empty FreeCell
  - `+30` per empty Tableau column
  - `+20` per adjacent alternating-color descending pair
- **Tuning Opportunities:**
  - *Blocking-Card Count:* Counting how many cards bury the lowest unplayed foundation cards (Aces, Twos, Threes) is one of the strongest classic FreeCell solver features (used in *Freecell Solver* / *Solvitaire*).
  - *Empty-Column Exponential Leverage:* Because empty columns *double* supermove capacity, empty-column value is super-linear — keeping at least one column open is far more valuable than packing a second low-rank run.

### Solvability & Win Rates

| Variant | Information Model | Winnability | Source |
| :--- | :--- | :---: | :--- |
| **FreeCell (Standard 4-Cell)** | Open (perfect info) | **~99.999%** *(only 1 unsolvable in first 32,000 MS deals: #11982; 8 in first 1,000,000)* | `[KellerFreeCell]`, `[BlakeGent2019]` |

- **Guaranteed Solvable (`Shift + N`) Notes:** Virtually every random deal is solvable (~1 in 78,000 deals is unsolvable). Any timeout during Guaranteed Solvable generation is almost always a heuristic local minimum rather than an unsolvable deal.

---

## 5. References

- `[Gardner1968]` — *Baker's Game* column in *Scientific American* (June 1968) that inspired Alfille. See [REFERENCES.md](../REFERENCES.md).
- `[KellerFreeCell]` — Authoritative history of Paul Alfille's 1978 PLATO FreeCell, Jim Horne's Windows FreeCell, Deal #11982, and safe auto-move rules. See [REFERENCES.md](../REFERENCES.md).
- `[Helmert2003]` — Proof of NP-completeness for generalized FreeCell. See [REFERENCES.md](../REFERENCES.md).
- `[BlakeGent2019]` — *Solvitaire* winnability analysis (~99.999%) and symmetry/dominance reductions. See [REFERENCES.md](../REFERENCES.md).
