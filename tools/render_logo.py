#!/usr/bin/env python3
"""
SolEngine Procedural Logo Generator
Renders the signature SolEngine Clockwork Logo at optimal resolutions with transparent background.
Outputs:
  - web/app_icon.png (512x512)
  - web/icon-512.png (512x512)
  - web/icon-192.png (192x192)
  - web/favicon.ico (16, 32, 48, 64)
  - src/resources/app_icon.ico (16, 32, 48, 64, 128, 256)
  - include/solengine/ui/app_icon_data.hpp (64x64 C++ array)
  - web/solengine_logo.svg (Scalable vector master)
  - src/resources/solengine_logo.svg
"""

import math
import os
import sys
import numpy as np
from PIL import Image
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.patches as patches
from matplotlib.path import Path

def create_logo_image(canvas_size=1024, pad_ratio=0.06):
    """
    Renders the Clockwork SolEngine logo onto an RGBA canvas of size canvas_size x canvas_size
    with transparent background using Matplotlib Agg for pristine anti-aliasing.
    """
    dpi = 128
    figsize = canvas_size / dpi
    fig, ax = plt.subplots(figsize=(figsize, figsize), dpi=dpi)
    fig.patch.set_alpha(0.0)
    ax.set_facecolor('none')
    ax.axis('off')
    
    # Coordinate system: [0, 1] x [0, 1]
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    
    cx, cy = 0.5, 0.5
    R = 0.5 - pad_ratio # outer cog radius
    
    # Palette definition (Hex codes matching SolEngine ClockworkPalette)
    CHASSIS_DARK = '#262320'
    CHASSIS_MID = '#332e29'
    CHASSIS_LIGHT = '#4a423a'
    CHASSIS_BEVEL = '#61574d'
    
    BRASS_HIGHLIGHT = '#ffe6a3'
    BRASS_BRIGHT = '#f0cc5c'
    BRASS_MID = '#c79e38'
    BRASS_DARK = '#75571f'
    BRASS_DEEP = '#4a3510'
    
    COPPER_SPINE = '#f5b885'
    COPPER_BRIGHT = '#e08a57'
    COPPER_MID = '#b86638'
    COPPER_DARK = '#6b381f'
    
    FURNACE_AMBER = '#f27514'
    FURNACE_CORE = '#ffb338'
    FURNACE_DARK = '#8c290a'
    
    DIAL_PARCHMENT = '#e8e0cc'
    DIAL_TEXT = '#3d362e'
    NEEDLE_RED = '#db2e24'
    
    TWO_PI = 2.0 * math.pi
    
    # -------------------------------------------------------------
    # 1. Soft Drop Shadow underneath outer chassis
    # -------------------------------------------------------------
    shadow_circle = plt.Circle((cx + 0.008, cy - 0.012), R * 0.98, color='#000000', alpha=0.35)
    ax.add_patch(shadow_circle)
    
    # -------------------------------------------------------------
    # 2. Outer Weathered Cast-Iron Chassis Gear (20 Teeth)
    # -------------------------------------------------------------
    teeth = 20
    r_outer = R
    r_root = R * 0.84
    r_collar_out = R * 0.74
    r_collar_in = R * 0.53
    r_inner = R * 0.70
    hub_radius = r_inner * 0.45
    
    # Build complete gear polygon path (perimeter with trapezoidal teeth)
    tooth_angle = TWO_PI / teeth
    gear_pts = []
    
    for t in range(teeth):
        base_a = t * tooth_angle
        a0 = base_a
        a1 = base_a + tooth_angle * 0.22
        a2 = base_a + tooth_angle * 0.52
        a3 = base_a + tooth_angle * 0.74
        a_next = (t + 1) * tooth_angle
        
        gear_pts.append((cx + r_root * math.cos(a0), cy + r_root * math.sin(a0)))
        gear_pts.append((cx + r_outer * math.cos(a1), cy + r_outer * math.sin(a1)))
        gear_pts.append((cx + r_outer * math.cos(a2), cy + r_outer * math.sin(a2)))
        gear_pts.append((cx + r_root * math.cos(a3), cy + r_root * math.sin(a3)))
        gear_pts.append((cx + r_root * math.cos(a_next), cy + r_root * math.sin(a_next)))
        
    gear_poly = plt.Polygon(gear_pts, closed=True, facecolor=CHASSIS_MID, edgecolor=CHASSIS_DARK, linewidth=1.5, zorder=2)
    ax.add_patch(gear_poly)
    
    # Tooth edge highlights & shadows
    for t in range(teeth):
        base_a = t * tooth_angle
        a0 = base_a
        a1 = base_a + tooth_angle * 0.22
        a2 = base_a + tooth_angle * 0.52
        a3 = base_a + tooth_angle * 0.74
        
        p0 = (cx + r_root * math.cos(a0), cy + r_root * math.sin(a0))
        p1 = (cx + r_outer * math.cos(a1), cy + r_outer * math.sin(a1))
        p2 = (cx + r_outer * math.cos(a2), cy + r_outer * math.sin(a2))
        p3 = (cx + r_root * math.cos(a3), cy + r_root * math.sin(a3))
        
        ax.plot([p0[0], p1[0]], [p0[1], p1[1]], color=CHASSIS_BEVEL, lw=1.2, zorder=3)
        ax.plot([p1[0], p2[0]], [p1[1], p2[1]], color=CHASSIS_LIGHT, lw=1.2, zorder=3)
        ax.plot([p2[0], p3[0]], [p2[1], p3[1]], color=CHASSIS_DARK, lw=1.0, zorder=3)
        
    # Rim concentric outlines
    ax.add_patch(plt.Circle((cx, cy), r_root, fill=False, edgecolor=CHASSIS_LIGHT, lw=1.2, zorder=3))
    ax.add_patch(plt.Circle((cx, cy), r_inner, fill=False, edgecolor=CHASSIS_DARK, lw=1.2, zorder=3))
    
    # 6 Radial Spokes
    spoke_count = 6
    half_th = (r_inner - hub_radius) * 0.12
    for s in range(spoke_count):
        sa = s * (TWO_PI / spoke_count)
        cos_s, sin_s = math.cos(sa), math.sin(sa)
        perp_x, perp_y = -sin_s * half_th, cos_s * half_th
        
        spk_pts = [
            (cx + hub_radius * cos_s + perp_x, cy + hub_radius * sin_s + perp_y),
            (cx + r_inner * cos_s + perp_x * 0.8, cy + r_inner * sin_s + perp_y * 0.8),
            (cx + r_inner * cos_s - perp_x * 0.8, cy + r_inner * sin_s - perp_y * 0.8),
            (cx + hub_radius * cos_s - perp_x, cy + hub_radius * sin_s - perp_y),
        ]
        ax.add_patch(plt.Polygon(spk_pts, closed=True, facecolor=CHASSIS_DARK, edgecolor=CHASSIS_LIGHT, lw=1.0, zorder=3))
        
    # -------------------------------------------------------------
    # 3. Riveted Viktoria Brass Annular Collar / Bezel
    # -------------------------------------------------------------
    collar_band = plt.Circle((cx, cy), r_collar_out, facecolor=BRASS_MID, edgecolor=BRASS_DARK, lw=1.8, zorder=4)
    ax.add_patch(collar_band)
    
    ax.add_patch(plt.Circle((cx, cy), (r_collar_out + r_collar_in) * 0.5, fill=False, edgecolor=BRASS_BRIGHT, lw=1.0, alpha=0.6, zorder=4))
    
    # 4 Radial Copper Conduit Struts
    for i in range(4):
        a = (i + 0.5) * (TWO_PI / 4.0)
        cos_a, sin_a = math.cos(a), math.sin(a)
        x0, y0 = cx + cos_a * r_collar_out, cy + sin_a * r_collar_out
        x1, y1 = cx + cos_a * (r_root * 0.98), cy + sin_a * (r_root * 0.98)
        ax.plot([x0, x1], [y0, y1], color=COPPER_BRIGHT, lw=4.5, solid_capstyle='round', zorder=5)
        ax.plot([x0, x1], [y0, y1], color=COPPER_SPINE, lw=1.5, solid_capstyle='round', zorder=5)
        ax.plot([x0], [y0], marker='h', markersize=6, color=BRASS_HIGHLIGHT, markeredgecolor=BRASS_DARK, zorder=6)
        
    # 12 Circumferential Brass Rivets along the collar orbit
    num_rivets = 12
    r_orbit = (r_collar_out + r_collar_in) * 0.5
    rivet_r = R * 0.032
    for i in range(num_rivets):
        ra = i * (TWO_PI / num_rivets) + (TWO_PI / (num_rivets * 2.0))
        rx = cx + math.cos(ra) * r_orbit
        ry = cy + math.sin(ra) * r_orbit
        ax.add_patch(plt.Circle((rx + 0.002, ry - 0.003), rivet_r, color='#1a1005', alpha=0.5, zorder=5))
        ax.add_patch(plt.Circle((rx, ry), rivet_r, facecolor=BRASS_BRIGHT, edgecolor=BRASS_DEEP, lw=0.8, zorder=5))
        ax.add_patch(plt.Circle((rx - rivet_r * 0.3, ry + rivet_r * 0.3), rivet_r * 0.35, color=BRASS_HIGHLIGHT, zorder=6))
        
    # -------------------------------------------------------------
    # 4. Recessed Dark Boilerplate Well & Glowing Furnace Hearth
    # -------------------------------------------------------------
    well_circle = plt.Circle((cx, cy), r_collar_in, facecolor='#110e0c', edgecolor=BRASS_DEEP, lw=2.2, zorder=7)
    ax.add_patch(well_circle)
    
    glow_steps = 16
    for g in range(glow_steps, 0, -1):
        frac = g / float(glow_steps)
        r_glow = r_collar_in * 0.95 * frac
        alpha_glow = 0.08 * (1.0 - frac)
        color_glow = FURNACE_AMBER if frac > 0.4 else FURNACE_CORE
        ax.add_patch(plt.Circle((cx + 0.015, cy - 0.01), r_glow, color=color_glow, alpha=alpha_glow, zorder=8))
        
    ax.add_patch(plt.Circle((cx + 0.01, cy - 0.005), r_collar_in * 0.35, color=FURNACE_CORE, alpha=0.25, zorder=8))
    
    # -------------------------------------------------------------
    # 5. Horological Sub-Assembly (Secondary Cogs & Gauges)
    # -------------------------------------------------------------
    def draw_sub_gear(gx, gy, gr, gteeth, gcolor_main, gcolor_hi, gspokes=3, angle_offset=0.0, z=9):
        gro = gr
        grt = gr * 0.78
        gri = gr * 0.38
        gt_ang = TWO_PI / gteeth
        pts = []
        for i in range(gteeth):
            ba = i * gt_ang + angle_offset
            pts.append((gx + grt * math.cos(ba), gy + grt * math.sin(ba)))
            pts.append((gx + gro * math.cos(ba + gt_ang * 0.22), gy + gro * math.sin(ba + gt_ang * 0.22)))
            pts.append((gx + gro * math.cos(ba + gt_ang * 0.52), gy + gro * math.sin(ba + gt_ang * 0.52)))
            pts.append((gx + grt * math.cos(ba + gt_ang * 0.74), gy + grt * math.sin(ba + gt_ang * 0.74)))
        ax.add_patch(plt.Polygon(pts, closed=True, facecolor=gcolor_main, edgecolor=gcolor_hi, lw=1.0, zorder=z))
        ax.add_patch(plt.Circle((gx, gy), gri, facecolor='#110e0c', edgecolor=gcolor_hi, lw=0.8, zorder=z+1))
        ax.add_patch(plt.Circle((gx, gy), gri * 0.45, facecolor=gcolor_hi, zorder=z+2))
        
    draw_sub_gear(cx - R * 0.15, cy + R * 0.18, R * 0.20, 9, BRASS_MID, BRASS_HIGHLIGHT, gspokes=3, angle_offset=0.2, z=9)
    draw_sub_gear(cx + R * 0.12, cy - R * 0.17, R * 0.16, 7, COPPER_MID, COPPER_BRIGHT, gspokes=3, angle_offset=0.5, z=9)
    
    pipe_path = Path([
        (cx - R * 0.28, cy - R * 0.18),
        (cx - R * 0.36, cy - R * 0.42),
        (cx - R * 0.08, cy - R * 0.48),
        (cx + R * 0.22, cy - R * 0.38)
    ], [Path.MOVETO, Path.CURVE4, Path.CURVE4, Path.CURVE4])
    ax.add_patch(patches.PathPatch(pipe_path, fill=False, edgecolor=COPPER_MID, lw=5.0, zorder=10))
    ax.add_patch(patches.PathPatch(pipe_path, fill=False, edgecolor=COPPER_SPINE, lw=1.8, zorder=10))
    
    gauge_cx = cx + R * 0.30
    gauge_cy = cy + R * 0.56
    gauge_r = R * 0.095
    ax.add_patch(plt.Circle((gauge_cx, gauge_cy), gauge_r, facecolor=BRASS_BRIGHT, edgecolor=BRASS_DARK, lw=1.2, zorder=11))
    ax.add_patch(plt.Circle((gauge_cx, gauge_cy), gauge_r * 0.72, facecolor=DIAL_PARCHMENT, edgecolor='#221a12', lw=0.6, zorder=12))
    na = math.radians(50)
    nx = gauge_cx + math.cos(na) * (gauge_r * 0.55)
    ny = gauge_cy + math.sin(na) * (gauge_r * 0.55)
    ax.plot([gauge_cx, nx], [gauge_cy, ny], color=NEEDLE_RED, lw=1.4, zorder=13)
    ax.plot([gauge_cx], [gauge_cy], marker='o', markersize=3, color=BRASS_DARK, zorder=14)
    
    # -------------------------------------------------------------
    # 6. Iconic Victorian Riveted Brass 'S' Insignia
    # -------------------------------------------------------------
    s_scale = R * 0.78
    
    top_term  = (cx + s_scale * 0.22, cy + s_scale * 0.44)
    top_arch0 = (cx + s_scale * 0.20, cy + s_scale * 0.60)
    top_arch1 = (cx - s_scale * 0.36, cy + s_scale * 0.58)
    mid_upper = (cx - s_scale * 0.34, cy + s_scale * 0.22)
    center_pt = (cx, cy)
    mid_lower = (cx + s_scale * 0.34, cy - s_scale * 0.22)
    bot_arch0 = (cx + s_scale * 0.36, cy - s_scale * 0.58)
    bot_arch1 = (cx - s_scale * 0.20, cy - s_scale * 0.60)
    bot_term  = (cx - s_scale * 0.22, cy - s_scale * 0.44)
    
    s_verts = [
        top_term,
        top_arch0, top_arch1, mid_upper,
        (mid_upper[0] + s_scale * 0.08, center_pt[1] + s_scale * 0.08),
        (center_pt[0] - s_scale * 0.08, center_pt[1] + s_scale * 0.04),
        center_pt,
        (center_pt[0] + s_scale * 0.08, center_pt[1] - s_scale * 0.04),
        (mid_lower[0] - s_scale * 0.08, center_pt[1] - s_scale * 0.08),
        mid_lower,
        bot_arch0, bot_arch1, bot_term
    ]
    s_codes = [
        Path.MOVETO,
        Path.CURVE4, Path.CURVE4, Path.CURVE4,
        Path.CURVE4, Path.CURVE4, Path.CURVE4,
        Path.CURVE4, Path.CURVE4, Path.CURVE4,
        Path.CURVE4, Path.CURVE4, Path.CURVE4
    ]
    s_path = Path(s_verts, s_codes)
    
    shadow_off_x = 0.005
    shadow_off_y = -0.007
    s_shadow_verts = [(v[0] + shadow_off_x, v[1] + shadow_off_y) for v in s_verts]
    s_shadow_path = Path(s_shadow_verts, s_codes)
    ax.add_patch(patches.PathPatch(s_shadow_path, fill=False, edgecolor='#050403', lw=12.0, alpha=0.7, capstyle='round', zorder=15))
    
    ax.add_patch(patches.PathPatch(s_path, fill=False, edgecolor=FURNACE_AMBER, lw=15.0, alpha=0.6, capstyle='round', zorder=16))
    ax.add_patch(patches.PathPatch(s_path, fill=False, edgecolor=FURNACE_CORE, lw=10.0, alpha=0.45, capstyle='round', zorder=16))
    
    ax.add_patch(patches.PathPatch(s_path, fill=False, edgecolor=BRASS_DARK, lw=9.5, capstyle='round', zorder=17))
    ax.add_patch(patches.PathPatch(s_path, fill=False, edgecolor=BRASS_MID, lw=8.0, capstyle='round', zorder=18))
    ax.add_patch(patches.PathPatch(s_path, fill=False, edgecolor=BRASS_BRIGHT, lw=5.0, capstyle='round', zorder=19))
    
    ax.add_patch(patches.PathPatch(s_path, fill=False, edgecolor=BRASS_HIGHLIGHT, lw=2.0, capstyle='round', zorder=20))
    
    top_box_w = s_scale * 0.28
    top_box_h = s_scale * 0.26
    top_box_x = top_term[0] - top_box_w * 0.25
    top_box_y = top_term[1] - top_box_h * 0.25
    ax.add_patch(patches.FancyBboxPatch((top_box_x, top_box_y), top_box_w, top_box_h,
                                        boxstyle="round,pad=0.005,rounding_size=0.01",
                                        facecolor=BRASS_MID, edgecolor=BRASS_HIGHLIGHT, lw=1.2, zorder=21))
    ax.plot([top_box_x + 0.01, top_box_x + top_box_w - 0.01, top_box_x + 0.01, top_box_x + top_box_w - 0.01],
            [top_box_y + 0.01, top_box_y + 0.01, top_box_y + top_box_h - 0.01, top_box_y + top_box_h - 0.01],
            'o', color=BRASS_HIGHLIGHT, markersize=3, zorder=22)
            
    bot_box_w = s_scale * 0.28
    bot_box_h = s_scale * 0.26
    bot_box_x = bot_term[0] - bot_box_w * 0.75
    bot_box_y = bot_term[1] - bot_box_h * 0.75
    ax.add_patch(patches.FancyBboxPatch((bot_box_x, bot_box_y), bot_box_w, bot_box_h,
                                        boxstyle="round,pad=0.005,rounding_size=0.01",
                                        facecolor=BRASS_MID, edgecolor=BRASS_HIGHLIGHT, lw=1.2, zorder=21))
    ax.plot([bot_box_x + 0.01, bot_box_x + bot_box_w - 0.01, bot_box_x + 0.01, bot_box_x + bot_box_w - 0.01],
            [bot_box_y + 0.01, bot_box_y + 0.01, bot_box_y + bot_box_h - 0.01, bot_box_y + bot_box_h - 0.01],
            'o', color=BRASS_HIGHLIGHT, markersize=3, zorder=22)
            
    strap_w = s_scale * 0.16
    strap_h = s_scale * 0.08
    ax.add_patch(patches.FancyBboxPatch((cx - strap_w * 0.5, cy - strap_h * 0.5), strap_w, strap_h,
                                        boxstyle="round,pad=0.004,rounding_size=0.008",
                                        facecolor=BRASS_BRIGHT, edgecolor=BRASS_DARK, lw=1.0, zorder=23))
    ax.plot([cx - strap_w * 0.28, cx + strap_w * 0.28], [cy, cy], 'o', color=BRASS_HIGHLIGHT, markersize=3.5, zorder=24)
    
    fig.canvas.draw()
    rgba = np.asarray(fig.canvas.buffer_rgba())
    plt.close(fig)
    return Image.fromarray(rgba)

def generate_svg():
    cx, cy = 256.0, 256.0
    R = 230.0
    
    teeth = 20
    tooth_angle = (2.0 * math.pi) / teeth
    r_outer = R
    r_root = R * 0.84
    r_collar_out = R * 0.74
    r_collar_in = R * 0.53
    r_inner = R * 0.70
    
    gear_d = []
    for t in range(teeth):
        base_a = t * tooth_angle
        a0 = base_a
        a1 = base_a + tooth_angle * 0.22
        a2 = base_a + tooth_angle * 0.52
        a3 = base_a + tooth_angle * 0.74
        a_next = (t + 1) * tooth_angle
        
        p0 = (cx + r_root * math.cos(a0), cy + r_root * math.sin(a0))
        p1 = (cx + r_outer * math.cos(a1), cy + r_outer * math.sin(a1))
        p2 = (cx + r_outer * math.cos(a2), cy + r_outer * math.sin(a2))
        p3 = (cx + r_root * math.cos(a3), cy + r_root * math.sin(a3))
        p4 = (cx + r_root * math.cos(a_next), cy + r_root * math.sin(a_next))
        
        if t == 0:
            gear_d.append(f"M {p0[0]:.2f} {p0[1]:.2f}")
        else:
            gear_d.append(f"L {p0[0]:.2f} {p0[1]:.2f}")
        gear_d.append(f"L {p1[0]:.2f} {p1[1]:.2f}")
        gear_d.append(f"L {p2[0]:.2f} {p2[1]:.2f}")
        gear_d.append(f"L {p3[0]:.2f} {p3[1]:.2f}")
        gear_d.append(f"L {p4[0]:.2f} {p4[1]:.2f}")
    gear_d.append("Z")
    gear_path_str = " ".join(gear_d)
    
    num_rivets = 12
    r_orbit = (r_collar_out + r_collar_in) * 0.5
    rivet_r = R * 0.032
    rivets_xml = []
    for i in range(num_rivets):
        ra = i * (2.0 * math.pi / num_rivets) + (math.pi / num_rivets)
        rx = cx + math.cos(ra) * r_orbit
        ry = cy + math.sin(ra) * r_orbit
        rivets_xml.append(f'<circle cx="{rx:.2f}" cy="{ry:.2f}" r="{rivet_r:.2f}" fill="url(#brassRivetGrad)" stroke="#4a3510" stroke-width="0.8"/>')
        rivets_xml.append(f'<circle cx="{rx - rivet_r*0.3:.2f}" cy="{ry - rivet_r*0.3:.2f}" r="{rivet_r*0.3:.2f}" fill="#ffe6a3" opacity="0.9"/>')
    rivets_str = "\n    ".join(rivets_xml)
    
    struts_xml = []
    for i in range(4):
        a = (i + 0.5) * (math.pi * 0.5)
        cos_a, sin_a = math.cos(a), math.sin(a)
        x0, y0 = cx + cos_a * r_collar_out, cy + sin_a * r_collar_out
        x1, y1 = cx + cos_a * (r_root * 0.98), cy + sin_a * (r_root * 0.98)
        struts_xml.append(f'<line x1="{x0:.2f}" y1="{y0:.2f}" x2="{x1:.2f}" y2="{y1:.2f}" stroke="#b86638" stroke-width="4.5" stroke-linecap="round"/>')
        struts_xml.append(f'<line x1="{x0:.2f}" y1="{y0:.2f}" x2="{x1:.2f}" y2="{y1:.2f}" stroke="#f5b885" stroke-width="1.6" stroke-linecap="round"/>')
        struts_xml.append(f'<circle cx="{x0:.2f}" cy="{y0:.2f}" r="3.2" fill="#ffe6a3" stroke="#4a3510" stroke-width="0.8"/>')
    struts_str = "\n    ".join(struts_xml)
    
    s_scale = R * 0.78
    top_term  = (cx + s_scale * 0.22, cy - s_scale * 0.44)
    top_arch0 = (cx + s_scale * 0.20, cy - s_scale * 0.60)
    top_arch1 = (cx - s_scale * 0.36, cy - s_scale * 0.58)
    mid_upper = (cx - s_scale * 0.34, cy - s_scale * 0.22)
    center_pt = (cx, cy)
    mid_lower = (cx + s_scale * 0.34, cy + s_scale * 0.22)
    bot_arch0 = (cx + s_scale * 0.36, cy + s_scale * 0.58)
    bot_arch1 = (cx - s_scale * 0.20, cy + s_scale * 0.60)
    bot_term  = (cx - s_scale * 0.22, cy + s_scale * 0.44)
    
    s_d = (
        f"M {top_term[0]:.2f} {top_term[1]:.2f} "
        f"C {top_arch0[0]:.2f} {top_arch0[1]:.2f}, {top_arch1[0]:.2f} {top_arch1[1]:.2f}, {mid_upper[0]:.2f} {mid_upper[1]:.2f} "
        f"C {mid_upper[0] + s_scale * 0.08:.2f} {center_pt[1] - s_scale * 0.08:.2f}, {center_pt[0] - s_scale * 0.08:.2f} {center_pt[1] - s_scale * 0.04:.2f}, {center_pt[0]:.2f} {center_pt[1]:.2f} "
        f"C {center_pt[0] + s_scale * 0.08:.2f} {center_pt[1] + s_scale * 0.04:.2f}, {mid_lower[0] - s_scale * 0.08:.2f} {center_pt[1] + s_scale * 0.08:.2f}, {mid_lower[0]:.2f} {mid_lower[1]:.2f} "
        f"C {bot_arch0[0]:.2f} {bot_arch0[1]:.2f}, {bot_arch1[0]:.2f} {bot_arch1[1]:.2f}, {bot_term[0]:.2f} {bot_term[1]:.2f}"
    )
    
    svg = f'''<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" width="100%" height="100%">
  <defs>
    <radialGradient id="brassCollarGrad" cx="35%" cy="30%" r="70%">
      <stop offset="0%" stop-color="#ffe6a3"/>
      <stop offset="45%" stop-color="#f0cc5c"/>
      <stop offset="80%" stop-color="#c79e38"/>
      <stop offset="100%" stop-color="#75571f"/>
    </radialGradient>
    
    <radialGradient id="furnaceHearthGrad" cx="50%" cy="50%" r="50%">
      <stop offset="0%" stop-color="#ffb338" stop-opacity="0.95"/>
      <stop offset="35%" stop-color="#f27514" stop-opacity="0.75"/>
      <stop offset="70%" stop-color="#8c290a" stop-opacity="0.35"/>
      <stop offset="100%" stop-color="#141210" stop-opacity="0.0"/>
    </radialGradient>

    <radialGradient id="brassRivetGrad" cx="30%" cy="30%" r="70%">
      <stop offset="0%" stop-color="#ffe6a3"/>
      <stop offset="50%" stop-color="#f0cc5c"/>
      <stop offset="100%" stop-color="#75571f"/>
    </radialGradient>
    
    <radialGradient id="chassisGrad" cx="35%" cy="35%" r="65%">
      <stop offset="0%" stop-color="#4a423a"/>
      <stop offset="60%" stop-color="#332e29"/>
      <stop offset="100%" stop-color="#262320"/>
    </radialGradient>
    
    <filter id="shadowFilter" x="-10%" y="-10%" width="130%" height="130%">
      <feDropShadow dx="2" dy="4" stdDeviation="4" flood-color="#000000" flood-opacity="0.5"/>
    </filter>
  </defs>

  <circle cx="{cx + 2:.1f}" cy="{cy + 4:.1f}" r="{R * 0.98:.1f}" fill="#000000" opacity="0.3" filter="url(#shadowFilter)"/>

  <path d="{gear_path_str}" fill="url(#chassisGrad)" stroke="#1a1816" stroke-width="2"/>
  <circle cx="{cx:.1f}" cy="{cy:.1f}" r="{r_root:.1f}" fill="none" stroke="#61574d" stroke-width="1.4" opacity="0.8"/>
  <circle cx="{cx:.1f}" cy="{cy:.1f}" r="{r_inner:.1f}" fill="none" stroke="#1a1816" stroke-width="1.6"/>

  <circle cx="{cx:.1f}" cy="{cy:.1f}" r="{r_collar_out:.1f}" fill="url(#brassCollarGrad)" stroke="#75571f" stroke-width="2"/>
  <circle cx="{cx:.1f}" cy="{cy:.1f}" r="{r_orbit:.1f}" fill="none" stroke="#ffe6a3" stroke-width="1" opacity="0.5"/>

  {struts_str}
  {rivets_str}

  <circle cx="{cx:.1f}" cy="{cy:.1f}" r="{r_collar_in:.1f}" fill="#110e0c" stroke="#4a3510" stroke-width="3"/>
  <circle cx="{cx + 4:.1f}" cy="{cy + 3:.1f}" r="{r_collar_in * 0.92:.1f}" fill="url(#furnaceHearthGrad)"/>

  <circle cx="{cx - R * 0.15:.1f}" cy="{cy - R * 0.18:.1f}" r="{R * 0.18:.1f}" fill="#c79e38" stroke="#ffe6a3" stroke-width="1.2" opacity="0.9"/>
  <circle cx="{cx + R * 0.12:.1f}" cy="{cy + R * 0.17:.1f}" r="{R * 0.15:.1f}" fill="#b86638" stroke="#f5b885" stroke-width="1.2" opacity="0.85"/>

  <path d="M {cx - R*0.28:.1f} {cy + R*0.18:.1f} C {cx - R*0.36:.1f} {cy + R*0.42:.1f}, {cx - R*0.08:.1f} {cy + R*0.48:.1f}, {cx + R*0.22:.1f} {cy + R*0.38:.1f}" fill="none" stroke="#b86638" stroke-width="6" stroke-linecap="round"/>
  <path d="M {cx - R*0.28:.1f} {cy + R*0.18:.1f} C {cx - R*0.36:.1f} {cy + R*0.42:.1f}, {cx - R*0.08:.1f} {cy + R*0.48:.1f}, {cx + R*0.22:.1f} {cy + R*0.38:.1f}" fill="none" stroke="#f5b885" stroke-width="2" stroke-linecap="round"/>

  <circle cx="{cx + R*0.30:.1f}" cy="{cy - R*0.56:.1f}" r="{R*0.095:.1f}" fill="#f0cc5c" stroke="#75571f" stroke-width="1.4"/>
  <circle cx="{cx + R*0.30:.1f}" cy="{cy - R*0.56:.1f}" r="{R*0.070:.1f}" fill="#e8e0cc" stroke="#3d362e" stroke-width="0.8"/>
  <line x1="{cx + R*0.30:.1f}" y1="{cy - R*0.56:.1f}" x2="{cx + R*0.30 + R*0.05*math.cos(math.radians(-50)):.1f}" y2="{cy - R*0.56 + R*0.05*math.sin(math.radians(-50)):.1f}" stroke="#db2e24" stroke-width="1.6" stroke-linecap="round"/>
  <circle cx="{cx + R*0.30:.1f}" cy="{cy - R*0.56:.1f}" r="2.2" fill="#75571f"/>

  <path d="{s_d}" fill="none" stroke="#050403" stroke-width="22" stroke-linecap="round" opacity="0.75" transform="translate(2, 4)"/>
  <path d="{s_d}" fill="none" stroke="#f27514" stroke-width="24" stroke-linecap="round" opacity="0.6"/>
  <path d="{s_d}" fill="none" stroke="#ffb338" stroke-width="18" stroke-linecap="round" opacity="0.5"/>
  <path d="{s_d}" fill="none" stroke="#75571f" stroke-width="16" stroke-linecap="round"/>
  <path d="{s_d}" fill="none" stroke="#c79e38" stroke-width="13" stroke-linecap="round"/>
  <path d="{s_d}" fill="none" stroke="#f0cc5c" stroke-width="8" stroke-linecap="round"/>
  <path d="{s_d}" fill="none" stroke="#ffe6a3" stroke-width="3" stroke-linecap="round"/>

  <rect x="{top_term[0] - s_scale * 0.08:.1f}" y="{top_term[1] - s_scale * 0.15:.1f}" width="{s_scale * 0.28:.1f}" height="{s_scale * 0.26:.1f}" rx="4" fill="#c79e38" stroke="#ffe6a3" stroke-width="1.2"/>
  <circle cx="{top_term[0] - s_scale * 0.04:.1f}" cy="{top_term[1] - s_scale * 0.11:.1f}" r="2" fill="#ffe6a3"/>
  <circle cx="{top_term[0] + s_scale * 0.16:.1f}" cy="{top_term[1] - s_scale * 0.11:.1f}" r="2" fill="#ffe6a3"/>
  <circle cx="{top_term[0] - s_scale * 0.04:.1f}" cy="{top_term[1] + s_scale * 0.07:.1f}" r="2" fill="#ffe6a3"/>
  <circle cx="{top_term[0] + s_scale * 0.16:.1f}" cy="{top_term[1] + s_scale * 0.07:.1f}" r="2" fill="#ffe6a3"/>

  <rect x="{bot_term[0] - s_scale * 0.20:.1f}" y="{bot_term[1] - s_scale * 0.11:.1f}" width="{s_scale * 0.28:.1f}" height="{s_scale * 0.26:.1f}" rx="4" fill="#c79e38" stroke="#ffe6a3" stroke-width="1.2"/>
  <circle cx="{bot_term[0] - s_scale * 0.16:.1f}" cy="{bot_term[1] - s_scale * 0.07:.1f}" r="2" fill="#ffe6a3"/>
  <circle cx="{bot_term[0] + s_scale * 0.04:.1f}" cy="{bot_term[1] - s_scale * 0.07:.1f}" r="2" fill="#ffe6a3"/>
  <circle cx="{bot_term[0] - s_scale * 0.16:.1f}" cy="{bot_term[1] + s_scale * 0.11:.1f}" r="2" fill="#ffe6a3"/>
  <circle cx="{bot_term[0] + s_scale * 0.04:.1f}" cy="{bot_term[1] + s_scale * 0.11:.1f}" r="2" fill="#ffe6a3"/>

  <rect x="{cx - s_scale * 0.09:.1f}" y="{cy - s_scale * 0.045:.1f}" width="{s_scale * 0.18:.1f}" height="{s_scale * 0.09:.1f}" rx="3" fill="#f0cc5c" stroke="#75571f" stroke-width="1"/>
  <circle cx="{cx - s_scale * 0.05:.1f}" cy="{cy:.1f}" r="2" fill="#ffe6a3"/>
  <circle cx="{cx + s_scale * 0.05:.1f}" cy="{cy:.1f}" r="2" fill="#ffe6a3"/>
</svg>
'''
    return svg

def main():
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    print(f"Targeting repository root: {repo_root}")
    
    # 1. Render Master High-Res 1024x1024 RGBA image
    print("Rendering master anti-aliased 1024x1024 emblem with transparent background...")
    master_im = create_logo_image(1024, pad_ratio=0.06)
    
    corners = [master_im.getpixel((0,0)), master_im.getpixel((1023,0)), master_im.getpixel((0,1023)), master_im.getpixel((1023,1023))]
    print(f"Master corner alphas: {[c[3] for c in corners]} (Expected: 0)")
    
    # 2. Output 512x512 PNGs
    print("Downsampling to 512x512 with Lanczos resampling...")
    im512 = master_im.resize((512, 512), Image.Resampling.LANCZOS)
    
    app_icon_png_path = os.path.join(repo_root, "web", "app_icon.png")
    icon_512_png_path = os.path.join(repo_root, "web", "icon-512.png")
    im512.save(app_icon_png_path, "PNG", optimize=True)
    im512.save(icon_512_png_path, "PNG", optimize=True)
    print(f"  Saved -> {app_icon_png_path}")
    print(f"  Saved -> {icon_512_png_path}")
    
    # 3. Output 192x192 PNG
    print("Downsampling to 192x192 with Lanczos resampling...")
    im192 = master_im.resize((192, 192), Image.Resampling.LANCZOS)
    icon_192_png_path = os.path.join(repo_root, "web", "icon-192.png")
    im192.save(icon_192_png_path, "PNG", optimize=True)
    print(f"  Saved -> {icon_192_png_path}")
    
    # 4. Generate Multi-Resolution ICO for Windows App
    print("Generating Windows executable app_icon.ico...")
    win_ico_path = os.path.join(repo_root, "src", "resources", "app_icon.ico")
    im256 = master_im.resize((256, 256), Image.Resampling.LANCZOS)
    im256.save(
        win_ico_path,
        format="ICO",
        sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
    )
    print(f"  Saved -> {win_ico_path}")
    
    # 5. Generate Multi-Resolution favicon.ico for Web
    print("Generating Web favicon.ico...")
    web_ico_path = os.path.join(repo_root, "web", "favicon.ico")
    im64 = master_im.resize((64, 64), Image.Resampling.LANCZOS)
    im64.save(
        web_ico_path,
        format="ICO",
        sizes=[(16, 16), (32, 32), (48, 48), (64, 64)]
    )
    print(f"  Saved -> {web_ico_path}")
    
    # 6. Generate C++ app_icon_data.hpp header (64x64 raw RGBA byte array for GLFW)
    print("Generating include/solengine/ui/app_icon_data.hpp (64x64 RGBA byte array)...")
    im64_data = master_im.resize((64, 64), Image.Resampling.LANCZOS)
    rgba_bytes = im64_data.tobytes()
    
    cpp_lines = [
        "// Auto-generated SolEngine Clockwork app icon pixel data (transparent background)",
        "#pragma once",
        "#include <cstdint>",
        "",
        "namespace solengine::clockwork_icon {",
        "constexpr int kAppIconWidth = 64;",
        "constexpr int kAppIconHeight = 64;",
        "inline const uint8_t kAppIconRgba64[64 * 64 * 4] = {"
    ]
    
    row = []
    for i, b in enumerate(rgba_bytes):
        row.append(f"0x{b:02x}")
        if (i + 1) % 16 == 0:
            cpp_lines.append("    " + ",".join(row) + ",")
            row = []
    if row:
        cpp_lines.append("    " + ",".join(row) + ",")
        
    cpp_lines.append("};")
    cpp_lines.append("} // namespace solengine::clockwork_icon\n")
    
    app_icon_hpp_path = os.path.join(repo_root, "include", "solengine", "ui", "app_icon_data.hpp")
    with open(app_icon_hpp_path, "w", encoding="utf-8") as f:
        f.write("\n".join(cpp_lines))
    print(f"  Saved -> {app_icon_hpp_path}")
    
    # 7. Generate Standalone Vector SVG documents
    print("Generating Scalable Vector Graphics (SVG) master files...")
    svg_content = generate_svg()
    web_svg_path = os.path.join(repo_root, "web", "solengine_logo.svg")
    src_svg_path = os.path.join(repo_root, "src", "resources", "solengine_logo.svg")
    with open(web_svg_path, "w", encoding="utf-8") as f:
        f.write(svg_content)
    with open(src_svg_path, "w", encoding="utf-8") as f:
        f.write(svg_content)
    print(f"  Saved -> {web_svg_path}")
    print(f"  Saved -> {src_svg_path}")
    
    # 8. Sync to build-web if it exists
    build_web_dir = os.path.join(repo_root, "build-web")
    if os.path.isdir(build_web_dir):
        print(f"Synchronizing updated assets to {build_web_dir}...")
        for name in ["app_icon.png", "icon-512.png", "icon-192.png", "favicon.ico", "solengine_logo.svg"]:
            src_f = os.path.join(repo_root, "web", name)
            dst_f = os.path.join(build_web_dir, name)
            if os.path.exists(src_f):
                with open(src_f, "rb") as sf, open(dst_f, "wb") as df:
                    df.write(sf.read())
                print(f"  Updated -> {dst_f}")
                
    print("\nAll SolEngine logo assets rendered successfully with transparent backgrounds!")

if __name__ == "__main__":
    main()
