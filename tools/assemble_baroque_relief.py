#!/usr/bin/env python3
"""
tools/assemble_baroque_relief.py
================================
Assembles the Baroque Relief 54-card deck:
1. Verifies/resizes all 54 card PNGs to exact 360x504.
2. Compiles the master 9x6 bitmap sheet (3240x3024 PNG) at decks/baroque_relief.png.
3. Automatically packages decks/baroque_relief.eats for the SolEngine engine.
"""

import os
import sys
from PIL import Image

sys.path.insert(0, 'tools')
from deck_pipeline import CARD_NAMES
from package_deck_eats import package_png_deck

cards_dir = "assets/deck_cards/baroque_relief/png"
fallback_dir = "assets/deck_cards/baroque_noir/png"
out_master_png = "decks/baroque_relief.png"
out_master_eats = "decks/baroque_relief.eats"
out_master_bin = "decks/baroque_relief.bin"

target_w, target_h = 360, 504
cols, rows = 9, 6
sheet_w = cols * target_w
sheet_h = rows * target_h

master_sheet = Image.new("RGBA", (sheet_w, sheet_h), (20, 22, 26, 255))
missing_cards = []

for idx, name in enumerate(CARD_NAMES):
    card_path = os.path.join(cards_dir, f"{name}.png")
    if not os.path.exists(card_path):
        missing_cards.append(name)
        # Use fallback if available during incremental progress
        fb_path = os.path.join(fallback_dir, f"{name}.png")
        if os.path.exists(fb_path):
            im = Image.open(fb_path).convert('RGBA')
        else:
            im = Image.new('RGBA', (target_w, target_h), (30, 32, 36, 255))
    else:
        im = Image.open(card_path).convert('RGBA')

    if im.size != (target_w, target_h):
        im = im.resize((target_w, target_h), Image.Resampling.LANCZOS)
        im.save(card_path, format='PNG')

    col = idx % cols
    row = idx // cols
    master_sheet.paste(im, (col * target_w, row * target_h))

os.makedirs('decks', exist_ok=True)
master_sheet.save(out_master_png, format="PNG")
print(f"Master 9x6 sheet saved: {out_master_png} ({sheet_w}x{sheet_h}, {os.path.getsize(out_master_png):,} bytes)")

if os.path.exists(out_master_bin):
    os.remove(out_master_bin)
    print("Invalidated stale .bin cache.")

package_png_deck(out_master_png, out_master_eats, name="Baroque Relief", theme_id="baroque_relief")
print("Packaged deck to", out_master_eats)

if missing_cards:
    print(f"\n[NOTE] Incremental build: {len(CARD_NAMES) - len(missing_cards)}/54 native cards present.")
    print(f"       {len(missing_cards)} cards temporarily bridged from Baroque Noir baseline.")
else:
    print("\n[SUCCESS] Full 54-card Baroque Relief deck assembled completely!")

