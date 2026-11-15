#!/usr/bin/env python3
"""
Build the full 54-card (9x6, including 2 Jokers) Another World-style low-poly vector deck
and export SVG, EVECZ, and Eatscript (.eats) + PNG preview.
"""

import argparse
import base64
import sys
import zlib
from pathlib import Path
from typing import Dict, List, Tuple

import cv2
import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from svg_to_evec import svg_to_evec_bytes  # noqa: E402

# Locked Another World-inspired flat palette (R, G, B)
PALETTE_RGB = np.array([
    [0x3C, 0x37, 0x48],  # 0: slate-purple window background (skipped in portrait trace)
    [0x1A, 0x17, 0x33],  # 1: deep navy (hair / dark cloak)
    [0x2B, 0x27, 0x42],  # 2: mid indigo
    [0x8A, 0x18, 0x24],  # 3: dark crimson (suits / shadow)
    [0xC8, 0x20, 0x2A],  # 4: bright crimson (highlights)
    [0xE5, 0xC9, 0x47],  # 5: gold bright (crown)
    [0xC2, 0x9C, 0x2E],  # 6: gold shadow
    [0xEB, 0xC0, 0xB8],  # 7: skin light
    [0xC2, 0x87, 0x87],  # 8: skin shadow
    [0xEC, 0xEB, 0xE2],  # 9: off-white (collar / highlights)
    [0x0E, 0x0D, 0x14],  # 10: near-black
    [0x9C, 0xA2, 0xB4],  # 11: steel / silver
    [0x68, 0x6D, 0x80],  # 12: steel shadow
], dtype=np.uint8)


def rgb_hex(rgb: np.ndarray) -> str:
    return f"#{int(rgb[0]):02x}{int(rgb[1]):02x}{int(rgb[2]):02x}"


def vectorize_portrait(
    img_path: Path,
    out_w: float,
    out_h: float,
    chamfer: float = 26.0,
    eps_px: float = 2.4,
    min_area: float = 36.0,
    black_suit: bool = False,
) -> Tuple[List[str], int]:
    """
    Quantize a raster portrait to PALETTE_RGB, mask to the chamfered window,
    extract contours per color, and simplify into flat-shaded SVG <path> elements.
    """
    im = Image.open(img_path).convert("RGB")
    arr = np.array(im, dtype=np.uint8)
    arr = cv2.medianBlur(arr, 3)

    h, w, _ = arr.shape
    # Detect near-cream/parchment studio background (#F4F2E6) so it drops out to the #3C3748 slate window
    bg_dist = np.sum((arr.astype(np.int32) - np.array([0xF4, 0xF2, 0xE6])) ** 2, axis=2)
    is_cream_bg = bg_dist < (38 ** 2)

    flat = arr.reshape(-1, 3).astype(np.int32)
    pal = PALETTE_RGB.astype(np.int32)
    dists = np.sum((flat[:, None, :] - pal[None, :, :]) ** 2, axis=2)
    labels = np.argmin(dists, axis=1).reshape(h, w)
    labels[is_cream_bg] = 0

    # Mask outside the chamfered window so no polygon pokes past the corners
    ch_x = chamfer * (w / out_w)
    ch_y = chamfer * (h / out_h)
    yy, xx = np.mgrid[0:h, 0:w]
    in_chamfer = (
        ((xx / ch_x) + (yy / ch_y) >= 1.0) &
        (((w - 1 - xx) / ch_x) + ((h - 1 - yy) / ch_y) >= 1.0)
    )
    labels[~in_chamfer] = 0

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
        mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, np.ones((3, 3), np.uint8))

        contours, hierarchy = cv2.findContours(mask, cv2.RETR_CCOMP, cv2.CHAIN_APPROX_SIMPLE)
        if not contours or hierarchy is None:
            continue

        d_parts: List[str] = []
        for cnt in contours:
            if cv2.contourArea(cnt) < min_area:
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
                col = "#52576E"  # cool slate shadow
            elif black_suit and idx == 4:
                col = "#7E85A1"  # cool steel midtone
            else:
                col = rgb_hex(PALETTE_RGB[idx])
            paths_svg.append(f'<path d="{" ".join(d_parts)}" fill="{col}" fill-rule="evenodd"/>')

    return paths_svg, poly_count


def suit_svg(suit: str, cx: float, cy: float, size: float, angle: float = 0.0) -> str:
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
    elif suit == "club":
        c1, c2 = "#1A1733", "#2E2952"
        left = [
            (0, -0.98), (-0.42, -0.72), (-0.36, -0.25), (-0.86, -0.15),
            (-0.96, 0.25), (-0.58, 0.52), (-0.14, 0.35), (-0.30, 0.95), (0, 0.95)
        ]
        right = [
            (0, -0.98), (0, 0.95), (0.30, 0.95), (0.14, 0.35),
            (0.58, 0.52), (0.96, 0.25), (0.86, -0.15), (0.36, -0.25), (0.42, -0.72)
        ]
    else:
        return ""

    def fmt(pts):
        return " ".join(f"{px * s:.1f},{py * s:.1f}" for px, py in pts)

    inner = f'<polygon points="{fmt(left)}" fill="{c1}"/><polygon points="{fmt(right)}" fill="{c2}"/>'
    if angle != 0.0:
        return f'<g transform="translate({cx:.1f},{cy:.1f}) rotate({angle:.0f})">{inner}</g>'
    return f'<g transform="translate({cx:.1f},{cy:.1f})">{inner}</g>'


def joker_star_svg(cx: float, cy: float, size: float, red: bool) -> str:
    """Symmetric 4-point faceted geometric star for Joker corner indices."""
    s = size * 0.5
    c1, c2 = ("#9E1B24", "#C8202A") if red else ("#1A1733", "#3E445A")
    left = [(0, -1.0), (-0.28, -0.28), (-1.0, 0), (-0.28, 0.28), (0, 1.0)]
    right = [(0, -1.0), (0.28, -0.28), (1.0, 0), (0.28, 0.28), (0, 1.0)]

    def fmt(pts):
        return " ".join(f"{cx + px * s:.1f},{cy + py * s:.1f}" for px, py in pts)

    return f'<polygon points="{fmt(left)}" fill="{c1}"/><polygon points="{fmt(right)}" fill="{c2}"/>'


def rank_path_svg(rank: str, cx: float, cy: float, h: float, col: str) -> str:
    """Angular, chamfered low-poly <path> glyphs on a 16x24 grid."""
    glyphs = {
        "A": "M3,24 L0,24 L0,10 L4,0 L12,0 L16,10 L16,24 L13,24 L13,15 L3,15 Z M3,12 L13,12 L13,9 L10.5,3 L5.5,3 L3,9 Z",
        "2": "M0,3 L3,0 L13,0 L16,3 L16,10 L13,13 L3,19 L3,21 L16,21 L16,24 L0,24 L0,17 L13,10 L13,3 L3,3 L3,7 L0,7 Z",
        "3": "M0,3 L3,0 L13,0 L16,3 L16,10 L13,12 L16,14 L16,21 L13,24 L3,24 L0,21 L0,18 L3,18 L3,21 L13,21 L13,14 L6,14 L6,10 L13,10 L13,3 L3,3 L3,6 L0,6 Z",
        "4": "M10,0 L14,0 L14,15 L16,15 L16,18 L14,18 L14,24 L11,24 L11,18 L0,18 L0,14 L10,0 Z M11,15 L11,4 L3,15 Z",
        "5": "M0,0 L16,0 L16,3 L3,3 L3,10 L13,10 L16,13 L16,21 L13,24 L3,24 L0,21 L0,18 L3,18 L3,21 L13,21 L13,13 L0,13 Z",
        "6": "M3,0 L13,0 L16,3 L16,6 L13,6 L13,3 L3,3 L3,10 L13,10 L16,13 L16,21 L13,24 L3,24 L0,21 L0,3 Z M3,13 L3,21 L13,21 L13,13 Z",
        "7": "M0,0 L16,0 L16,4 L7,24 L3,24 L12,3 L0,3 Z",
        "8": "M3,0 L13,0 L16,3 L16,10 L13,12 L16,14 L16,21 L13,24 L3,24 L0,21 L0,14 L3,12 L0,10 L0,3 Z M3,3 L3,10 L13,10 L13,3 Z M3,14 L3,21 L13,21 L13,14 Z",
        "9": "M3,0 L13,0 L16,3 L16,21 L13,24 L3,24 L0,21 L0,18 L3,18 L3,21 L13,21 L13,14 L3,14 L0,11 L0,3 Z M3,3 L3,11 L13,11 L13,3 Z",
        "10": "M1,0 L4,0 L4,24 L1,24 Z M7,0 L13,0 L15,2 L15,22 L13,24 L7,24 L5,22 L5,2 Z M8,3 L8,21 L12,21 L12,3 Z",
        "J": "M12,0 L16,0 L16,21 L13,24 L3,24 L0,21 L0,16 L3,16 L3,21 L12,21 Z",
        "Q": "M3,0 L13,0 L16,3 L16,20 L14,22 L17,25 L14,26 L11,23 L3,23 L0,20 L0,3 Z M3,3 L3,20 L13,20 L13,3 Z",
        "K": "M0,0 L3,0 L3,10 L12,0 L16,0 L7,11 L16,24 L12,24 L4,13 L3,14 L3,24 L0,24 Z",
        "O": "M3,0 L13,0 L16,3 L16,21 L13,24 L3,24 L0,21 L0,3 Z M3,3 L3,21 L13,21 L13,3 Z",
        "E": "M0,0 L16,0 L16,3 L3,3 L3,10 L13,10 L13,13 L3,13 L3,21 L16,21 L16,24 L0,24 Z",
        "R": "M0,0 L13,0 L16,3 L16,10 L13,13 L9,13 L16,24 L12,24 L5,13 L3,13 L3,24 L0,24 Z M3,3 L3,10 L13,10 L13,3 Z",
    }
    d = glyphs.get(rank)
    if not d:
        return ""
    sc = h / 24.0
    tx = cx - 8.0 * sc
    ty = cy - 12.0 * sc
    return f'<g transform="translate({tx:.1f},{ty:.1f}) scale({sc:.4f})"><path d="{d}" fill="{col}" fill-rule="evenodd"/></g>'


def card_frame_svg(x: float, y: float, w: float, h: float, rank: str, suit: str, inner_svg: str,
                   is_court_or_joker: bool = False) -> str:
    if suit == "joker_red":
        col = "#9E1B24"
    elif suit == "joker_black":
        col = "#1A1733"
    else:
        col = "#9E1B24" if suit in ("heart", "diamond") else "#1A1733"

    wx, wy, ww, wh = 68.0, 56.0, w - 136.0, h - 112.0
    ch = 26.0
    win_pts = (
        f"{wx + ch},{wy} {wx + ww},{wy} {wx + ww},{wy + wh - ch} "
        f"{wx + ww - ch},{wy + wh} {wx},{wy + wh} {wx},{wy + ch}"
    )
    win_fill = "#3C3748" if is_court_or_joker else None

    if suit.startswith("joker"):
        red = (suit == "joker_red")
        idx_items = [joker_star_svg(35.0, 36.0, 26.0, red)]
        for k, ch_let in enumerate("JOKER"):
            idx_items.append(rank_path_svg(ch_let, 35.0, 68.0 + k * 24.0, 18.0, col))
        corner_svg = "\n".join(idx_items)
    else:
        corner_svg = (
            rank_path_svg(rank, 35.0, 44.0, 34.0, col) + "\n" +
            suit_svg(suit, 35.0, 82.0, 28.0)
        )

    parts = [
        f'<g transform="translate({x:.1f},{y:.1f})">',
        f'<rect x="0" y="0" width="{w}" height="{h}" fill="#F4F2E6"/>',
    ]
    if win_fill is not None:
        parts.append(f'<polygon points="{win_pts}" fill="{win_fill}"/>')
    parts.extend([
        f'<g>{inner_svg}</g>',
        corner_svg,
        f'<g transform="translate({w},{h}) rotate(180)">',
        corner_svg,
        '</g>',
        '</g>',
    ])
    return "\n".join(parts)


def pip_card_inner(rank: str, suit: str, w: float, h: float) -> str:
    # Standard playing-card pip coordinates (nx, ny, scale, angle)
    layouts = {
        "A": [(0.50, 0.50, 1.85, 0)],
        "2": [(0.50, 0.24, 1.00, 0), (0.50, 0.76, 1.00, 180)],
        "3": [(0.50, 0.23, 0.98, 0), (0.50, 0.50, 0.98, 0), (0.50, 0.77, 0.98, 180)],
        "4": [(0.34, 0.24, 0.95, 0), (0.66, 0.24, 0.95, 0), (0.34, 0.76, 0.95, 180), (0.66, 0.76, 0.95, 180)],
        "5": [(0.34, 0.24, 0.95, 0), (0.66, 0.24, 0.95, 0), (0.50, 0.50, 0.95, 0), (0.34, 0.76, 0.95, 180), (0.66, 0.76, 0.95, 180)],
        "6": [(0.34, 0.23, 0.92, 0), (0.66, 0.23, 0.92, 0), (0.34, 0.50, 0.92, 0), (0.66, 0.50, 0.92, 0), (0.34, 0.77, 0.92, 180), (0.66, 0.77, 0.92, 180)],
        "7": [(0.34, 0.23, 0.90, 0), (0.66, 0.23, 0.90, 0), (0.50, 0.365, 0.90, 0), (0.34, 0.50, 0.90, 0), (0.66, 0.50, 0.90, 0), (0.34, 0.77, 0.90, 180), (0.66, 0.77, 0.90, 180)],
        "8": [(0.34, 0.23, 0.88, 0), (0.66, 0.23, 0.88, 0), (0.50, 0.365, 0.88, 0), (0.34, 0.50, 0.88, 0), (0.66, 0.50, 0.88, 0), (0.50, 0.635, 0.88, 180), (0.34, 0.77, 0.88, 180), (0.66, 0.77, 0.88, 180)],
        "9": [(0.34, 0.21, 0.84, 0), (0.66, 0.21, 0.84, 0), (0.34, 0.39, 0.84, 0), (0.66, 0.39, 0.84, 0), (0.50, 0.50, 0.84, 0), (0.34, 0.61, 0.84, 180), (0.66, 0.61, 0.84, 180), (0.34, 0.79, 0.84, 180), (0.66, 0.79, 0.84, 180)],
        "10": [(0.34, 0.21, 0.82, 0), (0.66, 0.21, 0.82, 0), (0.50, 0.30, 0.82, 0), (0.34, 0.39, 0.82, 0), (0.66, 0.39, 0.82, 0), (0.34, 0.61, 0.82, 180), (0.66, 0.61, 0.82, 180), (0.50, 0.70, 0.82, 180), (0.34, 0.79, 0.82, 180), (0.66, 0.79, 0.82, 180)],
    }
    pips = layouts[rank]
    return "\n".join(suit_svg(suit, w * nx, h * ny, 54.0 * sc, angle) for nx, ny, sc, angle in pips)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--artifacts-dir", required=True, help="Directory with generated court & joker portraits")
    ap.add_argument("--out-svg", required=True, help="Output 9x6 SVG sheet path")
    ap.add_argument("--out-eats", required=True, help="Output Eatscript .eats deck path")
    args = ap.parse_args()

    art = Path(args.artifacts_dir)
    portraits: Dict[Tuple[str, str], Tuple[Path, bool]] = {
        ("J", "spade"): (art / "jack_spades_portrait_1794694075436.jpg", True),
        ("Q", "spade"): (art / "queen_spades_portrait_1794694078869.jpg", True),
        ("K", "spade"): (art / "king_spades_portrait_v2_1794696058768.jpg", True),
        ("J", "heart"): (art / "jack_hearts_portrait_1794694080178.jpg", False),
        ("Q", "heart"): (art / "queen_hearts_portrait_v2_1794695977221.jpg", False),
        ("K", "heart"): (art / "king_hearts_portrait_1794694077418.jpg", False),
        ("J", "club"): (art / "jack_clubs_portrait_1794694088607.jpg", True),
        ("Q", "club"): (art / "queen_clubs_portrait_1794694091417.jpg", True),
        ("K", "club"): (art / "king_clubs_portrait_1794694094056.jpg", True),
        ("J", "diamond"): (art / "jack_diamonds_portrait_v2_1794696120287.jpg", False),
        ("Q", "diamond"): (art / "queen_diamonds_portrait_1794694114073.jpg", False),
        ("K", "diamond"): (art / "king_diamonds_portrait_1794694097598.jpg", False),
        ("JOKER", "joker_black"): (art / "joker_black_portrait_1794694105716.jpg", True),
        ("JOKER", "joker_red"): (art / "joker_red_portrait_1794694105826.jpg", False),
    }

    card_w, card_h = 360.0, 504.0
    cols, rows = 9, 6
    sheet_w = int(cols * card_w)
    sheet_h = int(rows * card_h)

    ranks = ["A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"]
    suits = ["spade", "heart", "club", "diamond"]

    cells: List[Tuple[str, str, str]] = []
    total_polys = 0
    for suit in suits:
        for rank in ranks:
            if (rank, suit) in portraits:
                p_path, is_black = portraits[(rank, suit)]
                paths, polys = vectorize_portrait(
                    p_path, out_w=card_w - 136.0, out_h=card_h - 112.0, black_suit=is_black
                )
                total_polys += polys
                inner = f'<g transform="translate(68,56)">{"".join(paths)}</g>'
                print(f"Vectorized {rank} {suit}: {len(paths)} layers, {polys} polygons")
            else:
                inner = pip_card_inner(rank, suit, card_w, card_h)
            cells.append((rank, suit, inner))

    for j_suit in ("joker_black", "joker_red"):
        p_path, is_black = portraits[("JOKER", j_suit)]
        paths, polys = vectorize_portrait(
            p_path, out_w=card_w - 136.0, out_h=card_h - 112.0, black_suit=is_black
        )
        total_polys += polys
        inner = f'<g transform="translate(68,56)">{"".join(paths)}</g>'
        print(f"Vectorized {j_suit}: {len(paths)} layers, {polys} polygons")
        cells.append(("JOKER", j_suit, inner))

    body = []
    for idx, (rank, suit, inner) in enumerate(cells):
        c = idx % cols
        r = idx // cols
        is_portrait = (rank in ("J", "Q", "K", "JOKER"))
        body.append(card_frame_svg(c * card_w, r * card_h, card_w, card_h, rank, suit, inner, is_portrait))

    svg = (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{sheet_w}" height="{sheet_h}" '
        f'viewBox="0 0 {sheet_w} {sheet_h}">\n' + "\n".join(body) + "\n</svg>\n"
    )
    out_svg = Path(args.out_svg)
    out_svg.write_text(svg, encoding="utf-8")

    evec_raw = svg_to_evec_bytes(svg)
    evecz = zlib.compress(evec_raw, level=9)
    b64 = base64.b64encode(evecz).decode("ascii")

    eats = f'''# SolEngine Deck Recipe — Another World Low-Poly

sol.set_theme({{
    id: "another_world",
    name: "Another World Low-Poly",
    author: "SolEngine",
    description: "Flat-shaded 1991 Another World-style pure binary vector deck (54 cards incl. 2 Jokers).",
    corner_radius: 0.0,
    version: 1
}})

format = "evecz"
deck_blob = "{b64}"
'''
    Path(args.out_eats).write_text(eats, encoding="utf-8")
    print(f"Full 54-card deck: total portrait polys={total_polys}")
    print(f"SVG={len(svg)} B -> EVEC={len(evec_raw)} B -> EVECZ={len(evecz)} B -> .eats={len(eats)} B")


if __name__ == "__main__":
    main()
