#!/usr/bin/env python3
"""
Prototype low-poly Another World-style card deck generator & portrait vectorizer.
"""

import argparse
import sys
from pathlib import Path
from typing import List, Tuple

import cv2
import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from svg_to_evec import svg_to_evec_bytes  # noqa: E402
import zlib

# Locked Another World-inspired flat palette (R, G, B)
PALETTE_RGB = np.array([
    [0x3C, 0x37, 0x48],  # 0: slate-purple window background
    [0x1A, 0x17, 0x33],  # 1: deep navy (hair / dark cloak)
    [0x2B, 0x27, 0x42],  # 2: mid indigo
    [0x8A, 0x18, 0x24],  # 3: dark crimson (suits / shadow)
    [0xC8, 0x20, 0x2A],  # 4: bright crimson (rose / highlights)
    [0xE5, 0xC9, 0x47],  # 5: gold bright (crown)
    [0xC2, 0x9C, 0x2E],  # 6: gold shadow
    [0xEB, 0xC0, 0xB8],  # 7: skin light
    [0xC2, 0x87, 0x87],  # 8: skin shadow
    [0xEC, 0xEB, 0xE2],  # 9: off-white (collar / highlights)
    [0x0E, 0x0D, 0x14],  # 10: near-black
    [0x9C, 0xA2, 0xB4],  # 11: steel / silver (sword)
    [0x68, 0x6D, 0x80],  # 12: steel shadow
], dtype=np.uint8)


def rgb_hex(rgb: np.ndarray) -> str:
    return f"#{int(rgb[0]):02x}{int(rgb[1]):02x}{int(rgb[2]):02x}"


def vectorize_portrait(img_path: Path, crop_norm: Tuple[float, float, float, float],
                       out_w: float, out_h: float, eps_px: float = 2.5,
                       min_area: float = 40.0, black_suit: bool = False) -> Tuple[List[str], int]:
    """
    Quantize a raster portrait to PALETTE_RGB, extract contours per color,
    and simplify with approxPolyDP into crisp flat-shaded SVG <path> elements.
    """
    im = Image.open(img_path).convert("RGB")
    w_full, h_full = im.size
    x0 = int(crop_norm[0] * w_full)
    y0 = int(crop_norm[1] * h_full)
    x1 = int(crop_norm[2] * w_full)
    y1 = int(crop_norm[3] * h_full)
    im = im.crop((x0, y0, x1, y1))

    arr = np.array(im, dtype=np.uint8)
    # Remove JPEG compression speckles while keeping sharp polygon corners
    arr = cv2.medianBlur(arr, 3)

    h, w, _ = arr.shape
    flat = arr.reshape(-1, 3).astype(np.int32)
    pal = PALETTE_RGB.astype(np.int32)
    dists = np.sum((flat[:, None, :] - pal[None, :, :]) ** 2, axis=2)
    labels = np.argmin(dists, axis=1).reshape(h, w)

    # Layer order: largest pixel count first (painter's order), skipping index 0 (window bg)
    counts = [(int(np.sum(labels == idx)), idx) for idx in range(1, len(PALETTE_RGB))]
    counts.sort(reverse=True)

    sx = out_w / float(w)
    sy = out_h / float(h)
    paths_svg: List[str] = []
    poly_count = 0

    for pix_count, idx in counts:
        if pix_count < min_area:
            continue
        mask = (labels == idx).astype(np.uint8) * 255
        # Close tiny 1px pinholes from quantization
        mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, np.ones((3, 3), np.uint8))

        contours, hierarchy = cv2.findContours(mask, cv2.RETR_CCOMP, cv2.CHAIN_APPROX_SIMPLE)
        if not contours or hierarchy is None:
            continue

        d_parts: List[str] = []
        for cnt in contours:
            area = cv2.contourArea(cnt)
            if area < min_area:
                continue
            approx = cv2.approxPolyDP(cnt, eps_px, True)
            if len(approx) < 3:
                continue
            pts = approx.reshape(-1, 2)
            cmds = [f"M{pts[0][0] * sx:.1f},{pts[0][1] * sy:.1f}"]
            for px, py in pts[1:]:
                cmds.append(f"L{px * sx:.1f},{py * sy:.1f}")
            cmds.append("Z")
            d_parts.append("".join(cmds))
            poly_count += 1

        if d_parts:
            if black_suit and idx == 3:
                col = "#52576E"  # cool slate shadow replacing dark crimson
            elif black_suit and idx == 4:
                col = "#7E85A1"  # cool steel midtone replacing bright crimson
            else:
                col = rgb_hex(PALETTE_RGB[idx])
            paths_svg.append(f'<path d="{" ".join(d_parts)}" fill="{col}" fill-rule="evenodd"/>')

    return paths_svg, poly_count


def suit_svg(suit: str, cx: float, cy: float, size: float) -> str:
    """Return symmetric low-poly 2-tone faceted SVG polygons for a suit symbol."""
    s = size * 0.5
    if suit == "heart":
        c1, c2 = "#9E1B24", "#C8202A"
        left = [(0, 0.92), (-0.95, -0.05), (-0.85, -0.62), (-0.42, -0.88), (0, -0.45)]
        right = [(0, 0.92), (0, -0.45), (0.42, -0.88), (0.85, -0.62), (0.95, -0.05)]
    elif suit == "diamond":
        c1, c2 = "#9E1B24", "#C8202A"
        left = [(0, -1.0), (-0.76, 0.0), (0, 1.0)]
        right = [(0, -1.0), (0.76, 0.0), (0, 1.0)]
    elif suit == "spade":
        c1, c2 = "#1A1733", "#2E2952"
        left = [(0, -1.0), (-0.92, 0.05), (-0.75, 0.48), (-0.18, 0.35), (-0.34, 0.95), (0, 0.95)]
        right = [(0, -1.0), (0, 0.95), (0.34, 0.95), (0.18, 0.35), (0.75, 0.48), (0.92, 0.05)]
    else:  # club
        c1, c2 = "#1A1733", "#2E2952"
        left = [
            (0, -0.98), (-0.42, -0.72), (-0.36, -0.25), (-0.86, -0.15),
            (-0.96, 0.25), (-0.58, 0.52), (-0.14, 0.35), (-0.30, 0.95), (0, 0.95)
        ]
        right = [
            (0, -0.98), (0, 0.95), (0.30, 0.95), (0.14, 0.35),
            (0.58, 0.52), (0.96, 0.25), (0.86, -0.15), (0.36, -0.25), (0.42, -0.72)
        ]

    def fmt(pts):
        return " ".join(f"{cx + px * s:.1f},{cy + py * s:.1f}" for px, py in pts)

    return f'<polygon points="{fmt(left)}" fill="{c1}"/><polygon points="{fmt(right)}" fill="{c2}"/>'


def rank_path_svg(rank: str, cx: float, cy: float, h: float, col: str) -> str:
    """Return angular, chamfered low-poly <path> glyphs for card ranks (0..10 normalized)."""
    # Defined on a 16x24 grid, centered at (cx, cy), scaled to height h
    glyphs = {
        "A": "M3,24 L0,24 L0,10 L4,0 L12,0 L16,10 L16,24 L13,24 L13,15 L3,15 Z M3,12 L13,12 L13,9 L10.5,3 L5.5,3 L3,9 Z",
        "3": "M0,3 L3,0 L13,0 L16,3 L16,10 L13,12 L16,14 L16,21 L13,24 L3,24 L0,21 L0,18 L3,18 L3,21 L13,21 L13,14 L6,14 L6,10 L13,10 L13,3 L3,3 L3,6 L0,6 Z",
        "5": "M0,0 L16,0 L16,3 L3,3 L3,10 L13,10 L16,13 L16,21 L13,24 L3,24 L0,21 L0,18 L3,18 L3,21 L13,21 L13,13 L0,13 Z",
        "7": "M0,0 L16,0 L16,4 L7,24 L3,24 L12,3 L0,3 Z",
        "J": "M12,0 L16,0 L16,21 L13,24 L3,24 L0,21 L0,16 L3,16 L3,21 L12,21 Z",
        "Q": "M3,0 L13,0 L16,3 L16,20 L14,22 L17,25 L14,26 L11,23 L3,23 L0,20 L0,3 Z M3,3 L3,20 L13,20 L13,3 Z",
        "K": "M0,0 L3,0 L3,10 L12,0 L16,0 L7,11 L16,24 L12,24 L4,13 L3,14 L3,24 L0,24 Z",
    }
    d = glyphs.get(rank)
    if not d:
        return ""
    sc = h / 24.0
    tx = cx - 8.0 * sc
    ty = cy - 12.0 * sc
    return f'<g transform="translate({tx:.1f},{ty:.1f}) scale({sc:.4f})"><path d="{d}" fill="{col}" fill-rule="evenodd"/></g>'


def card_frame_svg(x: float, y: float, w: float, h: float, rank: str, suit: str,
                   inner_svg: str) -> str:
    col = "#9E1B24" if suit in ("heart", "diamond") else "#1A1733"
    # Chamfered inner window matching the Queen of Hearts sample
    wx, wy, ww, wh = 68.0, 56.0, w - 136.0, h - 112.0
    ch = 26.0
    win_pts = (
        f"{wx + ch},{wy} {wx + ww},{wy} {wx + ww},{wy + wh - ch} "
        f"{wx + ww - ch},{wy + wh} {wx},{wy + wh} {wx},{wy + ch}"
    )

    parts = [
        f'<g transform="translate({x:.1f},{y:.1f})">',
        f'<rect x="0" y="0" width="{w}" height="{h}" fill="#F4F2E6"/>',
        f'<polygon points="{win_pts}" fill="#3C3748"/>',
        f'<g>{inner_svg}</g>',
        # Top-left index
        rank_path_svg(rank, 35.0, 44.0, 34.0, col),
        suit_svg(suit, 35.0, 82.0, 28.0),
        # Bottom-right index (rotated 180)
        f'<g transform="translate({w},{h}) rotate(180)">',
        rank_path_svg(rank, 35.0, 44.0, 34.0, col),
        suit_svg(suit, 35.0, 82.0, 28.0),
        '</g>',
        '</g>',
    ]
    return "\n".join(parts)


def pip_card_inner(rank: str, suit: str, w: float, h: float) -> str:
    layouts = {
        "A": [(0.5, 0.5, 1.85)],
        "3": [(0.5, 0.24, 1.0), (0.5, 0.5, 1.0), (0.5, 0.76, 1.0)],
        "5": [(0.34, 0.25, 0.95), (0.66, 0.25, 0.95), (0.5, 0.5, 0.95), (0.34, 0.75, 0.95), (0.66, 0.75, 0.95)],
        "7": [(0.34, 0.23, 0.9), (0.66, 0.23, 0.9), (0.5, 0.36, 0.9), (0.34, 0.5, 0.9), (0.66, 0.5, 0.9), (0.34, 0.77, 0.9), (0.66, 0.77, 0.9)],
    }
    pips = layouts.get(rank, [(0.5, 0.5, 1.2)])
    out = []
    for nx, ny, sc in pips:
        out.append(suit_svg(suit, w * nx, h * ny, 56.0 * sc))
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--queen", required=True, help="Path to Queen of Hearts portrait image")
    ap.add_argument("--king", help="Optional path to King of Spades portrait image")
    ap.add_argument("--jack", help="Optional path to Jack of Diamonds portrait image")
    ap.add_argument("--out-svg", required=True, help="Output 6-card prototype SVG")
    args = ap.parse_args()

    card_w, card_h = 360.0, 504.0
    gap = 28.0
    cols, rows = 3, 2
    sheet_w = int(cols * card_w + (cols + 1) * gap)
    sheet_h = int(rows * card_h + (rows + 1) * gap)

    q_paths, q_polys = vectorize_portrait(
        Path(args.queen), crop_norm=(0.0, 0.0, 1.0, 1.0),
        out_w=card_w - 136.0, out_h=card_h - 112.0, eps_px=2.5, min_area=40.0
    )
    q_inner = f'<g transform="translate(68,56)">{"".join(q_paths)}</g>'
    print(f"Queen of Hearts vectorized: {len(q_paths)} color layers, {q_polys} polygons")

    cards = [
        ("A", "spade", pip_card_inner("A", "spade", card_w, card_h)),
        ("7", "heart", pip_card_inner("7", "heart", card_w, card_h)),
        ("5", "diamond", pip_card_inner("5", "diamond", card_w, card_h)),
    ]

    if args.jack and Path(args.jack).exists():
        j_paths, j_polys = vectorize_portrait(
            Path(args.jack), crop_norm=(0.0, 0.0, 1.0, 1.0),
            out_w=card_w - 136.0, out_h=card_h - 112.0, eps_px=2.5, min_area=40.0
        )
        j_inner = f'<g transform="translate(68,56)">{"".join(j_paths)}</g>'
        print(f"Jack of Diamonds vectorized: {len(j_paths)} color layers, {j_polys} polygons")
        cards.append(("J", "diamond", j_inner))
    else:
        cards.append(("3", "club", pip_card_inner("3", "club", card_w, card_h)))

    cards.append(("Q", "heart", q_inner))

    if args.king and Path(args.king).exists():
        k_paths, k_polys = vectorize_portrait(
            Path(args.king), crop_norm=(0.0, 0.0, 1.0, 1.0),
            out_w=card_w - 136.0, out_h=card_h - 112.0, eps_px=2.5, min_area=40.0, black_suit=True
        )
        k_inner = f'<g transform="translate(68,56)">{"".join(k_paths)}</g>'
        print(f"King of Spades vectorized: {len(k_paths)} color layers, {k_polys} polygons")
        cards.append(("K", "spade", k_inner))
    else:
        cards.append(("K", "spade", pip_card_inner("A", "spade", card_w, card_h)))

    body = [f'<rect x="0" y="0" width="{sheet_w}" height="{sheet_h}" fill="#221F2B"/>']
    for idx, (rank, suit, inner) in enumerate(cards):
        c = idx % cols
        r = idx // cols
        cx = gap + c * (card_w + gap)
        cy = gap + r * (card_h + gap)
        body.append(card_frame_svg(cx, cy, card_w, card_h, rank, suit, inner))

    svg = (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{sheet_w}" height="{sheet_h}" '
        f'viewBox="0 0 {sheet_w} {sheet_h}">\n' + "\n".join(body) + "\n</svg>\n"
    )
    out_svg_path = Path(args.out_svg)
    out_svg_path.write_text(svg, encoding="utf-8")

    evec_raw = svg_to_evec_bytes(svg)
    evecz = zlib.compress(evec_raw, level=9)
    print(f"Prototype 6-card sheet: SVG={len(svg)} B -> EVEC={len(evec_raw)} B -> EVECZ={len(evecz)} B")


if __name__ == "__main__":
    main()
