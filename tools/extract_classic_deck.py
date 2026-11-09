#!/usr/bin/env python3
"""
tools/extract_classic_deck.py
Extracts CC0 vector assets from decks/classic_svg.svg and generates:
include/solengine/classic_deck_symbols.hpp
"""

import os
import re
import zlib
import xml.etree.ElementTree as ET
import numpy as np

def parse_svg_path_to_polygons(d_str, bezier_steps=6):
    tokens = re.findall(r'[MmLlCcZz]|[-+]?[0-9]*\.?[0-9]+(?:[eE][-+]?[0-9]+)?', d_str)
    polygons = []
    current_poly = []
    curr_x, curr_y = 0.0, 0.0
    start_x, start_y = 0.0, 0.0
    
    i = 0
    cmd = ''
    while i < len(tokens):
        t = tokens[i]
        if t in 'MmLlCcZz':
            cmd = t
            i += 1
            if cmd in 'Zz':
                if len(current_poly) > 2:
                    polygons.append(current_poly)
                current_poly = []
                curr_x, curr_y = start_x, start_y
                continue
        
        if cmd == 'M':
            curr_x, curr_y = float(tokens[i]), float(tokens[i+1])
            start_x, start_y = curr_x, curr_y
            if current_poly:
                polygons.append(current_poly)
            current_poly = [(curr_x, curr_y)]
            i += 2
            cmd = 'L'
        elif cmd == 'L':
            curr_x, curr_y = float(tokens[i]), float(tokens[i+1])
            current_poly.append((curr_x, curr_y))
            i += 2
        elif cmd == 'C':
            p0 = np.array([curr_x, curr_y])
            p1 = np.array([float(tokens[i]), float(tokens[i+1])])
            p2 = np.array([float(tokens[i+2]), float(tokens[i+3])])
            p3 = np.array([float(tokens[i+4]), float(tokens[i+5])])
            t_vals = np.linspace(0, 1, bezier_steps + 1)[1:]
            for t in t_vals:
                pt = (1-t)**3 * p0 + 3*(1-t)**2 * t * p1 + 3*(1-t)*t**2 * p2 + t**3 * p3
                current_poly.append((float(pt[0]), float(pt[1])))
            curr_x, curr_y = float(p3[0]), float(p3[1])
            i += 6
        else:
            i += 1
    if len(current_poly) > 2:
        polygons.append(current_poly)
    return polygons

def is_point_in_triangle(p, a, b, c):
    def sign(p1, p2, p3):
        return (p1[0] - p3[0]) * (p2[1] - p3[1]) - (p2[0] - p3[0]) * (p1[1] - p3[1])
    d1 = sign(p, a, b)
    d2 = sign(p, b, c)
    d3 = sign(p, c, a)
    has_neg = (d1 < 0) or (d2 < 0) or (d3 < 0)
    has_pos = (d1 > 0) or (d2 > 0) or (d3 > 0)
    return not (has_neg and has_pos)

def triangulate_polygon(pts):
    pts = list(pts)
    n = len(pts)
    if n < 3:
        return []
    area = 0.0
    for i in range(n):
        j = (i + 1) % n
        area += pts[i][0] * pts[j][1] - pts[j][0] * pts[i][1]
    if abs(area) < 1e-9:
        return []
    if area < 0:
        pts = pts[::-1]
    
    indices = list(range(len(pts)))
    triangles = []
    
    max_iters = len(indices) * len(indices)
    while len(indices) > 2 and max_iters > 0:
        max_iters -= 1
        m = len(indices)
        ear_found = False
        for i in range(m):
            prev_idx = indices[(i - 1 + m) % m]
            curr_idx = indices[i]
            next_idx = indices[(i + 1) % m]
            
            a = pts[prev_idx]
            b = pts[curr_idx]
            c = pts[next_idx]
            
            cross = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
            if cross <= 1e-9:
                continue
            
            ear = True
            for j in range(m):
                if j in ((i - 1 + m) % m, i, (i + 1) % m):
                    continue
                p = pts[indices[j]]
                if is_point_in_triangle(p, a, b, c):
                    ear = False
                    break
            if ear:
                triangles.append((a, b, c))
                indices.pop(i)
                ear_found = True
                break
        if not ear_found:
            break
    return triangles

def normalize_svg_path(d_str):
    nums = [float(x) for x in re.findall(r'[-+]?[0-9]*\.?[0-9]+(?:[eE][-+]?[0-9]+)?', d_str)]
    xs = nums[0::2]
    ys = nums[1::2]
    min_x, max_x = min(xs), max(xs)
    min_y, max_y = min(ys), max(ys)
    cx = (min_x + max_x) * 0.5
    cy = (min_y + max_y) * 0.5
    scale = 1.0 / max((max_x - min_x) * 0.5, (max_y - min_y) * 0.5)
    
    tokens = re.split(r'([MmLlCcZz]|,|-|\s+)', d_str)
    out = []
    is_x = True
    for tok in tokens:
        if re.match(r'^[-+]?[0-9]*\.?[0-9]+(?:[eE][-+]?[0-9]+)?$', tok):
            val = float(tok)
            if is_x:
                norm_val = (val - cx) * scale
                out.append(f'{norm_val:.4f}')
                is_x = False
            else:
                norm_val = (val - cy) * scale
                out.append(f'{norm_val:.4f}')
                is_x = True
        else:
            if any(c in tok for c in 'MmLlCcZz'):
                if any(c in tok for c in 'Zz'):
                    is_x = True
            out.append(tok)
    return ''.join(out)

def main():
    svg_path = 'decks/classic_svg.svg'
    out_header = 'include/solengine/classic_deck_symbols.hpp'
    
    if not os.path.exists(svg_path):
        print(f'Error: {svg_path} not found')
        return
    
    tree = ET.parse(svg_path)
    root = tree.getroot()
    
    suits_info = {
        'Spades': (0, 788),
        'Hearts': (13, 828),
        'Clubs': (26, 1042),
        'Diamonds': (39, 198)
    }
    
    triangles_by_suit = {}
    norm_paths_by_suit = {}
    
    for sname, (cidx, plen) in suits_info.items():
        card = root[cidx]
        for el in card.iter():
            if el.tag.endswith('path') and len(el.attrib.get('d', '')) == plen:
                raw_d = el.attrib['d']
                norm_d = normalize_svg_path(raw_d)
                norm_paths_by_suit[sname] = norm_d
                
                polys = parse_svg_path_to_polygons(raw_d, bezier_steps=6)
                all_pts = [pt for poly in polys for pt in poly]
                min_x = min(p[0] for p in all_pts)
                max_x = max(p[0] for p in all_pts)
                min_y = min(p[1] for p in all_pts)
                max_y = max(p[1] for p in all_pts)
                cx = (min_x + max_x) * 0.5
                cy = (min_y + max_y) * 0.5
                scale = 1.0 / max((max_x - min_x) * 0.5, (max_y - min_y) * 0.5)
                
                norm_polys = [[((p[0] - cx) * scale, (p[1] - cy) * scale) for p in poly] for poly in polys]
                tris = []
                for poly in norm_polys:
                    tris.extend(triangulate_polygon(poly))
                triangles_by_suit[sname] = tris
                print(f'Extracted {sname}: {len(tris)} triangles')
                break
                
    # Generate C++ Header
    lines = [
        '#ifndef SOLENGINE_CLASSIC_DECK_SYMBOLS_HPP',
        '#define SOLENGINE_CLASSIC_DECK_SYMBOLS_HPP',
        '',
        '#include <array>',
        '#include <cstdint>',
        '#include <string_view>',
        '#include "solengine/solitaire_board.hpp"',
        '#include "eatsbits/ui/batch_renderer_2d.hpp"',
        '',
        'namespace solengine {',
        '',
        'struct UnitTriangle {',
        '    float x0, y0;',
        '    float x1, y1;',
        '    float x2, y2;',
        '};',
        '',
        'struct PipLayoutItem {',
        '    float x;        // Normalized [0.0 .. 1.0] across card width',
        '    float y;        // Normalized [0.0 .. 1.0] across card height',
        '    float scale;    // Relative scale multiplier (default 1.0)',
        '    bool flipY;     // Inverted vertically (for two-way symmetry)',
        '};',
        ''
    ]
    
    # Write normalized SVG path constants
    for sname in ['Spades', 'Hearts', 'Clubs', 'Diamonds']:
        lines.append(f'inline constexpr std::string_view kClassicSuitPath{sname} = "{norm_paths_by_suit[sname]}";')
    lines.append('')
    
    # Write unit triangle arrays
    for sname in ['Spades', 'Hearts', 'Clubs', 'Diamonds']:
        tris = triangles_by_suit[sname]
        lines.append(f'inline constexpr std::array<UnitTriangle, {len(tris)}> kClassicSuitTriangles{sname} = {{{{')
        for t in tris:
            a, b, c = t
            lines.append(f'    {{ {a[0]:.5f}f, {a[1]:.5f}f, {b[0]:.5f}f, {b[1]:.5f}f, {c[0]:.5f}f, {c[1]:.5f}f }},')
        lines.append('}};\n')
        
    # Helper to get triangles for any suit
    lines.extend([
        'template<Suit S>',
        'constexpr const auto& getClassicSuitTriangles() {',
        '    if constexpr (S == Suit::Spades) return kClassicSuitTrianglesSpades;',
        '    else if constexpr (S == Suit::Hearts) return kClassicSuitTrianglesHearts;',
        '    else if constexpr (S == Suit::Clubs) return kClassicSuitTrianglesClubs;',
        '    else return kClassicSuitTrianglesDiamonds;',
        '}',
        '',
        'inline void drawClassicSuitPip(',
        '    eatsbits::ui::BatchRenderer2D& r,',
        '    Suit suit,',
        '    float cx, float cy,',
        '    float rx, float ry,',
        '    float red, float green, float blue,',
        '    float alpha = 1.0f',
        ') {',
        '    if (rx < 0.5f || ry < 0.5f) return;',
        '    auto drawTris = [&](const auto& tris) {',
        '        for (const auto& tri : tris) {',
        '            r.drawTriangle(',
        '                cx + tri.x0 * rx, cy + tri.y0 * ry,',
        '                cx + tri.x1 * rx, cy + tri.y1 * ry,',
        '                cx + tri.x2 * rx, cy + tri.y2 * ry,',
        '                red, green, blue, alpha',
        '            );',
        '        }',
        '    };',
        '    switch (suit) {',
        '        case Suit::Spades:   drawTris(kClassicSuitTrianglesSpades); break;',
        '        case Suit::Hearts:   drawTris(kClassicSuitTrianglesHearts); break;',
        '        case Suit::Clubs:    drawTris(kClassicSuitTrianglesClubs); break;',
        '        case Suit::Diamonds: drawTris(kClassicSuitTrianglesDiamonds); break;',
        '    }',
        '}',
        '',
        '// Standard Pip Layout Matrices for Ranks 1 (Ace) through 10',
        '// Coordinates are normalized to card width/height [0..1]',
        'inline const std::vector<PipLayoutItem>& getStandardPipLayout(int rank) {',
        '    static const std::vector<PipLayoutItem> kEmpty;',
        '    static const std::vector<PipLayoutItem> kAce   = { {0.50f, 0.50f, 2.20f, false} };',
        '    static const std::vector<PipLayoutItem> kTwo   = { {0.50f, 0.22f, 1.00f, false}, {0.50f, 0.78f, 1.00f, true} };',
        '    static const std::vector<PipLayoutItem> kThree = { {0.50f, 0.22f, 1.00f, false}, {0.50f, 0.50f, 1.00f, false}, {0.50f, 0.78f, 1.00f, true} };',
        '    static const std::vector<PipLayoutItem> kFour  = {',
        '        {0.32f, 0.22f, 1.00f, false}, {0.68f, 0.22f, 1.00f, false},',
        '        {0.32f, 0.78f, 1.00f, true},  {0.68f, 0.78f, 1.00f, true}',
        '    };',
        '    static const std::vector<PipLayoutItem> kFive  = {',
        '        {0.32f, 0.22f, 1.00f, false}, {0.68f, 0.22f, 1.00f, false},',
        '        {0.50f, 0.50f, 1.00f, false},',
        '        {0.32f, 0.78f, 1.00f, true},  {0.68f, 0.78f, 1.00f, true}',
        '    };',
        '    static const std::vector<PipLayoutItem> kSix   = {',
        '        {0.32f, 0.22f, 1.00f, false}, {0.68f, 0.22f, 1.00f, false},',
        '        {0.32f, 0.50f, 1.00f, false}, {0.68f, 0.50f, 1.00f, false},',
        '        {0.32f, 0.78f, 1.00f, true},  {0.68f, 0.78f, 1.00f, true}',
        '    };',
        '    static const std::vector<PipLayoutItem> kSeven = {',
        '        {0.32f, 0.22f, 1.00f, false}, {0.68f, 0.22f, 1.00f, false},',
        '        {0.50f, 0.36f, 1.00f, false},',
        '        {0.32f, 0.50f, 1.00f, false}, {0.68f, 0.50f, 1.00f, false},',
        '        {0.32f, 0.78f, 1.00f, true},  {0.68f, 0.78f, 1.00f, true}',
        '    };',
        '    static const std::vector<PipLayoutItem> kEight = {',
        '        {0.32f, 0.22f, 1.00f, false}, {0.68f, 0.22f, 1.00f, false},',
        '        {0.50f, 0.36f, 1.00f, false},',
        '        {0.32f, 0.50f, 1.00f, false}, {0.68f, 0.50f, 1.00f, false},',
        '        {0.50f, 0.64f, 1.00f, true},',
        '        {0.32f, 0.78f, 1.00f, true},  {0.68f, 0.78f, 1.00f, true}',
        '    };',
        '    static const std::vector<PipLayoutItem> kNine  = {',
        '        {0.32f, 0.20f, 0.95f, false}, {0.68f, 0.20f, 0.95f, false},',
        '        {0.32f, 0.38f, 0.95f, false}, {0.68f, 0.38f, 0.95f, false},',
        '        {0.50f, 0.50f, 0.95f, false},',
        '        {0.32f, 0.62f, 0.95f, true},  {0.68f, 0.62f, 0.95f, true},',
        '        {0.32f, 0.80f, 0.95f, true},  {0.68f, 0.80f, 0.95f, true}',
        '    };',
        '    static const std::vector<PipLayoutItem> kTen   = {',
        '        {0.32f, 0.20f, 0.92f, false}, {0.68f, 0.20f, 0.92f, false},',
        '        {0.50f, 0.30f, 0.92f, false},',
        '        {0.32f, 0.40f, 0.92f, false}, {0.68f, 0.40f, 0.92f, false},',
        '        {0.32f, 0.60f, 0.92f, true},  {0.68f, 0.60f, 0.92f, true},',
        '        {0.50f, 0.70f, 0.92f, true},',
        '        {0.32f, 0.80f, 0.92f, true},  {0.68f, 0.80f, 0.92f, true}',
        '    };',
        '    switch (rank) {',
        '        case 1:  return kAce;',
        '        case 2:  return kTwo;',
        '        case 3:  return kThree;',
        '        case 4:  return kFour;',
        '        case 5:  return kFive;',
        '        case 6:  return kSix;',
        '        case 7:  return kSeven;',
        '        case 8:  return kEight;',
        '        case 9:  return kNine;',
        '        case 10: return kTen;',
        '        default: return kEmpty;',
        '    }',
        '}',
        '',
        '} // namespace solengine',
        '',
        '#endif // SOLENGINE_CLASSIC_DECK_SYMBOLS_HPP',
        ''
    ])
    
    with open(out_header, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))
    print(f'Generated {out_header}')

if __name__ == '__main__':
    main()
