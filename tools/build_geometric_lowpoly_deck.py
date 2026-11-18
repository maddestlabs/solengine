#!/usr/bin/env python3
"""
tools/build_geometric_lowpoly_deck.py
Builds a crisp flat-shaded geometric low-poly vector deck (.eats using evecz)
in the style of modern minimalist faceted playing cards:
  - Crisp white full-bleed card plates
  - Clean geometric sans-serif corner indices + flat corner suit pips
  - 4-quadrant faceted geometric suit pips for A-10
  - Thin-framed symmetrical low-poly court cards (using meshes from decks/solengine_lowpoly_courts.svg)
  - 2 geometric Jokers (54 cards total, 9x6 master sheet -> evecz)

Usage:
  python tools/build_geometric_lowpoly_deck.py
"""

import argparse
import base64
import re
import sys
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path
from typing import Dict, List, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))
from svg_to_evec import svg_to_evec_bytes  # noqa: E402

SUITS = ["spades", "hearts", "clubs", "diamonds"]
RANKS = ["A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"]

RED_INDEX = "#c81e32"
BLACK_INDEX = "#222930"
FRAME_STROKE = "#2b3844"


def parse_triangle_pts(d: str) -> List[Tuple[float, float]]:
    nums = [float(x) for x in re.findall(r"[-+]?(?:\d*\.\d+|\d+)", d)]
    return [(nums[i], nums[i + 1]) for i in range(0, len(nums) - 1, 2)]


def clip_poly_y_max(pts: List[Tuple[float, float]], y_max: float) -> List[Tuple[float, float]]:
    """Sutherland-Hodgman clip of a polygon against y <= y_max."""
    if not pts:
        return []
    out: List[Tuple[float, float]] = []
    n = len(pts)
    for i in range(n):
        x1, y1 = pts[i]
        x2, y2 = pts[(i + 1) % n]
        in1 = y1 <= y_max
        in2 = y2 <= y_max
        if in1 and in2:
            out.append((x2, y2))
        elif in1 and not in2:
            t = (y_max - y1) / (y2 - y1) if abs(y2 - y1) > 1e-8 else 0.0
            out.append((x1 + t * (x2 - x1), y_max))
        elif not in1 and in2:
            t = (y_max - y1) / (y2 - y1) if abs(y2 - y1) > 1e-8 else 0.0
            out.append((x1 + t * (x2 - x1), y_max))
            out.append((x2, y2))
    return out


def extract_court_meshes(svg_path: Path) -> Dict[str, List[Tuple[List[Tuple[float, float]], str]]]:
    """Extract per-court polygon lists from decks/solengine_lowpoly_courts.svg."""
    tree = ET.parse(svg_path)
    root = tree.getroot()
    ns = "{http://www.w3.org/2000/svg}"
    courts: Dict[str, List[Tuple[List[Tuple[float, float]], str]]] = {}

    for g in root.iter(f"{ns}g"):
        gid = g.attrib.get("id", "")
        if not any(gid.startswith(p) for p in ("J_", "Q_", "K_")):
            continue
        polys: List[Tuple[List[Tuple[float, float]], str]] = []
        for path in g.findall(f"{ns}path"):
            d = path.attrib.get("d", "")
            style = path.attrib.get("style", "")
            m = re.search(r"fill:\s*([^;]+)", style)
            fill = m.group(1).strip() if m else path.attrib.get("fill", "#000000")
            pts = parse_triangle_pts(d)
            if len(pts) >= 3:
                polys.append((pts, fill))
        courts[gid] = polys
    return courts


def flat_suit_svg(suit: str, cx: float, cy: float, size: float, angle: float = 0.0) -> str:
    """Crisp flat geometric suit symbol for corner indices and court-frame corner pips."""
    s = size * 0.5
    if suit == "hearts":
        col = RED_INDEX
        d = (
            f"M 0,{0.95*s:.2f} "
            f"C {-0.45*s:.2f},{0.50*s:.2f} {-0.98*s:.2f},{0.08*s:.2f} {-0.98*s:.2f},{-0.38*s:.2f} "
            f"C {-0.98*s:.2f},{-0.76*s:.2f} {-0.68*s:.2f},{-0.96*s:.2f} {-0.38*s:.2f},{-0.96*s:.2f} "
            f"C {-0.18*s:.2f},{-0.96*s:.2f} {-0.05*s:.2f},{-0.82*s:.2f} 0,{-0.62*s:.2f} "
            f"C {0.05*s:.2f},{-0.82*s:.2f} {0.18*s:.2f},{-0.96*s:.2f} {0.38*s:.2f},{-0.96*s:.2f} "
            f"C {0.68*s:.2f},{-0.96*s:.2f} {0.98*s:.2f},{-0.76*s:.2f} {0.98*s:.2f},{-0.38*s:.2f} "
            f"C {0.98*s:.2f},{0.08*s:.2f} {0.45*s:.2f},{0.50*s:.2f} 0,{0.95*s:.2f} Z"
        )
    elif suit == "diamonds":
        col = RED_INDEX
        d = f"M 0,{-1.0*s:.2f} L {0.72*s:.2f},0 L 0,{1.0*s:.2f} L {-0.72*s:.2f},0 Z"
    elif suit == "spades":
        col = BLACK_INDEX
        d = (
            f"M 0,{-1.0*s:.2f} "
            f"C {0.45*s:.2f},{-0.52*s:.2f} {0.95*s:.2f},{-0.10*s:.2f} {0.95*s:.2f},{0.30*s:.2f} "
            f"C {0.95*s:.2f},{0.62*s:.2f} {0.68*s:.2f},{0.78*s:.2f} {0.42*s:.2f},{0.78*s:.2f} "
            f"C {0.25*s:.2f},{0.78*s:.2f} {0.14*s:.2f},{0.68*s:.2f} {0.09*s:.2f},{0.56*s:.2f} "
            f"L {0.26*s:.2f},{0.98*s:.2f} L {-0.26*s:.2f},{0.98*s:.2f} L {-0.09*s:.2f},{0.56*s:.2f} "
            f"C {-0.14*s:.2f},{0.68*s:.2f} {-0.25*s:.2f},{0.78*s:.2f} {-0.42*s:.2f},{0.78*s:.2f} "
            f"C {-0.68*s:.2f},{0.78*s:.2f} {-0.95*s:.2f},{0.62*s:.2f} {-0.95*s:.2f},{0.30*s:.2f} "
            f"C {-0.95*s:.2f},{-0.10*s:.2f} {-0.45*s:.2f},{-0.52*s:.2f} 0,{-1.0*s:.2f} Z"
        )
    elif suit == "clubs":
        col = BLACK_INDEX
        # Crisp 3-lobe clover + stem
        d = (
            f"M 0,{-0.96*s:.2f} "
            f"C {0.30*s:.2f},{-0.96*s:.2f} {0.48*s:.2f},{-0.76*s:.2f} {0.48*s:.2f},{-0.48*s:.2f} "
            f"C {0.48*s:.2f},{-0.32*s:.2f} {0.38*s:.2f},{-0.18*s:.2f} {0.24*s:.2f},{-0.10*s:.2f} "
            f"C {0.42*s:.2f},{-0.16*s:.2f} {0.62*s:.2f},{-0.12*s:.2f} {0.78*s:.2f},{0.02*s:.2f} "
            f"C {0.96*s:.2f},{0.18*s:.2f} {0.96*s:.2f},{0.48*s:.2f} {0.74*s:.2f},{0.64*s:.2f} "
            f"C {0.54*s:.2f},{0.78*s:.2f} {0.26*s:.2f},{0.72*s:.2f} {0.10*s:.2f},{0.52*s:.2f} "
            f"L {0.26*s:.2f},{0.98*s:.2f} L {-0.26*s:.2f},{0.98*s:.2f} L {-0.10*s:.2f},{0.52*s:.2f} "
            f"C {-0.26*s:.2f},{0.72*s:.2f} {-0.54*s:.2f},{0.78*s:.2f} {-0.74*s:.2f},{0.64*s:.2f} "
            f"C {-0.96*s:.2f},{0.48*s:.2f} {-0.96*s:.2f},{0.18*s:.2f} {-0.78*s:.2f},{0.02*s:.2f} "
            f"C {-0.62*s:.2f},{-0.12*s:.2f} {-0.42*s:.2f},{-0.16*s:.2f} {-0.24*s:.2f},{-0.10*s:.2f} "
            f"C {-0.38*s:.2f},{-0.18*s:.2f} {-0.48*s:.2f},{-0.32*s:.2f} {-0.48*s:.2f},{-0.48*s:.2f} "
            f"C {-0.48*s:.2f},{-0.76*s:.2f} {-0.30*s:.2f},{-0.96*s:.2f} 0,{-0.96*s:.2f} Z"
        )
    else:
        return ""

    tf = f"translate({cx:.1f},{cy:.1f})" + (f" rotate({angle:.0f})" if angle != 0.0 else "")
    return f'<g transform="{tf}"><path d="{d}" fill="{col}"/></g>'


def faceted_pip_svg(suit: str, cx: float, cy: float, size: float, angle: float = 0.0) -> str:
    """4-quadrant / low-poly faceted center suit pip matching the reference 10 of Diamonds."""
    s = size * 0.5
    parts: List[str] = []

    if suit == "diamonds":
        # Exact 4-quadrant faceted diamond from the reference image
        tl, tr, bl, br = "#e11d48", "#c81e32", "#be123c", "#9f1239"
        parts.append(f'<polygon points="0,{-1.0*s:.1f} {-0.74*s:.1f},0 0,0" fill="{tl}"/>')
        parts.append(f'<polygon points="0,{-1.0*s:.1f} {0.74*s:.1f},0 0,0" fill="{tr}"/>')
        parts.append(f'<polygon points="{-0.74*s:.1f},0 0,{1.0*s:.1f} 0,0" fill="{bl}"/>')
        parts.append(f'<polygon points="{0.74*s:.1f},0 0,{1.0*s:.1f} 0,0" fill="{br}"/>')
    elif suit == "hearts":
        tl, tr, bl, br = "#e11d48", "#c81e32", "#be123c", "#9f1239"
        # Faceted low-poly heart (4 clean geometric regions)
        parts.append(
            f'<polygon points="0,{-0.52*s:.1f} {-0.32*s:.1f},{-0.92*s:.1f} '
            f'{-0.78*s:.1f},{-0.78*s:.1f} {-0.96*s:.1f},{-0.28*s:.1f} 0,{-0.05*s:.1f}" fill="{tl}"/>'
        )
        parts.append(
            f'<polygon points="0,{-0.52*s:.1f} {0.32*s:.1f},{-0.92*s:.1f} '
            f'{0.78*s:.1f},{-0.78*s:.1f} {0.96*s:.1f},{-0.28*s:.1f} 0,{-0.05*s:.1f}" fill="{tr}"/>'
        )
        parts.append(
            f'<polygon points="{-0.96*s:.1f},{-0.28*s:.1f} {-0.62*s:.1f},{0.35*s:.1f} '
            f'0,{0.95*s:.1f} 0,{-0.05*s:.1f}" fill="{bl}"/>'
        )
        parts.append(
            f'<polygon points="{0.96*s:.1f},{-0.28*s:.1f} {0.62*s:.1f},{0.35*s:.1f} '
            f'0,{0.95*s:.1f} 0,{-0.05*s:.1f}" fill="{br}"/>'
        )
    elif suit == "spades":
        tl, tr, bl, br = "#334155", "#222930", "#1e293b", "#0f172a"
        parts.append(
            f'<polygon points="0,{-1.0*s:.1f} {-0.66*s:.1f},{-0.24*s:.1f} '
            f'{-0.94*s:.1f},{0.26*s:.1f} 0,{0.06*s:.1f}" fill="{tl}"/>'
        )
        parts.append(
            f'<polygon points="0,{-1.0*s:.1f} {0.66*s:.1f},{-0.24*s:.1f} '
            f'{0.94*s:.1f},{0.26*s:.1f} 0,{0.06*s:.1f}" fill="{tr}"/>'
        )
        parts.append(
            f'<polygon points="{-0.94*s:.1f},{0.26*s:.1f} {-0.62*s:.1f},{0.72*s:.1f} '
            f'{-0.18*s:.1f},{0.54*s:.1f} 0,{0.06*s:.1f}" fill="{bl}"/>'
        )
        parts.append(
            f'<polygon points="{0.94*s:.1f},{0.26*s:.1f} {0.62*s:.1f},{0.72*s:.1f} '
            f'{0.18*s:.1f},{0.54*s:.1f} 0,{0.06*s:.1f}" fill="{br}"/>'
        )
        parts.append(
            f'<polygon points="0,{0.06*s:.1f} {-0.12*s:.1f},{0.54*s:.1f} '
            f'{-0.28*s:.1f},{0.98*s:.1f} 0,{0.98*s:.1f}" fill="{bl}"/>'
        )
        parts.append(
            f'<polygon points="0,{0.06*s:.1f} {0.12*s:.1f},{0.54*s:.1f} '
            f'{0.28*s:.1f},{0.98*s:.1f} 0,{0.98*s:.1f}" fill="{br}"/>'
        )
    elif suit == "clubs":
        tl, tr, bl, br = "#334155", "#222930", "#1e293b", "#0f172a"
        # Top lobe (2 facets)
        parts.append(
            f'<polygon points="0,{-0.98*s:.1f} {-0.40*s:.1f},{-0.76*s:.1f} '
            f'{-0.44*s:.1f},{-0.32*s:.1f} 0,{-0.06*s:.1f}" fill="{tl}"/>'
        )
        parts.append(
            f'<polygon points="0,{-0.98*s:.1f} {0.40*s:.1f},{-0.76*s:.1f} '
            f'{0.44*s:.1f},{-0.32*s:.1f} 0,{-0.06*s:.1f}" fill="{tr}"/>'
        )
        # Left lobe
        parts.append(
            f'<polygon points="0,{-0.06*s:.1f} {-0.52*s:.1f},{-0.18*s:.1f} '
            f'{-0.94*s:.1f},{0.18*s:.1f} {-0.84*s:.1f},{0.58*s:.1f} '
            f'{-0.38*s:.1f},{0.68*s:.1f} {-0.10*s:.1f},{0.44*s:.1f}" fill="{bl}"/>'
        )
        # Right lobe
        parts.append(
            f'<polygon points="0,{-0.06*s:.1f} {0.52*s:.1f},{-0.18*s:.1f} '
            f'{0.94*s:.1f},{0.18*s:.1f} {0.84*s:.1f},{0.58*s:.1f} '
            f'{0.38*s:.1f},{0.68*s:.1f} {0.10*s:.1f},{0.44*s:.1f}" fill="{br}"/>'
        )
        # Stem
        parts.append(
            f'<polygon points="0,{-0.06*s:.1f} {-0.12*s:.1f},{0.46*s:.1f} '
            f'{-0.28*s:.1f},{0.98*s:.1f} 0,{0.98*s:.1f}" fill="{bl}"/>'
        )
        parts.append(
            f'<polygon points="0,{-0.06*s:.1f} {0.12*s:.1f},{0.46*s:.1f} '
            f'{0.28*s:.1f},{0.98*s:.1f} 0,{0.98*s:.1f}" fill="{br}"/>'
        )

    tf = f"translate({cx:.1f},{cy:.1f})" + (f" rotate({angle:.0f})" if angle != 0.0 else "")
    return f'<g transform="{tf}">{"".join(parts)}</g>'


def clean_rank_svg(rank: str, cx: float, cy: float, h: float, col: str) -> str:
    """Clean, tall condensed geometric sans-serif rank glyphs on a 16x24 grid."""
    glyphs = {
        "A": "M 6.2,0 L 9.8,0 L 15.6,24 L 12.2,24 L 10.8,17.8 L 5.2,17.8 L 3.8,24 L 0.4,24 Z "
             "M 5.8,14.8 L 10.2,14.8 L 8.0,4.6 Z",
        "2": "M 1.2,6.2 C 1.2,2.2 4.0,0 8.2,0 C 12.4,0 14.8,2.4 14.8,6.2 C 14.8,9.6 12.6,12.2 5.4,20.8 "
             "L 15.0,20.8 L 15.0,24 L 1.0,24 L 1.0,21.0 C 8.8,11.6 11.4,9.2 11.4,6.2 "
             "C 11.4,4.0 10.1,3.0 8.1,3.0 C 6.0,3.0 4.6,4.2 4.6,6.8 L 1.2,6.8 Z",
        "3": "M 1.4,5.6 C 1.6,2.0 4.2,0 8.0,0 C 12.0,0 14.6,2.2 14.6,5.8 C 14.6,8.4 13.0,10.2 10.8,11.2 "
             "C 13.4,12.2 15.0,14.2 15.0,17.4 C 15.0,21.6 12.2,24 8.0,24 C 4.0,24 1.4,21.8 1.0,18.2 "
             "L 4.4,17.8 C 4.7,19.8 6.0,21.0 8.0,21.0 C 10.2,21.0 11.6,19.6 11.6,17.2 "
             "C 11.6,14.6 10.0,13.0 7.2,13.0 L 5.8,13.0 L 5.8,10.0 L 7.2,10.0 "
             "C 9.6,10.0 11.2,8.6 11.2,6.2 C 11.2,4.2 9.9,3.0 8.0,3.0 C 6.1,3.0 4.8,4.0 4.6,6.0 Z",
        "4": "M 9.6,0 L 13.2,0 L 13.2,15.6 L 15.6,15.6 L 15.6,18.6 L 13.2,18.6 L 13.2,24 L 9.8,24 "
             "L 9.8,18.6 L 0.4,18.6 L 0.4,15.4 Z M 9.8,15.6 L 9.8,5.2 L 3.8,15.6 Z",
        "5": "M 2.0,0 L 14.2,0 L 14.2,3.2 L 5.0,3.2 L 4.6,9.4 C 5.6,8.6 7.0,8.2 8.6,8.2 "
             "C 12.4,8.2 15.0,11.0 15.0,16.0 C 15.0,21.0 12.2,24 7.8,24 C 3.8,24 1.4,21.6 1.0,18.0 "
             "L 4.4,17.6 C 4.8,19.6 6.0,20.8 7.8,20.8 C 10.2,20.8 11.6,19.0 11.6,16.0 "
             "C 11.6,13.0 10.2,11.2 7.8,11.2 C 6.2,11.2 5.0,11.8 4.2,12.8 L 1.4,12.2 Z",
        "6": "M 14.4,5.6 L 11.2,6.0 C 10.8,4.0 9.8,3.0 8.2,3.0 C 5.6,3.0 4.4,5.8 4.2,11.0 "
             "C 5.4,9.6 6.8,8.8 8.8,8.8 C 12.6,8.8 15.0,11.6 15.0,16.2 C 15.0,21.2 12.2,24 8.0,24 "
             "C 3.2,24 1.0,20.4 1.0,12.8 C 1.0,4.2 3.6,0 8.4,0 C 11.8,0 14.0,2.0 14.4,5.6 Z "
             "M 4.4,16.2 C 4.4,19.2 5.8,21.0 8.0,21.0 C 10.2,21.0 11.6,19.2 11.6,16.2 "
             "C 11.6,13.4 10.2,11.8 8.0,11.8 C 5.8,11.8 4.4,13.4 4.4,16.2 Z",
        "7": "M 1.0,0 L 15.0,0 L 15.0,3.2 L 7.4,24 L 3.8,24 L 11.2,3.2 L 1.0,3.2 Z",
        "8": "M 8.0,0 C 12.0,0 14.4,2.2 14.4,5.8 C 14.4,8.4 13.0,10.2 11.0,11.2 "
             "C 13.4,12.2 15.0,14.4 15.0,17.4 C 15.0,21.6 12.2,24 8.0,24 C 3.8,24 1.0,21.6 1.0,17.4 "
             "C 1.0,14.4 2.6,12.2 5.0,11.2 C 3.0,10.2 1.6,8.4 1.6,5.8 C 1.6,2.2 4.0,0 8.0,0 Z "
             "M 4.8,6.0 C 4.8,8.4 6.1,9.8 8.0,9.8 C 9.9,9.8 11.2,8.4 11.2,6.0 C 11.2,4.0 9.9,2.8 8.0,2.8 "
             "C 6.1,2.8 4.8,4.0 4.8,6.0 Z "
             "M 4.4,17.0 C 4.4,19.4 5.9,21.0 8.0,21.0 C 10.1,21.0 11.6,19.4 11.6,17.0 "
             "C 11.6,14.4 10.1,12.8 8.0,12.8 C 5.9,12.8 4.4,14.4 4.4,17.0 Z",
        "9": "M 1.6,18.4 L 4.8,18.0 C 5.2,20.0 6.2,21.0 7.8,21.0 C 10.4,21.0 11.6,18.2 11.8,13.0 "
             "C 10.6,14.4 9.2,15.2 7.2,15.2 C 3.4,15.2 1.0,12.4 1.0,7.8 C 1.0,2.8 3.8,0 8.0,0 "
             "C 12.8,0 15.0,3.6 15.0,11.2 C 15.0,19.8 12.4,24 7.6,24 C 4.2,24 2.0,22.0 1.6,18.4 Z "
             "M 11.6,7.8 C 11.6,4.8 10.2,3.0 8.0,3.0 C 5.8,3.0 4.4,4.8 4.4,7.8 "
             "C 4.4,10.6 5.8,12.2 8.0,12.2 C 10.2,12.2 11.6,10.6 11.6,7.8 Z",
        "10": "M 0.0,0 L 3.4,0 L 3.4,24 L 0.2,24 L 0.2,4.4 L -2.2,6.2 L -2.2,2.8 Z "
              "M 10.8,0 C 14.4,0 16.4,3.6 16.4,12.0 C 16.4,20.4 14.4,24 10.8,24 "
              "C 7.2,24 5.2,20.4 5.2,12.0 C 5.2,3.6 7.2,0 10.8,0 Z "
              "M 10.8,3.0 C 9.1,3.0 8.4,5.4 8.4,12.0 C 8.4,18.6 9.1,21.0 10.8,21.0 "
              "C 12.5,21.0 13.2,18.6 13.2,12.0 C 13.2,5.4 12.5,3.0 10.8,3.0 Z",
        "J": "M 10.4,0 L 13.8,0 L 13.8,16.8 C 13.8,21.4 11.4,24 7.2,24 C 3.4,24 1.2,21.6 1.0,17.8 "
             "L 4.4,17.4 C 4.6,19.8 5.6,20.8 7.2,20.8 C 9.2,20.8 10.4,19.4 10.4,16.6 Z",
        "Q": "M 8.0,0 C 12.4,0 15.0,3.8 15.0,11.6 C 15.0,16.2 14.0,19.4 12.2,21.4 L 15.4,24.4 "
             "L 13.0,26.6 L 9.8,23.4 C 9.2,23.6 8.6,23.8 8.0,23.8 C 3.6,23.8 1.0,19.8 1.0,11.6 "
             "C 1.0,3.8 3.6,0 8.0,0 Z "
             "M 8.0,3.0 C 5.6,3.0 4.4,5.8 4.4,11.6 C 4.4,17.6 5.6,20.6 8.0,20.6 "
             "C 10.4,20.6 11.6,17.6 11.6,11.6 C 11.6,5.8 10.4,3.0 8.0,3.0 Z",
        "K": "M 1.4,0 L 4.8,0 L 4.8,11.0 L 11.6,0 L 15.6,0 L 8.6,11.4 L 16.0,24 L 12.0,24 "
             "L 6.4,14.0 L 4.8,16.6 L 4.8,24 L 1.4,24 Z",
        "O": "M 8.0,0 C 12.4,0 15.0,3.8 15.0,12.0 C 15.0,20.2 12.4,24 8.0,24 "
             "C 3.6,24 1.0,20.2 1.0,12.0 C 1.0,3.8 3.6,0 8.0,0 Z "
             "M 8.0,3.0 C 5.6,3.0 4.4,5.8 4.4,12.0 C 4.4,18.2 5.6,21.0 8.0,21.0 "
             "C 10.4,21.0 11.6,18.2 11.6,12.0 C 11.6,5.8 10.4,3.0 8.0,3.0 Z",
        "E": "M 1.8,0 L 14.4,0 L 14.4,3.2 L 5.2,3.2 L 5.2,10.2 L 13.2,10.2 L 13.2,13.4 "
             "L 5.2,13.4 L 5.2,20.8 L 14.6,20.8 L 14.6,24 L 1.8,24 Z",
        "R": "M 1.6,0 L 9.0,0 C 12.8,0 14.8,2.2 14.8,6.6 C 14.8,10.0 13.2,12.2 10.4,13.0 "
             "L 15.4,24 L 11.6,24 L 7.2,13.6 L 5.0,13.6 L 5.0,24 L 1.6,24 Z "
             "M 5.0,3.2 L 5.0,10.6 L 8.8,10.6 C 10.6,10.6 11.6,9.2 11.6,6.8 "
             "C 11.6,4.4 10.6,3.2 8.8,3.2 Z",
    }
    d = glyphs.get(rank, "")
    if not d:
        return ""
    sc = h / 24.0
    tx = cx - 8.0 * sc
    ty = cy - 12.0 * sc
    return f'<g transform="translate({tx:.1f},{ty:.1f}) scale({sc:.4f})"><path d="{d}" fill="{col}" fill-rule="evenodd"/></g>'


def build_court_inner(rank: str, suit: str, court_meshes: Dict[str, List[Tuple[List[Tuple[float, float]], str]]],
                      w: float, h: float) -> str:
    """Build symmetrical low-poly court illustration inside a thin-bordered frame."""
    bx, by, bw, bh = 66.0, 48.0, w - 132.0, h - 96.0  # 228 x 408
    cx = bx + bw * 0.5
    cy = by + bh * 0.5

    # Court meshes in solengine_lowpoly_courts.svg are authored in 252x388, top half y=14..230 (waist ~204)
    sx = bw / 252.0
    sy = (bh * 0.5) / 204.0

    key = f"{rank}_{suit}"
    polys = court_meshes.get(key, [])

    half_paths: List[str] = []
    for pts, fill in polys:
        clipped = clip_poly_y_max(pts, 204.0)
        if len(clipped) < 3:
            continue
        d = "M " + " L ".join(f"{px * sx:.1f},{py * sy:.1f}" for px, py in clipped) + " Z"
        half_paths.append(f'<path d="{d}" fill="{fill}"/>')

    half_group = "".join(half_paths)
    top_half = f'<g transform="translate({bx:.1f},{by:.1f})">{half_group}</g>'
    bot_half = f'<g transform="translate({bx + bw:.1f},{by + bh:.1f}) rotate(180)">{half_group}</g>'

    # Large corner suit pips inside the court frame (matching the reference image)
    tl_pip = flat_suit_svg(suit, bx + 24.0, by + 34.0, 32.0)
    br_pip = flat_suit_svg(suit, bx + bw - 24.0, by + bh - 34.0, 32.0, 180.0)

    border = f'<rect x="{bx:.1f}" y="{by:.1f}" width="{bw:.1f}" height="{bh:.1f}" fill="none" stroke="{FRAME_STROKE}" stroke-width="2.2"/>'
    return "\n".join([top_half, bot_half, tl_pip, br_pip, border])


def build_joker_inner(is_red: bool, w: float, h: float) -> str:
    """Low-poly geometric jester diamond emblem inside the thin court frame."""
    bx, by, bw, bh = 66.0, 48.0, w - 132.0, h - 96.0
    cx = bx + bw * 0.5
    cy = by + bh * 0.5
    c1, c2, c3, c4 = ("#e11d48", "#c81e32", "#fbbf24", "#9f1239") if is_red else ("#334155", "#1e293b", "#fbbf24", "#0f172a")
    tris = [
        f'<polygon points="{cx:.1f},{cy-110:.1f} {cx-62:.1f},{cy:.1f} {cx:.1f},{cy:.1f}" fill="{c1}"/>',
        f'<polygon points="{cx:.1f},{cy-110:.1f} {cx+62:.1f},{cy:.1f} {cx:.1f},{cy:.1f}" fill="{c2}"/>',
        f'<polygon points="{cx-62:.1f},{cy:.1f} {cx:.1f},{cy+110:.1f} {cx:.1f},{cy:.1f}" fill="{c2}"/>',
        f'<polygon points="{cx+62:.1f},{cy:.1f} {cx:.1f},{cy+110:.1f} {cx:.1f},{cy:.1f}" fill="{c4}"/>',
        f'<polygon points="{cx:.1f},{cy-46:.1f} {cx-26:.1f},{cy:.1f} {cx:.1f},{cy+46:.1f} {cx+26:.1f},{cy:.1f}" fill="{c3}"/>',
    ]
    border = f'<rect x="{bx:.1f}" y="{by:.1f}" width="{bw:.1f}" height="{bh:.1f}" fill="none" stroke="{FRAME_STROKE}" stroke-width="2.2"/>'
    return "\n".join(tris + [border])


def pip_card_inner(rank: str, suit: str, w: float, h: float) -> str:
    layouts = {
        "A": [(0.50, 0.50, 2.05, 0)],
        "2": [(0.50, 0.23, 1.05, 0), (0.50, 0.77, 1.05, 180)],
        "3": [(0.50, 0.22, 1.02, 0), (0.50, 0.50, 1.02, 0), (0.50, 0.78, 1.02, 180)],
        "4": [(0.35, 0.23, 1.00, 0), (0.65, 0.23, 1.00, 0), (0.35, 0.77, 1.00, 180), (0.65, 0.77, 1.00, 180)],
        "5": [(0.35, 0.23, 1.00, 0), (0.65, 0.23, 1.00, 0), (0.50, 0.50, 1.00, 0), (0.35, 0.77, 1.00, 180), (0.65, 0.77, 1.00, 180)],
        "6": [(0.35, 0.22, 0.96, 0), (0.65, 0.22, 0.96, 0), (0.35, 0.50, 0.96, 0), (0.65, 0.50, 0.96, 0), (0.35, 0.78, 0.96, 180), (0.65, 0.78, 0.96, 180)],
        "7": [(0.35, 0.22, 0.94, 0), (0.65, 0.22, 0.94, 0), (0.50, 0.36, 0.94, 0), (0.35, 0.50, 0.94, 0), (0.65, 0.50, 0.94, 0), (0.35, 0.78, 0.94, 180), (0.65, 0.78, 0.94, 180)],
        "8": [(0.35, 0.22, 0.92, 0), (0.65, 0.22, 0.92, 0), (0.50, 0.36, 0.92, 0), (0.35, 0.50, 0.92, 0), (0.65, 0.50, 0.92, 0), (0.50, 0.64, 0.92, 180), (0.35, 0.78, 0.92, 180), (0.65, 0.78, 0.92, 180)],
        "9": [(0.35, 0.20, 0.88, 0), (0.65, 0.20, 0.88, 0), (0.35, 0.39, 0.88, 0), (0.65, 0.39, 0.88, 0), (0.50, 0.50, 0.88, 0), (0.35, 0.61, 0.88, 180), (0.65, 0.61, 0.88, 180), (0.35, 0.80, 0.88, 180), (0.65, 0.80, 0.88, 180)],
        "10": [(0.35, 0.20, 0.86, 0), (0.65, 0.20, 0.86, 0), (0.50, 0.30, 0.86, 0), (0.35, 0.40, 0.86, 0), (0.65, 0.40, 0.86, 0), (0.35, 0.60, 0.86, 180), (0.65, 0.60, 0.86, 180), (0.50, 0.70, 0.86, 180), (0.35, 0.80, 0.86, 180), (0.65, 0.80, 0.86, 180)],
    }
    return "\n".join(faceted_pip_svg(suit, w * nx, h * ny, 56.0 * sc, ang) for nx, ny, sc, ang in layouts[rank])


def card_svg(x: float, y: float, w: float, h: float, rank: str, suit: str, inner: str) -> str:
    if suit == "joker_red":
        col = RED_INDEX
    elif suit == "joker_black":
        col = BLACK_INDEX
    else:
        col = RED_INDEX if suit in ("hearts", "diamonds") else BLACK_INDEX

    if suit.startswith("joker"):
        corner_svg = "\n".join(
            clean_rank_svg(ch, 34.0, 44.0 + k * 24.0, 18.0, col) for k, ch in enumerate("JOKER")
        )
    else:
        corner_svg = (
            clean_rank_svg(rank, 34.0, 46.0, 34.0, col) + "\n" +
            flat_suit_svg(suit, 34.0, 84.0, 26.0)
        )

    return "\n".join([
        f'<g transform="translate({x:.1f},{y:.1f})">',
        f'<rect x="0" y="0" width="{w}" height="{h}" fill="#ffffff"/>',
        inner,
        corner_svg,
        f'<g transform="translate({w},{h}) rotate(180)">',
        corner_svg,
        '</g>',
        '</g>',
    ])


def main() -> None:
    ap = argparse.ArgumentParser(description="Build Geometric Low-Poly SolEngine .eats deck.")
    ap.add_argument("--courts-svg", type=Path, default=Path("decks/solengine_lowpoly_courts.svg"))
    ap.add_argument("--out-eats", type=Path, default=Path("decks/geometric_lowpoly.eats"))
    args = ap.parse_args()

    court_meshes = extract_court_meshes(args.courts_svg)
    card_w, card_h = 360.0, 504.0
    cols, rows = 9, 6
    sheet_w, sheet_h = int(cols * card_w), int(rows * card_h)

    cells: List[Tuple[str, str, str]] = []
    for suit in SUITS:
        for rank in RANKS:
            if rank in ("J", "Q", "K"):
                inner = build_court_inner(rank, suit, court_meshes, card_w, card_h)
            else:
                inner = pip_card_inner(rank, suit, card_w, card_h)
            cells.append((rank, suit, inner))

    cells.append(("JOKER", "joker_black", build_joker_inner(False, card_w, card_h)))
    cells.append(("JOKER", "joker_red", build_joker_inner(True, card_w, card_h)))

    body = [
        card_svg((idx % cols) * card_w, (idx // cols) * card_h, card_w, card_h, rank, suit, inner)
        for idx, (rank, suit, inner) in enumerate(cells)
    ]
    master_svg = (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{sheet_w}" height="{sheet_h}" '
        f'viewBox="0 0 {sheet_w} {sheet_h}">\n' + "\n".join(body) + "\n</svg>\n"
    )

    evec_raw = svg_to_evec_bytes(master_svg)
    evecz = zlib.compress(evec_raw, level=9)
    b64 = base64.b64encode(evecz).decode("ascii")

    eats = f'''# SolEngine Deck Recipe — Geometric Low-Poly

sol.set_theme({{
    id: "geometric_lowpoly",
    name: "Geometric Low-Poly",
    author: "SolEngine",
    description: "Crisp white-stock flat-shaded low-poly geometric vector deck with faceted pips and symmetrical courts.",
    corner_radius: 0.0,
    version: 1
}})

format = "evecz"
deck_blob = "{b64}"

def on_draw_card_back(ctx, w, h):
    ctx.fill_rect(0, 0, w, h, "#ffffff")
    ctx.fill_rect(w * 0.06, h * 0.045, w * 0.88, h * 0.91, "#1e293b")
    ctx.stroke_rect(w * 0.06, h * 0.045, w * 0.88, h * 0.91, "#334155", 1.5)
    back_svg = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 140'><polygon points='50,24 22,70 50,70' fill='#334155'/><polygon points='50,24 78,70 50,70' fill='#0f172a'/><polygon points='22,70 50,70 50,116' fill='#475569'/><polygon points='78,70 50,70 50,116' fill='#1e293b'/><polygon points='50,48 36,70 50,70' fill='#fbbf24'/><polygon points='50,48 64,70 50,70' fill='#d97706'/><polygon points='36,70 50,70 50,92' fill='#f59e0b'/><polygon points='64,70 50,70 50,92' fill='#b45309'/></svg>"
    ctx.draw_svg(back_svg, 0.0, 0.0, w, h)
'''
    args.out_eats.parent.mkdir(parents=True, exist_ok=True)
    args.out_eats.write_text(eats, encoding="utf-8")
    print(f"[GeometricLowPoly] Wrote {args.out_eats} (EVEC={len(evec_raw)} B -> EVECZ={len(evecz)} B -> .eats={len(eats)} B)")


if __name__ == "__main__":
    main()
