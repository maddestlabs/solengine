#!/usr/bin/env python3
"""
SolEngine PySolFC Game Importer / Extractor (.eats)
=====================================================
Parses PySolFC Python game definitions via static AST analysis
and generates Eatscript control-plane specification (.eats) files.

Features:
- Completely static AST parsing (requires no PySolFC, Tkinter, or external dependencies).
- Extracts Talon (Stock), Foundation, Tableau (RowStack), and Freecell rules.
- Translates PySolFC stacking & movement semantics into idiomatic Eatscript (.eats) hooks.
- Supports single file input, directory batch processing, or fetching directly from GitHub.

Usage:
    python tools/import_pysolfc.py path/to/pysolfc/pysollib/games/spider.py
    python tools/import_pysolfc.py --url https://raw.githubusercontent.com/shlomif/PySolFC/master/pysollib/games/golf.py
    python tools/import_pysolfc.py --scan path/to/pysollib/games/ --output-dir games/
"""

import ast
import argparse
import sys
import os
import urllib.request
from typing import Dict, Any, List, Optional


class PySolGameVisitor(ast.NodeVisitor):
    """AST Visitor that inspects PySolFC Game class definitions."""

    def __init__(self):
        self.games: List[Dict[str, Any]] = []
        self.current_game: Optional[Dict[str, Any]] = None

    def visit_ClassDef(self, node: ast.ClassDef):
        # Determine if this class derives from Game, FreeCell, Spider, etc.
        base_names = []
        for base in node.bases:
            if isinstance(base, ast.Name):
                base_names.append(base.id)
            elif isinstance(base, ast.Attribute):
                base_names.append(base.attr)

        # Check if it looks like a Solitaire game class
        is_game = any(
            b in base_names
            for b in [
                "Game", "FreeCell", "Spider", "Golf", "Klondike",
                "Yukon", "FortyThieves", "BakersDozen", "Canfield"
            ]
        ) or any("Game" in b for b in base_names)

        if is_game:
            doc = ast.get_docstring(node) or ""
            game_data: Dict[str, Any] = {
                "class_name": node.name,
                "bases": base_names,
                "doc": doc.strip(),
                "attributes": {},
                "talon_class": None,
                "foundation_class": None,
                "rowstack_class": None,
                "decks": 1,
                "suits": 4,
                "tableaus": 7,
                "foundations": 4,
                "freecells": 0,
                "max_redeals": 0,
                "tableau_deal_counts": [],
                "stock_deal_mode": "waste_draw_1",
            }

            # Inspect class-level attributes
            for stmt in node.body:
                if isinstance(stmt, ast.Assign):
                    for target in stmt.targets:
                        if isinstance(target, ast.Name):
                            val = self._extract_value(stmt.value)
                            game_data["attributes"][target.id] = val
                            if target.id == "Talon_Class":
                                game_data["talon_class"] = str(val)
                            elif target.id == "Foundation_Class":
                                game_data["foundation_class"] = str(val)
                            elif target.id == "RowStack_Class":
                                game_data["rowstack_class"] = str(val)
                            elif target.id in ("max_redeals", "redeals"):
                                game_data["max_redeals"] = val

            self._infer_game_semantics(game_data)
            self.games.append(game_data)

        self.generic_visit(node)

    def _extract_value(self, node: ast.AST) -> Any:
        if isinstance(node, ast.Constant):
            return node.value
        elif isinstance(node, ast.Name):
            return node.id
        elif isinstance(node, ast.List):
            return [self._extract_value(elt) for elt in node.elts]
        elif isinstance(node, ast.Tuple):
            return tuple(self._extract_value(elt) for elt in node.elts)
        elif isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
            val = self._extract_value(node.operand)
            return -val if isinstance(val, (int, float)) else val
        return None

    def _infer_game_semantics(self, game: Dict[str, Any]):
        cname = game["class_name"].lower()
        bases_lower = [b.lower() for b in game["bases"]]
        attrs = game["attributes"]

        # 1. Family inference
        if "spiderette" in cname:
            game["family"] = "Spider"
            game["decks"] = 1
            game["tableaus"] = 7
            game["foundations"] = 4
            game["tableau_deal_counts"] = [1, 2, 3, 4, 5, 6, 7]
            game["stock_deal_mode"] = "deal_to_all_columns"
        elif "spider" in cname or any("spider" in b for b in bases_lower):
            game["family"] = "Spider"
            game["decks"] = 2
            game["tableaus"] = 10
            game["foundations"] = 8
            game["tableau_deal_counts"] = [6, 6, 6, 6, 5, 5, 5, 5, 5, 5]
            game["stock_deal_mode"] = "deal_to_all_columns"
        elif "tripeaks" in cname or "tri_peaks" in cname:
            game["family"] = "Golf"
            game["decks"] = 1
            game["tableaus"] = 7
            game["foundations"] = 1
            game["tableau_deal_counts"] = [4, 4, 4, 4, 4, 4, 4]
            game["stock_deal_mode"] = "deal_to_foundation"
            game["max_redeals"] = 0
        elif "golf" in cname or any("golf" in b for b in bases_lower):
            game["family"] = "Golf"
            game["decks"] = 1
            game["tableaus"] = 7
            game["foundations"] = 1
            game["tableau_deal_counts"] = [5, 5, 5, 5, 5, 5, 5]
            game["stock_deal_mode"] = "deal_to_foundation"
            game["max_redeals"] = 0
        elif "baker" in cname:
            game["family"] = "FreeCell"
            game["decks"] = 1
            game["tableaus"] = 8
            game["foundations"] = 4
            game["freecells"] = 4
            game["tableau_deal_counts"] = [7, 7, 7, 7, 6, 6, 6, 6]
            game["stock_deal_mode"] = "none"
        elif "seahaven" in cname:
            game["family"] = "FreeCell"
            game["decks"] = 1
            game["tableaus"] = 10
            game["foundations"] = 4
            game["freecells"] = 4
            game["tableau_deal_counts"] = [5] * 10
            game["stock_deal_mode"] = "none"
        elif "eight_off" in cname or "eightoff" in cname:
            game["family"] = "FreeCell"
            game["decks"] = 1
            game["tableaus"] = 8
            game["foundations"] = 4
            game["freecells"] = 8
            game["tableau_deal_counts"] = [6] * 8
            game["stock_deal_mode"] = "none"
        elif "freecell" in cname or any("freecell" in b for b in bases_lower):
            game["family"] = "FreeCell"
            game["decks"] = 1
            game["tableaus"] = 8
            game["foundations"] = 4
            game["freecells"] = 4
            game["tableau_deal_counts"] = [7, 7, 7, 7, 6, 6, 6, 6]
            game["stock_deal_mode"] = "none"
        elif "easthaven" in cname:
            game["family"] = "Klondike"
            game["decks"] = 1
            game["tableaus"] = 7
            game["foundations"] = 4
            game["tableau_deal_counts"] = [3, 3, 3, 3, 3, 3, 3]
            game["stock_deal_mode"] = "deal_to_all_columns"
        elif "klondike" in cname or any("klondike" in b for b in bases_lower):
            game["family"] = "Klondike"
            game["decks"] = 1
            game["tableaus"] = 7
            game["foundations"] = 4
            game["tableau_deal_counts"] = [1, 2, 3, 4, 5, 6, 7]
            game["stock_deal_mode"] = "waste_draw_1"
        elif "russian" in cname:
            game["family"] = "Yukon"
            game["decks"] = 1
            game["tableaus"] = 7
            game["foundations"] = 4
            game["stock_deal_mode"] = "none"
            game["tableau_deal_counts"] = [1, 6, 7, 8, 9, 10, 11]
        elif "yukon" in cname:
            game["family"] = "Yukon"
            game["decks"] = 1
            game["tableaus"] = 7
            game["foundations"] = 4
            game["stock_deal_mode"] = "none"
            game["tableau_deal_counts"] = [1, 6, 7, 8, 9, 10, 11]
        elif "forty" in cname or "thieves" in cname:
            game["family"] = "FortyThieves"
            game["decks"] = 2
            game["tableaus"] = 10
            game["foundations"] = 8
            game["tableau_deal_counts"] = [4] * 10
            game["stock_deal_mode"] = "waste_draw_1"
        else:
            game["family"] = game["class_name"]


def generate_eats_code(game: Dict[str, Any]) -> str:
    """Generates clean, idiomatic Eatscript code for SolEngine from game metadata."""
    name = game.get("class_name", "Solitaire")
    family = game.get("family", name)
    decks = game.get("decks", 1)
    tableaus = game.get("tableaus", 7)
    foundations = game.get("foundations", 4)
    freecells = game.get("freecells", 0)
    deal_counts = game.get("tableau_deal_counts", [5] * tableaus)
    stock_mode = game.get("stock_deal_mode", "waste_draw_1")
    max_redeals = game.get("max_redeals", 0)

    lines = [
        "# =============================================================================",
        f"# SolEngine Game Variant: {name} (.eats)",
        f"# Ported from PySolFC component graph architecture",
        "# =============================================================================",
        f'name = "{name}"',
        f'family = "{family}"',
        f"decks = {decks}",
        f"tableaus = {tableaus}",
        f"foundations = {foundations}",
        f"freecells = {freecells}",
        f"tableau_deal_counts = {deal_counts}",
        f'stock_deal_mode = "{stock_mode}"',
        f"max_redeals = {max_redeals}",
        "",
    ]

    # Family-specific Eatscript behavior hooks
    if family == "Golf":
        lines.extend([
            "def can_drop_foundation(foundation_card, moving_card):",
            '    """In Golf, cards stack onto Foundation if rank is +- 1 (any suit). Kings are dead ends."""',
            "    if foundation_card.is_empty:",
            "        return True",
            "    if foundation_card.rank == 13: # King cannot be built upon",
            "        return False",
            "    return (moving_card.rank == foundation_card.rank + 1) or (moving_card.rank == foundation_card.rank - 1)",
            "",
            "def can_move_tableau_to_tableau():",
            "    return False",
            "",
            "def can_lift_tableau(column_cards, card_index):",
            "    return card_index == (len(column_cards) - 1)",
            "",
            "def check_victory(tableaus):",
            "    for col in tableaus:",
            "        if len(col) > 0:",
            "            return False",
            "    return True",
        ])
    elif family == "Spider":
        lines.extend([
            "def can_drop_tableau(dest_card, moving_card):",
            '    """In Spider, any card can drop onto empty column. On filled column, rank must be dest - 1."""',
            "    if dest_card.is_empty:",
            "        return True",
            "    return moving_card.rank == dest_card.rank - 1",
            "",
            "def can_lift_sequence(cards):",
            '    """Multi-card moves require uniform suit and continuous descending sequence."""',
            "    if len(cards) <= 1:",
            "        return True",
            "    for i in range(len(cards) - 1):",
            "        if cards[i].suit != cards[i + 1].suit or cards[i].rank != cards[i + 1].rank + 1:",
            "            return False",
            "    return True",
            "",
            "def check_completed_run(column_cards):",
            '    """Checks if the column ends with a complete 13-card King-to-Ace run of same suit."""',
            "    n = len(column_cards)",
            "    if n < 13 or not column_cards[n - 1].face_up or column_cards[n - 1].rank != 1:",
            "        return -1",
            "    lead_suit = column_cards[n - 1].suit",
            "    for i in range(13):",
            "        c = column_cards[n - 1 - i]",
            "        if not c.face_up or c.suit != lead_suit or c.rank != (1 + i):",
            "            return -1",
            "    return n - 13",
        ])
    elif family == "FreeCell":
        lines.extend([
            "def can_drop_tableau(dest_card, moving_card):",
            '    """FreeCell builds down in alternating colors."""',
            "    if dest_card.is_empty:",
            "        return True",
            "    is_moving_red = (moving_card.suit == 1 or moving_card.suit == 2)",
            "    is_dest_red = (dest_card.suit == 1 or dest_card.suit == 2)",
            "    return (is_moving_red != is_dest_red) and (dest_card.rank == moving_card.rank + 1)",
            "",
            "def can_drop_foundation(foundation_card, moving_card):",
            '    """Foundations build up by same suit from Ace to King."""',
            "    if foundation_card.is_empty:",
            "        return moving_card.rank == 1 # Ace",
            "    return (foundation_card.suit == moving_card.suit) and (moving_card.rank == foundation_card.rank + 1)",
        ])
    else: # Klondike / Standard
        lines.extend([
            "def can_drop_tableau(dest_card, moving_card):",
            '    """Klondike: descending alternating colors; Kings on empty spaces."""',
            "    if dest_card.is_empty:",
            "        return moving_card.rank == 13 # King",
            "    is_moving_red = (moving_card.suit == 1 or moving_card.suit == 2)",
            "    is_dest_red = (dest_card.suit == 1 or dest_card.suit == 2)",
            "    return (is_moving_red != is_dest_red) and (dest_card.rank == moving_card.rank + 1)",
            "",
            "def can_drop_foundation(foundation_card, moving_card):",
            '    """Foundations build up by same suit from Ace to King."""',
            "    if foundation_card.is_empty:",
            "        return moving_card.rank == 1 # Ace",
            "    return (foundation_card.suit == moving_card.suit) and (moving_card.rank == foundation_card.rank + 1)",
        ])

    return "\n".join(lines) + "\n"


def parse_pysolfc_source(source_text: str) -> List[Dict[str, Any]]:
    tree = ast.parse(source_text)
    visitor = PySolGameVisitor()
    visitor.visit(tree)
    return visitor.games


def process_file(file_path: str, output_dir: str):
    with open(file_path, "r", encoding="utf-8", errors="ignore") as f:
        source = f.read()

    games = parse_pysolfc_source(source)
    if not games:
        print(f"[-] No Game classes detected in {file_path}")
        return

    os.makedirs(output_dir, exist_ok=True)
    for g in games:
        cname = g["class_name"]
        slug = cname.lower().replace(" ", "_")
        out_path = os.path.join(output_dir, f"{slug}.eats")
        code = generate_eats_code(g)
        with open(out_path, "w", encoding="utf-8") as out_f:
            out_f.write(code)
        print(f"[+] Successfully extracted {cname} -> {out_path}")


def main():
    parser = argparse.ArgumentParser(description="PySolFC Game Importer for SolEngine (.eats)")
    parser.add_argument("file", nargs="?", help="Path to a PySolFC Python game file")
    parser.add_argument("--url", help="Fetch and process directly from raw GitHub URL")
    parser.add_argument("--scan", help="Scan and process all .py files in a directory")
    parser.add_argument("--output-dir", default="games", help="Target output directory for .eats files (default: games/)")
    args = parser.parse_args()

    if args.url:
        print(f"[*] Fetching {args.url} ...")
        req = urllib.request.Request(args.url, headers={"User-Agent": "SolEngine-Importer"})
        with urllib.request.urlopen(req) as resp:
            source = resp.read().decode("utf-8", errors="ignore")
        games = parse_pysolfc_source(source)
        os.makedirs(args.output_dir, exist_ok=True)
        for g in games:
            slug = g["class_name"].lower().replace(" ", "_")
            out_path = os.path.join(args.output_dir, f"{slug}.eats")
            with open(out_path, "w", encoding="utf-8") as f:
                f.write(generate_eats_code(g))
            print(f"[+] Successfully extracted {g['class_name']} -> {out_path}")
    elif args.scan:
        for root, _, files in os.walk(args.scan):
            for file in files:
                if file.endswith(".py"):
                    process_file(os.path.join(root, file), args.output_dir)
    elif args.file:
        process_file(args.file, args.output_dir)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()

