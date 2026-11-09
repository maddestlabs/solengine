#!/usr/bin/env python3
"""
tools/hone_baroque_noir.py
==========================
Surgically refines and hones the Baroque Noir card deck:
1. Aligns and centers all cards to match the 2 of Clubs baseline (cx=180.0, cy=252.0).
2. Seamlessly inpaints outer margins with the authentic gray-ish slate cardstock texture,
   eliminating all black letterbox bars.
3. Enhances radiant ruby bloom on Hearts and Diamonds (anchored to Ace of Hearts),
   with gentle bloom tightening on Jack and King of Hearts.
4. Enhances obsidian black contrast on Spades and Clubs pips (anchored to Aces of Spades & Clubs),
   making suits instantly legible on mobile screens without altering character face art.
5. Reassembles the 9x6 master sheet decks/baroque_noir.png and packages decks/baroque_noir.eats.
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

BACKUP_DIR = "assets/deck_cards/baroque_noir_original_backup"
SRC_DIR = "assets/deck_cards/baroque_noir/png"
CLASSIC_DIR = "assets/deck_cards/classic/png"
MASTER_PNG = "decks/baroque_noir.png"
MASTER_EATS = "decks/baroque_noir.eats"
MASTER_BIN = "decks/baroque_noir.bin"

TARGET_W = 360
TARGET_H = 504
TARGET_CX = 180.0
TARGET_CY = 252.0

# 2 of Clubs baseline frame dimensions
REF_FRAME_W = 344.0
REF_FRAME_H = 496.0

def detect_frame_bbox(img):
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    h, w = gray.shape
    border_zone = np.zeros((h, w), dtype=bool)
    margin_x = 60
    margin_y = 60
    border_zone[:margin_y, :] = True
    border_zone[-margin_y:, :] = True
    border_zone[:, :margin_x] = True
    border_zone[:, -margin_x:] = True
    
    # Silver relief frame has bright pixels > 75 in the perimeter zone
    bright = (gray > 75) & border_zone
    ys, xs = np.where(bright)
    if len(xs) == 0:
        return 0, 0, w, h
    return int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())

def inpaint_card_margins(img, margin_width=20, dark_threshold=36):
    """
    Seamlessly inpaints the dark/black margin lines at the borders of the card
    with the surrounding gray-ish slate cardstock texture and matching paper tooth.
    """
    h, w = img.shape[:2]
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    mask = np.zeros((h, w), dtype=np.uint8)
    
    # Outer border region where margins occur
    outer = np.zeros((h, w), dtype=bool)
    outer[:margin_y_thresh(img), :] = True
    outer[-margin_y_thresh(img):, :] = True
    outer[:, :margin_width] = True
    outer[:, -margin_width:] = True
    
    # Identify dark margin pixels
    dark_in_outer = outer & (gray < dark_threshold)
    mask[dark_in_outer] = 255
    
    # Dilate 1px to blend cleanly into the edge
    kernel = np.ones((3, 3), np.uint8)
    mask = cv2.dilate(mask, kernel, iterations=1)
    
    if np.sum(mask) == 0:
        return img
    
    # Inpaint using Telea
    inpainted = cv2.inpaint(img, mask, inpaintRadius=5, flags=cv2.INPAINT_TELEA)
    
    # Add subtle Gaussian noise to match the cardstock's paper grain (std dev ~3.2)
    noise = np.random.normal(0, 3.2, inpainted.shape).astype(np.float32)
    mask_3d = (mask[:, :, np.newaxis] > 0).astype(np.float32)
    result = np.clip(inpainted.astype(np.float32) + noise * mask_3d, 0, 255).astype(np.uint8)
    return result

def margin_y_thresh(img):
    return 16

def align_and_scale_card(img, name):
    """
    Aligns and normalizes the card scale and center to match the 2 of Clubs baseline.
    """
    h, w = img.shape[:2]
    x1, y1, x2, y2 = detect_frame_bbox(img)
    fw = float(x2 - x1)
    fh = float(y2 - y1)
    cx = float(x1 + x2) / 2.0
    cy = float(y1 + y2) / 2.0
    
    if name == "clubs_02":
        # 2 of Clubs is our ground truth baseline! Don't scale or warp, just inpaint margins.
        return inpaint_card_margins(img)
    
    # Compute scale factor relative to 2 of Clubs
    scale_x = REF_FRAME_W / fw
    scale_y = REF_FRAME_H / fh
    
    # Clamp scale factor to avoid excessive warping on irregular bounds
    scale_x = np.clip(scale_x, 0.94, 1.08)
    scale_y = np.clip(scale_y, 0.94, 1.08)
    
    # Target center is exactly (180.0, 252.0)
    tx = TARGET_CX - scale_x * cx
    ty = TARGET_CY - scale_y * cy
    
    M = np.float32([[scale_x, 0, tx], [0, scale_y, ty]])
    
    # Fill background with slate cardstock average color: (55, 54, 53) in BGR
    bg_color = (55, 54, 53)
    warped = cv2.warpAffine(img, M, (w, h), flags=cv2.INTER_LANCZOS4,
                            borderMode=cv2.BORDER_CONSTANT, borderValue=bg_color)
    
    # Inpaint any revealed margins
    return inpaint_card_margins(warped)

def get_pip_mask(name):
    """
    Retrieves the dilated pip mask from the classic template reference card.
    """
    classic_path = os.path.join(CLASSIC_DIR, f"{name}.png")
    if not os.path.exists(classic_path):
        return None
    classic = cv2.imread(classic_path)
    if classic is None:
        return None
    gray = cv2.cvtColor(classic, cv2.COLOR_BGR2GRAY)
    pips = gray < 150
    # Dilate by 21px to encompass the full baroque relief pip and corner pips
    kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (21, 21))
    pip_zone = cv2.dilate(pips.astype(np.uint8), kernel) > 0
    return pip_zone

def enhance_black_pips(img, name):
    """
    Remaps Spades & Clubs pips to obsidian black (#111111), matching the Aces
    so suit symbols are instantly legible against the background and silver filigree.
    """
    if name in ("spades_01_ace", "clubs_01_ace"):
        # Aces are our benchmark references! Preserve 100% of their artwork.
        return img
    
    is_court = any(c in name for c in ("_jack", "_queen", "_king"))
    
    # Tone curve to darken mid-tone darks to obsidian black while preserving silver highlights
    lut = np.arange(256, dtype=np.float32)
    for i in range(256):
        if i < 75:
            lut[i] = 75.0 * ((i / 75.0) ** 1.85)
        else:
            lut[i] = i
    lut = np.clip(lut, 0, 255).astype(np.uint8)
    
    darkened = cv2.LUT(img, lut)
    
    h, w = img.shape[:2]
    pip_zone = get_pip_mask(name)
    
    if is_court or pip_zone is None:
        # On court cards, only darken corner indices and corner pips so face art is untouched!
        mask = np.zeros((h, w), dtype=np.float32)
        # Top-left corner index zone
        mask[:105, :90] = 1.0
        # Bottom-right corner index zone
        mask[-105:, -90:] = 1.0
    else:
        # Number cards: darken all pips and corner indices
        mask = pip_zone.astype(np.float32)
    
    # Soften mask edges with Gaussian blur to prevent seam artifacts
    mask_soft = cv2.GaussianBlur(mask, (21, 21), 0)[:, :, np.newaxis]
    result = (img.astype(np.float32) * (1.0 - mask_soft) + darkened.astype(np.float32) * mask_soft).astype(np.uint8)
    return result

def enhance_red_bloom(img, name):
    """
    Calibrates the radiant ruby glow/bloom on Hearts and Diamonds, anchored to Ace of Hearts.
    Tightens Jack & King of Hearts slightly as requested.
    """
    if name == "hearts_01_ace":
        # Benchmark reference! Keep unchanged.
        return img
    
    b = img[:,:,0].astype(np.float32)
    g = img[:,:,1].astype(np.float32)
    r = img[:,:,2].astype(np.float32)
    
    red_excess = np.maximum(0.0, r - np.maximum(g, b))
    
    if name in ("hearts_11_jack", "hearts_13_king"):
        # Gentle ~15% bloom tightening on wide component for crisp face/armor linework
        bloom_tight = cv2.GaussianBlur(red_excess, (0, 0), sigmaX=3.0)
        bloom_wide = cv2.GaussianBlur(red_excess, (0, 0), sigmaX=10.0)
        # Subtract a small fraction of the wide bloom
        r_mod = np.clip(r - bloom_wide * 0.15 + bloom_tight * 0.05, 0, 255)
        return np.stack([b, g, r_mod], axis=2).astype(np.uint8)
    
    # For all other Hearts and Diamonds (number cards and Diamond court cards):
    # Apply calibrated dual-radius radiant ruby screen-bloom
    bloom_small = cv2.GaussianBlur(red_excess, (0, 0), sigmaX=3.5)
    bloom_large = cv2.GaussianBlur(red_excess, (0, 0), sigmaX=9.0)
    bloom_combined = (bloom_small * 0.70 + bloom_large * 0.50)
    
    # Add bloom in red channel with subtle rose warmth
    r_out = np.clip(r + bloom_combined * 0.85, 0, 255)
    g_out = np.clip(g + bloom_combined * 0.07, 0, 255)
    b_out = np.clip(b + bloom_combined * 0.10, 0, 255)
    
    return np.stack([b_out, g_out, r_out], axis=2).astype(np.uint8)

def hone_single_card(name):
    src_file = os.path.join(BACKUP_DIR, f"{name}.png")
    if not os.path.exists(src_file):
        print(f"Warning: {src_file} not found!")
        return None
    
    # Read with alpha if available
    im_pil = Image.open(src_file)
    has_alpha = (im_pil.mode == "RGBA")
    
    img = cv2.imread(src_file, cv2.IMREAD_COLOR)
    
    # 1. Alignment & Centering + Full-Bleed Slate Texture Inpainting
    aligned = align_and_scale_card(img, name)
    
    # 2. Color & Lighting Calibration
    if "hearts" in name or "diamonds" in name or name == "joker_red":
        honed = enhance_red_bloom(aligned, name)
    else:
        honed = enhance_black_pips(aligned, name)
    
    # Save back to PNG with alpha channel restored
    out_file = os.path.join(SRC_DIR, f"{name}.png")
    if has_alpha:
        alpha = np.array(im_pil.split()[-1])
        honed_rgba = cv2.cvtColor(honed, cv2.COLOR_BGR2RGBA)
        honed_rgba[:, :, 3] = alpha
        Image.fromarray(honed_rgba).save(out_file, format="PNG")
    else:
        cv2.imwrite(out_file, honed)
    
    return out_file

def main():
    print("=" * 60)
    print("Honing Baroque Noir: Alignment, Full-Bleed Slate & Color Tuning")
    print("=" * 60)
    
    os.makedirs(SRC_DIR, exist_ok=True)
    
    # Process all 54 cards
    for idx, name in enumerate(CARD_NAMES):
        out_file = hone_single_card(name)
        print(f"[{idx+1:02d}/54] Honed {name} -> {out_file}")
    
    print("\nAssembling Master 9x6 Bitmap Sheet (decks/baroque_noir.png)...")
    cols, rows = 9, 6
    sheet_w = cols * TARGET_W
    sheet_h = rows * TARGET_H
    master_sheet = Image.new("RGBA", (sheet_w, sheet_h), (55, 54, 53, 255))
    
    for idx, name in enumerate(CARD_NAMES):
        card_p = os.path.join(SRC_DIR, f"{name}.png")
        card_im = Image.open(card_p).convert("RGBA")
        col = idx % cols
        row = idx // cols
        master_sheet.paste(card_im, (col * TARGET_W, row * TARGET_H))
    
    master_sheet.save(MASTER_PNG, format="PNG")
    print(f"Saved master sheet: {MASTER_PNG} ({sheet_w}x{sheet_h}, {os.path.getsize(MASTER_PNG):,} bytes)")
    
    print("\nPackaging Eatscript container (decks/baroque_noir.eats)...")
    package_png_deck(MASTER_PNG, MASTER_EATS, name="Baroque Noir", theme_id="baroque_noir")
    
    # Invalidate stale binary cache so SolEngine will re-bake clean GPU textures
    if os.path.exists(MASTER_BIN):
        os.remove(MASTER_BIN)
        print(f"Removed stale cache: {MASTER_BIN}")
    
    print("\nAll 54 cards honed, master sheet assembled, and package compiled successfully!")

if __name__ == "__main__":
    main()

