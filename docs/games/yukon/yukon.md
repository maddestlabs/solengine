---
id: "yukon"
name: "Yukon"
aka: ["Klondike Without Stock"]
family: "Yukon"
ancestor: "klondike_draw_1"
decks: 1
info_type: "half_open"
mechanic: "building"
status: "documented"
eatscript: "../../../games/yukon.eats"
video: null
---

# Yukon

> **One-line summary:** Klondike without a stock — all 52 cards start on the 7-column tableau, and any face-up card can haul every card stacked above it regardless of sequence.

| Field | Value |
| :--- | :--- |
| **Game ID** | `yukon` |
| **Family** | [Yukon Family](./README.md) |
| **Decks** | 1 (52 cards) |
| **Information Model** | Half-Open (21 face-down tableau cards, 31 face-up at deal; no stock) |
| **Status** | `documented` · `implemented` |
| **Eatscript Source** | [games/yukon.eats](../../../games/yukon.eats#L91-L131) |
| **Play Online** | [Play Yukon](https://solitaireengine.org/?game=yukon) |

---

## 1. History & Lineage

- **20th-Century Codification:** Yukon does not appear in the earliest 19th-century patience anthologies (`[Cadogan1870]`, `[Dick1883]`). Its modern ruleset was codified for American audiences by **Albert H. Morehead and Geoffrey Mott-Smith** in *The Complete Book of Solitaire and Patience Games* (1949) `[MoreheadMottSmith1949]` and later placed taxonomically within the Klondike-derived builders by **David Parlett** in *The Penguin Book of Patience* (1979) `[Parlett1979]`.
- **Structural Evolution from Klondike:** Conceptually, Yukon begins with Klondike's 28-card triangle (`1..7` with `0..6` face-down) and deals Klondike's 24 stock cards directly face-up across columns 2–7 (4 extra face-up cards per column, giving `1, 6, 7, 8, 9, 10, 11`). Removing the stock and adding **free-stack movement** shifts the game from stock-cycling luck to deep tactical unpacking.
- **Descendants:** tightening the tableau build rule from alternating colors to same suit produces [Russian Solitaire](russian-solitaire.md); combining Yukon's free-stack movement with a Klondike stock produces *Australian Patience*.

> [!NOTE]
> **Unverified / Folklore Claims:** Like *Klondike*, *Yukon* takes its name from Canada's Yukon Territory (site of the 1896–1899 Klondike Gold Rush), likely as a deliberate thematic nod to its parent game Klondike. No primary print evidence establishes an exact inventor or pre-20th-century origin `[Parlett1979]`.

---

## 2. Formal Rules

### Setup & Deal
- **Deck:** 1 standard 52-card deck, all 52 cards dealt directly to the tableau (no Stock, no Waste).
- **Tableau (7 columns):** `[1, 6, 7, 8, 9, 10, 11]` cards:
  - Column 1: 1 card face-up (0 face-down).
  - Columns 2–7: `1, 2, 3, 4, 5, 6` face-down cards respectively (21 face-down total), each topped by **5 face-up cards** (31 face-up total).
- **Foundations (4 piles):** Start empty.

### Play Mechanics
- **Foundations:** Build **up in suit** from Ace (`1`) to King (`13`). Single cards only (top card of a column).
- **Tableau:** Build **down in rank** in **alternating colors** (Red on Black, Black on Red).
  - **Free-Stack Movement:** **Any** face-up card in a tableau column may be picked up and moved along with **all cards lying on top of it**, even if the overlying cards are completely disordered. Only the bottom card of the moved packet must legally match the destination column's top card.
  - Exposing a face-down card flips it face-up at turn end.
  - Only a **King** (with whatever cards sit on top of it) may fill an empty tableau column.

### Win & Loss Conditions
- **Victory:** All 52 cards reach the 4 Foundations.
- **Loss / Deadlock:** No legal moves remain that can progress toward victory.

### Rule Ambiguities & SolEngine Conventions

| Rule Point | Historical / Regional Variations | SolEngine Convention |
| :--- | :--- | :--- |
| **Empty Tableau Columns** | Standard Yukon restricts empty columns to **Kings only** `[MoreheadMottSmith1949]`; relaxed variants allow any card. | **Kings only** (`empty_tableau: "king"`). |
| **Foundation-to-Tableau** | Disallowed in standard rules. | **Disallowed** (`yukon_can_pick()` permits picks only from tableau columns `4..10`). |

---

## 3. Eatscript Implementation Mapping

Defined in [games/yukon.eats](../../../games/yukon.eats#L10-L131).

### Zone Map
| Zone Index | Role | Count | Initial State |
| :---: | :--- | :---: | :--- |
| `0..3` | Foundations | 4 | Empty |
| `4..10` | Tableau Columns | 7 | `[1, 6, 7, 8, 9, 10, 11]` (`0..6` face-down, topped by `1, 5, 5, 5, 5, 5, 5` face-up) |

### Callbacks & Shared Helpers
- `yukon_can_pick(z, i)` ([yukon.eats:L34-L38](../../../games/yukon.eats#L34-L38)): Simply checks `z in 4..10` and `card_face_up(z, i)` — deliberately omitting any sequence-order check on `i+1 .. sz-1`.
- `yukon_drop_alt(fz, i, tz)` → `yukon_can_drop(False, fz, i, tz)` ([yukon.eats:L40-L64](../../../games/yukon.eats#L40-L64)): Single-card Ace-to-King suit foundations (`0..3`); King-on-empty or alternating-color descending drops on tableau (`4..10`).
- `yukon_on_turn_end()` ([yukon.eats:L66-L73](../../../games/yukon.eats#L66-L73)): Reveals newly exposed face-down tableau cards.
- `yukon_status()` ([yukon.eats:L75-L79](../../../games/yukon.eats#L75-L79)): Victory when `zone_size(0..3)` sums to `52`.

---

## 4. Solver Considerations

### Search & Information Profile
- **Information Visibility:** Half-open (31 of 52 cards visible immediately; 21 face-down in columns 2–7). Unlike Klondike, there is **no stock randomness** after the initial deal — every card is already on the board.
- **Branching Factor:** **High.** Because *any* face-up card can carry an arbitrary tower of overlying cards, a single board state often has 15–30+ legal moves, many of which simply rearrange disordered stacks without uncovering face-down cards or advancing foundations.
- **Cycles & Transpositions:**
  - *Disordered-tower ping-pong:* Moving a card + its disordered tail back and forth between two opposite-color parents of the same rank. Transposition hashing is mandatory.
  - *Deadlock detection:* If a low card (e.g., `2♠`) is buried beneath its own suit's higher cards (`3♠`..`K♠`) and no opposite-color parent is free to lift the blocking packet off it, the column can form an unbreakable mutual-dependency cycle (e.g., `7♥` under `8♣` and `8♠` while `7♦` is also trapped).

### Heuristic (`eval_heuristic`) Design
- **Current Eatscript Heuristic (`yukon_eval`, [yukon.eats:L10-L32](../../../games/yukon.eats#L10-L32)):**
  - `+500` per Foundation card
  - `+150` per empty Tableau column
  - `-200` per face-down card (`fd`)
  - `-15` per face-up card resting on top of a column that still has face-down cards (`if fd > 0: -(sz - fd) * 15`)
- **Strengths:** The `-15 * (sz - fd)` buried-height penalty is a smart domain-specific term — it actively discourages dumping tall disordered packets onto columns that still hide face-down cards, steering the solver toward excavating buried columns first.
- **Tuning Opportunities:**
  - *Ordered-Pair Bonus:* Adding a small reward for adjacent alternating-color descending pairs helps consolidate disordered towers into clean runs once face-down cards are cleared.
  - *King-Readiness Weighting:* Empty columns (`+150`) are only actionable when a buried King (not already at index `0` of a column) can move into them; discounting empty columns when no movable King exists avoids vacating a column prematurely.

### Solvability & Win Rates
- **Empirical / Solver Context:** Yukon's open/thoughtful solvability is significantly lower than Klondike Draw 1 due to the absence of a stock pile and the Kings-only empty-column restriction, while human win rates on random deals typically sit around **20%–30%** (and solver/thoughtful rates in the **~30%–40%+** range depending on exact rules) `[WolterSolitaire]`.
- **Guaranteed Solvable (`Shift + N`) Notes:** Because a meaningful fraction of random deals are unsolvable (and high branching factor can slow dead-end proofs), Guaranteed Solvable mode benefits from aggressive transposition pruning and a tight per-seed node cap before advancing to the next seed.

---

## 5. References

- `[MoreheadMottSmith1949]` — Early American codification of Yukon's 52-card free-stack ruleset. See [REFERENCES.md](../REFERENCES.md).
- `[Parlett1979]` — Taxonomy and lineage linking Yukon to Klondike and Russian Solitaire. See [REFERENCES.md](../REFERENCES.md).
- `[WolterSolitaire]` — Empirical solver analysis of patience games and Yukon-class search spaces. See [REFERENCES.md](../REFERENCES.md).
