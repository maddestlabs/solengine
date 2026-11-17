#!/usr/bin/env python3
"""
tools/generate_scoundrel_deck.py
Parametric 3D-projected, flat-shaded low-poly Scoundrel deck generator.

Generates a complete 52-card + card-back Eatscript deck (.eats) in the
dark-charcoal + diagonal-bevel low-poly style:
  - Spades & Clubs: Low-poly Monsters (Undead/Shadow & Beasts/Brutes)
  - Hearts:         Low-poly Health Potions (lathed polygonal flasks)
  - Diamonds:       Low-poly Weapons (beveled 3D blades & hilts)
  - Bottom-left & bottom-right corners remain clear for RPG HP/ATK/DUR badges.

Usage:
  python tools/generate_scoundrel_deck.py --seed 42 --out decks/scoundrel_lowpoly.eats
"""

import argparse
import math
import random
import sys
from pathlib import Path
from typing import List, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_another_world_deck import rank_path_svg, suit_svg  # noqa: E402

Vec3 = Tuple[float, float, float]
Tri3 = Tuple[Vec3, Vec3, Vec3, str]  # v0, v1, v2, material_key

SUITS = ["spades", "hearts", "diamonds", "clubs"]
RANKS = ["2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A"]

# Key light from top-left-front
LIGHT_DIR: Vec3 = (-0.52, -0.62, 0.59)


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


def rot_z(p: Vec3, deg: float) -> Vec3:
    r = math.radians(deg)
    c, s = math.cos(r), math.sin(r)
    return (p[0] * c - p[1] * s, p[0] * s + p[1] * c, p[2])


def rot_y(p: Vec3, deg: float) -> Vec3:
    r = math.radians(deg)
    c, s = math.cos(r), math.sin(r)
    return (p[0] * c + p[2] * s, p[1], -p[0] * s + p[2] * c)


def rot_x(p: Vec3, deg: float) -> Vec3:
    r = math.radians(deg)
    c, s = math.cos(r), math.sin(r)
    return (p[0], p[1] * c - p[2] * s, p[1] * s + p[2] * c)


def rank_num(rank: str) -> int:
    if rank == "A":
        return 14
    if rank == "K":
        return 13
    if rank == "Q":
        return 12
    if rank == "J":
        return 11
    return int(rank)


def render_tris_svg(
    tris: List[Tri3],
    palettes: dict,
    cx: float = 50.0,
    cy: float = 73.0,
    cull_backface: bool = True,
) -> str:
    """Painter-sort 3D triangles, flat-shade against LIGHT_DIR, and emit SVG polygons."""
    drawable = []
    ld = v_norm(LIGHT_DIR)
    for v0, v1, v2, mat in tris:
        n = v_cross(v_sub(v1, v0), v_sub(v2, v0))
        if cull_backface and n[2] <= 0.0:
            continue
        nn = v_norm(n)
        # Ensure normal faces camera for two-sided facets
        if nn[2] < 0.0:
            nn = (-nn[0], -nn[1], -nn[2])
        intensity = max(0.0, min(1.0, 0.5 + 0.5 * v_dot(nn, ld)))
        ramp = palettes[mat]
        idx = min(len(ramp) - 1, int(intensity * len(ramp)))
        # Ramp is ordered dark -> bright
        col = ramp[idx]
        depth = (v0[2] + v1[2] + v2[2]) / 3.0
        pts = (
            f"{cx + v0[0]:.1f},{cy + v0[1]:.1f} "
            f"{cx + v1[0]:.1f},{cy + v1[1]:.1f} "
            f"{cx + v2[0]:.1f},{cy + v2[1]:.1f}"
        )
        drawable.append((depth, pts, col))

    drawable.sort(key=lambda item: item[0])
    return "".join(f"<polygon points='{pts}' fill='{col}'/>" for _, pts, col in drawable)


# ---------------------------------------------------------------------------
# 1. Parametric Low-Poly Weapon Generator (Diamonds)
# ---------------------------------------------------------------------------
WEAPON_PALETTES = [
    {  # Ice Crystal + Slate Blue (matches reference 7D)
        "blade": ["#283b5b", "#39567d", "#4f74a3", "#6e96c7"],
        "tip": ["#0284c7", "#0ea5e9", "#38bdf8", "#7dd3fc"],
        "guard": ["#b45309", "#d97706", "#f59e0b", "#fde047"],
        "grip": ["#1e293b", "#334155", "#475569", "#64748b"],
    },
    {  # Obsidian + Ember Ruby
        "blade": ["#1f242d", "#2e3542", "#404959", "#5b667a"],
        "tip": ["#991b1b", "#dc2626", "#ef4444", "#fca5a5"],
        "guard": ["#92400e", "#d97706", "#f59e0b", "#fef08a"],
        "grip": ["#18181b", "#27272a", "#3f3f46", "#52525b"],
    },
    {  # Emerald Jade + Bronze
        "blade": ["#1e3a34", "#285249", "#346e61", "#499180"],
        "tip": ["#059669", "#10b981", "#34d399", "#a7f3d0"],
        "guard": ["#78350f", "#b45309", "#d97706", "#fbbf24"],
        "grip": ["#292524", "#44403c", "#57534e", "#78716c"],
    },
]


def generate_weapon(rng: random.Random, rnum: int) -> str:
    pal = rng.choice(WEAPON_PALETTES)
    tier = (rnum - 2) / 12.0  # 0.0 .. 1.0

    blade_len = 24.0 + tier * 16.0 + rng.uniform(-2.0, 2.0)
    blade_w = 4.2 + tier * 2.6 + rng.uniform(-0.5, 0.6)
    ridge_z = 3.2 + rng.uniform(-0.4, 0.8)
    tip_len = 7.5 + tier * 4.5 + rng.uniform(-1.0, 1.5)
    guard_w = 10.0 + tier * 5.0 + rng.uniform(-1.5, 1.5)
    grip_len = 10.0 + rng.uniform(-1.0, 2.0)
    angle = -42.0 + rng.uniform(-6.0, 6.0)

    tris: List[Tri3] = []

    def add_quad_prism(y0: float, y1: float, w0: float, w1: float, z0: float, z1: float, mat: str):
        tl = (-w0, y0, 0.0)
        tr = (w0, y0, 0.0)
        tc = (0.0, y0, z0)
        bl = (-w1, y1, 0.0)
        br = (w1, y1, 0.0)
        bc = (0.0, y1, z1)
        tris.append((tl, tc, bc, mat))
        tris.append((tl, bc, bl, mat))
        tris.append((tc, tr, br, mat))
        tris.append((tc, br, bc, mat))

    y_guard = 10.0
    y_shoulder = y_guard - blade_len
    y_tip = y_shoulder - tip_len

    # Blade body (2 segments for crisp faceted reflections)
    y_mid = (y_guard + y_shoulder) * 0.5
    w_mid = blade_w * rng.uniform(0.92, 1.08)
    add_quad_prism(y_shoulder, y_mid, blade_w * 0.85, w_mid, ridge_z * 0.9, ridge_z, "blade")
    add_quad_prism(y_mid, y_guard, w_mid, blade_w, ridge_z, ridge_z * 0.85, "blade")

    # Crystal / forged tip
    tip_pt = (0.0, y_tip, 0.5)
    sl = (-blade_w * 0.85, y_shoulder, 0.0)
    sr = (blade_w * 0.85, y_shoulder, 0.0)
    sc = (0.0, y_shoulder, ridge_z * 0.9)
    tris.append((sl, tip_pt, sc, "tip"))
    tris.append((sc, tip_pt, sr, "tip"))

    # Diamond / Winged Crossguard
    g_center_top = (0.0, y_guard - 3.2, 3.5)
    g_center_bot = (0.0, y_guard + 3.2, 3.5)
    g_left = (-guard_w, y_guard + rng.uniform(-2.0, 2.0), 0.0)
    g_right = (guard_w, y_guard + rng.uniform(-2.0, 2.0), 0.0)
    tris.append((g_left, g_center_top, g_center_bot, "guard"))
    tris.append((g_center_top, g_right, g_center_bot, "guard"))

    # Grip + Pommel
    y_pom = y_guard + 3.2 + grip_len
    add_quad_prism(y_guard + 2.5, y_pom, 2.0, 1.8, 2.2, 2.0, "grip")
    p_top = (0.0, y_pom - 1.0, 2.8)
    p_bot = (0.0, y_pom + 6.0, 0.5)
    p_l = (-4.2, y_pom + 2.5, 0.0)
    p_r = (4.2, y_pom + 2.5, 0.0)
    tris.append((p_l, p_top, p_bot, "tip" if rnum >= 8 else "guard"))
    tris.append((p_top, p_r, p_bot, "tip" if rnum >= 8 else "guard"))

    # Rotate whole weapon diagonally
    r_tris = [(rot_z(a, angle), rot_z(b, angle), rot_z(c, angle), m) for a, b, c, m in tris]
    return render_tris_svg(r_tris, pal, cx=50.0, cy=72.0, cull_backface=False)


# ---------------------------------------------------------------------------
# 2. Parametric Low-Poly Potion Generator (Hearts)
# ---------------------------------------------------------------------------
POTION_PALETTES = [
    {  # Ruby Rose (matches reference 5H)
        "glass": ["#881337", "#be123c", "#e11d48", "#fb7185"],
        "rim": ["#9f1239", "#e11d48", "#fb7185", "#fda4af"],
        "cork": ["#78350f", "#92400e", "#b45309", "#d97706"],
    },
    {  # Arcane Crimson-Magenta
        "glass": ["#701a75", "#a21caf", "#d946ef", "#f0abfc"],
        "rim": ["#86198f", "#c026d3", "#e879f9", "#f5d0fe"],
        "cork": ["#78350f", "#92400e", "#b45309", "#d97706"],
    },
    {  # Sunfire Elixir
        "glass": ["#9a3412", "#c2410c", "#ea580c", "#fb923c"],
        "rim": ["#b45309", "#d97706", "#f59e0b", "#fcd34d"],
        "cork": ["#57534e", "#78716c", "#a8a29e", "#d6d3d1"],
    },
]


def generate_potion(rng: random.Random, rnum: int) -> str:
    pal = rng.choice(POTION_PALETTES)
    tier = (rnum - 2) / 12.0
    sides = rng.choice([5, 6, 7, 8]) if rnum >= 6 else rng.choice([4, 5, 6])

    # Lathed profile: (y, radius, mat)
    belly_r = 15.0 + tier * 5.0 + rng.uniform(-1.5, 1.5)
    profile = [
        (-28.0, 5.2, "cork"),
        (-21.0, 4.6, "cork"),
        (-21.0, 6.8, "rim"),
        (-17.5, 6.8, "rim"),
        (-17.5, 4.5, "glass"),
        (-9.0, 5.0, "glass"),
        (1.0 + rng.uniform(-2.0, 2.0), belly_r, "glass"),
        (14.0 + rng.uniform(-2.0, 2.0), belly_r * rng.uniform(0.65, 0.88), "glass"),
        (26.0 + tier * 3.0, 0.5, "glass"),
    ]

    phase = rng.uniform(0.0, math.pi / sides)
    tilt_x = rng.uniform(10.0, 18.0)
    tilt_y = rng.uniform(-14.0, 14.0)

    def ring_pts(y: float, r: float) -> List[Vec3]:
        pts = []
        for i in range(sides):
            ang = phase + (2.0 * math.pi * i) / sides
            p = (math.cos(ang) * r, y, math.sin(ang) * r)
            p = rot_y(rot_x(p, tilt_x), tilt_y)
            pts.append(p)
        return pts

    tris: List[Tri3] = []
    rings = [ring_pts(y, r) for y, r, _ in profile]
    for ri in range(len(profile) - 1):
        mat = profile[ri][2]
        if profile[ri][2] != profile[ri + 1][2] and profile[ri][2] == "cork":
            mat = "cork"
        r0 = rings[ri]
        r1 = rings[ri + 1]
        for i in range(sides):
            j = (i + 1) % sides
            tris.append((r0[i], r0[j], r1[j], mat))
            tris.append((r0[i], r1[j], r1[i], mat))

    return render_tris_svg(tris, pal, cx=50.0, cy=74.0, cull_backface=True)


# ---------------------------------------------------------------------------
# 3. Parametric Low-Poly Monster Generator (Spades & Clubs)
# ---------------------------------------------------------------------------
MONSTER_PALETTES = {
    "spades": [
        {  # Bone + Shadow Slate
            "bone": ["#57534e", "#78716c", "#a8a29e", "#e7e5e4"],
            "horn": ["#334155", "#475569", "#64748b", "#94a3b8"],
            "void": ["#11141c", "#11141c", "#181c26", "#181c26"],
        },
        {  # Pale Ash + Violet Steel
            "bone": ["#475569", "#64748b", "#94a3b8", "#cbd5e1"],
            "horn": ["#3b0764", "#581c87", "#7e22ce", "#a855f7"],
            "void": ["#0f1219", "#0f1219", "#171c28", "#171c28"],
        },
    ],
    "clubs": [
        {  # Warm Horned Skull (matches reference 10C)
            "bone": ["#6b685e", "#948f82", "#bfb8a8", "#e6dfd1"],
            "horn": ["#4a4842", "#6e6b62", "#918c7f", "#b5afa0"],
            "void": ["#16181f", "#16181f", "#1c1f28", "#1c1f28"],
        },
        {  # Bronze Gargoyle + Ember Horn
            "bone": ["#57534e", "#78716c", "#a8a29e", "#d6d3d1"],
            "horn": ["#7c2d12", "#b45309", "#d97706", "#f59e0b"],
            "void": ["#161312", "#161312", "#1f1b19", "#1f1b19"],
        },
    ],
}


def generate_monster(rng: random.Random, suit: str, rnum: int) -> str:
    pal = rng.choice(MONSTER_PALETTES[suit])
    tier = (rnum - 2) / 12.0

    # Parametric skull proportions
    w_brow = 14.0 + tier * 3.0 + rng.uniform(-1.5, 1.5)
    w_cheek = 12.0 + rng.uniform(-1.5, 1.5)
    w_jaw = 7.5 + rng.uniform(-1.2, 1.2)
    h_top = -18.0 - tier * 2.0
    h_brow = -6.0 + rng.uniform(-1.5, 1.5)
    h_cheek = 6.0 + rng.uniform(-1.0, 1.5)
    h_jaw = 19.0 + tier * 2.5

    tris: List[Tri3] = []

    def add_sym(v0: Vec3, v1: Vec3, v2: Vec3, mat: str):
        # Right half (x >= 0) and mirrored left half (x <= 0)
        tris.append((v0, v1, v2, mat))
        tris.append(((-v0[0], v0[1], v0[2]), (-v2[0], v2[1], v2[2]), (-v1[0], v1[1], v1[2]), mat))

    # Cranium & Cheekbone (3D depth gives authentic low-poly shading!)
    crown_c = (0.0, h_top, 4.0)
    brow_c = (0.0, h_brow, 9.0)
    nose_top = (0.0, 2.0, 8.5)
    nose_bot = (0.0, 11.0, 6.5)
    chin_c = (0.0, h_jaw, 5.5)

    temple = (w_brow * 0.85, h_top + 6.0, 2.0)
    brow_out = (w_brow, h_brow, 4.5)
    cheek_out = (w_cheek, h_cheek, 3.0)
    jaw_out = (w_jaw, h_jaw - 3.0, 2.0)
    fang_tip = (w_jaw * 0.72, h_jaw + 5.5, 1.5)

    # Upper skull facets
    add_sym(crown_c, temple, brow_c, "bone")
    add_sym(temple, brow_out, brow_c, "bone")
    # Mid-face / cheek bridge
    add_sym(brow_c, brow_out, nose_top, "bone")
    add_sym(brow_out, cheek_out, nose_top, "bone")
    # Lower jaw & fangs
    add_sym(nose_top, cheek_out, nose_bot, "bone")
    add_sym(cheek_out, jaw_out, nose_bot, "bone")
    add_sym(nose_bot, jaw_out, chin_c, "bone")
    add_sym(jaw_out, (w_jaw * 0.48, h_jaw, 3.0), fang_tip, "bone")

    # Recessed eye socket & nasal cavity (dark void)
    eye_in = (3.2, h_brow + 4.0, 9.2)
    eye_out = (w_brow * 0.72, h_brow + 2.5, 6.2)
    eye_bot = (w_cheek * 0.56, h_cheek - 1.5, 6.2)
    add_sym(eye_in, eye_out, eye_bot, "void")

    nasal_out = (2.8, 9.5, 8.8)
    add_sym(nose_top, nasal_out, nose_bot, "void")

    # Parametric Horns / Antlers (2-segment swept low-poly horns)
    horn_style = rng.choice(["bull", "swept", "crown"]) if rnum >= 6 else "short"
    if horn_style == "short":
        hb0 = (w_brow * 0.78, h_top + 6.0, 2.0)
        hb1 = (w_brow * 0.95, h_top + 10.0, 1.0)
        ht = (w_brow + 9.0, h_top - 4.0, 0.0)
        add_sym(hb0, ht, hb1, "horn")
    elif horn_style == "crown" and rnum >= 10:
        for idx_s, sx in enumerate([4.5, 11.5]):
            b0 = (sx - 2.5, h_top + 2.0, 3.0)
            b1 = (sx + 2.5, h_top + 3.0, 2.0)
            tip = (sx + 1.5 + idx_s * 3.0, h_top - 12.0 + idx_s * 2.0, 1.0)
            add_sym(b0, tip, b1, "horn")
    else:
        # Swept 2-segment curved horns (like reference 10C)
        hb0 = (w_brow * 0.76, h_top + 7.0, 2.5)
        hb1 = (w_brow * 0.95, h_brow - 1.0, 2.0)
        hm0 = (w_brow + 9.5, h_top + 2.0, 2.5)
        hm1 = (w_brow + 11.5, h_brow - 5.0, 1.5)
        htip = (w_brow + 6.0 + tier * 3.0, h_top - 13.0 - tier * 3.0, 1.0)
        add_sym(hb0, hm0, hb1, "horn")
        add_sym(hb1, hm0, hm1, "horn")
        add_sym(hm0, htip, hm1, "horn")

    return render_tris_svg(tris, pal, cx=50.0, cy=73.0, cull_backface=False)


# ---------------------------------------------------------------------------
# Eatscript Deck Assembly
# ---------------------------------------------------------------------------
def build_eats_deck(seed: int) -> str:
    rng = random.Random(seed)
    lines = [
        f"# SolEngine Procedural Low-Poly Scoundrel Deck (seed={seed})",
        "# Generated by tools/generate_scoundrel_deck.py",
        "# Dark charcoal + diagonal bevel frame; bottom corners clear for RPG HP/ATK/DUR badges.",
        "",
        "sol.set_theme({",
        f'    id: "scoundrel_lowpoly",',
        f'    name: "Scoundrel Low-Poly (Seed {seed})",',
        '    author: "SolEngine Procedural",',
        f'    description: "Seeded 3D-shaded low-poly dungeon deck (seed {seed}).",',
        "    corner_radius: 0.0,",
        "    version: 1",
        "})",
        "",
        "CARD_SVGS = {",
    ]

    for suit in SUITS:
        for rank in RANKS:
            rn = rank_num(rank)
            if suit == "diamonds":
                body = generate_weapon(rng, rn)
            elif suit == "hearts":
                body = generate_potion(rng, rn)
            else:
                body = generate_monster(rng, suit, rn)

            aw_suit = {"spades": "spade", "hearts": "heart", "diamonds": "diamond", "clubs": "club"}[suit]
            idx_col = "#f2d8d5" if suit in ("hearts", "diamonds") else "#e8dfd1"
            pip_svg = suit_svg(aw_suit, 11.0, 26.0, 8.2)
            # Recolor Another World suit pips for high contrast on dark charcoal (#2b303a)
            pip_svg = (
                pip_svg.replace("#1A1733", "#94a3b8")
                .replace("#2E2952", "#e2e8f0")
                .replace("#9E1B24", "#e11d48")
                .replace("#C8202A", "#fb7185")
            )
            tl_index = rank_path_svg(rank, 11.0, 14.0, 10.5, idx_col) + pip_svg

            # Dark charcoal card plate + bottom-right diagonal bevel (in SVG viewBox 0..100 x 0..140)
            card_svg = (
                "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 140'>"
                "<rect x='0' y='0' width='100' height='140' fill='#2b303a'/>"
                "<polygon points='100,82 54,140 100,140' fill='#22262f'/>"
                + body
                + tl_index
                + "</svg>"
            ).replace('"', "'")
            key = f"{suit}_{rank}"
            lines.append(f'    "{key}": "{card_svg}",')

    lines.append("}")
    lines.append("")
    lines.append(
        """def on_draw_card_face(ctx, rank, suit, w, h):
    key = suit + "_" + rank
    svg = CARD_SVGS[key]
    ctx.draw_svg(svg, 0.0, 0.0, w, h)

def on_draw_card_back(ctx, w, h):
    back_svg = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 140'><rect x='0' y='0' width='100' height='140' fill='#242832'/><polygon points='100,82 54,140 100,140' fill='#1c2028'/><polygon points='50,42 32,70 50,70' fill='#64748b'/><polygon points='50,42 68,70 50,70' fill='#334155'/><polygon points='32,70 50,70 50,98' fill='#475569'/><polygon points='68,70 50,70 50,98' fill='#1e293b'/><polygon points='50,56 42,70 50,84 58,70' fill='#f59e0b'/></svg>"
    ctx.draw_svg(back_svg, 0.0, 0.0, w, h)
    ctx.stroke_rect(w * 0.055, h * 0.04, w * 0.89, h * 0.92, "#333946", 1.2)
"""
    )
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description="Generate a seeded low-poly Scoundrel .eats deck.")
    ap.add_argument("--seed", type=int, default=42, help="Procedural generation seed")
    ap.add_argument("--out", type=Path, default=Path("decks/scoundrel_lowpoly.eats"), help="Output .eats path")
    args = ap.parse_args()

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(build_eats_deck(args.seed), encoding="utf-8")
    print(f"[ScoundrelGen] Wrote {args.out} (seed={args.seed}, 52 faces + back)")


if __name__ == "__main__":
    main()
