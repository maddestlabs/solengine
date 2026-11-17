#!/usr/bin/env python3
"""
tools/generate_classic_lowpoly_deck.py
Parametric 3D-projected, flat-shaded low-poly Classic Solitaire deck generator.

Features:
  - Square-corner full-bleed card plate (supports both Square and Rounded corner settings)
  - Configurable background theme (--bg white | ivory | dark)
  - Chamfered low-poly rank & 2-tone faceted suit corner indices (from Another World style)
  - Traditional 4-point faceted Diamond in matching crimson-red
  - Suit-locked Court palettes: Hearts/Diamonds = Crimson Red, Spades/Clubs = Dark Slate/Gray
  - Distinct Crowns: Queen = Silver, King = Gold; Jack = unified faceted Page/Cap bust
  - Centered, properly scaled 2-10 pip layouts baked cleanly in 100x140 space

Usage:
  python tools/generate_classic_lowpoly_deck.py --seed 42 --bg white --out decks/solpoly.eats --id solpoly --name SolPoly
"""

import argparse
import math
import random
import sys
from pathlib import Path
from typing import Dict, List, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_another_world_deck import rank_path_svg, suit_svg  # noqa: E402

Vec3 = Tuple[float, float, float]
Tri3 = Tuple[Vec3, Vec3, Vec3, str]

SUITS = ["spades", "hearts", "diamonds", "clubs"]
RANKS = ["A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"]

LIGHT_DIR: Vec3 = (-0.52, -0.62, 0.59)

BG_PRESETS: Dict[str, Dict[str, str]] = {
    "white": {
        "plate": "#f4f2ec",
        "bevel": "#e6e2d8",
        "red_idx": "#9e1b24",
        "black_idx": "#1a1733",
    },
    "ivory": {
        "plate": "#f2ece1",
        "bevel": "#e4dccb",
        "red_idx": "#9e1b24",
        "black_idx": "#1c1917",
    },
    "dark": {
        "plate": "#2b303a",
        "bevel": "#22262f",
        "red_idx": "#f2d8d5",
        "black_idx": "#e8dfd1",
    },
}


def v_sub(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def v_cross(a: Vec3, b: Vec3) -> Vec3:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def v_dot(a: Vec3, b: Vec3) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def v_norm(a: Vec3) -> Vec3:
    l = math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])
    if l < 1e-8:
        return (0.0, 0.0, 1.0)
    return (a[0] / l, a[1] / l, a[2] / l)


def render_tris_svg(
    tris: List[Tri3],
    palettes: dict,
    cx: float = 50.0,
    cy: float = 70.0,
    scale: float = 1.0,
    rotate_180: bool = False,
) -> str:
    drawable = []
    ld = v_norm(LIGHT_DIR)
    for v0, v1, v2, mat in tris:
        n = v_cross(v_sub(v1, v0), v_sub(v2, v0))
        nn = v_norm(n)
        if nn[2] < 0.0:
            nn = (-nn[0], -nn[1], -nn[2])
        intensity = max(0.0, min(1.0, 0.5 + 0.5 * v_dot(nn, ld)))
        ramp = palettes[mat]
        idx = min(len(ramp) - 1, int(intensity * len(ramp)))
        col = ramp[idx]
        depth = (v0[2] + v1[2] + v2[2]) / 3.0
        s = -scale if rotate_180 else scale
        pts = (
            f"{cx + v0[0] * s:.1f},{cy + v0[1] * s:.1f} "
            f"{cx + v1[0] * s:.1f},{cy + v1[1] * s:.1f} "
            f"{cx + v2[0] * s:.1f},{cy + v2[1] * s:.1f}"
        )
        drawable.append((depth, pts, col))

    drawable.sort(key=lambda item: item[0])
    return "".join(f"<polygon points='{pts}' fill='{col}'/>" for _, pts, col in drawable)


# ---------------------------------------------------------------------------
# 3D Faceted Suit Gems (used for Aces and Number Pips)
# ---------------------------------------------------------------------------
def get_suit_ramps(bg_name: str) -> dict:
    if bg_name == "dark":
        black_ramp = ["#334155", "#475569", "#94a3b8", "#e2e8f0"]
    else:
        # High-contrast dark slate facets so Spades/Clubs pop cleanly on white/ivory
        black_ramp = ["#0f172a", "#1e293b", "#334155", "#64748b"]

    red_ramp = ["#7f1d1d", "#9e1b24", "#c8202a", "#ef4444"]
    return {
        "spades": {"main": black_ramp},
        "clubs": {"main": black_ramp},
        "hearts": {"main": red_ramp},
        "diamonds": {"main": red_ramp},
    }


def build_diamond_tris() -> List[Tri3]:
    # Traditional 4-point symmetric playing-card diamond (lozenge) with faceted ridge
    tris: List[Tri3] = []
    top = (0.0, -25.0, 1.5)
    bot = (0.0, 25.0, 1.5)
    left = (-18.5, 0.0, 1.5)
    right = (18.5, 0.0, 1.5)
    hub = (0.0, 0.0, 9.0)
    in_t = (0.0, -10.5, 7.5)
    in_b = (0.0, 10.5, 7.5)
    in_l = (-7.5, 0.0, 7.5)
    in_r = (7.5, 0.0, 7.5)

    tris.extend([
        # Outer bevels
        (top, in_t, left, "main"),
        (left, in_t, in_l, "main"),
        (top, right, in_t, "main"),
        (in_t, right, in_r, "main"),
        (left, in_l, bot, "main"),
        (in_l, in_b, bot, "main"),
        (in_r, right, bot, "main"),
        (in_b, in_r, bot, "main"),
        # Raised center facets
        (in_t, hub, in_l, "main"),
        (in_t, in_r, hub, "main"),
        (in_l, hub, in_b, "main"),
        (hub, in_r, in_b, "main"),
    ])
    return tris


def build_heart_tris() -> List[Tri3]:
    tris: List[Tri3] = []
    apex = (0.0, -6.0, 8.5)
    c_mid = (0.0, 4.0, 7.0)
    bot = (0.0, 24.0, 1.0)

    def sym(a: Vec3, b: Vec3, c: Vec3):
        tris.append((a, b, c, "main"))
        tris.append(((-a[0], a[1], a[2]), (-c[0], c[1], c[2]), (-b[0], b[1], b[2]), "main"))

    lobe_top = (11.0, -20.0, 2.0)
    lobe_out = (22.0, -6.0, 2.0)
    cheek = (16.0, 8.0, 2.5)
    sym((0.0, -11.0, 5.0), lobe_top, apex)
    sym(lobe_top, lobe_out, apex)
    sym(apex, lobe_out, c_mid)
    sym(lobe_out, cheek, c_mid)
    sym(c_mid, cheek, bot)
    return tris


def build_spade_tris() -> List[Tri3]:
    tris: List[Tri3] = []
    tip = (0.0, -26.0, 1.5)
    ridge = (0.0, -6.0, 9.0)
    center = (0.0, 6.0, 7.5)
    stem_top = (0.0, 10.0, 5.0)
    stem_bot = (0.0, 24.0, 2.0)

    def sym(a: Vec3, b: Vec3, c: Vec3):
        tris.append((a, b, c, "main"))
        tris.append(((-a[0], a[1], a[2]), (-c[0], c[1], c[2]), (-b[0], b[1], b[2]), "main"))

    wing = (22.0, 4.0, 2.0)
    lobe = (13.0, 14.0, 2.0)
    sym(tip, wing, ridge)
    sym(ridge, wing, center)
    sym(center, wing, lobe)
    sym(center, lobe, stem_top)
    sym(stem_top, (8.5, 24.0, 1.5), stem_bot)
    return tris


def build_club_tris() -> List[Tri3]:
    tris: List[Tri3] = []
    center = (0.0, 2.0, 8.5)

    def add_leaf(cx: float, cy: float, r: float):
        hub = (cx, cy, 7.0)
        pts = [
            (cx, cy - r, 2.0),
            (cx + r * 0.9, cy - r * 0.3, 2.0),
            (cx + r * 0.6, cy + r * 0.8, 2.0),
            (cx - r * 0.6, cy + r * 0.8, 2.0),
            (cx - r * 0.9, cy - r * 0.3, 2.0),
        ]
        for i in range(len(pts)):
            tris.append((hub, pts[i], pts[(i + 1) % len(pts)], "main"))

    add_leaf(0.0, -14.0, 11.5)
    add_leaf(-13.0, 4.0, 10.5)
    add_leaf(13.0, 4.0, 10.5)
    tris.append(((-3.5, 8.0, 5.0), (3.5, 8.0, 5.0), (8.0, 24.0, 1.5), "main"))
    tris.append(((-3.5, 8.0, 5.0), (8.0, 24.0, 1.5), (-8.0, 24.0, 1.5), "main"))
    tris.append(((-3.5, 8.0, 5.0), center, (3.5, 8.0, 5.0), "main"))
    return tris


def get_suit_tris(suit: str) -> List[Tri3]:
    if suit == "diamonds":
        return build_diamond_tris()
    if suit == "hearts":
        return build_heart_tris()
    if suit == "spades":
        return build_spade_tris()
    return build_club_tris()


# ---------------------------------------------------------------------------
# 3D Low-Poly Court Busts (J, Q, K)
# - Hearts & Diamonds: Crimson Red garments & hair/beard
# - Spades & Clubs:    Dark Slate / Charcoal-Gray garments & hair/beard
# - Queen Crowns:      Silver
# - King Crowns:       Gold
# - Jacks:             Unified faceted Page/Cap bust (no knight visor)
# ---------------------------------------------------------------------------
def get_court_palette(suit: str, bg_name: str) -> dict:
    # Skin & crown ramps tuned for crisp contrast even on white card stock
    skin = ["#b9836b", "#cf9982", "#e2b29d", "#f0cab8"]
    silver = ["#475569", "#64748b", "#94a3b8", "#cbd5e1"]
    gold = ["#92400e", "#b45309", "#d97706", "#f59e0b"]

    if suit == "hearts" or suit == "diamonds":
        robe = ["#7f1d1d", "#9e1b24", "#c8202a", "#e11d48"]
        trim = gold
    elif suit == "spades":
        if bg_name == "dark":
            robe = ["#334155", "#475569", "#64748b", "#94a3b8"]
        else:
            robe = ["#1e293b", "#283548", "#334155", "#475569"]
        trim = silver
    else:  # clubs
        if bg_name == "dark":
            robe = ["#44403c", "#57534e", "#78716c", "#a8a29e"]
        else:
            robe = ["#262322", "#383432", "#4a4542", "#625b57"]
        trim = silver

    return {
        "skin": skin,
        "robe": robe,
        "trim": trim,
        "silver": silver,
        "gold": gold,
    }


def build_court_svg(suit: str, rank: str, rng: random.Random, bg_name: str) -> str:
    pal = get_court_palette(suit, bg_name)
    tris: List[Tri3] = []

    def sym(a: Vec3, b: Vec3, c: Vec3, mat: str):
        tris.append((a, b, c, mat))
        tris.append(((-a[0], a[1], a[2]), (-c[0], c[1], c[2]), (-b[0], b[1], b[2]), mat))

    # Shoulders / Mantle (V-cut bust)
    chest_top = (0.0, 12.0, 8.0)
    chest_bot = (0.0, 34.0, 4.0)
    sh_out = (26.0 + rng.uniform(-1.5, 1.5), 16.0, 2.0)
    neck_out = (10.0, 8.0, 5.0)
    sym(chest_top, neck_out, sh_out, "robe")
    sym(chest_top, sh_out, chest_bot, "robe")

    # Collar / Trim
    collar_mat = "gold" if rank == "K" else ("silver" if rank == "Q" else "trim")
    sym((0.0, 10.0, 9.0), (8.5, 12.0, 7.5), (0.0, 20.0, 8.5), collar_mat)

    # Faceted Sovereign Face + Hair/Hood
    forehead = (0.0, -14.0, 8.5)
    cheek = (10.5, -2.0, 6.0)
    chin = (0.0, 10.0, 7.5)
    jaw = (8.0, 5.0, 5.0)

    sym(forehead, cheek, (0.0, -2.0, 9.5), "skin")
    sym((0.0, -2.0, 9.5), cheek, chin, "skin")
    sym(cheek, jaw, chin, "skin")

    hair_top = (13.0, -14.0, 4.0)
    hair_mid = (16.5, 2.0, 3.0)
    sym(forehead, hair_top, cheek, "robe")
    sym(cheek, hair_top, hair_mid, "robe")
    sym(cheek, hair_mid, jaw, "robe")

    if rank == "J":
        # Unified Jack Page/Rogue Cap (same silhouette as Jack of Diamonds)
        sym((0.0, -25.0, 7.0), (16.5, -19.0, 5.0), forehead, "robe")
        sym(forehead, (16.5, -19.0, 5.0), hair_top, "trim")
    elif rank == "Q":
        # Queen's Silver Tiara / Crown
        sym(forehead, (13.0, -14.0, 4.0), (11.0, -21.0, 5.0), "silver")
        sym((0.0, -33.0, 6.5), (4.5, -20.0, 6.0), forehead, "silver")
        sym((4.5, -20.0, 6.0), (11.0, -31.0, 4.5), (11.0, -21.0, 5.0), "silver")
        sym((11.0, -21.0, 5.0), (17.5, -27.5, 3.5), (13.0, -14.0, 4.0), "silver")
    else:
        # King's Gold Crown + Faceted Beard
        sym((0.0, -30.0, 7.0), (5.5, -21.0, 6.5), forehead, "gold")
        sym((5.5, -21.0, 6.5), (13.0, -29.0, 5.0), (13.0, -14.0, 4.0), "gold")
        sym(forehead, (5.5, -21.0, 6.5), (13.0, -14.0, 4.0), "gold")
        # Royal beard
        sym((0.0, 4.0, 9.0), jaw, (0.0, 14.0, 7.0), "robe")
        sym(jaw, (12.0, 9.0, 4.0), (0.0, 14.0, 7.0), "robe")

    return render_tris_svg(tris, pal, cx=50.0, cy=72.0, scale=1.02)


# ---------------------------------------------------------------------------
# Centered Pip Layout (in 100x140 card coordinates)
# ---------------------------------------------------------------------------
PIP_LAYOUTS_100X140: Dict[str, List[Tuple[float, float, bool]]] = {
    "2": [(50.0, 36.0, False), (50.0, 104.0, True)],
    "3": [(50.0, 34.0, False), (50.0, 70.0, False), (50.0, 106.0, True)],
    "4": [(34.0, 36.0, False), (66.0, 36.0, False), (34.0, 104.0, True), (66.0, 104.0, True)],
    "5": [(34.0, 36.0, False), (66.0, 36.0, False), (50.0, 70.0, False), (34.0, 104.0, True), (66.0, 104.0, True)],
    "6": [(34.0, 34.0, False), (66.0, 34.0, False), (34.0, 70.0, False), (66.0, 70.0, False), (34.0, 106.0, True), (66.0, 106.0, True)],
    "7": [(34.0, 34.0, False), (66.0, 34.0, False), (50.0, 52.0, False), (34.0, 70.0, False), (66.0, 70.0, False), (34.0, 106.0, True), (66.0, 106.0, True)],
    "8": [(34.0, 34.0, False), (66.0, 34.0, False), (50.0, 52.0, False), (34.0, 70.0, False), (66.0, 70.0, False), (50.0, 88.0, True), (34.0, 106.0, True), (66.0, 106.0, True)],
    "9": [(34.0, 30.0, False), (66.0, 30.0, False), (34.0, 56.0, False), (66.0, 56.0, False), (50.0, 70.0, False), (34.0, 84.0, True), (66.0, 84.0, True), (34.0, 110.0, True), (66.0, 110.0, True)],
    "10": [(34.0, 30.0, False), (66.0, 30.0, False), (50.0, 43.0, False), (34.0, 56.0, False), (66.0, 56.0, False), (34.0, 84.0, True), (66.0, 84.0, True), (50.0, 97.0, True), (34.0, 110.0, True), (66.0, 110.0, True)],
}


def build_corner_svg(rank: str, suit: str, idx_col: str, clear_bottom_right: bool, bg_name: str) -> str:
    aw_suit = {"spades": "spade", "hearts": "heart", "diamonds": "diamond", "clubs": "club"}[suit]
    pip = suit_svg(aw_suit, 11.0, 26.0, 8.2)
    if bg_name == "dark":
        pip = (
            pip.replace("#1A1733", "#94a3b8")
            .replace("#2E2952", "#e2e8f0")
            .replace("#9E1B24", "#e11d48")
            .replace("#C8202A", "#fb7185")
        )
    tl = rank_path_svg(rank, 11.0, 14.0, 10.5, idx_col) + pip
    if clear_bottom_right:
        return tl
    return tl + f"<g transform='translate(100,140) rotate(180)'>{tl}</g>"


def build_classic_eats(
    seed: int,
    bg_name: str,
    clear_bottom_right: bool,
    theme_id: str,
    theme_name: str,
) -> str:
    rng = random.Random(seed)
    bg = BG_PRESETS[bg_name]
    suit_ramps = get_suit_ramps(bg_name)
    plate_hex = bg["plate"]
    bevel_hex = bg["bevel"]

    lines = [
        f"# SolEngine Procedural Low-Poly Classic Deck (seed={seed}, bg={bg_name})",
        "# Generated by tools/generate_classic_lowpoly_deck.py",
        "# Square-corner full-bleed card plate so both Square and Rounded settings work cleanly.",
        "",
        "sol.set_theme({",
        f'    id: "{theme_id}",',
        f'    name: "{theme_name}",',
        '    author: "SolEngine Procedural",',
        f'    description: "Fast-loading 3D flat-shaded low-poly deck ({bg_name} stock, seed {seed}).",',
        "    corner_radius: 0.0,",
        "    version: 1",
        "})",
        "",
        "CARD_SVGS = {",
    ]

    for suit in SUITS:
        is_red = suit in ("hearts", "diamonds")
        idx_col = bg["red_idx"] if is_red else bg["black_idx"]
        suit_tris = get_suit_tris(suit)
        ramp = suit_ramps[suit]

        for rank in RANKS:
            if rank == "A":
                art = render_tris_svg(suit_tris, ramp, cx=50.0, cy=70.0, scale=1.18)
            elif rank in ("J", "Q", "K"):
                art = build_court_svg(suit, rank, rng, bg_name)
            else:
                pts = PIP_LAYOUTS_100X140[rank]
                art = "".join(
                    render_tris_svg(suit_tris, ramp, cx=px, cy=py, scale=0.31, rotate_180=flip)
                    for (px, py, flip) in pts
                )

            corners = build_corner_svg(rank, suit, idx_col, clear_bottom_right, bg_name)
            card_svg = (
                "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 140'>"
                f"<rect x='0' y='0' width='100' height='140' fill='{plate_hex}'/>"
                f"<polygon points='100,84 56,140 100,140' fill='{bevel_hex}'/>"
                + art
                + corners
                + "</svg>"
            ).replace('"', "'")
            lines.append(f'    "{suit}_{rank}": "{card_svg}",')

    lines.append("}")
    lines.append("")
    lines.append(
        """def on_draw_card_face(ctx, rank, suit, w, h):
    key = suit + "_" + rank
    ctx.draw_svg(CARD_SVGS[key], 0.0, 0.0, w, h)

def on_draw_card_back(ctx, w, h):
    back_svg = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 140'><rect x='0' y='0' width='100' height='140' fill='#1e2430'/><polygon points='100,84 56,140 100,140' fill='#171c26'/><polygon points='50,24 22,70 50,70' fill='#334155'/><polygon points='50,24 78,70 50,70' fill='#1e293b'/><polygon points='22,70 50,70 50,116' fill='#475569'/><polygon points='78,70 50,70 50,116' fill='#0f172a'/><polygon points='50,48 36,70 50,70' fill='#38bdf8'/><polygon points='50,48 64,70 50,70' fill='#0284c7'/><polygon points='36,70 50,70 50,92' fill='#7dd3fc'/><polygon points='64,70 50,70 50,92' fill='#0369a1'/></svg>"
    ctx.draw_svg(back_svg, 0.0, 0.0, w, h)
    ctx.stroke_rect(w * 0.06, h * 0.045, w * 0.88, h * 0.91, "#334155", 1.5)
"""
    )
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description="Generate a seeded low-poly Classic Solitaire .eats deck.")
    ap.add_argument("--seed", type=int, default=42, help="Procedural generation seed")
    ap.add_argument("--bg", choices=["white", "ivory", "dark"], default="white", help="Card stock background")
    ap.add_argument("--clear-bottom-right", action="store_true", help="Omit bottom-right index (for RPG overlays)")
    ap.add_argument("--id", default="solpoly", help="Theme ID")
    ap.add_argument("--name", default="SolPoly", help="Display name")
    ap.add_argument("--out", type=Path, default=Path("decks/solpoly.eats"), help="Output .eats path")
    args = ap.parse_args()

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        build_classic_eats(args.seed, args.bg, args.clear_bottom_right, args.id, args.name),
        encoding="utf-8",
    )
    print(f"[ClassicLowPoly] Wrote {args.out} (id={args.id}, seed={args.seed}, bg={args.bg}, 52 faces + back)")


if __name__ == "__main__":
    main()
