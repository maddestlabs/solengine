---
id: "bakers_game"
name: "Baker's Game"
aka: ["Baker's FreeCell", "Same-Suit FreeCell"]
family: "FreeCell"
ancestor: null
decks: 1
info_type: "open"
mechanic: "building"
status: "documented"
eatscript: "../../../games/freecell.eats"
video: null
---

# Baker's Game

> **One-line summary:** The historical parent of FreeCell — all 52 cards dealt face-up across 8 columns with 4 free cells, building down strictly by **same suit**.

| Field | Value |
| :--- | :--- |
| **Game ID** | `bakers_game` |
| **Family** | [FreeCell Family](./README.md) |
| **Decks** | 1 (52 cards) |
| **Information Model** | Open (perfect information — all 52 cards visible at deal) |
| **Status** | `documented` · `implemented` |
| **Eatscript Source** | [games/freecell.eats](../../../games/freecell.eats#L213-L253) |
| **Play Online** | [Play Baker's Game](https://solitaireengine.org/?game=bakers_game) |

---

## 1. History & Lineage

- **First Print Publication (1968):** Baker's Game first appeared in print in **Martin Gardner's** June 1968 *"Mathematical Games"* column in *Scientific American* `[Gardner1968]`.
- **Origin & Attribution:** Gardner credited the game to **C. L. Baker**, who had learned it from his father, who in turn had picked it up from an Englishman in the 1920s `[Gardner1968]`, `[KellerFreeCell]`.
- **Direct Descendants:** Reading Gardner's 1968 column inspired **Paul Alfille** in 1978 to relax the tableau build rule from same-suit to alternating colors, creating **FreeCell** on the PLATO system `[KellerFreeCell]`. Baker's Game also shares core DNA with **Eight Off** and **Seahaven Towers**, which retain same-suit building while varying cell count, column count, and empty-column restrictions `[Parlett1979]`.

---

## 2. Formal Rules

### Setup & Deal
- **Deck:** 1 standard 52-card deck, dealt completely face-up.
- **Tableau (8 columns):** First 4 columns receive 7 cards each; last 4 columns receive 6 cards each (`[7, 7, 7, 7, 6, 6, 6, 6]` = 52 cards).
- **FreeCells (4 cells):** Start empty; each holds at most 1 card.
- **Foundations (4 piles):** Start empty.

### Play Mechanics
- **Foundations:** Build **up in suit** from Ace (`1`) to King (`13`). Single cards only.
- **Tableau:** Build **down in the SAME SUIT** (e.g., `7♠` only on `8♠`).
  - Technically a single-card movement game; ordered same-suit runs may be moved together as a **supermove** whenever enough empty FreeCells and empty Tableau columns exist to decompose the transfer into legal single-card moves:
    $$\text{max\_cards} = (1 + \text{empty\_cells}) \times 2^{\text{empty\_non\_target\_columns}}$$
  - **Any card** (or valid supermove sequence) may fill an empty tableau column.
- **FreeCells:** Any exposed top tableau card may be placed into an empty FreeCell and later moved back to a Foundation or Tableau column.

### Win & Loss Conditions
- **Victory:** All 52 cards reach the 4 Foundations.
- **Loss / Deadlock:** All FreeCells and columns lock with no legal moves remaining to reach victory.

### Rule Ambiguities & SolEngine Conventions

| Rule Point | Historical / Regional Variations | SolEngine Convention |
| :--- | :--- | :--- |
| **Supermoves** | Strict physical play moves 1 card at a time; digital clients allow atomic supermoves up to `(1 + fc) * 2^(empty_cols)`. | **Atomic supermoves enabled** via `fc_max_supermove()`, excluding the destination column when moving into an empty column. |
| **Worrying Back** (Foundation → Tableau) | Disallowed in standard rules; rarely relevant in same-suit games. | **Disallowed** (`fc_can_pick()` only permits picks from FreeCells `0..3` and Tableau `8..15`). |

---

## 3. Eatscript Implementation Mapping

Defined in [games/freecell.eats](../../../games/freecell.eats#L134-L143) and [L213-L253](../../../games/freecell.eats#L213-L253).

### Zone Map
| Zone Index | Role | Count | Initial State |
| :---: | :--- | :---: | :--- |
| `0..3` | FreeCells | 4 | Empty |
| `4..7` | Foundations | 4 | Empty |
| `8..15` | Tableau Columns | 8 | `[7, 7, 7, 7, 6, 6, 6, 6]` face-up |

### Callbacks & Shared Helpers
- `bakers_eval()` → `fc_eval(3, 4, 7, 8, 15, True, 20, 25, 25)`
- `bakers_pick(z, i)` → `fc_can_pick(3, 8, 15, True, z, i)`
- `bakers_drop(fz, i, tz)` → `fc_can_drop(3, 4, 7, 8, 15, True, False, fz, i, tz)`
- `fc4_status()` → `fc_status(4, 7)`

---

## 4. Solver Considerations

### Search & Information Profile
- **Information Visibility:** 100% open (perfect information). No hidden-card uncertainty or stock randomness.
- **Branching Factor:** Noticeably **lower** than FreeCell because each card has at most **1 valid tableau parent** (same suit, rank + 1) instead of 2 opposite-color parents. However, unsolvable dead-ends occur much more frequently (~25% of random deals).
- **Cycles & Transpositions:**
  - *Cell/column shuffling:* Moving cards idly between empty FreeCells and empty columns creates large permutation symmetries. Canonicalizing the unordered set of FreeCell contents and sorting empty columns dramatically shrinks the transposition table `[BlakeGent2019]`.
  - *Safe auto-foundation dominance:* In a **same-suit** game, promoting a card to Foundation is **always safe**! Unlike alternating-color FreeCell (where a red 5 might be needed as a tableau bridge for a black 4), in Baker's Game a `5♠` can only ever hold `4♠`, which is already on the Spade foundation if `5♠` is eligible to promote `[BlakeGent2019]`. That means **every** legal foundation move in Baker's Game is a strictly dominant move.

### Heuristic (`eval_heuristic`) Design
- **Current Eatscript Heuristic (`bakers_eval`, [freecell.eats:L135-L136](../../../games/freecell.eats#L135-L136)):**
  - `+150` per foundation card
  - `+20` per empty FreeCell
  - `+25` per empty Tableau column
  - `+25` per adjacent same-suit descending pair in Tableau
- **Tuning Opportunities:**
  - *Buried Low-Card Penalty:* Penalizing the number of cards sitting *above* the next needed foundation card of each suit (the "blocking depth" or "well-orderedness" metric) strongly guides best-first search out of deadlocks.
  - *Mandatory Safe-Foundation Dominance:* Because all foundation promotions are strictly safe in same-suit games, auto-pruning non-foundation moves whenever a foundation move is available speeds up search dramatically.

### Solvability & Win Rates

| Variant | Information Model | Winnability | Source |
| :--- | :--- | :---: | :--- |
| **Baker's Game** | Open (perfect info) | **~75.05%** | `[BlakeGent2019]` |

- **Guaranteed Solvable (`Shift + N`) Notes:** Roughly **1 in 4 random deals is mathematically unsolvable**, so Guaranteed Solvable mode should expect occasional deal rejections and keep a fast dead-end detector (transposition table + forced foundation promotions).

---

## 5. References

- `[Gardner1968]` — First print publication of *Baker's Game* in *Scientific American* (June 1968). See [REFERENCES.md](../REFERENCES.md).
- `[Parlett1979]` — Taxonomic placement within the open-cell builders family. See [REFERENCES.md](../REFERENCES.md).
- `[KellerFreeCell]` — Historical account of C. L. Baker's lineage and how *Baker's Game* led to *FreeCell*. See [REFERENCES.md](../REFERENCES.md).
- `[BlakeGent2019]` — *Solvitaire* winnability benchmark (~75.05%) and dominance analysis for same-suit open-cell games. See [REFERENCES.md](../REFERENCES.md).
