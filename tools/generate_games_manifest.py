#!/usr/bin/env python3
"""
Generate games/manifest.json (and mirror to web/games/manifest.json if present)
so SolEngine Desktop and Web builds can populate the Game Browser in O(1) RAM
without eagerly parsing all .eats files at startup.
"""
import glob
import json
import os
import re
import shutil


def parse_eats_value(raw: str):
    raw = raw.strip()
    if raw == "True":
        return True
    if raw == "False":
        return False
    if raw.startswith('"') and raw.endswith('"'):
        return json.loads(raw)
    if raw.startswith('[') and raw.endswith(']'):
        inner = raw[1:-1].strip()
        if not inner:
            return []
        items = []
        for part in inner.split(','):
            part = part.strip()
            if not part:
                continue
            items.append(parse_eats_value(part))
        return items
    try:
        return int(raw)
    except ValueError:
        try:
            return float(raw)
        except ValueError:
            return raw


STRING_KEYS = [
    "id", "name", "family", "category", "mechanic", "visibility", "layout",
    "tableau_build", "empty_tableau", "move_group", "foundation_mode",
    "ribbon_rule", "description", "stock_deal_mode", "deck", "default_deck",
    "theme", "default_theme", "postfx", "ambience", "sound_pack", "preset",
    "series_id", "series_name",
]
INT_KEYS = [
    "decks", "deck_cards", "suit_count", "jokers", "grid_cols",
    "tableaus", "foundations", "freecells", "stock_deal_count",
    "max_redeals", "demo_seed", "series_order",
]
BOOL_KEYS = [
    "wrap_ranks", "king_dead_end", "exclude_red_royals",
    "supports_solver", "prefer_curated_solvable", "dynamic_layout",
    "allow_undo", "tableau_all_face_up", "foundation_starter_card",
]
LIST_KEYS = [
    "tags", "exclude_ranks", "tableau_deal_counts", "demo_seeds",
]


def extract_games_from_file(path: str):
    filename = os.path.basename(path)
    with open(path, "r", encoding="utf-8") as f:
        txt = f.read()

    entries = []
    for m in re.finditer(r'sol\.define_game\(\{([\s\S]*?)\n\}\)', txt):
        block = m.group(1)
        # Strip nested doc: { ... } block so key regexes only inspect top-level fields
        top = re.sub(r'\bdoc\s*:\s*\{[\s\S]*', '', block)

        item = {"file": filename}

        for k in STRING_KEYS:
            mm = re.search(rf'^\s*{k}\s*:\s*("(?:[^"\\]|\\.)*")', top, re.MULTILINE)
            if mm:
                item[k] = json.loads(mm.group(1))

        for k in INT_KEYS:
            mm = re.search(rf'^\s*{k}\s*:\s*(-?\d+)', top, re.MULTILINE)
            if mm:
                item[k] = int(mm.group(1))

        for k in BOOL_KEYS:
            mm = re.search(rf'^\s*{k}\s*:\s*(True|False)\b', top, re.MULTILINE)
            if mm:
                item[k] = (mm.group(1) == "True")

        for k in LIST_KEYS:
            mm = re.search(rf'^\s*{k}\s*:\s*(\[[^\]]*\])', top, re.MULTILINE)
            if mm:
                item[k] = parse_eats_value(mm.group(1))

        if "id" in item:
            entries.append(item)

    return entries


def main():
    games = []
    for path in sorted(glob.glob("games/*.eats")):
        games.extend(extract_games_from_file(path))

    manifest = {
        "schema_version": 1,
        "count": len(games),
        "games": games,
    }

    out_path = os.path.join("games", "manifest.json")
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, separators=(",", ":"), ensure_ascii=False)
        f.write("\n")

    size_kb = os.path.getsize(out_path) / 1024.0
    print(f"Generated {out_path}: {len(games)} games ({size_kb:.1f} KB)")

    web_games_dir = os.path.join("web", "games")
    if os.path.isdir(web_games_dir):
        shutil.copy2(out_path, os.path.join(web_games_dir, "manifest.json"))
        print(f"Mirrored to {os.path.join(web_games_dir, 'manifest.json')}")


if __name__ == "__main__":
    main()
