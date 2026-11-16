#!/usr/bin/env python3
"""
Extract the 12 court card illustrations directly from decks/classic_svg.svg
in pure vector space, strip small intricate hatching/filigree paths by their
rendered area in court-box pixels, remove the court-box suit symbols (to be
drawn by CardCanvas::drawSuit), and export EVECZ + an anti-aliased preview sheet.
"""

import argparse
import re
import struct
import sys
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path
from typing import List, Tuple

import cv2
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from svg_to_evec import (  # noqa: E402
    IDENTITY,
    Matrix,
    apply_mat,
    mul_mat,
    parse_transform,
    svg_to_evec_bytes,
)

SVG_NS = "{http://www.w3.org/2000/svg}"

# Matches svgCellIndexForCard() in src/render/card_texture_cache.cpp
SUIT_BASES = [
    ("spades", 0),
    ("hearts", 13),
    ("clubs", 26),
    ("diamonds", 39),
]
COURT_RANKS = [(11, "J"), (12, "Q"), (13, "K")]


def path_screen_bbox(d: str, mat: Matrix) -> Tuple[float, float, float, float]:
    nums = [float(x) for x in re.findall(r"[-+]?(?:\d*\.\d+|\d+)(?:[eE][-+]?\d+)?", d)]
    if len(nums) < 4:
        return 0.0, 0.0, 0.0, 0.0
    xs, ys = [], []
    for i in range(0, len(nums) - 1, 2):
        px, py = apply_mat(mat, nums[i], nums[i + 1])
        xs.append(px)
        ys.append(py)
    return min(xs), min(ys), max(xs), max(ys)


def extract_court_svg(
    card_g: ET.Element,
    robe_min_area: float = 150.0,
    robe_stroke_min_area: float = 180.0,
    robe_max_complexity: float = 0.72,
    face_min_area: float = 10.0,
    face_stroke_min_area: float = 10.0,
    face_stroke_scale: float = 0.55,
    strip_strokes: bool = True,
    out_w: float = 252.0,
    out_h: float = 388.0,
) -> Tuple[str, int, int]:
    """
    Find the court artwork <g> inside card_g[0][1][0] (the child with the most paths),
    take art[0] (the illustration without the art[1] border), and filter paths by
    their rendered bounding-box area in out_w x out_h space.
    """
    inner = card_g[0][1][0]
    art = max(inner, key=lambda ch: len(list(ch.iter(f"{SVG_NS}path"))))
    art_tf = art.attrib.get("transform", "")
    illus = art[0]

    scale = out_w / 525.0
    root_mat: Matrix = (scale, 0.0, 0.0, scale, -112.5 * scale, -120.0 * scale)
    root_mat = mul_mat(root_mat, parse_transform(art_tf))

    total_paths = 0
    kept_paths = 0

    def filter_elem(el: ET.Element, cur_mat: Matrix) -> ET.Element | None:
        nonlocal total_paths, kept_paths
        tag = el.tag.split("}")[-1]
        tf = el.attrib.get("transform", "")
        next_mat = mul_mat(cur_mat, parse_transform(tf)) if tf else cur_mat

        if tag == "path":
            total_paths += 1
            st = el.attrib.get("style", "")
            d = el.attrib.get("d", "")
            if not d:
                return None
            x0, y0, x1, y1 = path_screen_bbox(d, next_mat)
            area = (x1 - x0) * (y1 - y0)
            cy = 0.5 * (y0 + y1)
            in_head_zone = (cy < out_h * 0.29) or (cy > out_h * 0.71)
            cmds = len(re.findall(r"[MLCQAZmlcqaz]", d))
            complexity = cmds / max(1.0, area ** 0.5)
            is_stroke_only = ("fill:none" in st)

            if in_head_zone and face_stroke_scale != 1.0:
                st = re.sub(
                    r"stroke-width\s*:\s*([0-9.]+)(px)?",
                    lambda m: f"stroke-width:{float(m.group(1)) * face_stroke_scale:.2f}{m.group(2) or ''}",
                    st,
                )

            if is_stroke_only:
                thresh = face_stroke_min_area if in_head_zone else robe_stroke_min_area
                if area < thresh:
                    return None
                if not in_head_zone and cmds >= 34 and area < 6000.0:
                    return None
                kept_paths += 1
                out = ET.Element("path", {"style": st, "d": d})
                if tf:
                    out.attrib["transform"] = tf
                return out

            thresh = face_min_area if in_head_zone else robe_min_area
            if area < thresh:
                return None
            if not in_head_zone and cmds >= 34 and complexity > robe_max_complexity and area < 6000.0:
                return None
            kept_paths += 1
            if strip_strokes:
                st = re.sub(r"stroke[^;]*;?", "", st)
            if st and not st.endswith(";"):
                st += ";"
            if "fill:" not in st:
                st = "fill:rgb(0,0,0);" + st
            out = ET.Element("path", {"style": st, "d": d})
            if tf:
                out.attrib["transform"] = tf
            return out

        out_g = ET.Element("g")
        if tf:
            out_g.attrib["transform"] = tf
        for ch in el:
            fch = filter_elem(ch, next_mat)
            if fch is not None:
                out_g.append(fch)
        return out_g if len(out_g) > 0 else None

    filtered_illus = filter_elem(illus, root_mat)
    body = ET.tostring(filtered_illus, encoding="unicode") if filtered_illus is not None else ""

    svg = (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{out_w:.0f}" height="{out_h:.0f}" '
        f'viewBox="0 0 {out_w:.0f} {out_h:.0f}">\n'
        f'<g transform="scale({scale:.6f}) translate(-112.5,-120.0)">\n'
        f'<g transform="{art_tf}">\n'
        f"{body}\n"
        f"</g>\n</g>\n</svg>"
    )
    return svg, total_paths, kept_paths


def render_evec(evec: bytes, w: int, h: int, ss: int = 2) -> np.ndarray:
    """Decode EVEC and render supersampled anti-aliased polygons."""
    magic, ver, flags, vw, vh, num_shapes = struct.unpack_from("<4sHHffI", evec, 0)
    off = struct.calcsize("<4sHHffI")
    rw, rh = w * ss, h * ss
    canvas = np.full((rh, rw, 3), 255, dtype=np.uint8)
    sx = rw / vw
    sy = rh / vh

    for _ in range(num_shapes):
        fill_col, stroke_col, sw, sflags, pad, x0, y0, x1, y1, n_paths = struct.unpack_from("<IIfB3sffffH", evec, off)
        off += struct.calcsize("<IIfB3sffffH")
        b = fill_col & 0xFF
        g = (fill_col >> 8) & 0xFF
        r = (fill_col >> 16) & 0xFF
        a = (fill_col >> 24) & 0xFF

        polys = []
        for _ in range(n_paths):
            closed, n_verbs = struct.unpack_from("<BI", evec, off)
            off += 5
            pts = []
            cx, cy = 0.0, 0.0
            for _ in range(n_verbs):
                vtype = evec[off]
                if vtype in (0, 1):
                    _, x, y = struct.unpack_from("<Bff", evec, off)
                    off += 9
                    pts.append((x * sx, y * sy))
                    cx, cy = x, y
                else:
                    _, x1c, y1c, x2c, y2c, x3, y3 = struct.unpack_from("<Bffffff", evec, off)
                    off += 25
                    for t in np.linspace(0.125, 1.0, 8):
                        mt = 1.0 - t
                        px = mt**3 * cx + 3 * mt**2 * t * x1c + 3 * mt * t**2 * x2c + t**3 * x3
                        py = mt**3 * cy + 3 * mt**2 * t * y1c + 3 * mt * t**2 * y2c + t**3 * y3
                        pts.append((px * sx, py * sy))
                    cx, cy = x3, y3
            if len(pts) >= 3:
                polys.append(np.array(pts, dtype=np.int32))
        if a > 0 and polys:
            cv2.fillPoly(canvas, polys, (b, g, r), lineType=cv2.LINE_AA)
        sa = (stroke_col >> 24) & 0xFF
        if sa > 0 and sw > 0.0 and polys:
            sb = stroke_col & 0xFF
            sg = (stroke_col >> 8) & 0xFF
            sr = (stroke_col >> 16) & 0xFF
            thick = max(1, int(round(sw * sx)))
            for p, cl in zip(polys, [True] * len(polys)):
                cv2.polylines(canvas, [p], False, (sb, sg, sr), thickness=thick, lineType=cv2.LINE_AA)

    return cv2.resize(canvas, (w, h), interpolation=cv2.INTER_AREA) if ss > 1 else canvas


def extract_full_court_group(
    card_g: ET.Element,
    card_id: str,
    out_w: float = 252.0,
    out_h: float = 388.0,
) -> str:
    """
    Extract the full, unfiltered court illustration (art[0]) from card_g
    with all original paths, fills, and strokes intact, scaled to out_w x out_h.
    """
    inner = card_g[0][1][0]
    art = max(inner, key=lambda ch: len(list(ch.iter(f"{SVG_NS}path"))))
    art_tf = art.attrib.get("transform", "")
    illus = art[0]

    def strip_ns(el: ET.Element) -> ET.Element:
        tag = el.tag.split("}")[-1]
        out = ET.Element(tag, dict(el.attrib))
        for ch in el:
            out.append(strip_ns(ch))
        return out

    clean_illus = strip_ns(illus)
    body = ET.tostring(clean_illus, encoding="unicode")
    scale = out_w / 525.0
    return (
        f'<g id="{card_id}" clip-path="url(#cell-clip)">\n'
        f'  <g transform="scale({scale:.6f}) translate(-112.5,-120.0)">\n'
        f'    <g transform="{art_tf}">\n'
        f'      {body}\n'
        f'    </g>\n'
        f'  </g>\n'
        f'</g>'
    )


def export_inkscape_sheet(svg_in: Path, svg_out: Path) -> None:
    tree = ET.parse(svg_in)
    root = tree.getroot()

    cw, ch = 252.0, 388.0
    pad = 16.0
    cols, rows = 6, 2
    total_w = cols * (cw + pad) + pad
    total_h = rows * (ch + pad) + pad

    groups: List[str] = []
    guides: List[str] = []
    idx = 0
    for s_name, s_base in SUIT_BASES:
        for r_val, r_label in COURT_RANKS:
            cell_idx = s_base + (r_val - 1)
            c = idx % cols
            r = idx // cols
            x0 = pad + c * (cw + pad)
            y0 = pad + r * (ch + pad)
            card_id = f"{r_label}_{s_name}"
            inner_g = extract_full_court_group(root[cell_idx], card_id, cw, ch)
            groups.append(
                f'<g transform="translate({x0:.1f},{y0:.1f})">\n'
                f'<rect x="0" y="0" width="{cw:.0f}" height="{ch:.0f}" fill="#ffffff"/>\n'
                f'{inner_g}\n</g>'
            )
            guides.append(
                f'<rect id="guide_{card_id}" x="{x0:.1f}" y="{y0:.1f}" width="{cw:.0f}" height="{ch:.0f}" '
                f'fill="none" stroke="#999999" stroke-width="1"/>'
            )
            idx += 1

    out_svg = (
        f'<?xml version="1.0" encoding="UTF-8" standalone="no"?>\n'
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{total_w:.0f}" height="{total_h:.0f}" '
        f'viewBox="0 0 {total_w:.0f} {total_h:.0f}">\n'
        f'<defs>\n'
        f'  <clipPath id="cell-clip"><rect x="0" y="0" width="{cw:.0f}" height="{ch:.0f}"/></clipPath>\n'
        f'</defs>\n'
        f'<rect x="0" y="0" width="{total_w:.0f}" height="{total_h:.0f}" fill="#e6e6e6"/>\n'
        f'<g id="court_cards">\n'
        + "\n".join(groups)
        + "\n</g>\n"
        f'<g id="guides">\n'
        + "\n".join(guides)
        + "\n</g>\n"
        f'</svg>\n'
    )
    svg_out.write_text(out_svg, encoding="utf-8")
    print(f"Wrote editable Inkscape SVG sheet to {svg_out} ({len(out_svg):,d} B)")


def import_inkscape_sheet(
    svg_in: Path,
    preview_out: Path,
    top_half_only: bool = True,
    face_stroke_scale: float = 0.65,
    export_cpp: Path | None = None,
) -> None:
    tree = ET.parse(svg_in)
    root = tree.getroot()
    cw, ch = 252.0, 388.0

    # Index all <g id="J_spades"> etc.
    by_id = {}
    for el in root.iter():
        eid = el.attrib.get("id", "")
        if eid and any(eid.startswith(p) for p in ("J_", "Q_", "K_")):
            by_id[eid] = el

    total_evec = 0
    total_evecz = 0
    total_paths = 0
    previews = []
    exported_blobs = []

    for s_name, _ in SUIT_BASES:
        for _, r_label in COURT_RANKS:
            card_id = f"{r_label}_{s_name}"
            card_el = by_id.get(card_id)
            if card_el is None:
                print(f"WARNING: missing {card_id}")
                continue

            kept = 0
            def clone_filtered(el: ET.Element, cur_mat: Matrix, is_root: bool = False) -> ET.Element | None:
                nonlocal kept
                tag = el.tag.split("}")[-1]
                tf = el.attrib.get("transform", "")
                next_mat = IDENTITY if is_root else (mul_mat(cur_mat, parse_transform(tf)) if tf else cur_mat)

                if tag == "path":
                    d = el.attrib.get("d", "")
                    if not d:
                        return None
                    x0, y0, x1, y1 = path_screen_bbox(d, next_mat)
                    cy = 0.5 * (y0 + y1)
                    if top_half_only and cy > ch * 0.52:
                        return None
                    kept += 1
                    st = el.attrib.get("style", "")
                    if cy < ch * 0.29 and face_stroke_scale != 1.0:
                        st = re.sub(
                            r"stroke-width\s*:\s*([0-9.]+)(px)?",
                            lambda m: f"stroke-width:{float(m.group(1)) * face_stroke_scale:.2f}{m.group(2) or ''}",
                            st,
                        )
                    attrs = {k: v for k, v in el.attrib.items() if k in ("d", "style", "fill", "stroke", "stroke-width", "fill-rule", "transform")}
                    if st:
                        attrs["style"] = st
                    return ET.Element("path", attrs)

                out_g = ET.Element("g")
                if not is_root and tf:
                    out_g.attrib["transform"] = tf
                for ch_el in el:
                    fch = clone_filtered(ch_el, next_mat, False)
                    if fch is not None:
                        out_g.append(fch)
                return out_g if len(out_g) > 0 else None

            filtered_g = clone_filtered(card_el, IDENTITY, True)
            body = ET.tostring(filtered_g, encoding="unicode") if filtered_g is not None else ""
            svg = (
                f'<svg xmlns="http://www.w3.org/2000/svg" width="{cw:.0f}" height="{ch:.0f}" '
                f'viewBox="0 0 {cw:.0f} {ch:.0f}">\n{body}\n</svg>'
            )
            evec = svg_to_evec_bytes(svg)
            evecz = zlib.compress(evec, level=9)
            total_evec += len(evec)
            total_evecz += len(evecz)
            total_paths += kept

            img = render_evec(evec, int(cw), int(ch), ss=2)
            if top_half_only:
                # Composite 180-degree mirrored bottom half onto white background
                rot = cv2.rotate(img, cv2.ROTATE_180)
                mask = np.any(rot < 250, axis=2)
                # Keep top half on top where both overlap, or blend non-white
                top_mask = np.any(img < 250, axis=2)
                combined = img.copy()
                write_bot = mask & (~top_mask)
                combined[write_bot] = rot[write_bot]
                img = combined

            previews.append((card_id, img))
            exported_blobs.append((card_id, s_name, r_label, evecz))
            print(f"{card_id:10s}: {kept:3d} paths | evec={len(evec):6,d} B | evecz={len(evecz):6,d} B")

    print(f"TOTAL (12 edited top-halves): {total_paths} paths | EVEC={total_evec:,d} B ({total_evec/1024:.1f} KB) | EVECZ={total_evecz:,d} B ({total_evecz/1024:.1f} KB)")

    icw, ich = int(cw), int(ch)
    sheet = np.full((2 * (ich + 16), 6 * (icw + 16), 3), 230, dtype=np.uint8)
    for i, (name, img) in enumerate(previews):
        c = i % 6
        r = i // 6
        x0 = c * (icw + 16) + 8
        y0 = r * (ich + 16) + 8
        sheet[y0:y0 + ich, x0:x0 + icw] = img
        cv2.rectangle(sheet, (x0, y0), (x0 + icw - 1, y0 + ich - 1), (80, 80, 80), 1)
    cv2.imwrite(str(preview_out), sheet)
    print(f"Saved preview sheet to {preview_out}")

    if export_cpp is not None:
        lines = [
            "#pragma once",
            "// Auto-generated by tools/build_classic_minimal.py from decks/classic_courts_editable.svg",
            "// Derivative of CC0 Wikimedia classic playing cards (edited top halves, EVECZ compressed).",
            "#include <cstddef>",
            "#include <cstdint>",
            '#include "solengine/solitaire_board.hpp"',
            "",
            "namespace solengine {",
            "namespace detail {",
            "",
        ]
        for card_id, s_name, r_label, blob in exported_blobs:
            lines.append(f"static constexpr uint8_t kCourt_{card_id}[] = {{")
            for i in range(0, len(blob), 16):
                chunk = ", ".join(f"0x{b:02x}" for b in blob[i:i + 16])
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
        export_cpp.write_text("\n".join(lines), encoding="utf-8")
        print(f"Wrote C++ header {export_cpp} ({total_evecz:,d} B total EVECZ)")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--svg", type=Path, default=Path("decks/classic_svg.svg"))
    ap.add_argument("--robe-min-area", type=float, default=150.0)
    ap.add_argument("--robe-stroke-min-area", type=float, default=180.0)
    ap.add_argument("--robe-max-complexity", type=float, default=0.72)
    ap.add_argument("--face-min-area", type=float, default=10.0)
    ap.add_argument("--face-stroke-min-area", type=float, default=10.0)
    ap.add_argument("--face-stroke-scale", type=float, default=0.55)
    ap.add_argument("--keep-strokes", action="store_true")
    ap.add_argument("--preview", type=Path, default=Path("classic_minimal_preview.png"))
    ap.add_argument("--export-svg", type=Path, default=None, help="Export full-detail 6x2 Inkscape-editable SVG of the 12 courts")
    ap.add_argument("--import-svg", type=Path, default=None, help="Import edited Inkscape SVG, extract top halves to EVECZ, and preview mirrored")
    ap.add_argument("--export-cpp", type=Path, default=None, help="When used with --import-svg, write C++ header with embedded EVECZ arrays")
    args = ap.parse_args()

    if args.export_svg is not None:
        export_inkscape_sheet(args.svg, args.export_svg)
        return
    if args.import_svg is not None:
        import_inkscape_sheet(args.import_svg, args.preview, export_cpp=args.export_cpp)
        return

    tree = ET.parse(args.svg)
    root = tree.getroot()

    total_evecz = 0
    total_orig = 0
    total_kept = 0
    previews = []

    for s_name, s_base in SUIT_BASES:
        for r_val, r_label in COURT_RANKS:
            cell_idx = s_base + (r_val - 1)
            svg, orig_n, kept_n = extract_court_svg(
                root[cell_idx],
                robe_min_area=args.robe_min_area,
                robe_stroke_min_area=args.robe_stroke_min_area,
                robe_max_complexity=args.robe_max_complexity,
                face_min_area=args.face_min_area,
                face_stroke_min_area=args.face_stroke_min_area,
                face_stroke_scale=args.face_stroke_scale,
                strip_strokes=not args.keep_strokes,
            )
            evec = svg_to_evec_bytes(svg)
            evecz = zlib.compress(evec, level=9)
            total_evecz += len(evecz)
            total_orig += orig_n
            total_kept += kept_n
            img = render_evec(evec, 252, 388)
            previews.append((f"{r_label}_{s_name}", img))
            print(f"{r_label}_{s_name:8s}: {kept_n:3d}/{orig_n:3d} paths | evec={len(evec):6,d} B | evecz={len(evecz):6,d} B")

    print(f"TOTAL (12 courts): {total_kept}/{total_orig} paths | evecz = {total_evecz:,d} B ({total_evecz/1024:.1f} KB)")

    cw, ch = 252, 388
    sheet = np.full((2 * (ch + 16), 6 * (cw + 16), 3), 230, dtype=np.uint8)
    for i, (name, img) in enumerate(previews):
        c = i % 6
        r = i // 6
        x0 = c * (cw + 16) + 8
        y0 = r * (ch + 16) + 8
        sheet[y0:y0 + ch, x0:x0 + cw] = img
        cv2.rectangle(sheet, (x0, y0), (x0 + cw - 1, y0 + ch - 1), (80, 80, 80), 1)
    cv2.imwrite(str(args.preview), sheet)
    print(f"Saved preview sheet to {args.preview}")


if __name__ == "__main__":
    main()
