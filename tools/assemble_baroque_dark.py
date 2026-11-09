import glob
import os
import sys
from PIL import Image

sys.path.insert(0, 'tools')
from deck_pipeline import CARD_NAMES
from package_deck_eats import package_png_deck

brain_dir = r"C:\Users\root\.gemini\antigravity\brain\3d5a625f-03b4-4eda-87a9-4e8a55b9a99a"
out_cards_dir = "assets/deck_cards/baroque_dark/png"
os.makedirs(out_cards_dir, exist_ok=True)

all_jpgs = glob.glob(os.path.join(brain_dir, "*.jpg"))
card_file_map = {}

# Special cases from initial test run
card_file_map['spades_01_ace'] = os.path.join(brain_dir, 'baroque_dark_spades_01_ace_1793905701458.jpg')
card_file_map['hearts_01_ace'] = os.path.join(brain_dir, 'baroque_dark_hearts_01_ace_1793905721407.jpg')

for f in all_jpgs:
    base = os.path.basename(f)
    for name in CARD_NAMES:
        if name in ('spades_01_ace', 'hearts_01_ace'):
            continue
        if base.startswith(name + "_") or base.startswith(name + "."):
            if name not in card_file_map or os.path.getmtime(f) > os.path.getmtime(card_file_map[name]):
                card_file_map[name] = f

print(f"Found {len(card_file_map)}/54 cards in brain directory.")
missing = [name for name in CARD_NAMES if name not in card_file_map]
if missing:
    print(f"Still pending ({len(missing)} cards): {missing}")
    sys.exit(1)

print("All 54 cards located! Resizing with Full Bleed Direct Lanczos to 360x504...")
target_w, target_h = 360, 504
cols, rows = 9, 6
sheet_w = cols * target_w
sheet_h = rows * target_h

master_sheet = Image.new("RGBA", (sheet_w, sheet_h), (20, 22, 26, 255))

for idx, name in enumerate(CARD_NAMES):
    src = card_file_map[name]
    im = Image.open(src).convert('RGBA')
    resized = im.resize((target_w, target_h), Image.Resampling.LANCZOS)
    dest = os.path.join(out_cards_dir, f"{name}.png")
    resized.save(dest, format='PNG')
    
    col = idx % cols
    row = idx // cols
    master_sheet.paste(resized, (col * target_w, row * target_h))

print("All 54 individual card PNGs saved to", out_cards_dir)

master_png = "decks/baroque_dark.png"
master_sheet.save(master_png, format="PNG")
print(f"Saved master sheet ({sheet_w}x{sheet_h}) to {master_png} ({os.path.getsize(master_png):,} bytes)")

master_eats = "decks/baroque_dark.eats"
package_png_deck(master_png, master_eats, name="Baroque Dark", theme_id="baroque_dark")
print("Baroque Dark deck assembly and packaging complete!")
