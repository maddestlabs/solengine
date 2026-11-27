---
id: "seahaven_towers"
name: "Seahaven Towers"
aka: ["Seahaven", "Sea Towers"]
family: "FreeCell"
ancestor: "bakers_game"
decks: 1
info_type: "open"
mechanic: "building"
status: "documented"
eatscript: "../../../games/freecell.eats"
video: null
---

# Seahaven Towers

> **One-line summary:** The 1988 Macintosh & SGI cult classic — 10 shallow columns of 5 cards, 4 FreeCells (2 pre-filled), same-suit building, and Kings-only empty columns.

| Field | Value |
| :--- | :--- |
| **Game ID** | `seahaven_towers` |
| **Family** | [FreeCell Family](./README.md) |
| **Decks** | 1 (52 cards) |
| **Information Model** | Open (perfect information — all 52 cards visible at deal) |
| **Status** | `documented` · `implemented` |
| **Eatscript Source** | [games/freecell.eats](../../../games/freecell.eats#L297-L337) |
| **Play Online** | [Play Seahaven Towers](https://solitaireengine.org/?game=seahaven_towers) |

---

## 1. History & Lineage

- **Macintosh Shareware Classic (1988):** **Seahaven Towers** was named and popularized in **1988** by software developer **Art Cabral** as a shareware game for the Apple Macintosh. Cabral noted that he adapted existing open-cell patience principles rather than inventing the underlying card mechanics from scratch.
- **Silicon Graphics IRIX Port:** The game gained a devoted technical following when it was ported to **Silicon Graphics (SGI) IRIX** workstations — famously shipping with a built-in automated solver — before spreading to Linux (GNOME AisleRiot), PySolFC, and modern web clients.
- **Lineage:** Sits in the same-suit open-cell branch alongside [Baker's Game](bakers-game.md) and [Eight Off](eight-off.md): it trades Eight Off's 8 cells for **10 shallower columns** (`5 × 10 = 50` cards, leaving 2 cards pre-filled in the 4 FreeCells), creating a crisp, compact puzzle where every FreeCell slot counts.

---

## 2. Formal Rules

### Setup & Deal
- **Deck:** 1 standard 52-card deck, dealt completely face-up.
- **Tableau (10 columns):** 5 cards per column (`[5, 5, 5, 5, 5, 5, 5, 5, 5, 5]` = 50 cards).
- **FreeCells (4 cells):** The remaining **2 cards** are dealt face-up into 2 of the 4 FreeCells; 2 FreeCells start empty.
- **Foundations (4 piles):** Start empty.

### Play Mechanics
- **Foundations:** Build **up in suit** from Ace (`1`) to King (`13`). Single cards only.
- **Tableau:** Build **down in the SAME SUIT**.
  - Only a **King** (or a same-suit sequence headed by a King) may fill an empty tableau column.
  - Ordered same-suit runs may move together as a supermove (see Engine Note below).
- **FreeCells:** 4 single-card reserve cells (starting half full).

### Win & Loss Conditions
- **Victory:** All 52 cards reach the 4 Foundations.
- **Loss / Deadlock:** No legal moves remain.

### Rule Ambiguities & SolEngine Conventions

| Rule Point | Historical / Regional Variations | SolEngine Convention |
| :--- | :--- | :--- |
| **Empty Tableau Columns** | Standard Seahaven Towers restricts empty columns strictly to **Kings only**. | **Kings only** (`king_only = True`, `empty_tableau: "king"`). |
| **Supermove Formula under Kings-Only** | In strict single-card play, empty King-only columns cannot buffer non-King cards, limiting non-King supermoves to `(1 + empty_fc)`. | **Engine Note:** `fc_max_supermove()` ([freecell.eats:L42-L55](../../../games/freecell.eats#L42-L55)) currently includes the `2^(empty_cols)` factor for all FreeCell variants, making SolEngine slightly more permissive when empty columns are open. |

---

## 3. Eatscript Implementation Mapping

Defined in [games/freecell.eats](../../../games/freecell.eats#L157-L165) and [L297-L337](../../../games/freecell.eats#L297-L337).

### Zone Map
| Zone Index | Role | Count | Initial State |
| :---: | :--- | :---: | :--- |
| `0..3` | FreeCells | 4 | 2 occupied, 2 empty |
| `4..7` | Foundations | 4 | Empty |
| `8..17` | Tableau Columns | 10 | `[5, 5, 5, 5, 5, 5, 5, 5, 5, 5]` face-up |

### Callbacks & Shared Helpers
- `seahaven_eval()` → `fc_eval(3, 4, 7, 8, 17, True, 20, 20, 25)`
- `seahaven_pick(z, i)` → `fc_can_pick(3, 8, 17, True, z, i)`
- `seahaven_drop(fz, i, tz)` → `fc_can_drop(3, 4, 7, 8, 17, True, True, fz, i, tz)`
- `fc4_status()` → `fc_status(4, 7)`

---

## 4. Solver Considerations

### Search & Information Profile
- **Information Visibility:** 100% open (perfect information).
- **Branching Factor:** Low per-card branching (same-suit building gives at most 1 tableau target per card), though 10 columns mean more exposed top cards at deal than 8-column games. Because 2 of the 4 FreeCells start occupied, opening-move tightness is high — many unsolvable deals deadlock within the first 5–10 moves.
- **Dominance Rules:** Because building is strictly **same-suit**, **every legal Foundation promotion is unconditionally safe** and should be forced immediately `[BlakeGent2019]`.

### Heuristic (`eval_heuristic`) Design
- **Current Eatscript Heuristic (`seahaven_eval`, [freecell.eats:L158-L159](../../../games/freecell.eats#L158-L159)):**
  - `+150` per foundation card
  - `+20` per empty FreeCell
  - `+20` per empty Tableau column
  - `+25` per adjacent same-suit descending pair
- **Tuning Opportunities:**
  - *Opening Cell Relief:* Starting with only 2 empty cells makes freeing the 2 pre-filled cells (or promoting Aces/Twos immediately) disproportionately critical compared to standard FreeCell; slightly weighting empty FreeCells above empty columns when `< 2` cells are free helps prevent early lockups.
  - *Blocking Depth:* Penalizing cards stacked above buried Aces/Twos/Threes aligns well with Seahaven's shallow 5-card columns.

### Solvability & Win Rates

| Variant | Information Model | Winnability | Source |
| :--- | :--- | :---: | :--- |
| **Seahaven Towers** | Open (perfect info) | **~89.33%** | `[BlakeGent2019]` |

- **Guaranteed Solvable (`Shift + N`) Notes:** Roughly **1 in 10 random deals is unsolvable**, and dead deals typically fail early due to the 2-free-cell opening bottleneck — making random-seed rejection fast when paired with forced safe-foundation moves.

---

## 5. References

- `[Parlett1979]` — Open-cell same-suit patience taxonomy. See [REFERENCES.md](../REFERENCES.md).
- `[KellerFreeCell]` — Context on the FreeCell / Baker's Game / Seahaven family. See [REFERENCES.md](../REFERENCES.md).
- `[BlakeGent2019]` — *Solvitaire* winnability benchmark (~89.33%) and same-suit dominance analysis. See [REFERENCES.md](../REFERENCES.md).
