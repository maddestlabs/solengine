#!/usr/bin/env python3
"""
Convert a 6x2 court-card reference sheet image into a crisp, straight-line-only
geometric low-poly Inkscape-editable SVG matching the exact template structure of
decks/classic_courts_editable.svg (1624x824, 6x2 cells of 252x388).
"""

import argparse
from pathlib import Path
from typing import List, Tuple

import cv2
import numpy as np

COURT_IDS = [
    "J_spades", "Q_spades", "K_spades", "J_hearts", "Q_hearts", "K_hearts",
    "J_clubs", "Q_clubs", "K_clubs", "J_diamonds", "Q_diamonds", "K_diamonds",
]


def detect_cards(img: np.ndarray) -> List[Tuple[int, int, int, int]]:
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    mask = (gray > 200).astype(np.uint8) * 255
    num_labels, _, stats, _ = cv2.connectedComponentsWithStats(mask)
    cards = []
    for i in range(1, num_labels):
        x, y, w, h, area = stats[i]
        if area > 10000:
            cards.append((int(x), int(y), int(w), int(h)))
    cards.sort(key=lambda c: (c[1] // 200, c[0]))
    if len(cards) != 12:
        raise RuntimeError(f"Expected 12 cards in sheet, found {len(cards)}")
    return cards


def vectorize_card_angular_lowpoly(
    card_bgr: np.ndarray,
    k_colors: int = 11,
) -> Tuple[List[str], int, int]:
    """
    Quantize the top half of a card into clean flat colors and simplify each
    region into straight-line-only polygons (zero curves) over a dark slate
    backing silhouette for crisp stained-glass / geometric low-poly facets.
    """
    h, w = card_bgr.shape[:2]
    half_h = h // 2 + 5
    top = card_bgr[:half_h, :].copy()

    # Clean dark rounded-corner pixels in top corners & 2px side borders
    corner = 14
    for cx0, cx1 in [(0, corner), (w - corner, w)]:
        patch = top[0:corner, cx0:cx1]
        patch[np.max(patch, axis=2) < 160] = [255, 255, 255]
    top[:, :2] = [255, 255, 255]
    top[:, -2:] = [255, 255, 255]

    # Foreground silhouette mask (flood-fill outer white background)
    is_white = np.all(top > 222, axis=2).astype(np.uint8)
    _, labels = cv2.connectedComponents(is_white)
    border = set(labels[0, :]) | set(labels[:, 0]) | set(labels[:, -1])
    border.discard(0)
    fg = (~np.isin(labels, list(border))).astype(np.uint8)
    fg = cv2.morphologyEx(fg, cv2.MORPH_CLOSE, np.ones((3, 3), np.uint8))

    # Edge-preserving smooth + K-means color quantization on foreground
    smooth = cv2.bilateralFilter(top, 7, 36, 36)
    fg_idx = fg > 0
    pixels = smooth[fg_idx].reshape(-1, 3).astype(np.float32)
    criteria = (cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_MAX_ITER, 25, 1.0)
    _, labels_k, centers = cv2.kmeans(pixels, k_colors, None, criteria, 3, cv2.KMEANS_PP_CENTERS)

    # 1. Dark slate backing silhouette so adjacent facets never show white gaps
    bg_paths: List[str] = []
    sil_cnts, _ = cv2.findContours(fg, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    for cnt in sil_cnts:
        if cv2.contourArea(cnt) < 30:
            continue
        approx = cv2.approxPolyDP(cnt, 2.2, True)
        if len(approx) < 3:
            continue
        pts_str = " L".join(f"{float(p[0][0]):.1f},{float(p[0][1]):.1f}" for p in approx)
        bg_paths.append(f'<path d="M{pts_str} Z" style="fill:#1e222a;fill-rule:nonzero;" />')

    # 2. Straight-line polygon facets per color (finer detail in head zone)
    label_img = np.full((half_h, w), -1, dtype=np.int32)
    label_img[fg_idx] = labels_k.flatten()

    layer_polys: List[Tuple[float, str]] = []
    for ci in range(k_colors):
        mask = (label_img == ci).astype(np.uint8)
        mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, np.ones((2, 2), np.uint8))
        contours, _ = cv2.findContours(mask, cv2.RETR_CCOMP, cv2.CHAIN_APPROX_SIMPLE)
        b, g, r = int(centers[ci][0]), int(centers[ci][1]), int(centers[ci][2])
        col = f"#{r:02x}{g:02x}{b:02x}"
        for cnt in contours:
            area = cv2.contourArea(cnt)
            M = cv2.moments(cnt)
            cy = (M["m01"] / M["m00"]) if M["m00"] > 0 else half_h
            in_head = cy < half_h * 0.48
            min_a = 3.5 if in_head else 15.0
            eps = 1.3 if in_head else 2.5
            if area < min_a:
                continue
            approx = cv2.approxPolyDP(cnt, eps, True)
            if len(approx) < 3:
                continue
            pts_str = " L".join(f"{float(p[0][0]):.1f},{float(p[0][1]):.1f}" for p in approx)
            d = f"M{pts_str} Z"
            layer_polys.append(
                (area, f'<path d="{d}" style="fill:{col};stroke:{col};stroke-width:0.7;fill-rule:nonzero;" />')
            )

    layer_polys.sort(key=lambda x: -x[0])
    return bg_paths + [p for _, p in layer_polys], w, h


def build_editable_sheet(src_img: Path, out_svg: Path, k_colors: int = 11) -> None:
    img = cv2.imread(str(src_img))
    if img is None:
        raise FileNotFoundError(f"Could not read {src_img}")

    cards = detect_cards(img)
    cw, ch = 252.0, 388.0
    pad = 16.0
    total_w = 6 * cw + 7 * pad  # 1624
    total_h = 2 * ch + 3 * pad  # 824

    groups: List[str] = []
    guides: List[str] = []

    for idx, (card_id, (x, y, w, h)) in enumerate(zip(COURT_IDS, cards)):
        col = idx % 6
        row = idx // 6
        x0 = pad + col * (cw + pad)
        y0 = pad + row * (ch + pad)

        card_bgr = img[y : y + h, x : x + w]
        path_elems_list, src_w, src_h = vectorize_card_angular_lowpoly(card_bgr, k_colors=k_colors)

        scale = (ch * 0.5 - 2.0) / (src_h * 0.5 - 1.0)
        tx = (cw - src_w * scale) * 0.5
        ty = 2.0

        path_elems = "\n".join(f"          {p}" for p in path_elems_list)

        groups.append(
            f'    <g transform="translate({x0:.1f},{y0:.1f})" id="cell_{card_id}">\n'
            f'      <rect x="0" y="0" width="{cw:.0f}" height="{ch:.0f}" fill="#ffffff" />\n'
            f'      <g id="{card_id}" clip-path="url(#cell-clip)">\n'
            f'        <g id="{card_id}_bottom" transform="translate({cw:.0f},{ch:.0f}) rotate(180) translate({tx:.3f},{ty:.3f}) scale({scale:.6f})">\n'
            f'{path_elems}\n'
            f'        </g>\n'
            f'        <g id="{card_id}_top" transform="translate({tx:.3f},{ty:.3f}) scale({scale:.6f})">\n'
            f'{path_elems}\n'
            f'        </g>\n'
            f'      </g>\n'
            f'    </g>'
        )
        guides.append(
            f'    <rect id="guide_{card_id}" x="{x0:.1f}" y="{y0:.1f}" width="{cw:.0f}" height="{ch:.0f}" '
            f'fill="none" stroke="#999999" stroke-width="1" />'
        )
        print(f"Vectorized {card_id:10s}: {len(path_elems_list):3d} straight-line polygons")

    svg_text = (
        f'<?xml version="1.0" encoding="UTF-8" standalone="no"?>\n'
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{total_w:.0f}" height="{total_h:.0f}" '
        f'viewBox="0 0 {total_w:.0f} {total_h:.0f}">\n'
        f'  <defs>\n'
        f'    <clipPath id="cell-clip"><rect x="0" y="0" width="{cw:.0f}" height="{ch:.0f}" /></clipPath>\n'
        f'  </defs>\n'
        f'  <rect x="0" y="0" width="{total_w:.0f}" height="{total_h:.0f}" fill="#e6e6e6" />\n'
        f'  <g id="court_cards">\n'
        + "\n".join(groups)
        + "\n  </g>\n"
        f'  <g id="guides">\n'
        + "\n".join(guides)
        + "\n  </g>\n"
        f'</svg>\n'
    )
    out_svg.write_text(svg_text, encoding="utf-8")
    print(f"Wrote {out_svg} ({len(svg_text):,d} B)")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", type=Path, required=True)
    ap.add_argument("--out", type=Path, default=Path("decks/classic_courts_lowpoly_editable.svg"))
    ap.add_argument("--k-colors", type=int, default=11)
    args = ap.parse_args()
    build_editable_sheet(args.src, args.out, k_colors=args.k_colors)


if __name__ == "__main__":
    main()
