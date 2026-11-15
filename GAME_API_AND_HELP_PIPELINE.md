# SolEngine Unified Game API, Auto-Discovered Help & Universal Solver Pipeline

> **Purpose:** This document defines one of SolEngine's core architectural pillars: every game's `.eats` script serves as a **single source of truth** that simultaneously drives **gameplay rules**, **auto-discovered Help/Docs (with live table callouts)**, and the **universal Auto-Solver / Auto-Play** engine.

---

## 1. Philosophy: One API, Three Consumers

Instead of hardcoding rules in C++, writing separate static help pages, and building per-game solver logic, SolEngine routes all three through a unified `ScriptRuleEngine` + `GameDocManifest` contract:

1. **Gameplay (`ScriptRuleEngine`):** Eatscript lifecycle hooks mutate the board via atomic, undoable `BoardOp` transactions (`TurnTransaction`).
2. **Help & Table Callouts (`GameDocManifest`):** Top-level `doc_*` declarations bind human-readable descriptions directly to real `Zone` indices, `BoardBlackboard` stat keys, and `Card` tags—allowing a Hardwood 3-style right-hand Help Sidebar to outline exact piles on the table.
3. **Universal Solver & Auto-Play (`SolitaireSolver` / `AutoPlayController`):** Enumerates legal actions (`on_stock_click`, `on_card_click`, `can_drop`, `can_target`), prunes `GameStatus::Defeat`, detects `GameStatus::Victory`, and ranks states via `eval_heuristic()`.

---

## 2. Authoring a New Game in `.eats` (Checklist for AI & Contributors)

Whenever adding a new game in `games/*.eats`, **always** include both the rule hooks and the introspection/solver metadata:

### A. Declarative Header
```python
name = "My Variant"
family = "Scoundrel"
category = "RPG"          # e.g. POPULAR, KLONDIKE, SPIDER, ADDING, RPG
layout = "columns"
description = "One-line summary for the Game Browser."
decks = 1
tableaus = 4
foundations = 1
freecells = 0
tableau_deal_counts = [1, 1, 1, 1]
tableau_all_face_up = True
stock_deal_mode = "none"
demo_seed = 2026          # Required for built-in games: known-solvable seed for "SHOW ME" fallback
alternate_names = ["Alias 1", "Alias 2"]
```

### B. Help & Introspection Manifest (`doc_*`)
Call these at top-level in the `.eats` file so `GameDocManifest` is populated immediately on load:
* `doc_goal(win_text, lose_text)` — Describes victory and defeat conditions.
* `doc_zone(zone_idx, title, description)` — Documents a specific pile/zone index (`0..N-1`) for live table outline highlighting.
* `doc_stat(key, title, description)` — Documents a global `BoardBlackboard` stat (e.g., `"HP"`, `"WEAPON"`, `"MANA"`).
* `doc_tag(tag, title, description)` — Documents a card tag/role (e.g., `"monster"`, `"weapon"`, `"potion"`).

### C. Lifecycle Hooks
* `on_deal()` — Initialize `BoardBlackboard` stats (`set_stat("HP", 20)`) and tag/stat initial cards (`set_card_stat`, `add_card_tag`).
* `can_pick(z, i)` / `can_drop(from_z, i, to_z)` — Stack/pile movement legality.
* `can_target(from_z, from_i, to_z, to_i)` / `on_target(...)` — Card-on-card interactions (combat, pairing, equipping).
* `on_card_click(z, i)` — Single-click activation (potions, abilities, room encounters).
* `on_stock_click()` — Custom stock draw or room refill.
* `on_turn_end()` — Post-action triggers (room refill, enemy retaliation, status ticks).
* `get_status()` — Returns `"playing"`, `"victory"`, or `"defeat"`.

### D. Solver Heuristic (`eval_heuristic()`)
Provide `def eval_heuristic():` returning an integer score (higher = closer to winning) so `SolitaireSolver` and `AutoPlayController` can search intelligently without per-game C++ code:
```python
def eval_heuristic():
    return get_stat("HP", 20) * 120 - remaining_cards * 80
```

---

## 3. Reference Implementation
See [scoundrel.eats](file:///c:/git/solengine/games/scoundrel.eats) for the canonical RPG solitaire example implementing `doc_goal`, `doc_zone`, `doc_stat`, `doc_tag`, `eval_heuristic`, card stat badges (`hp`, `attack`, `durability`), and `Victory`/`Defeat` states.
