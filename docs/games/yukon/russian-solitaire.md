---
id: "russian_solitaire"
name: "Russian Solitaire"
aka: ["Russian Yukon", "Russische Patience"]
family: "Yukon"
ancestor: "yukon"
decks: 1
info_type: "half_open"
mechanic: "building"
status: "documented"
eatscript: "../../../games/yukon.eats"
video: null
---

# Russian Solitaire

> **One-line summary:** Yukon's punishing same-suit sibling — all 52 cards dealt to 7 columns, free-stack movement, Kings-only empty columns, and strict **same-suit** tableau building.

| Field | Value |
| :--- | :--- |
| **Game ID** | `russian_solitaire` |
| **Family** | [Yukon Family](./README.md) |
| **Decks** | 1 (52 cards) |
| **Information Model** | Half-Open (21 face-down tableau cards, 31 face-up at deal; no stock) |
| **Status** | `documented` · `implemented` |
| **Eatscript Source** | [games/yukon.eats](../../../games/yukon.eats#L133-L173) |
| **Play Online** | [Play Russian Solitaire](https://solitaireengine.org/?game=russian_solitaire) |

---

## 1. History & Lineage

- **Relationship to Yukon:** Russian Solitaire is the direct same-suit tightening of [Yukon](yukon.md) — identical to the relationship between [Baker's Game](../freecell/bakers-game.md) and [FreeCell](../freecell/freecell.md). Retaining Yukon's exact 52-card deal and free-stack movement while changing tableau building from alternating colors to **same suit** turns a moderately forgiving game into one of the hardest classic single-deck solitaires `[Parlett1979]`, `[KellerSolLab]`.
- **Nomenclature Note:** Card historians note that the title *"Russian Solitaire"* (or *Russian Patience*) has historically been applied to several unrelated games in older 20th-century Hoyle editions (such as *Nidgi Novgorod*) before settling in modern digital collections onto this same-suit Yukon variant `[Parlett1979]`, `[KellerSolLab]`.

---

## 2. Formal Rules

### Setup & Deal
- **Deck:** 1 standard 52-card deck, all 52 cards dealt directly to the tableau (no Stock, no Waste).
- **Tableau (7 columns):** `[1, 6, 7, 8, 9, 10, 11]` cards (21 face-down, 31 face-up), identical to [Yukon](yukon.md).
- **Foundations (4 piles):** Start empty.

### Play Mechanics
- **Foundations:** Build **up in suit** from Ace (`1`) to King (`13`). Single cards only.
- **Tableau:** Build **down in the SAME SUIT** (e.g., `9♥` only on `10♥`).
  - **Free-Stack Movement:** Any face-up card may be moved along with all cards stacked above it, regardless of whether those overlying cards are in sequence.
  - Exposing a face-down card flips it face-up at turn end.
  - Only a **King** (with any cards on top of it) may fill an empty tableau column.

### Win & Loss Conditions
- **Victory:** All 52 cards reach the 4 Foundations.
- **Loss / Deadlock:** No legal moves remain.

### Rule Ambiguities & SolEngine Conventions

| Rule Point | Historical / Regional Variations | SolEngine Convention |
| :--- | :--- | :--- |
| **Empty Tableau Columns** | Standard Russian Solitaire restricts empty columns strictly to **Kings only**. | **Kings only** (`empty_tableau: "king"`). |
| **Foundation-to-Tableau** | Disallowed in standard rules (and never beneficial in a same-suit game). | **Disallowed** (`yukon_can_pick()` permits picks only from `4..10`). |

---

## 3. Eatscript Implementation Mapping

Defined in [games/yukon.eats](../../../games/yukon.eats#L84-L85) and [L133-L173](../../../games/yukon.eats#L133-L173).

### Zone Map
| Zone Index | Role | Count | Initial State |
| :---: | :--- | :---: | :--- |
| `0..3` | Foundations | 4 | Empty |
| `4..10` | Tableau Columns | 7 | `[1, 6, 7, 8, 9, 10, 11]` (`0..6` face-down, topped by `1, 5, 5, 5, 5, 5, 5` face-up) |

### Callbacks & Shared Helpers
- `yukon_eval()` ([yukon.eats:L10-L32](../../../games/yukon.eats#L10-L32)): Shared with Yukon.
- `yukon_can_pick(z, i)` ([yukon.eats:L34-L38](../../../games/yukon.eats#L34-L38)): Shared with Yukon (any face-up card in `4..10`).
- `yukon_drop_suit(fz, i, tz)` → `yukon_can_drop(True, fz, i, tz)` ([yukon.eats:L40-L64](../../../games/yukon.eats#L40-L64)): Same-suit descending tableau drops; Kings-only on empty columns; Ace-to-King suit foundations.
- `yukon_on_turn_end()` & `yukon_status()` ([yukon.eats:L66-L79](../../../games/yukon.eats#L66-L79)): Shared with Yukon.

---

## 4. Solver Considerations

### Search & Information Profile
- **Information Visibility:** Half-open (21 face-down cards, 31 face-up; no stock).
- **Branching Factor:** **Much lower** than Yukon — because building is strictly same-suit, every non-King card has **at most 1 legal tableau destination** (`rank + 1` of the exact same suit), and only Kings can use empty columns. Consequently, dead-end search trees terminate very quickly compared to Yukon.
- **Cycles & Dominance Rules:**
  - *Mandatory Safe-Foundation Dominance:* Because tableau building is **strictly same-suit**, **every legal Foundation move is unconditionally safe** — a card of suit `S` and rank `r` can only ever support `(S, r - 1)`, which is already on the foundation when `(S, r)` is eligible `[BlakeGent2019]`. Forcing foundation promotions immediately shrinks the search tree with zero risk of pruning a winning line.
  - *Immediate Deadlock Patterns:* Any deal where a card `C` is buried beneath its own suit's `rank + 1` (e.g., `5♣` directly on top of `4♣` with no way to lift `5♣` onto `6♣` because `6♣` is buried underneath `4♣`) forms an unbreakable suit-cycle deadlock.

### Heuristic (`eval_heuristic`) Design
- **Current Eatscript Heuristic:** Shares `yukon_eval()` (`+500` foundation, `+150` empty column, `-200` face-down, `-15` buried face-up height).
- **Tuning Opportunities:**
  - *Suit-Order Bonus:* Rewarding adjacent same-suit descending pairs helps distinguish constructive consolidations from arbitrary packet moves.
  - *Zero-Mobility / Deadlock Early Exit:* Because ~90%+ of random deals are unsolvable, fast cycle/deadlock detection and forced foundation moves matter far more than deep heuristic sophistication.

### Solvability & Win Rates
- **Empirical / Solver Winnability:** Notoriously low — solver simulations on random deals place Russian Solitaire's theoretical win rate around **~7%–10%** `[WolterSolitaire]`, `[KellerSolLab]`.
- **Guaranteed Solvable (`Shift + N`) Notes:** Notice the sparse `demo_seeds` in [games/yukon.eats:L158](../../../games/yukon.eats#L158) (`[7, 43, 55, 58, 71, 85, 106, 144]`), reflecting how frequently random seeds are unsolvable (~10–15 seeds tested per solvable deal!). Because the branching factor is low, dead seeds fail fast — making Guaranteed Solvable (`Shift + N`) especially valuable for giving players a fair, winnable game.

---

## 5. References

- `[Parlett1979]` — Taxonomy of Yukon and same-suit patience variants. See [REFERENCES.md](../REFERENCES.md).
- `[KellerSolLab]` — Historical notes on *Russian Solitaire* difficulty and naming overlap. See [REFERENCES.md](../REFERENCES.md).
- `[WolterSolitaire]` — Empirical solver analysis and win-rate studies. See [REFERENCES.md](../REFERENCES.md).
- `[BlakeGent2019]` — Same-suit foundation dominance principles in patience solvers. See [REFERENCES.md](../REFERENCES.md).
