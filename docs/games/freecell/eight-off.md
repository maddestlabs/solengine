---
id: "eight_off"
name: "Eight Off"
aka: ["Eight Cells"]
family: "FreeCell"
ancestor: "bakers_game"
decks: 1
info_type: "open"
mechanic: "building"
status: "documented"
eatscript: "../../../games/freecell.eats"
video: null
---

# Eight Off

> **One-line summary:** An ultra-high-solvability same-suit cousin of FreeCell with **8 FreeCells** (4 pre-filled at deal), 8 columns of 6 cards, and Kings-only empty columns.

| Field | Value |
| :--- | :--- |
| **Game ID** | `eight_off` |
| **Family** | [FreeCell Family](./README.md) |
| **Decks** | 1 (52 cards) |
| **Information Model** | Open (perfect information — all 52 cards visible at deal) |
| **Status** | `documented` · `implemented` |
| **Eatscript Source** | [games/freecell.eats](../../../games/freecell.eats#L255-L295) |
| **Play Online** | [Play Eight Off](https://solitaireengine.org/?game=eight_off) |

---

## 1. History & Lineage

- **Place in the Open-Cell Lineage:** *Eight Off* belongs to the classic open-cell patience family alongside [Baker's Game](bakers-game.md) and [FreeCell](freecell.md) `[Parlett1979]`, `[KellerFreeCell]`. Rather than relaxing the tableau build rule to alternating colors (as Paul Alfille did in FreeCell), Eight Off keeps **strict same-suit building** and compensates by doubling the reserve to **8 FreeCells** (4 pre-filled with the leftover cards from an `8 × 6 = 48` tableau deal) while restricting empty tableau columns to **Kings only**.
- **Design Contrast:** Together with Baker's Game (~75% solvable) and FreeCell (~99.999% solvable), Eight Off (~99.88% solvable) illustrates a core structural principle documented in solver studies: **extra reserve capacity outweighs stricter suit-building rules** `[BlakeGent2019]`.

---

## 2. Formal Rules

### Setup & Deal
- **Deck:** 1 standard 52-card deck, dealt completely face-up.
- **Tableau (8 columns):** 6 cards per column (`[6, 6, 6, 6, 6, 6, 6, 6]` = 48 cards).
- **FreeCells (8 cells):** The remaining **4 cards** are dealt face-up into the first 4 FreeCells; the other 4 FreeCells start empty.
- **Foundations (4 piles):** Start empty.

### Play Mechanics
- **Foundations:** Build **up in suit** from Ace (`1`) to King (`13`). Single cards only.
- **Tableau:** Build **down in the SAME SUIT**.
  - Only a **King** (or a same-suit sequence headed by a King) may fill an empty tableau column.
  - Ordered same-suit runs may move together as a supermove (see Engine Note below).
- **FreeCells:** Up to 8 single cards may be parked across the 8 cells.

### Win & Loss Conditions
- **Victory:** All 52 cards reach the 4 Foundations.
- **Loss / Deadlock:** No legal moves remain.

### Rule Ambiguities & SolEngine Conventions

| Rule Point | Historical / Regional Variations | SolEngine Convention |
| :--- | :--- | :--- |
| **Empty Tableau Columns** | Standard Eight Off restricts empty columns to **Kings only**; a relaxed variant ("Eight Off Relaxed") allows any card. | **Kings only** (`king_only = True`, `empty_tableau: "king"`). |
| **Supermove Formula under Kings-Only** | Strictly speaking, an empty King-only column *cannot* serve as a temporary buffer for non-King cards, so strict single-card decomposition allows `(1 + empty_fc)` cards rather than `(1 + empty_fc) * 2^(empty_cols)`. | **Engine Note:** `fc_max_supermove()` ([freecell.eats:L42-L55](../../../games/freecell.eats#L42-L55)) currently applies the standard `2^(empty_cols)` multiplier to all FreeCell variants, making SolEngine slightly more permissive on multi-card drags when empty columns exist. |

---

## 3. Eatscript Implementation Mapping

Defined in [games/freecell.eats](../../../games/freecell.eats#L144-L155) and [L255-L295](../../../games/freecell.eats#L255-L295).

### Zone Map
| Zone Index | Role | Count | Initial State |
| :---: | :--- | :---: | :--- |
| `0..7` | FreeCells | 8 | 4 occupied, 4 empty |
| `8..11` | Foundations | 4 | Empty |
| `12..19` | Tableau Columns | 8 | `[6, 6, 6, 6, 6, 6, 6, 6]` face-up |

### Callbacks & Shared Helpers
- `eight_off_eval()` → `fc_eval(7, 8, 11, 12, 19, True, 20, 20, 25)`
- `eight_off_pick(z, i)` → `fc_can_pick(7, 12, 19, True, z, i)`
- `eight_off_drop(fz, i, tz)` → `fc_can_drop(7, 8, 11, 12, 19, True, True, fz, i, tz)`
- `eight_off_status()` → `fc_status(8, 11)`

---

## 4. Solver Considerations

### Search & Information Profile
- **Information Visibility:** 100% open (perfect information).
- **Branching Factor:** High cell-permutation symmetry across **8 FreeCells** (up to `8!` equivalent arrangements if cells are not canonicalized as an unordered multiset).
- **Dominance Rules:** Like Baker's Game, because building is strictly **same-suit**, **every legal Foundation move is unconditionally safe** and can be forced immediately without losing solvability `[BlakeGent2019]`.

### Heuristic (`eval_heuristic`) Design
- **Current Eatscript Heuristic (`eight_off_eval`, [freecell.eats:L145-L146](../../../games/freecell.eats#L145-L146)):**
  - `+150` per foundation card
  - `+20` per empty FreeCell
  - `+20` per empty Tableau column
  - `+25` per adjacent same-suit descending pair
- **Tuning Opportunities:**
  - *King Placement Priority:* Because only Kings can use empty columns, freeing a column *without* a King accessible is less immediately useful than parking a King in an empty column to clear its suit's lower cards.
  - *Cell Canonicalization:* Essential for the solver's transposition table given 8 interchangeable FreeCells.

### Solvability & Win Rates

| Variant | Information Model | Winnability | Source |
| :--- | :--- | :---: | :--- |
| **Eight Off** (standard Kings-only) | Open (perfect info) | **~99.88%** | `[BlakeGent2019]` |

- **Guaranteed Solvable (`Shift + N`) Notes:** Nearly every deal is solvable (~99.9%). With safe auto-foundation forcing and cell canonicalization, deals solve cleanly with minimal rejections.

---

## 5. References

- `[Parlett1979]` — Taxonomy of open-cell same-suit patience games. See [REFERENCES.md](../REFERENCES.md).
- `[KellerFreeCell]` — Relationship between *Eight Off*, *Baker's Game*, and *FreeCell*. See [REFERENCES.md](../REFERENCES.md).
- `[BlakeGent2019]` — *Solvitaire* winnability benchmark (~99.88%) and analysis of FreeCell-family reserve capacity vs. build strictness. See [REFERENCES.md](../REFERENCES.md).
