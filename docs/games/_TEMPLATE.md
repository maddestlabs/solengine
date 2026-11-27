---
id: "<eatscript_game_id>"
name: "<Canonical Game Name>"
aka: ["<Historical Alias 1>", "<Regional Alias 2>"]
family: "<Family Name>"
ancestor: "<Parent Game ID or null>"
decks: 1
info_type: "half_open"          # open | half_open | hidden
mechanic: "building"            # building | pairing | sequence | compression | memory | roguelike
status: "researched"            # researched | implemented | solver-tuned
eatscript: "../../../games/<family>.eats"
video: null                     # Optional YouTube video ID (e.g., "Y7oD9qvcFKw")
---

# <Canonical Game Name>

> **One-line summary:** Concise description of the game's core loop and distinguishing mechanic.

| Field | Value |
| :--- | :--- |
| **Game ID(s)** | `` `<id>` `` |
| **Family** | [<Family Name>](./README.md) |
| **Decks** | 1 (52 cards) |
| **Information Model** | Half-Open / Open / Hidden |
| **Status** | `researched` / `implemented` / `solver-tuned` |
| **Eatscript Source** | [games/<family>.eats](../../../games/<family>.eats) |
| **Play Online** | [Play <Canonical Game Name>](https://solitaireengine.org/?game=<id>) |

### Video Walkthrough *(Optional)*

<!-- Include when a SolEngine walkthrough video exists; matches doc.video in the .eats file -->
[![Watch <Canonical Game Name> Walkthrough](https://img.youtube.com/vi/<VIDEO_ID>/hqdefault.jpg)](https://www.youtube.com/watch?v=<VIDEO_ID>)

*[Watch on YouTube](https://www.youtube.com/watch?v=<VIDEO_ID>)*

---

## 1. History & Lineage

*Every historical claim must carry an inline citation key from [REFERENCES.md](../REFERENCES.md) (e.g., `[Parlett1979]`, `[MoreheadMottSmith1949]`). Do not repeat unsourced internet folklore as fact.*

- **Earliest Known Print Appearance:** Author, title, year, and original name `[CiteKey]`.
- **Nomenclature & Aliases:** Historical or regional names, noting any documented name collisions with other games `[CiteKey]`.
- **Lineage:** Parent game(s) this evolved from, and notable descendant variants.

> [!NOTE]
> **Unverified / Folklore Claims:** Explicitly isolate any popular origin stories or etymologies that lack primary print evidence here so readers know what is documented vs. anecdotal.

---

## 2. Formal Rules

### Setup & Deal
- **Deck(s):** Number of standard 52-card decks (or custom deck subset).
- **Zones:** Exact zone breakdown (Stock, Waste, Foundations, Reserve, Free Cells, Tableau columns) and initial card counts (face-down vs. face-up).

### Play Mechanics
- **Foundations:** Starting rank, build direction, suit rule, whether wrapping is allowed, whether cards may be pulled back from Foundations ("worrying back").
- **Tableau:** Build rule (alternating color, same suit, any suit), group movement rule (single card, ordered run, arbitrary face-up substack, supermove capacity), and empty-column fill rule.
- **Stock / Waste / Redeals:** Cards turned per click, redeal limit (`0`, `N`, or unlimited), and whether stock deals to waste or across columns.

### Win & Loss Conditions
- **Victory:** Exact terminal condition.
- **Loss / Deadlock:** When no productive legal moves remain.

### Rule Ambiguities & SolEngine Conventions
*Classic patience rulebooks frequently disagree on edge cases. Document each ambiguity and the exact convention SolEngine adopts:*

| Rule Point | Historical / Regional Variations | SolEngine Convention |
| :--- | :--- | :--- |
| *e.g., Empty tableau spaces* | *Kings only vs. any card* | *Kings only* `[CiteKey]` |
| *e.g., Redeals* | *1 pass, 3 passes, or unlimited* | *Unlimited* |

---

## 3. Eatscript Implementation Mapping

*Keep this synchronized with [games/<family>.eats](../../../games/<family>.eats).*

### Zone Map
| Zone Index | Role | Notes |
| :---: | :--- | :--- |
| `0` | Stock | |
| `1` | Waste | |
| `2..5` | Foundations | |
| `6..12` | Tableau | |

### Callbacks & Shared Helpers
- `can_pick`: `<function_name>` — brief summary of pick constraints.
- `can_drop`: `<function_name>` — brief summary of drop constraints.
- `on_stock_click`: `<function_name>` — stock/redeal behavior.
- `on_turn_end`: `<function_name>` — automatic flips or refills.
- `get_status`: `<function_name>` — victory/loss detection.

### Edge Cases & Engine Notes
- Note any engine-specific behaviors (auto-foundation safety, custom metadata, demo seeds).

---

## 4. Solver Considerations

*Design the solver profile alongside the rules—not as an afterthought.*

### Search & Information Profile
- **Information Visibility:** Open (perfect information) vs. Partial/Hidden (imperfect information; solver operates in "thoughtful" omniscience mode during Guaranteed Solvable generation vs. belief-state / rollout during live hints).
- **Branching Factor:** Typical legal moves per state and what drives blowup (e.g., arbitrary substack splits, empty columns, stock cycling).
- **Cycles & Transpositions:** Reversible moves (e.g., bouncing a red Queen between two black Kings, cycling an unchanged waste pile, worrying cards back from foundations) and symmetry reductions (interchangeable empty columns or suit-symmetric foundations).

### Heuristic (`eval_heuristic`) Design
- **Current Eatscript Heuristic:** Summarize the terms in `<game>_eval()` and their weights.
- **Key Domain Features:** What actually correlates with winning (e.g., uncovering face-down cards, keeping columns empty, preserving low cards, minimizing buried aces/twos).
- **Known Pitfalls:** Local maxima or greedy traps (e.g., blindly rushing cards to foundations when they are needed as tableau bridges).

### Solvability & Win Rates
- **Theoretical / Empirical Winnability:** Cite known solver studies (e.g., `[BlakeGent2019]`, `[Bjarnason2009]`, `[WolterSolitaire]`) and clearly distinguish **thoughtful (open)** solvability from **blind/human** win rates.
- **Guaranteed Solvable (`Shift + N`) Notes:** Expected search difficulty, recommended timeout/node budget, and whether unsolvable-deal rejection rates are high.

---

## 5. References

List every citation key used in this document, linked to [REFERENCES.md](../REFERENCES.md), with specific page numbers, section titles, or archived URLs where applicable.

- `[CiteKey]` — Specific page/section notes.
