#!/usr/bin/env python3
"""
tools/generate_clockwork_courts.py

Generates a minimalist, blazing-fast vector Clockwork / Steampunk court card sheet
matching the exact 6x2 grid (1624x824, 252x388 cells) and group IDs of
decks/classic_courts_editable.svg.

Style: Low-poly geometric facets + crisp clockwork line art.
Outputs:
  - decks/clockwork_courts_editable.svg
  - decks/clockwork_courts_preview.png
"""

import argparse
import math
import zlib
from pathlib import Path
from typing import Dict, List, Tuple

import cv2
import numpy as np

from svg_to_evec import svg_to_evec_bytes

CELL_W = 252.0
CELL_H = 388.0
MARGIN = 16.0
GAP = 16.0
SHEET_W = int(MARGIN * 2 + CELL_W * 6 + GAP * 5)  # 1624
SHEET_H = int(MARGIN * 2 + CELL_H * 2 + GAP)      # 824

SUITS = ["spades", "hearts", "clubs", "diamonds"]
RANKS = ["J", "Q", "K"]

# Grid placement matching classic_courts_editable.svg
# Row 0: J_spades, Q_spades, K_spades, J_hearts, Q_hearts, K_hearts
# Row 1: J_clubs, Q_clubs, K_clubs, J_diamonds, Q_diamonds, K_diamonds
GRID_ORDER = [
    ("J", "spades", 0, 0),
    ("Q", "spades", 1, 0),
    ("K", "spades", 2, 0),
    ("J", "hearts", 3, 0),
    ("Q", "hearts", 4, 0),
    ("K", "hearts", 5, 0),
    ("J", "clubs", 0, 1),
    ("Q", "clubs", 1, 1),
    ("K", "clubs", 2, 1),
    ("J", "diamonds", 3, 1),
    ("Q", "diamonds", 4, 1),
    ("K", "diamonds", 5, 1),
]

PALETTES: Dict[str, Dict[str, str]] = {
    "spades": {
        "dark": "#18181b",
        "primary": "#27272a",
        "mid": "#3f3f46",
        "accent": "#d49b35",
        "accent_hi": "#f3c969",
        "accent_lo": "#9a6b1a",
        "ink": "#09090b",
        "skin_hi": "#f4dccb",
        "skin_lo": "#dfb8a0",
        "ivory": "#f5efe6",
    },
    "hearts": {
        "dark": "#450a0a",
        "primary": "#7f1d1d",
        "mid": "#991b1b",
        "accent": "#d98a36",
        "accent_hi": "#f4c062",
        "accent_lo": "#9c521b",
        "ink": "#240606",
        "skin_hi": "#f6dfd0",
        "skin_lo": "#e2bba3",
        "ivory": "#f7f0e8",
    },
    "clubs": {
        "dark": "#52525b",
        "primary": "#71717a",
        "mid": "#a1a1aa",
        "accent": "#c88d32",
        "accent_hi": "#eab854",
        "accent_lo": "#8c5c19",
        "ink": "#18181b",
        "skin_hi": "#f2dac9",
        "skin_lo": "#ddb59d",
        "ivory": "#f3efe6",
    },
    "diamonds": {
        "dark": "#6b1229",
        "primary": "#c81e3a",
        "mid": "#e84558",
        "accent": "#d99126",
        "accent_hi": "#fbcf58",
        "accent_lo": "#9a5d12",
        "ink": "#291016",
        "skin_hi": "#f6dfd0",
        "skin_lo": "#e2bba3",
        "ivory": "#f8f1e7",
    },
}


# ---------------------------------------------------------------------------
# Minimalist geometry helpers (also tracked for direct PNG preview rasterization)
# ---------------------------------------------------------------------------

class ShapeList:
    """Collects crisp SVG primitives for one half (or full) card."""

    def __init__(self) -> None:
        self.items: List[dict] = []

    def poly(
        self,
        pts: List[Tuple[float, float]],
        fill: str = "none",
        stroke: str = "none",
        sw: float = 1.5,
    ) -> None:
        self.items.append({
            "kind": "polygon",
            "pts": pts,
            "fill": fill,
            "stroke": stroke,
            "sw": sw,
        })

    def line(
        self,
        x1: float,
        y1: float,
        x2: float,
        y2: float,
        stroke: str,
        sw: float = 1.5,
    ) -> None:
        self.items.append({
            "kind": "line",
            "x1": x1,
            "y1": y1,
            "x2": x2,
            "y2": y2,
            "stroke": stroke,
            "sw": sw,
        })

    def circle(
        self,
        cx: float,
        cy: float,
        r: float,
        fill: str = "none",
        stroke: str = "none",
        sw: float = 1.5,
    ) -> None:
        self.items.append({
            "kind": "circle",
            "cx": cx,
            "cy": cy,
            "r": r,
            "fill": fill,
            "stroke": stroke,
            "sw": sw,
        })

    def rect(
        self,
        x: float,
        y: float,
        w: float,
        h: float,
        fill: str = "none",
        stroke: str = "none",
        sw: float = 1.5,
    ) -> None:
        self.items.append({
            "kind": "rect",
            "x": x,
            "y": y,
            "w": w,
            "h": h,
            "fill": fill,
            "stroke": stroke,
            "sw": sw,
        })


def gear_points(
    cx: float,
    cy: float,
    r_outer: float,
    r_inner: float,
    teeth: int = 8,
    phase_deg: float = 0.0,
) -> List[Tuple[float, float]]:
    """Generate a clean, low-vertex cogwheel polygon."""
    pts: List[Tuple[float, float]] = []
    step = 360.0 / teeth
    for i in range(teeth):
        a0 = math.radians(phase_deg + i * step)
        a1 = math.radians(phase_deg + i * step + step * 0.22)
        a2 = math.radians(phase_deg + i * step + step * 0.50)
        a3 = math.radians(phase_deg + i * step + step * 0.72)
        pts.append((cx + r_inner * math.cos(a0), cy + r_inner * math.sin(a0)))
        pts.append((cx + r_outer * math.cos(a1), cy + r_outer * math.sin(a1)))
        pts.append((cx + r_outer * math.cos(a2), cy + r_outer * math.sin(a2)))
        pts.append((cx + r_inner * math.cos(a3), cy + r_inner * math.sin(a3)))
    return pts


def draw_frame_and_hub(s: ShapeList, p: Dict[str, str]) -> None:
    """Central horizon belt (CardCanvas draws the card frame & corner pips)."""
    s.line(24, 194, 228, 194, p["ink"], 2.0)
    s.line(34, 194, 218, 194, p["accent"], 1.0)


def draw_center_escapement(s: ShapeList, p: Dict[str, str]) -> None:
    """Central interlocking clockwork medallion at (126, 194)."""
    s.poly(gear_points(126, 194, 15, 11, teeth=8, phase_deg=22.5), fill=p["accent"])
    s.circle(126, 194, 7.5, fill=p["dark"], stroke=p["accent_hi"], sw=1.2)
    s.poly([(126, 188), (131, 194), (126, 200), (121, 194)], fill=p["ivory"])


def build_jack_half(s: ShapeList, p: Dict[str, str]) -> None:
    """Jack: Clockwork Tinker / Automaton Page (top half, y <= 194)."""
    # Faceted torso & cloak (stroke-free flat shading)
    s.poly([(46, 194), (56, 134), (98, 112), (112, 194)], fill=p["primary"])
    s.poly([(98, 112), (154, 112), (138, 194), (112, 194)], fill=p["dark"])
    s.poly([(154, 112), (194, 132), (204, 194), (138, 194)], fill=p["mid"])

    # Diagonal brass bandolier / sash
    s.poly([(72, 124), (92, 114), (176, 194), (148, 194)], fill=p["accent"])
    s.poly([(92, 114), (104, 116), (176, 194), (162, 194)], fill=p["accent_hi"])
    s.circle(116, 146, 3.2, fill=p["dark"], stroke=p["ivory"], sw=1.0)
    s.circle(136, 166, 3.2, fill=p["dark"], stroke=p["ivory"], sw=1.0)

    # Collar
    s.poly([(96, 108), (126, 98), (156, 108), (126, 124)], fill=p["ivory"])

    # Neck & faceted 3/4 face
    s.poly([(114, 92), (138, 92), (134, 110), (118, 110)], fill=p["skin_lo"])
    s.poly([(106, 52), (130, 52), (126, 96), (108, 86)], fill=p["skin_hi"])
    s.poly([(130, 52), (148, 58), (144, 86), (126, 96)], fill=p["skin_lo"])

    # Tinker cap & visor
    s.poly([(98, 54), (110, 30), (148, 34), (152, 54)], fill=p["dark"])
    s.poly([(96, 54), (154, 50), (150, 58), (100, 60)], fill=p["accent"])

    # Signature Brass Monocle / Goggle
    s.circle(134, 68, 8.5, fill=p["ivory"], stroke=p["accent_hi"], sw=2.0)
    s.circle(134, 68, 4.5, fill=p["primary"], stroke=p["ink"], sw=1.0)
    s.line(126, 68, 142, 68, p["ink"], 0.9)
    s.line(134, 60, 134, 76, p["ink"], 0.9)

    # Clockwork Halberd / Spanner-Staff on right, extending to halfway point (y=194)
    s.line(202, 50, 202, 194, p["accent_lo"], 3.5)
    s.line(202, 50, 202, 194, p["accent_hi"], 1.4)
    # Halberd crescent + gear head
    s.poly([(202, 34), (218, 52), (202, 74), (190, 56)], fill=p["mid"])
    s.circle(202, 54, 5.0, fill=p["accent_hi"], stroke=p["ink"], sw=1.2)


def build_queen_half(s: ShapeList, p: Dict[str, str]) -> None:
    """Queen: Horologist Empress — smaller, slimmer frame (~1/8 shorter)."""
    # Hourglass faceted gown & cape (stroke-free flat shading)
    s.poly([(62, 194), (74, 140), (106, 126), (114, 194)], fill=p["primary"])
    s.poly([(190, 194), (178, 140), (146, 126), (138, 194)], fill=p["primary"])
    s.poly([(106, 126), (126, 126), (126, 194), (114, 194)], fill=p["dark"])
    s.poly([(126, 126), (146, 126), (138, 194), (126, 194)], fill=p["mid"])

    # Pleated brass & ivory gear-ruff collar
    s.poly([(92, 126), (106, 112), (146, 112), (160, 126), (126, 140)], fill=p["ivory"])
    s.line(109, 114, 119, 134, p["accent_lo"], 1.1)
    s.line(126, 113, 126, 138, p["accent_lo"], 1.1)
    s.line(143, 114, 133, 134, p["accent_lo"], 1.1)

    # Chest escapement brooch
    s.poly([(126, 148), (136, 162), (126, 176), (116, 162)], fill=p["accent_hi"])
    s.circle(126, 162, 3.8, fill=p["primary"], stroke=p["ink"], sw=1.0)

    # Hair / veil plates
    s.poly([(98, 114), (105, 70), (117, 70), (110, 114)], fill=p["dark"])
    s.poly([(154, 114), (147, 70), (135, 70), (142, 114)], fill=p["dark"])

    # Symmetrical faceted face (smaller & lower)
    s.poly([(109, 70), (126, 70), (126, 110), (112, 99)], fill=p["skin_hi"])
    s.poly([(143, 70), (126, 70), (126, 110), (140, 99)], fill=p["skin_lo"])

    # 3-point Clockwork Diadem Crown
    s.poly([(102, 70), (99, 51), (114, 61), (126, 44), (138, 61), (153, 51), (150, 70)], fill=p["accent_hi"])
    s.poly([(104, 70), (148, 70), (145, 63), (107, 63)], fill=p["accent"])
    s.circle(126, 58, 3.5, fill=p["primary"], stroke=p["ink"], sw=1.0)

    # Clockwork Rose / Balance Wheel held on right, stem extending to y=194
    s.line(182, 114, 162, 194, p["accent_hi"], 2.2)
    s.poly(gear_points(185, 104, 14, 9.5, teeth=8), fill=p["primary"])
    s.circle(185, 104, 5.5, fill=p["accent_hi"], stroke=p["ink"], sw=1.1)


def build_king_half(s: ShapeList, p: Dict[str, str]) -> None:
    """King: Grand Mechanist Monarch (top half, y <= 194)."""
    # Broad Armored Pauldrons & Royal Coat (stroke-free flat shading)
    s.poly([(40, 194), (48, 122), (92, 108), (106, 194)], fill=p["primary"])
    s.poly([(212, 194), (204, 122), (160, 108), (146, 194)], fill=p["mid"])
    s.poly([(92, 108), (160, 108), (146, 194), (106, 194)], fill=p["dark"])

    # Pauldron brass trim
    s.poly([(48, 122), (92, 108), (86, 126), (52, 138)], fill=p["accent"])
    s.poly([(204, 122), (160, 108), (166, 126), (200, 138)], fill=p["accent_hi"])

    # Chest Clock-Face Medallion
    s.circle(126, 152, 18, fill=p["ivory"], stroke=p["accent_hi"], sw=2.2)
    s.circle(126, 152, 14, fill="none", stroke=p["ink"], sw=1.1)
    s.line(126, 152, 126, 140, p["ink"], 1.8)
    s.line(126, 152, 135, 157, p["primary"], 1.8)
    s.circle(126, 152, 2.5, fill=p["accent_lo"], stroke=p["ink"], sw=1.0)

    # Faceplate & Sculpted Geometric Beard
    s.poly([(104, 52), (126, 52), (126, 86), (106, 82)], fill=p["skin_hi"])
    s.poly([(148, 52), (126, 52), (126, 86), (146, 82)], fill=p["skin_lo"])
    s.poly([(102, 78), (126, 84), (126, 114), (106, 102)], fill=p["dark"])
    s.poly([(150, 78), (126, 84), (126, 114), (146, 102)], fill=p["primary"])
    # Twin visor brow line
    s.line(108, 64, 122, 66, p["ink"], 2.0)
    s.line(144, 64, 130, 66, p["ink"], 2.0)

    # Heavy 5-Tooth Gear Crown
    s.poly([
        (94, 52), (92, 28), (104, 28), (108, 40),
        (118, 24), (134, 24), (144, 40), (148, 28),
        (160, 28), (158, 52)
    ], fill=p["accent_hi"])
    s.rect(95, 44, 62, 8, fill=p["accent"])
    s.poly([(126, 28), (132, 36), (126, 44), (120, 36)], fill=p["primary"])

    # Clockwork Broadsword on right (no hilt, blade extends cleanly to y=194)
    s.poly([(204, 30), (210, 44), (210, 194), (198, 194), (198, 44)], fill=p["ivory"], stroke=p["ink"], sw=1.2)
    s.line(204, 34, 204, 194, p["mid"], 1.3)


def shapes_to_svg_lines(shapes: ShapeList, indent: str = "      ") -> List[str]:
    out: List[str] = []
    for item in shapes.items:
        k = item["kind"]
        if k == "polygon":
            pts_str = " ".join(f"{x:.1f},{y:.1f}" for x, y in item["pts"])
            sw = f';stroke-width:{item["sw"]}' if item["stroke"] != "none" else ""
            out.append(f'{indent}<polygon points="{pts_str}" style="fill:{item["fill"]};stroke:{item["stroke"]}{sw};stroke-linejoin:round"/>')
        elif k == "line":
            out.append(
                f'{indent}<line x1="{item["x1"]:.1f}" y1="{item["y1"]:.1f}" '
                f'x2="{item["x2"]:.1f}" y2="{item["y2"]:.1f}" '
                f'style="stroke:{item["stroke"]};stroke-width:{item["sw"]};stroke-linecap:round"/>'
            )
        elif k == "circle":
            sw = f';stroke-width:{item["sw"]}' if item["stroke"] != "none" else ""
            out.append(
                f'{indent}<circle cx="{item["cx"]:.1f}" cy="{item["cy"]:.1f}" r="{item["r"]:.1f}" '
                f'style="fill:{item["fill"]};stroke:{item["stroke"]}{sw}"/>'
            )
        elif k == "rect":
            sw = f';stroke-width:{item["sw"]}' if item["stroke"] != "none" else ""
            out.append(
                f'{indent}<rect x="{item["x"]:.1f}" y="{item["y"]:.1f}" '
                f'width="{item["w"]:.1f}" height="{item["h"]:.1f}" '
                f'style="fill:{item["fill"]};stroke:{item["stroke"]}{sw}"/>'
            )
    return out


# ---------------------------------------------------------------------------
# Direct supersampled OpenCV rasterizer for PNG preview
# ---------------------------------------------------------------------------

def hex_to_bgr(h: str) -> Tuple[int, int, int]:
    h = h.lstrip("#")
    r, g, b = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
    return (b, g, r)


def rasterize_shapes(
    img: np.ndarray,
    shapes: ShapeList,
    ox: float,
    oy: float,
    scale: int,
    rotate180: bool = False,
) -> None:
    def tx(x: float, y: float) -> Tuple[int, int]:
        if rotate180:
            x = CELL_W - x
            y = CELL_H - y
        return (int(round((ox + x) * scale)), int(round((oy + y) * scale)))

    for item in shapes.items:
        k = item["kind"]
        if k == "polygon":
            pts = np.array([tx(x, y) for x, y in item["pts"]], dtype=np.int32)
            if item["fill"] != "none":
                cv2.fillPoly(img, [pts], hex_to_bgr(item["fill"]), lineType=cv2.LINE_AA)
            if item["stroke"] != "none":
                cv2.polylines(
                    img, [pts], isClosed=True, color=hex_to_bgr(item["stroke"]),
                    thickness=max(1, int(round(item["sw"] * scale))), lineType=cv2.LINE_AA,
                )
        elif k == "line":
            p1 = tx(item["x1"], item["y1"])
            p2 = tx(item["x2"], item["y2"])
            cv2.line(
                img, p1, p2, hex_to_bgr(item["stroke"]),
                thickness=max(1, int(round(item["sw"] * scale))), lineType=cv2.LINE_AA,
            )
        elif k == "circle":
            c = tx(item["cx"], item["cy"])
            r = max(1, int(round(item["r"] * scale)))
            if item["fill"] != "none":
                cv2.circle(img, c, r, hex_to_bgr(item["fill"]), thickness=-1, lineType=cv2.LINE_AA)
            if item["stroke"] != "none":
                cv2.circle(
                    img, c, r, hex_to_bgr(item["stroke"]),
                    thickness=max(1, int(round(item["sw"] * scale))), lineType=cv2.LINE_AA,
                )
        elif k == "rect":
            x, y, w, h = item["x"], item["y"], item["w"], item["h"]
            pts = np.array([tx(x, y), tx(x + w, y), tx(x + w, y + h), tx(x, y + h)], dtype=np.int32)
            if item["fill"] != "none":
                cv2.fillPoly(img, [pts], hex_to_bgr(item["fill"]), lineType=cv2.LINE_AA)
            if item["stroke"] != "none":
                cv2.polylines(
                    img, [pts], isClosed=True, color=hex_to_bgr(item["stroke"]),
                    thickness=max(1, int(round(item["sw"] * scale))), lineType=cv2.LINE_AA,
                )


def write_hpp(hpp_path: Path, card_svgs: List[Tuple[str, str]]) -> None:
    """Emit include/solengine/classic_minimal_courts.hpp with top-half EVECZ blobs."""
    lines: List[str] = [
        "#pragma once",
        "// Auto-generated by tools/generate_clockwork_courts.py",
        "// Minimalist Clockwork / Steampunk court cards (top-half EVECZ compressed).",
        "#include <cstddef>",
        "#include <cstdint>",
        '#include "solengine/solitaire_board.hpp"',
        "",
        "namespace solengine {",
        "namespace detail {",
        "",
    ]

    total_evec = 0
    total_evecz = 0
    for card_id, top_svg in card_svgs:
        evec = svg_to_evec_bytes(top_svg)
        evecz = zlib.compress(evec, 9)
        total_evec += len(evec)
        total_evecz += len(evecz)
        lines.append(f"static constexpr uint8_t kCourt_{card_id}[] = {{")
        for i in range(0, len(evecz), 16):
            chunk = ", ".join(f"0x{b:02x}" for b in evecz[i : i + 16])
            lines.append(f"    {chunk},")
        lines.append("};")
        lines.append("")

    lines.extend([
        "} // namespace detail",
        "",
        "inline const uint8_t* getClassicMinimalCourtEvecz(Suit suit, int rank, size_t& outSize) noexcept {",
        "    // rank: 11 = Jack, 12 = Queen, 13 = King",
        "    switch (suit) {",
        "        case Suit::Spades:",
        "            if (rank == 11) { outSize = sizeof(detail::kCourt_J_spades); return detail::kCourt_J_spades; }",
        "            if (rank == 12) { outSize = sizeof(detail::kCourt_Q_spades); return detail::kCourt_Q_spades; }",
        "            if (rank == 13) { outSize = sizeof(detail::kCourt_K_spades); return detail::kCourt_K_spades; }",
        "            break;",
        "        case Suit::Hearts:",
        "            if (rank == 11) { outSize = sizeof(detail::kCourt_J_hearts); return detail::kCourt_J_hearts; }",
        "            if (rank == 12) { outSize = sizeof(detail::kCourt_Q_hearts); return detail::kCourt_Q_hearts; }",
        "            if (rank == 13) { outSize = sizeof(detail::kCourt_K_hearts); return detail::kCourt_K_hearts; }",
        "            break;",
        "        case Suit::Clubs:",
        "            if (rank == 11) { outSize = sizeof(detail::kCourt_J_clubs); return detail::kCourt_J_clubs; }",
        "            if (rank == 12) { outSize = sizeof(detail::kCourt_Q_clubs); return detail::kCourt_Q_clubs; }",
        "            if (rank == 13) { outSize = sizeof(detail::kCourt_K_clubs); return detail::kCourt_K_clubs; }",
        "            break;",
        "        case Suit::Diamonds:",
        "            if (rank == 11) { outSize = sizeof(detail::kCourt_J_diamonds); return detail::kCourt_J_diamonds; }",
        "            if (rank == 12) { outSize = sizeof(detail::kCourt_Q_diamonds); return detail::kCourt_Q_diamonds; }",
        "            if (rank == 13) { outSize = sizeof(detail::kCourt_K_diamonds); return detail::kCourt_K_diamonds; }",
        "            break;",
        "    }",
        "    outSize = 0;",
        "    return nullptr;",
        "}",
        "",
        "} // namespace solengine",
        "",
    ])

    hpp_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"Wrote {hpp_path} ({hpp_path.stat().st_size:,} bytes, EVEC={total_evec:,} B, EVECZ={total_evecz:,} B)")


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate Clockwork court cards SVG, preview PNG, and optional C++ header.")
    parser.add_argument("--hpp", type=Path, default=None, help="Optional output C++ header path (e.g. include/solengine/classic_minimal_courts.hpp)")
    args = parser.parse_args()

    out_svg = Path("decks/clockwork_courts_editable.svg")
    out_png = Path("decks/clockwork_courts_preview.png")

    svg_lines: List[str] = [
        '<?xml version="1.0" encoding="UTF-8" standalone="no"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{SHEET_W}" height="{SHEET_H}" viewBox="0 0 {SHEET_W} {SHEET_H}">',
        '  <defs>',
        '    <clipPath id="cell-clip">',
        f'      <rect x="0" y="0" width="{int(CELL_W)}" height="{int(CELL_H)}"/>',
        '    </clipPath>',
        '  </defs>',
        f'  <rect x="0" y="0" width="{SHEET_W}" height="{SHEET_H}" fill="#e6e6e6"/>',
        '  <g id="court_cards">',
    ]

    scale = 2
    canvas = np.full((SHEET_H * scale, SHEET_W * scale, 3), hex_to_bgr("#e6e6e6"), dtype=np.uint8)

    total_primitives = 0
    top_card_svgs: List[Tuple[str, str]] = []
    for rank, suit, col, row in GRID_ORDER:
        ox = MARGIN + col * (CELL_W + GAP)
        oy = MARGIN + row * (CELL_H + GAP)
        p = PALETTES[suit]

        frame_shapes = ShapeList()
        draw_frame_and_hub(frame_shapes, p)

        half_shapes = ShapeList()
        if rank == "J":
            build_jack_half(half_shapes, p)
        elif rank == "Q":
            build_queen_half(half_shapes, p)
        else:
            build_king_half(half_shapes, p)

        center_shapes = ShapeList()
        draw_center_escapement(center_shapes, p)

        total_primitives += len(frame_shapes.items) + len(half_shapes.items) * 2 + len(center_shapes.items)

        frame_svg = shapes_to_svg_lines(frame_shapes, "        ")
        half_svg = shapes_to_svg_lines(half_shapes, "          ")
        center_svg = shapes_to_svg_lines(center_shapes, "        ")

        # Top-half-only standalone SVG for EVECZ (CardCanvas::drawCourt mirrors 180 deg in C++)
        top_svg = "\n".join([
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{int(CELL_W)}" height="{int(CELL_H)}" viewBox="0 0 {int(CELL_W)} {int(CELL_H)}">',
            *frame_svg,
            *half_svg,
            *center_svg,
            '</svg>',
        ])
        top_card_svgs.append((f"{rank}_{suit}", top_svg))

        # SVG sheet output
        svg_lines.append(f'    <g transform="translate({ox:.1f},{oy:.1f})">')
        svg_lines.append(f'      <rect x="0" y="0" width="{int(CELL_W)}" height="{int(CELL_H)}" fill="#ffffff"/>')
        svg_lines.append(f'      <g id="{rank}_{suit}" clip-path="url(#cell-clip)">')
        svg_lines.extend(frame_svg)
        svg_lines.append('        <g class="top_half">')
        svg_lines.extend(half_svg)
        svg_lines.append('        </g>')
        svg_lines.append('        <g class="bottom_half" transform="rotate(180 126 194)">')
        svg_lines.extend(half_svg)
        svg_lines.append('        </g>')
        svg_lines.extend(center_svg)
        svg_lines.append('      </g>')
        svg_lines.append('    </g>')

        # PNG rasterization
        x0, y0 = int(ox * scale), int(oy * scale)
        x1, y1 = int((ox + CELL_W) * scale), int((oy + CELL_H) * scale)
        cv2.rectangle(canvas, (x0, y0), (x1, y1), (255, 255, 255), thickness=-1)
        rasterize_shapes(canvas, frame_shapes, ox, oy, scale, rotate180=False)
        rasterize_shapes(canvas, half_shapes, ox, oy, scale, rotate180=False)
        rasterize_shapes(canvas, half_shapes, ox, oy, scale, rotate180=True)
        rasterize_shapes(canvas, center_shapes, ox, oy, scale, rotate180=False)

    svg_lines.append('  </g>')
    svg_lines.append('</svg>')

    out_svg.write_text("\n".join(svg_lines) + "\n", encoding="utf-8")
    preview = cv2.resize(canvas, (SHEET_W, SHEET_H), interpolation=cv2.INTER_AREA)
    cv2.imwrite(str(out_png), preview)
    print(f"Wrote {out_svg} ({out_svg.stat().st_size:,} bytes, {total_primitives} primitives total)")
    print(f"Wrote {out_png}")

    if args.hpp is not None:
        write_hpp(args.hpp, top_card_svgs)


if __name__ == "__main__":
    main()
