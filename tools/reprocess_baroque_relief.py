#!/usr/bin/env python3
"""
tools/reprocess_baroque_relief.py
==================================
Completely reprocesses the Baroque Relief deck from untampered raw source images:
1. Loads all 54 cards directly from their original generated raw JPGs in the brain directory.
2. Accurately pinpoints and removes artificial border lines:
   - On Ace of Spades: interpolates strictly across the border lines without modulo tiling to eliminate mirror artifacts.
   - On cards with elevated bottom borders (Jack/Queen Spades, 4, 6, 7, 8, 10 Hearts): interpolates across the bottom border line.
   - On all other cards: extends adjacent authentic slate outward to the perimeter.
3. For cards narrower than 5:7 (848x1264), seamlessly extends slate cardstock on left and right via reflection padding.
4. For cards wider than 5:7 (896x1200), symmetrically trims excess slate margins to 857x1200.
5. Preserves 100% sharpness of central artwork, cathedral arches, portraits, suit symbols, and corner indices.
6. Generates 360x504 game cards, 720x1008 references, master 9x6 sheet (decks/baroque_relief.png), and compiles decks/baroque_relief.eats.
"""

import os
import sys
import glob
import cv2
import numpy as np
from PIL import Image

sys.path.insert(0, 'tools')
from deck_pipeline import CARD_NAMES
from package_deck_eats import package_png_deck

BRAIN_DIR = r"C:\Users\root\.gemini\antigravity\brain\4adab648-3377-4cfd-add3-fa3ba5528747"
OUT_CARDS_DIR = "assets/deck_cards/baroque_relief/png"
OUT_REF_DIR = "assets/reference"
MASTER_PNG = "decks/baroque_relief.png"
MASTER_EATS = "decks/baroque_relief.eats"
MASTER_BIN = "decks/baroque_relief.bin"

SPECIAL_MAP = {
    'spades_01_ace': 'baroque_ace_of_spades_revamped_1793992342352.jpg',
    'hearts_01_ace': 'ace_of_hearts_baroque_1793992510635.jpg',
    'spades_08': 'cathedral_eight_spades_1793994242183.jpg',
    'spades_13_king': 'cathedral_king_spades_1793994267297.jpg',
    'hearts_12_queen': 'queen_of_hearts_baroque_1793993986812.jpg'
}

def map_raw_images():
    all_jpgs = glob.glob(os.path.join(BRAIN_DIR, '*.jpg'))
    card_to_raw = {}
    for name, fname in SPECIAL_MAP.items():
        card_to_raw[name] = os.path.join(BRAIN_DIR, fname)
    for name in CARD_NAMES:
        if name in card_to_raw:
            continue
        matches = [f for f in all_jpgs if os.path.basename(f).startswith(name + '_')]
        if matches:
            matches.sort(key=lambda x: os.path.getmtime(x), reverse=True)
            card_to_raw[name] = matches[0]
    return card_to_raw

def detect_border_peak(profile, base_offset=0, min_peak=75, delta=20):
    idx = int(np.argmax(profile))
    val = profile[idx]
    if val >= min_peak and val >= profile.min() + delta:
        return base_offset + idx
    return None

def process_card(raw_path, card_name=None):
    im = cv2.imread(raw_path)
    if im is None:
        raise ValueError(f"Could not load image: {raw_path}")
    h, w = im.shape[:2]
    gray = cv2.cvtColor(im, cv2.COLOR_BGR2GRAY)
    
    top_profile = gray[:50, :].mean(axis=1)
    btm_profile = gray[h-75:, :].mean(axis=1)
    left_profile = gray[:, :50].mean(axis=0)
    right_profile = gray[:, w-50:].mean(axis=0)
    
    T = detect_border_peak(top_profile, 0)
    B = detect_border_peak(btm_profile, h-75)
    L = detect_border_peak(left_profile, 0)
    R = detect_border_peak(right_profile, w-50)
    
    if card_name == 'spades_01_ace':
        # Clean Ace of Spades strictly across border lines (zero mirroring into margins)
        # Top (23..32)
        for y in range(23, 33):
            a = (y - 23) / 10.0
            im[y, :] = ((1.0 - a) * im[22, :].astype(float) + a * im[33, :].astype(float)).astype(np.uint8)
        # Bottom (1171..1178)
        for y in range(1171, 1179):
            a = (y - 1171) / 8.0
            im[y, :] = ((1.0 - a) * im[1170, :].astype(float) + a * im[1179, :].astype(float)).astype(np.uint8)
        # Left (22..32)
        for x in range(22, 33):
            a = (x - 22) / 11.0
            im[:, x] = ((1.0 - a) * im[:, 21].astype(float) + a * im[:, 33].astype(float)).astype(np.uint8)
        # Right (863..873)
        for x in range(863, 874):
            a = (x - 863) / 11.0
            im[:, x] = ((1.0 - a) * im[:, 862].astype(float) + a * im[:, 874].astype(float)).astype(np.uint8)
    else:
        # Standard clean borders logic for all other cards
        if T is not None and T <= 40:
            span = 14
            for y in range(T + 8):
                src_y = T + 8 + (T + 8 - y) % span
                im[y, :] = im[src_y, :]
                
        if B is not None:
            if B >= h - 45:
                # Normal bottom border near edge
                span = 14
                for y in range(B - 7, h):
                    src_y = (B - 8) - (y - (B - 7)) % span
                    im[y, :] = im[src_y, :]
            elif B >= h - 70:
                # Elevated bottom border: interpolate across border line
                y1 = B - 4
                y2 = B + 8
                for y in range(y1, y2 + 1):
                    alpha = (y - y1) / float(y2 - y1 + 1)
                    im[y, :] = ((1.0 - alpha) * im[y1 - 1, :].astype(float) + alpha * im[y2 + 1, :].astype(float)).astype(np.uint8)
                
        if L is not None and L <= 40:
            span = 14
            for x in range(L + 8):
                src_x = L + 8 + (L + 8 - x) % span
                im[:, x] = im[:, src_x]
                
        if R is not None and R >= w - 45:
            span = 14
            for x in range(R - 7, w):
                src_x = (R - 8) - (x - (R - 7)) % span
                im[:, x] = im[:, src_x]
            
    # 3. Fit to exact 5:7 aspect ratio
    if w == 848 and h == 1264:
        # Narrow card: pad width with authentic reflected slate
        target_w = int(round(h * 5 / 7)) # 903
        pad_l = (target_w - w) // 2
        pad_r = target_w - w - pad_l
        card_5_7 = cv2.copyMakeBorder(im, 0, 0, pad_l, pad_r, cv2.BORDER_REFLECT_101)
    elif w == 896 and h == 1200:
        # Wide card: crop excess width symmetrically
        target_w = int(round(h * 5 / 7)) # 857
        x0 = (w - target_w) // 2
        card_5_7 = im[:, x0:x0 + target_w]
    else:
        # General aspect fit
        curr_ratio = w / float(h)
        target_ratio = 5.0 / 7.0
        if curr_ratio > target_ratio:
            target_w = int(round(h * target_ratio))
            x0 = (w - target_w) // 2
            card_5_7 = im[:, x0:x0 + target_w]
        else:
            target_w = int(round(h * target_ratio))
            pad_l = (target_w - w) // 2
            pad_r = target_w - w - pad_l
            card_5_7 = cv2.copyMakeBorder(im, 0, 0, pad_l, pad_r, cv2.BORDER_REFLECT_101)
            
    card_360 = cv2.resize(card_5_7, (360, 504), interpolation=cv2.INTER_LANCZOS4)
    card_720 = cv2.resize(card_5_7, (720, 1008), interpolation=cv2.INTER_LANCZOS4)
    return card_360, card_720, (T, B, L, R)

def main():
    os.makedirs(OUT_CARDS_DIR, exist_ok=True)
    os.makedirs(OUT_REF_DIR, exist_ok=True)
    os.makedirs('decks', exist_ok=True)
    
    card_map = map_raw_images()
    print(f"Mapped {len(card_map)}/54 cards to raw generated files.")
    
    for idx, name in enumerate(CARD_NAMES):
        raw_p = card_map.get(name)
        if not raw_p or not os.path.exists(raw_p):
            print(f"Error: Missing raw file for {name}")
            continue
            
        c360, c720, borders = process_card(raw_p, card_name=name)
        out_360 = os.path.join(OUT_CARDS_DIR, f"{name}.png")
        out_720 = os.path.join(OUT_REF_DIR, f"{name}_720x1008.png")
        
        cv2.imwrite(out_360, c360)
        cv2.imwrite(out_720, c720)
        
        if (idx + 1) % 10 == 0 or idx == len(CARD_NAMES) - 1:
            print(f"  Processed {idx + 1}/54: {name} (borders={borders})")
            
    # Assemble 9x6 master sheet
    print(f"\nAssembling 9x6 master sheet ({MASTER_PNG})...")
    cols, rows = 9, 6
    target_w, target_h = 360, 504
    sheet_w, sheet_h = cols * target_w, rows * target_h
    master = Image.new("RGBA", (sheet_w, sheet_h), (20, 22, 26, 255))
    
    for idx, name in enumerate(CARD_NAMES):
        cp = os.path.join(OUT_CARDS_DIR, f"{name}.png")
        im = Image.open(cp).convert("RGBA")
        col = idx % cols
        row = idx // cols
        master.paste(im, (col * target_w, row * target_h))
        
    master.save(MASTER_PNG, format="PNG")
    print(f"Master 9x6 sheet saved: {MASTER_PNG} ({sheet_w}x{sheet_h}, {os.path.getsize(MASTER_PNG):,} bytes)")
    
    if os.path.exists(MASTER_BIN):
        os.remove(MASTER_BIN)
        print("Invalidated stale .bin cache.")
        
    package_png_deck(MASTER_PNG, MASTER_EATS, name="Baroque Relief", theme_id="baroque_relief")
    print(f"Packaged deck to {MASTER_EATS}")
    print("\n[SUCCESS] Baroque Relief deck cleanly reprocessed and assembled!")

if __name__ == "__main__":
    main()
