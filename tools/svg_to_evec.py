#!/usr/bin/env python3
"""
Convert SVG files (or svgz deck_blob inside .eats) into SolEngine's compact
EVEC / EVECZ binary vector format.

Usage:
  python tools/svg_to_evec.py input.svg -o output.evec
  python tools/svg_to_evec.py input.svg -o output.evecz --compress
  python tools/svg_to_evec.py decks/classic_svg.svg --update-eats decks/classic.eats
"""

import argparse
import base64
import math
import re
import struct
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path
from typing import Dict, List, Optional, Tuple

NAMED_COLORS: Dict[str, Tuple[int, int, int]] = {
    "black": (0, 0, 0),
    "white": (255, 255, 255),
    "red": (255, 0, 0),
    "green": (0, 128, 0),
    "blue": (0, 0, 255),
    "yellow": (255, 255, 0),
    "cyan": (0, 255, 255),
    "magenta": (255, 0, 255),
    "gray": (128, 128, 128),
    "grey": (128, 128, 128),
    "silver": (192, 192, 192),
    "maroon": (128, 0, 0),
    "olive": (128, 128, 0),
    "lime": (0, 255, 0),
    "aqua": (0, 255, 255),
    "teal": (0, 128, 128),
    "navy": (0, 0, 128),
    "fuchsia": (255, 0, 255),
    "purple": (128, 0, 128),
    "orange": (255, 165, 0),
    "gold": (255, 215, 0),
    "transparent": (0, 0, 0),
}

Matrix = Tuple[float, float, float, float, float, float]
IDENTITY: Matrix = (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)


def mul_mat(m1: Matrix, m2: Matrix) -> Matrix:
    a1, b1, c1, d1, e1, f1 = m1
    a2, b2, c2, d2, e2, f2 = m2
    return (
        a1 * a2 + c1 * b2,
        b1 * a2 + d1 * b2,
        a1 * c2 + c1 * d2,
        b1 * c2 + d1 * d2,
        a1 * e2 + c1 * f2 + e1,
        b1 * e2 + d1 * f2 + f1,
    )


def apply_mat(m: Matrix, x: float, y: float) -> Tuple[float, float]:
    return (m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5])


def parse_transform(s: Optional[str]) -> Matrix:
    if not s:
        return IDENTITY
    m = IDENTITY
    for kind, args_str in re.findall(r"([a-zA-Z]+)\s*\(([^)]*)\)", s):
        nums = [float(x) for x in re.findall(r"[-+]?(?:\d*\.\d+|\d+)(?:[eE][-+]?\d+)?", args_str)]
        t = IDENTITY
        k = kind.lower()
        if k == "matrix" and len(nums) >= 6:
            t = (nums[0], nums[1], nums[2], nums[3], nums[4], nums[5])
        elif k == "translate" and len(nums) >= 1:
            tx = nums[0]
            ty = nums[1] if len(nums) >= 2 else 0.0
            t = (1.0, 0.0, 0.0, 1.0, tx, ty)
        elif k == "scale" and len(nums) >= 1:
            sx = nums[0]
            sy = nums[1] if len(nums) >= 2 else sx
            t = (sx, 0.0, 0.0, sy, 0.0, 0.0)
        elif k == "rotate" and len(nums) >= 1:
            ang = math.radians(nums[0])
            ca, sa = math.cos(ang), math.sin(ang)
            if len(nums) >= 3:
                cx, cy = nums[1], nums[2]
                t = mul_mat((1.0, 0.0, 0.0, 1.0, cx, cy),
                            mul_mat((ca, sa, -sa, ca, 0.0, 0.0), (1.0, 0.0, 0.0, 1.0, -cx, -cy)))
            else:
                t = (ca, sa, -sa, ca, 0.0, 0.0)
        elif k == "skewx" and len(nums) >= 1:
            t = (1.0, 0.0, math.tan(math.radians(nums[0])), 1.0, 0.0, 0.0)
        elif k == "skewy" and len(nums) >= 1:
            t = (1.0, math.tan(math.radians(nums[0])), 0.0, 1.0, 0.0, 0.0)
        m = mul_mat(m, t)
    return m


def parse_color(val: Optional[str], opacity: float = 1.0) -> Optional[int]:
    if not val:
        return None
    v = val.strip().lower()
    if v in ("none", "transparent"):
        return None
    if v.startswith("url("):
        return 0xFF000000  # Fallback for unsupported paint servers
    r = g = b = 0
    a = 255
    if v.startswith("#"):
        h = v[1:]
        if len(h) == 3:
            r, g, b = int(h[0] * 2, 16), int(h[1] * 2, 16), int(h[2] * 2, 16)
        elif len(h) == 6:
            r, g, b = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
        elif len(h) == 8:
            r, g, b, a = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), int(h[6:8], 16)
    elif v.startswith("rgb("):
        nums = [float(x) for x in re.findall(r"[-+]?(?:\d*\.\d+|\d+)", v)]
        if len(nums) >= 3:
            r, g, b = int(nums[0]), int(nums[1]), int(nums[2])
    elif v.startswith("rgba("):
        nums = [float(x) for x in re.findall(r"[-+]?(?:\d*\.\d+|\d+)", v)]
        if len(nums) >= 4:
            r, g, b = int(nums[0]), int(nums[1]), int(nums[2])
            a = int(round(max(0.0, min(1.0, nums[3])) * 255.0))
    elif v in NAMED_COLORS:
        r, g, b = NAMED_COLORS[v]
    else:
        return None

    a = int(round(max(0.0, min(255.0, a * opacity))))
    return (a << 24) | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF)


def parse_length(val: Optional[str], default: float = 0.0) -> float:
    if not val:
        return default
    m = re.match(r"^\s*([-+]?(?:\d*\.\d+|\d+)(?:[eE][-+]?\d+)?)", val)
    return float(m.group(1)) if m else default


def parse_style_dict(style_str: Optional[str]) -> Dict[str, str]:
    out: Dict[str, str] = {}
    if not style_str:
        return out
    for part in style_str.split(";"):
        if ":" in part:
            k, v = part.split(":", 1)
            out[k.strip().lower()] = v.strip()
    return out


_PATH_TOKEN_RE = re.compile(r"([a-zA-Z])|([-+]?(?:\d*\.\d+|\d+)(?:[eE][-+]?\d+)?)")


def arc_to_cubics(x1: float, y1: float, rx: float, ry: float, phi_deg: float,
                  large_arc: int, sweep: int, x2: float, y2: float) -> List[Tuple[float, float, float, float, float, float]]:
    if x1 == x2 and y1 == y2:
        return []
    if rx == 0.0 or ry == 0.0:
        return [(x1, y1, x2, y2, x2, y2)]

    rx = abs(rx)
    ry = abs(ry)
    phi = math.radians(phi_deg % 360.0)
    cos_phi = math.cos(phi)
    sin_phi = math.sin(phi)

    dx2 = (x1 - x2) * 0.5
    dy2 = (y1 - y2) * 0.5
    x1p = cos_phi * dx2 + sin_phi * dy2
    y1p = -sin_phi * dx2 + cos_phi * dy2

    rx2 = rx * rx
    ry2 = ry * ry
    x1p2 = x1p * x1p
    y1p2 = y1p * y1p

    lam = x1p2 / rx2 + y1p2 / ry2
    if lam > 1.0:
        s = math.sqrt(lam)
        rx *= s
        ry *= s
        rx2 = rx * rx
        ry2 = ry * ry

    num = max(0.0, rx2 * ry2 - rx2 * y1p2 - ry2 * x1p2)
    den = rx2 * y1p2 + ry2 * x1p2
    factor = math.sqrt(num / den) if den > 1e-12 else 0.0
    if large_arc == sweep:
        factor = -factor

    cxp = factor * (rx * y1p) / ry
    cyp = factor * (-ry * x1p) / rx
    cx = cos_phi * cxp - sin_phi * cyp + (x1 + x2) * 0.5
    cy = sin_phi * cxp + cos_phi * cyp + (y1 + y2) * 0.5

    def vec_angle(ux: float, uy: float, vx: float, vy: float) -> float:
        dot = ux * vx + uy * vy
        mag = math.hypot(ux, uy) * math.hypot(vx, vy)
        if mag < 1e-12:
            return 0.0
        ang = math.acos(max(-1.0, min(1.0, dot / mag)))
        return -ang if (ux * vy - uy * vx) < 0.0 else ang

    theta1 = vec_angle(1.0, 0.0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dtheta = vec_angle((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if sweep == 0 and dtheta > 0:
        dtheta -= 2.0 * math.pi
    elif sweep == 1 and dtheta < 0:
        dtheta += 2.0 * math.pi

    segs = max(1, int(math.ceil(abs(dtheta) / (math.pi * 0.5))))
    delta = dtheta / segs
    t = (4.0 / 3.0) * math.tan(delta * 0.25)

    out = []
    for i in range(segs):
        t1 = theta1 + i * delta
        t2 = t1 + delta
        c1, s1 = math.cos(t1), math.sin(t1)
        c2, s2 = math.cos(t2), math.sin(t2)

        ep1x = rx * (c1 - t * s1)
        ep1y = ry * (s1 + t * c1)
        ep2x = rx * (c2 + t * s2)
        ep2y = ry * (s2 - t * c2)
        ep3x = rx * c2
        ep3y = ry * s2

        p1x = cos_phi * ep1x - sin_phi * ep1y + cx
        p1y = sin_phi * ep1x + cos_phi * ep1y + cy
        p2x = cos_phi * ep2x - sin_phi * ep2y + cx
        p2y = sin_phi * ep2x + cos_phi * ep2y + cy
        p3x = cos_phi * ep3x - sin_phi * ep3y + cx
        p3y = sin_phi * ep3x + cos_phi * ep3y + cy
        out.append((p1x, p1y, p2x, p2y, p3x, p3y))
    return out


def parse_path_d(d: str) -> List[Tuple[bool, List[Tuple]]]:
    tokens = [m.group(0) for m in _PATH_TOKEN_RE.finditer(d)]
    paths: List[Tuple[bool, List[Tuple]]] = []
    verbs: List[Tuple] = []
    closed = False

    def flush():
        nonlocal verbs, closed
        if verbs:
            paths.append((closed, verbs))
            verbs = []
            closed = False

    i = 0
    n = len(tokens)
    cmd = ""
    cx = cy = sx = sy = 0.0
    last_cx = last_cy = 0.0
    last_cmd = ""

    def next_f() -> float:
        nonlocal i
        v = float(tokens[i])
        i += 1
        return v

    def is_num(idx: int) -> bool:
        return idx < n and not tokens[idx].isalpha()

    while i < n:
        if tokens[i].isalpha():
            cmd = tokens[i]
            i += 1
        if cmd in ("M", "m"):
            rel = (cmd == "m")
            first = True
            while is_num(i):
                x, y = next_f(), next_f()
                if rel and (not first or last_cmd != ""):
                    x += cx
                    y += cy
                elif rel and first and len(paths) > 0:
                    x += cx
                    y += cy
                if first:
                    flush()
                    sx, sy = x, y
                    verbs.append((0, x, y))
                    first = False
                    cmd = "l" if rel else "L"
                else:
                    verbs.append((1, x, y))
                cx, cy = x, y
                last_cmd = "M"
        elif cmd in ("L", "l"):
            rel = (cmd == "l")
            while is_num(i):
                x, y = next_f(), next_f()
                if rel:
                    x += cx
                    y += cy
                verbs.append((1, x, y))
                cx, cy = x, y
                last_cmd = "L"
        elif cmd in ("H", "h"):
            rel = (cmd == "h")
            while is_num(i):
                x = next_f() + (cx if rel else 0.0)
                verbs.append((1, x, cy))
                cx = x
                last_cmd = "H"
        elif cmd in ("V", "v"):
            rel = (cmd == "v")
            while is_num(i):
                y = next_f() + (cy if rel else 0.0)
                verbs.append((1, cx, y))
                cy = y
                last_cmd = "V"
        elif cmd in ("C", "c"):
            rel = (cmd == "c")
            while is_num(i):
                x1, y1, x2, y2, x, y = next_f(), next_f(), next_f(), next_f(), next_f(), next_f()
                if rel:
                    x1 += cx; y1 += cy; x2 += cx; y2 += cy; x += cx; y += cy
                verbs.append((2, x1, y1, x2, y2, x, y))
                last_cx, last_cy = x2, y2
                cx, cy = x, y
                last_cmd = "C"
        elif cmd in ("S", "s"):
            rel = (cmd == "s")
            while is_num(i):
                x2, y2, x, y = next_f(), next_f(), next_f(), next_f()
                if rel:
                    x2 += cx; y2 += cy; x += cx; y += cy
                if last_cmd in ("C", "S"):
                    x1, y1 = 2.0 * cx - last_cx, 2.0 * cy - last_cy
                else:
                    x1, y1 = cx, cy
                verbs.append((2, x1, y1, x2, y2, x, y))
                last_cx, last_cy = x2, y2
                cx, cy = x, y
                last_cmd = "S"
        elif cmd in ("Q", "q"):
            rel = (cmd == "q")
            while is_num(i):
                qx, qy, x, y = next_f(), next_f(), next_f(), next_f()
                if rel:
                    qx += cx; qy += cy; x += cx; y += cy
                x1 = cx + (2.0 / 3.0) * (qx - cx)
                y1 = cy + (2.0 / 3.0) * (qy - cy)
                x2 = x + (2.0 / 3.0) * (qx - x)
                y2 = y + (2.0 / 3.0) * (qy - y)
                verbs.append((2, x1, y1, x2, y2, x, y))
                last_cx, last_cy = qx, qy
                cx, cy = x, y
                last_cmd = "Q"
        elif cmd in ("T", "t"):
            rel = (cmd == "t")
            while is_num(i):
                x, y = next_f(), next_f()
                if rel:
                    x += cx; y += cy
                if last_cmd in ("Q", "T"):
                    qx, qy = 2.0 * cx - last_cx, 2.0 * cy - last_cy
                else:
                    qx, qy = cx, cy
                x1 = cx + (2.0 / 3.0) * (qx - cx)
                y1 = cy + (2.0 / 3.0) * (qy - cy)
                x2 = x + (2.0 / 3.0) * (qx - x)
                y2 = y + (2.0 / 3.0) * (qy - y)
                verbs.append((2, x1, y1, x2, y2, x, y))
                last_cx, last_cy = qx, qy
                cx, cy = x, y
                last_cmd = "T"
        elif cmd in ("A", "a"):
            rel = (cmd == "a")
            while is_num(i):
                rx, ry, rot, large, sweep, x, y = (
                    next_f(), next_f(), next_f(), int(next_f()), int(next_f()), next_f(), next_f()
                )
                if rel:
                    x += cx; y += cy
                for cub in arc_to_cubics(cx, cy, rx, ry, rot, large, sweep, x, y):
                    verbs.append((2, *cub))
                cx, cy = x, y
                last_cmd = "A"
        elif cmd in ("Z", "z"):
            closed = True
            cx, cy = sx, sy
            last_cmd = "Z"
            flush()
        else:
            i += 1
    flush()
    return paths


def svg_to_evec_bytes(svg_text: str) -> bytes:
    root = ET.fromstring(svg_text)
    vb = root.attrib.get("viewBox")
    vx = vy = 0.0
    if vb:
        nums = [float(x) for x in re.findall(r"[-+]?(?:\d*\.\d+|\d+)(?:[eE][-+]?\d+)?", vb)]
        if len(nums) >= 4:
            vx, vy, vw, vh = nums[0], nums[1], nums[2], nums[3]
        else:
            vw = parse_length(root.attrib.get("width"), 100.0)
            vh = parse_length(root.attrib.get("height"), 100.0)
    else:
        vw = parse_length(root.attrib.get("width"), 100.0)
        vh = parse_length(root.attrib.get("height"), 100.0)

    root_mat = (1.0, 0.0, 0.0, 1.0, -vx, -vy) if (vx != 0.0 or vy != 0.0) else IDENTITY
    defs: Dict[str, ET.Element] = {}
    css_classes: Dict[str, Dict[str, str]] = {}

    for elem in root.iter():
        eid = elem.attrib.get("id")
        if eid:
            defs[eid] = elem
        tag = elem.tag.split("}")[-1].lower()
        if tag == "style" and elem.text:
            for cls_name, body in re.findall(r"\.([a-zA-Z0-9_-]+)\s*\{([^}]*)\}", elem.text):
                css_classes[cls_name] = parse_style_dict(body)

    default_style = {
        "fill": "black",
        "stroke": "none",
        "stroke-width": "1",
        "fill-rule": "nonzero",
        "stroke-linecap": "butt",
        "stroke-linejoin": "miter",
        "opacity": "1",
        "fill-opacity": "1",
        "stroke-opacity": "1",
        "display": "inline",
        "visibility": "visible",
    }

    shapes: List[Tuple[int, int, float, int, float, float, float, float, List[Tuple[bool, List[Tuple]]]]] = []

    def merge_style(parent_style: Dict[str, str], elem: ET.Element) -> Dict[str, str]:
        st = dict(parent_style)
        cls_attr = elem.attrib.get("class", "")
        for c in cls_attr.split():
            if c in css_classes:
                st.update(css_classes[c])
        for k in ("fill", "stroke", "stroke-width", "fill-rule", "stroke-linecap",
                  "stroke-linejoin", "opacity", "fill-opacity", "stroke-opacity",
                  "display", "visibility"):
            if k in elem.attrib:
                st[k] = elem.attrib[k]
        st.update(parse_style_dict(elem.attrib.get("style")))
        return st

    def emit_shape(paths: List[Tuple[bool, List[Tuple]]], mat: Matrix, st: Dict[str, str]):
        if st.get("display") == "none" or st.get("visibility") == "hidden":
            return
        op = parse_length(st.get("opacity"), 1.0)
        fop = parse_length(st.get("fill-opacity"), 1.0) * op
        sop = parse_length(st.get("stroke-opacity"), 1.0) * op

        fill_col = parse_color(st.get("fill"), fop)
        stroke_col = parse_color(st.get("stroke"), sop)
        sw = parse_length(st.get("stroke-width"), 1.0)
        mat_scale = math.sqrt(abs(mat[0] * mat[3] - mat[1] * mat[2]))
        sw_scaled = sw * mat_scale

        has_fill = fill_col is not None and ((fill_col >> 24) & 0xFF) > 0
        has_stroke = stroke_col is not None and ((stroke_col >> 24) & 0xFF) > 0 and sw_scaled > 0.0
        if not has_fill and not has_stroke:
            return

        flags = 0
        if has_fill:
            flags |= 1
        if has_stroke:
            flags |= 2
        if st.get("fill-rule", "nonzero").lower() == "evenodd":
            flags |= 4
        cap = st.get("stroke-linecap", "butt").lower()
        if cap == "round":
            flags |= (1 << 3)
        elif cap == "square":
            flags |= (2 << 3)
        join = st.get("stroke-linejoin", "miter").lower()
        if join == "round":
            flags |= (1 << 5)
        elif join == "bevel":
            flags |= (2 << 5)

        min_x = min_y = 1e30
        max_x = max_y = -1e30
        t_paths: List[Tuple[bool, List[Tuple]]] = []

        def upd(px: float, py: float):
            nonlocal min_x, min_y, max_x, max_y
            min_x = min(min_x, px)
            min_y = min(min_y, py)
            max_x = max(max_x, px)
            max_y = max(max_y, py)

        for closed, verbs in paths:
            t_verbs = []
            for v in verbs:
                if v[0] in (0, 1):
                    px, py = apply_mat(mat, v[1], v[2])
                    upd(px, py)
                    t_verbs.append((v[0], px, py))
                elif v[0] == 2:
                    p1x, p1y = apply_mat(mat, v[1], v[2])
                    p2x, p2y = apply_mat(mat, v[3], v[4])
                    p3x, p3y = apply_mat(mat, v[5], v[6])
                    upd(p1x, p1y); upd(p2x, p2y); upd(p3x, p3y)
                    t_verbs.append((2, p1x, p1y, p2x, p2y, p3x, p3y))
            if t_verbs:
                t_paths.append((closed, t_verbs))

        if not t_paths:
            return
        shapes.append((
            fill_col if fill_col is not None else 0,
            stroke_col if stroke_col is not None else 0,
            sw_scaled,
            flags,
            min_x, min_y, max_x, max_y,
            t_paths,
        ))

    def walk(elem: ET.Element, parent_mat: Matrix, parent_style: Dict[str, str]):
        tag = elem.tag.split("}")[-1].lower()
        if tag in ("defs", "style", "title", "desc", "metadata"):
            return
        st = merge_style(parent_style, elem)
        mat = mul_mat(parent_mat, parse_transform(elem.attrib.get("transform")))

        if tag in ("svg", "g", "a"):
            for child in elem:
                walk(child, mat, st)
        elif tag == "use":
            href = elem.attrib.get("href") or elem.attrib.get("{http://www.w3.org/1999/xlink}href") or ""
            if href.startswith("#") and href[1:] in defs:
                tx = parse_length(elem.attrib.get("x"), 0.0)
                ty = parse_length(elem.attrib.get("y"), 0.0)
                umat = mul_mat(mat, (1.0, 0.0, 0.0, 1.0, tx, ty))
                walk(defs[href[1:]], umat, st)
        elif tag == "path":
            d = elem.attrib.get("d", "")
            if d:
                emit_shape(parse_path_d(d), mat, st)
        elif tag == "rect":
            x = parse_length(elem.attrib.get("x"), 0.0)
            y = parse_length(elem.attrib.get("y"), 0.0)
            w = parse_length(elem.attrib.get("width"), 0.0)
            h = parse_length(elem.attrib.get("height"), 0.0)
            rx = parse_length(elem.attrib.get("rx"), -1.0)
            ry = parse_length(elem.attrib.get("ry"), -1.0)
            if rx < 0 and ry >= 0: rx = ry
            if ry < 0 and rx >= 0: ry = rx
            rx = max(0.0, min(rx if rx >= 0 else 0.0, w * 0.5))
            ry = max(0.0, min(ry if ry >= 0 else 0.0, h * 0.5))
            if w > 0 and h > 0:
                if rx == 0.0 and ry == 0.0:
                    verbs = [(0, x, y), (1, x + w, y), (1, x + w, y + h), (1, x, y + h)]
                else:
                    k = 0.5522847498
                    kx, ky = rx * k, ry * k
                    verbs = [
                        (0, x + rx, y),
                        (1, x + w - rx, y),
                        (2, x + w - rx + kx, y, x + w, y + ry - ky, x + w, y + ry),
                        (1, x + w, y + h - ry),
                        (2, x + w, y + h - ry + ky, x + w - rx + kx, y + h, x + w - rx, y + h),
                        (1, x + rx, y + h),
                        (2, x + rx - kx, y + h, x, y + h - ry + ky, x, y + h - ry),
                        (1, x, y + ry),
                        (2, x, y + ry - ky, x + rx - kx, y, x + rx, y),
                    ]
                emit_shape([(True, verbs)], mat, st)
        elif tag in ("circle", "ellipse"):
            cx = parse_length(elem.attrib.get("cx"), 0.0)
            cy = parse_length(elem.attrib.get("cy"), 0.0)
            if tag == "circle":
                rx = ry = parse_length(elem.attrib.get("r"), 0.0)
            else:
                rx = parse_length(elem.attrib.get("rx"), 0.0)
                ry = parse_length(elem.attrib.get("ry"), 0.0)
            if rx > 0 and ry > 0:
                k = 0.5522847498
                kx, ky = rx * k, ry * k
                verbs = [
                    (0, cx + rx, cy),
                    (2, cx + rx, cy + ky, cx + kx, cy + ry, cx, cy + ry),
                    (2, cx - kx, cy + ry, cx - rx, cy + ky, cx - rx, cy),
                    (2, cx - rx, cy - ky, cx - kx, cy - ry, cx, cy - ry),
                    (2, cx + kx, cy - ry, cx + rx, cy - ky, cx + rx, cy),
                ]
                emit_shape([(True, verbs)], mat, st)
        elif tag == "line":
            x1 = parse_length(elem.attrib.get("x1"), 0.0)
            y1 = parse_length(elem.attrib.get("y1"), 0.0)
            x2 = parse_length(elem.attrib.get("x2"), 0.0)
            y2 = parse_length(elem.attrib.get("y2"), 0.0)
            emit_shape([(False, [(0, x1, y1), (1, x2, y2)])], mat, st)
        elif tag in ("polyline", "polygon"):
            pts = [float(x) for x in re.findall(r"[-+]?(?:\d*\.\d+|\d+)(?:[eE][-+]?\d+)?", elem.attrib.get("points", ""))]
            if len(pts) >= 4:
                verbs = [(0, pts[0], pts[1])]
                for idx in range(2, len(pts) - 1, 2):
                    verbs.append((1, pts[idx], pts[idx + 1]))
                emit_shape([(tag == "polygon", verbs)], mat, st)

    walk(root, root_mat, default_style)

    buf = bytearray()
    buf.extend(struct.pack("<4sHHffI", b"EVEC", 1, 0, vw, vh, len(shapes)))
    for fill_col, stroke_col, sw, flags, min_x, min_y, max_x, max_y, t_paths in shapes:
        buf.extend(struct.pack("<IIfB3sffffH",
                               fill_col & 0xFFFFFFFF, stroke_col & 0xFFFFFFFF, sw,
                               flags & 0xFF, b"\x00\x00\x00",
                               min_x, min_y, max_x, max_y, len(t_paths)))
        for closed, verbs in t_paths:
            buf.extend(struct.pack("<BI", 1 if closed else 0, len(verbs)))
            for v in verbs:
                if v[0] in (0, 1):
                    buf.extend(struct.pack("<Bff", v[0], v[1], v[2]))
                else:
                    buf.extend(struct.pack("<Bffffff", 2, v[1], v[2], v[3], v[4], v[5], v[6]))
    return bytes(buf)


def main():
    ap = argparse.ArgumentParser(description="Convert SVG to EVEC/EVECZ binary vector format.")
    ap.add_argument("input", help="Input .svg file")
    ap.add_argument("-o", "--output", help="Output .evec or .evecz file")
    ap.add_argument("--compress", action="store_true", help="Zlib-compress output (EVECZ)")
    ap.add_argument("--update-eats", help="Update deck_blob & format in an .eats file")
    args = ap.parse_args()

    svg_path = Path(args.input)
    svg_text = svg_path.read_text(encoding="utf-8")
    raw_evec = svg_to_evec_bytes(svg_text)
    payload = zlib.compress(raw_evec, level=9) if (args.compress or args.update_eats) else raw_evec

    if args.output:
        Path(args.output).write_bytes(payload)
        print(f"Wrote {args.output}: SVG={len(svg_text)} B -> EVEC={len(raw_evec)} B -> out={len(payload)} B")

    if args.update_eats:
        eats_path = Path(args.update_eats)
        eats_text = eats_path.read_text(encoding="utf-8")
        b64 = base64.b64encode(payload).decode("ascii")
        eats_text = re.sub(r'deck_blob\s*=\s*"""[\s\S]*?"""', f'deck_blob = """{b64}"""', eats_text, count=1)
        eats_text = re.sub(r'format\s*:\s*"[^"]+"', 'format: "evecz"', eats_text, count=1)
        eats_path.write_text(eats_text, encoding="utf-8")
        print(f"Updated {eats_path}: EVEC={len(raw_evec)} B, EVECZ={len(payload)} B, B64={len(b64)} B")


if __name__ == "__main__":
    main()
