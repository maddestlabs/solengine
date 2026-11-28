#include "solengine/card_back.hpp"
#include "solengine/card_canvas.hpp"
#include "solengine/lowpoly_deck_generator.hpp"
#include "eatsbits/eatscript/evaluator.hpp"
#include "eatsbits/eatscript/stdlib.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>

#include "stb_image.h"
#include "nanosvg.h"
#include "nanosvgrast.h"

namespace solengine {

namespace {

void bilinearResample(const uint8_t* src, int srcW, int srcH, uint8_t* dst, int dstW, int dstH) {
    if (!src || !dst || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return;
    const float scaleX = static_cast<float>(srcW) / static_cast<float>(dstW);
    const float scaleY = static_cast<float>(srcH) / static_cast<float>(dstH);

    for (int y = 0; y < dstH; ++y) {
        float srcY = (static_cast<float>(y) + 0.5f) * scaleY - 0.5f;
        int y0 = std::clamp(static_cast<int>(std::floor(srcY)), 0, srcH - 1);
        int y1 = std::clamp(y0 + 1, 0, srcH - 1);
        float fy = srcY - std::floor(srcY);

        for (int x = 0; x < dstW; ++x) {
            float srcX = (static_cast<float>(x) + 0.5f) * scaleX - 0.5f;
            int x0 = std::clamp(static_cast<int>(std::floor(srcX)), 0, srcW - 1);
            int x1 = std::clamp(x0 + 1, 0, srcW - 1);
            float fx = srcX - std::floor(srcX);

            const uint8_t* p00 = &src[(y0 * srcW + x0) * 4];
            const uint8_t* p10 = &src[(y0 * srcW + x1) * 4];
            const uint8_t* p01 = &src[(y1 * srcW + x0) * 4];
            const uint8_t* p11 = &src[(y1 * srcW + x1) * 4];

            uint8_t* out = &dst[(y * dstW + x) * 4];
            for (int c = 0; c < 4; ++c) {
                float top = p00[c] * (1.0f - fx) + p10[c] * fx;
                float bot = p01[c] * (1.0f - fx) + p11[c] * fx;
                out[c] = static_cast<uint8_t>(std::clamp(top * (1.0f - fy) + bot * fy + 0.5f, 0.0f, 255.0f));
            }
        }
    }
}

std::string cleanNameFromId(const std::string& id) {
    if (id.empty()) return "Card Back";
    std::string name = id;
    // Replace underscores and hyphens with spaces
    for (char& c : name) {
        if (c == '_' || c == '-') c = ' ';
    }
    // Capitalize words
    bool newWord = true;
    for (char& c : name) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            newWord = true;
        } else if (newWord) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            newWord = false;
        }
    }
    return name;
}

// Pure Eatscript Bytecode Shader: Classic Sapphire (Watered Silk, Diamond Lattice & Guilloche)
static constexpr const char* kClassicSapphireShader = R"EAT(
def classic_sapphire_back(u, v, nx, ny, in_r, in_g, in_b, px, py):
    ax = abs(nx)
    ay = abs(ny)
    aa = 0.006

    # 1. Warm ivory card stock with subtle linen paper grain & soft edge bevel
    grain = (fbm(px * 0.22, py * 0.22) - 0.5) * 0.028
    linen = (sin(px * 1.45) * sin(py * 1.45)) * 0.007
    ivory_r = 0.980 + grain + linen
    ivory_g = 0.968 + grain + linen
    ivory_b = 0.942 + grain + linen

    # Subtle outer card edge darkening
    qx_o = ax - 0.94
    qy_o = ay - 1.34
    d_outer = hypot(max(qx_o, 0.0), max(qy_o, 0.0)) + min(max(qx_o, qy_o), 0.0) - 0.06
    edge_shade = smoothstep(-0.025, 0.0, d_outer) * 0.12
    ivory_r = ivory_r - edge_shade
    ivory_g = ivory_g - edge_shade
    ivory_b = ivory_b - edge_shade * 0.9

    # 2. Inner sapphire panel SDF (6.5% margin)
    qx = ax - 0.81
    qy = ay - 1.21
    d_panel = hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - 0.06
    in_panel = 1.0 - smoothstep(-aa, aa, d_panel)

    # 3. Watered-silk sapphire substrate with domain-warped moire sheen & radial vignette
    warp = fbm(nx * 2.4 + 1.7, ny * 2.4 + 3.1) * 1.6
    silk = 0.5 + 0.5 * sin((nx * 5.2 + ny * 3.4) + warp * 3.2)
    twill = 0.5 + 0.5 * sin((px + py) * 0.85)
    rad_card = hypot(nx * 0.85, ny * 0.72)
    vig = 1.0 - smoothstep(0.35, 1.35, rad_card) * 0.32
    sheen = silk * 0.11 + twill * 0.025

    pan_r = (0.062 + sheen * 0.45) * vig
    pan_g = (0.135 + sheen * 0.72) * vig
    pan_b = (0.285 + sheen * 1.15) * vig

    # 4. Crisp diagonal casino cross-hatch lattice + micro-diamond intersections
    in_field = 1.0 - smoothstep(-aa, aa, d_panel + 0.055)
    lat_a = abs(sin((nx + ny) * 26.0))
    lat_b = abs(sin((nx - ny) * 26.0))
    lat_line = max(1.0 - smoothstep(0.0, 0.095, lat_a), 1.0 - smoothstep(0.0, 0.095, lat_b))
    lat_node = (1.0 - smoothstep(0.0, 0.16, lat_a)) * (1.0 - smoothstep(0.0, 0.16, lat_b))

    # Subtle guilloche rosette wave behind center
    r_c = hypot(nx, ny * 0.82)
    ang_c = atan2(ny, nx)
    guil_wave = sin(r_c * 42.0 + sin(ang_c * 12.0) * 1.35)
    guil_ring = (1.0 - smoothstep(0.0, 0.22, abs(guil_wave))) * (1.0 - smoothstep(0.45, 0.85, r_c)) * smoothstep(0.36, 0.48, r_c)

    silver_r = 0.86
    silver_g = 0.90
    silver_b = 0.96
    pat_alpha = (lat_line * 0.15 + lat_node * 0.14 + guil_ring * 0.22) * in_field
    pan_r = mix(pan_r, silver_r, pat_alpha)
    pan_g = mix(pan_g, silver_g, pat_alpha)
    pan_b = mix(pan_b, silver_b, pat_alpha)

    # 5. Double border frame, beaded trim & 4-way corner rosettes
    b_outer = 1.0 - smoothstep(0.005, 0.013, abs(d_panel + 0.008))
    b_inner = 1.0 - smoothstep(0.003, 0.009, abs(d_panel + 0.052))

    # Beaded pearl trim between outer and inner rules
    bead_band = 1.0 - smoothstep(0.006, 0.013, abs(d_panel + 0.030))
    bead_wave = 0.5 + 0.5 * sin(nx * 54.0) * sin(ny * 54.0)
    beads = bead_band * smoothstep(0.58, 0.82, bead_wave)

    # Corner 8-petal rosettes
    cx = ax - 0.72
    cy = ay - 1.12
    cr = hypot(cx, cy)
    ca = atan2(cy, cx)
    c_petal = cr - (0.068 + 0.018 * cos(ca * 8.0))
    c_fill = 1.0 - smoothstep(-aa, aa, c_petal)
    c_ring = 1.0 - smoothstep(0.004, 0.010, abs(c_petal))
    c_dot = 1.0 - smoothstep(0.014, 0.022, cr)

    pan_r = mix(pan_r, 0.055, c_fill * 0.85)
    pan_g = mix(pan_g, 0.115, c_fill * 0.85)
    pan_b = mix(pan_b, 0.245, c_fill * 0.85)

    trim = clamp(max(max(b_outer, b_inner * 0.85), max(beads * 0.8, max(c_ring, c_dot))), 0.0, 1.0)
    pan_r = mix(pan_r, 0.93, trim)
    pan_g = mix(pan_g, 0.95, trim)
    pan_b = mix(pan_b, 0.98, trim)

    # 6. Central Oval Cartouche & Embossed Spade
    orad = hypot(nx / 0.43, ny / 0.52)
    o_shadow = (1.0 - smoothstep(0.98, 1.14, orad)) * smoothstep(0.96, 1.02, orad) * 0.42
    pan_r = pan_r * (1.0 - o_shadow)
    pan_g = pan_g * (1.0 - o_shadow)
    pan_b = pan_b * (1.0 - o_shadow)

    in_oval = 1.0 - smoothstep(0.985, 1.015, orad)
    oval_bg_r = 0.050 + (1.0 - orad) * 0.04
    oval_bg_g = 0.108 + (1.0 - orad) * 0.07
    oval_bg_b = 0.235 + (1.0 - orad) * 0.12

    # Subtle radial sunburst inside oval
    sun = (0.5 + 0.5 * cos(ang_c * 24.0)) * smoothstep(0.28, 0.78, orad) * (1.0 - smoothstep(0.76, 0.85, orad)) * 0.14
    oval_bg_r = oval_bg_r + sun * 0.7
    oval_bg_g = oval_bg_g + sun * 0.8
    oval_bg_b = oval_bg_b + sun * 1.0

    pan_r = mix(pan_r, oval_bg_r, in_oval)
    pan_g = mix(pan_g, oval_bg_g, in_oval)
    pan_b = mix(pan_b, oval_bg_b, in_oval)

    # Double silver-and-ivory oval rings
    ring1 = 1.0 - smoothstep(0.015, 0.032, abs(orad - 0.985))
    ring2 = 1.0 - smoothstep(0.009, 0.022, abs(orad - 0.845))
    ring_m = clamp(max(ring1, ring2 * 0.9), 0.0, 1.0)
    pan_r = mix(pan_r, 0.92, ring_m)
    pan_g = mix(pan_g, 0.94, ring_m)
    pan_b = mix(pan_b, 0.98, ring_m)

    # Embossed Spade emblem via built-in SDF
    d_spade = sdf_suit(nx * 3.05, ny * 3.05, 0)
    d_spade_sh = sdf_suit((nx - 0.014) * 3.05, (ny - 0.014) * 3.05, 0)
    spade_sh = (1.0 - smoothstep(-0.02, 0.05, d_spade_sh)) * 0.45
    spade_fill = 1.0 - smoothstep(-0.018, 0.018, d_spade)
    spade_hi = clamp(-d_spade * 2.2, 0.0, 0.14)
    pan_r = pan_r * (1.0 - spade_sh)
    pan_g = pan_g * (1.0 - spade_sh)
    pan_b = pan_b * (1.0 - spade_sh)
    pan_r = mix(pan_r, 0.88 + spade_hi, spade_fill)
    pan_g = mix(pan_g, 0.91 + spade_hi, spade_fill)
    pan_b = mix(pan_b, 0.96 + spade_hi, spade_fill)

    out_r = clamp(mix(ivory_r, pan_r, in_panel), 0.0, 1.0)
    out_g = clamp(mix(ivory_g, pan_g, in_panel), 0.0, 1.0)
    out_b = clamp(mix(ivory_b, pan_b, in_panel), 0.0, 1.0)
    return [out_r, out_g, out_b]
)EAT";

// Pure Eatscript Bytecode Shader: Classic Crimson (Velvet Baize, Gold Diamond Filigree & Scalloped Medallion)
static constexpr const char* kClassicCrimsonShader = R"EAT(
def classic_crimson_back(u, v, nx, ny, in_r, in_g, in_b, px, py):
    ax = abs(nx)
    ay = abs(ny)
    aa = 0.006

    # 1. Warm antique ivory stock
    grain = (fbm(px * 0.22, py * 0.22) - 0.5) * 0.028
    linen = (sin(px * 1.45) * sin(py * 1.45)) * 0.007
    ivory_r = 0.985 + grain + linen
    ivory_g = 0.965 + grain + linen
    ivory_b = 0.930 + grain + linen

    qx_o = ax - 0.94
    qy_o = ay - 1.34
    d_outer = hypot(max(qx_o, 0.0), max(qy_o, 0.0)) + min(max(qx_o, qy_o), 0.0) - 0.06
    edge_shade = smoothstep(-0.025, 0.0, d_outer) * 0.12
    ivory_r = ivory_r - edge_shade
    ivory_g = ivory_g - edge_shade
    ivory_b = ivory_b - edge_shade * 0.9

    # 2. Inner crimson velvet panel SDF
    qx = ax - 0.81
    qy = ay - 1.21
    d_panel = hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - 0.06
    in_panel = 1.0 - smoothstep(-aa, aa, d_panel)

    # 3. Plush royal crimson velvet baize with soft spotlight & warm vignette
    velvet = fbm(nx * 5.5 + 2.1, ny * 5.5 + 4.3)
    nap = (fbm(px * 0.30, py * 0.30) - 0.5) * 0.045
    spot = 1.0 - smoothstep(0.15, 1.45, hypot(nx + 0.15, ny * 0.8 + 0.15)) * 0.36
    pan_r = (0.44 + velvet * 0.13 + nap) * spot
    pan_g = (0.042 + velvet * 0.025 + nap * 0.35) * spot
    pan_b = (0.072 + velvet * 0.030 + nap * 0.40) * spot

    # 4. Gold diamond-and-star casino filigree lattice
    in_field = 1.0 - smoothstep(-aa, aa, d_panel + 0.055)
    u_d = fract((nx + ny) * 4.2) - 0.5
    v_d = fract((nx - ny) * 4.2) - 0.5
    lat_line = max(1.0 - smoothstep(0.0, 0.045, abs(u_d)), 1.0 - smoothstep(0.0, 0.045, abs(v_d)))
    star_d = abs(u_d) + abs(v_d)
    lat_star = 1.0 - smoothstep(0.06, 0.14, star_d)

    # Warm gold foil gradient
    gold_sh = 0.92 + 0.14 * sin((nx * 2.2 - ny * 1.8) * 2.5)
    gold_r = clamp(0.90 * gold_sh, 0.0, 1.0)
    gold_g = clamp(0.76 * gold_sh, 0.0, 1.0)
    gold_b = clamp(0.46 * gold_sh, 0.0, 1.0)

    pat_a = (lat_line * 0.22 + lat_star * 0.20) * in_field
    pan_r = mix(pan_r, gold_r, pat_a)
    pan_g = mix(pan_g, gold_g, pat_a)
    pan_b = mix(pan_b, gold_b, pat_a)

    # 5. Burnished gold double frame, beaded trim & corner medallions
    b_outer = 1.0 - smoothstep(0.006, 0.015, abs(d_panel + 0.008))
    b_inner = 1.0 - smoothstep(0.003, 0.010, abs(d_panel + 0.052))
    bead_band = 1.0 - smoothstep(0.006, 0.013, abs(d_panel + 0.030))
    beads = bead_band * smoothstep(0.55, 0.80, 0.5 + 0.5 * sin(nx * 50.0) * sin(ny * 50.0))

    # Corner gold diamond cartouches
    cx = ax - 0.72
    cy = ay - 1.12
    cd = abs(cx) + abs(cy)
    c_fill = 1.0 - smoothstep(0.072, 0.084, cd)
    c_ring = 1.0 - smoothstep(0.005, 0.012, abs(cd - 0.078))
    c_dot = 1.0 - smoothstep(0.022, 0.032, cd)

    pan_r = mix(pan_r, 0.34, c_fill * 0.85)
    pan_g = mix(pan_g, 0.03, c_fill * 0.85)
    pan_b = mix(pan_b, 0.05, c_fill * 0.85)

    trim = clamp(max(max(b_outer, b_inner * 0.85), max(beads * 0.85, max(c_ring, c_dot))), 0.0, 1.0)
    pan_r = mix(pan_r, gold_r, trim)
    pan_g = mix(pan_g, gold_g, trim)
    pan_b = mix(pan_b, gold_b, trim)

    # 6. Central Scalloped Gold Cartouche & Embossed Diamond
    ang = atan2(ny, nx)
    orad = hypot(nx / 0.44, ny / 0.53)
    scallop = orad - 0.032 * cos(ang * 16.0)

    o_shadow = (1.0 - smoothstep(0.98, 1.14, scallop)) * smoothstep(0.95, 1.02, scallop) * 0.48
    pan_r = pan_r * (1.0 - o_shadow)
    pan_g = pan_g * (1.0 - o_shadow)
    pan_b = pan_b * (1.0 - o_shadow)

    in_med = 1.0 - smoothstep(0.985, 1.015, scallop)
    med_r = 0.36 + (1.0 - orad) * 0.14
    med_g = 0.030 + (1.0 - orad) * 0.025
    med_b = 0.052 + (1.0 - orad) * 0.030

    sun = (0.5 + 0.5 * cos(ang * 16.0)) * smoothstep(0.25, 0.78, orad) * (1.0 - smoothstep(0.76, 0.86, orad)) * 0.16
    med_r = med_r + sun * 0.85
    med_g = med_g + sun * 0.65
    med_b = med_b + sun * 0.30

    pan_r = mix(pan_r, med_r, in_med)
    pan_g = mix(pan_g, med_g, in_med)
    pan_b = mix(pan_b, med_b, in_med)

    ring1 = 1.0 - smoothstep(0.015, 0.032, abs(scallop - 0.985))
    ring2 = 1.0 - smoothstep(0.010, 0.022, abs(orad - 0.825))
    ring_m = clamp(max(ring1, ring2 * 0.9), 0.0, 1.0)
    pan_r = mix(pan_r, gold_r, ring_m)
    pan_g = mix(pan_g, gold_g, ring_m)
    pan_b = mix(pan_b, gold_b, ring_m)

    # Embossed Gold Diamond emblem via built-in SDF
    d_dia = sdf_suit(nx * 2.95, ny * 2.95, 2)
    d_dia_sh = sdf_suit((nx - 0.014) * 2.95, (ny - 0.014) * 2.95, 2)
    dia_sh = (1.0 - smoothstep(-0.02, 0.05, d_dia_sh)) * 0.45
    dia_fill = 1.0 - smoothstep(-0.018, 0.018, d_dia)
    dia_facet = 0.90 + 0.16 * sign(nx) * sign(ny) + clamp(-d_dia * 1.8, 0.0, 0.12)
    pan_r = pan_r * (1.0 - dia_sh)
    pan_g = pan_g * (1.0 - dia_sh)
    pan_b = pan_b * (1.0 - dia_sh)
    pan_r = mix(pan_r, clamp(gold_r * dia_facet, 0.0, 1.0), dia_fill)
    pan_g = mix(pan_g, clamp(gold_g * dia_facet, 0.0, 1.0), dia_fill)
    pan_b = mix(pan_b, clamp(gold_b * dia_facet, 0.0, 1.0), dia_fill)

    out_r = clamp(mix(ivory_r, pan_r, in_panel), 0.0, 1.0)
    out_g = clamp(mix(ivory_g, pan_g, in_panel), 0.0, 1.0)
    out_b = clamp(mix(ivory_b, pan_b, in_panel), 0.0, 1.0)
    return [out_r, out_g, out_b]
)EAT";

// Pure Eatscript Bytecode Shader: Emerald Filigree (Botanical Damask, Gold Guilloche & Club Rosette)
static constexpr const char* kEmeraldFiligreeShader = R"EAT(
def emerald_filigree_back(u, v, nx, ny, in_r, in_g, in_b, px, py):
    ax = abs(nx)
    ay = abs(ny)
    aa = 0.006

    # 1. Warm ivory parchment border
    grain = (fbm(px * 0.22, py * 0.22) - 0.5) * 0.028
    linen = (sin(px * 1.45) * sin(py * 1.45)) * 0.007
    ivory_r = 0.978 + grain + linen
    ivory_g = 0.962 + grain + linen
    ivory_b = 0.932 + grain + linen

    qx_o = ax - 0.94
    qy_o = ay - 1.34
    d_outer = hypot(max(qx_o, 0.0), max(qy_o, 0.0)) + min(max(qx_o, qy_o), 0.0) - 0.06
    edge_shade = smoothstep(-0.025, 0.0, d_outer) * 0.12
    ivory_r = ivory_r - edge_shade
    ivory_g = ivory_g - edge_shade
    ivory_b = ivory_b - edge_shade * 0.9

    # 2. Inner emerald panel SDF
    qx = ax - 0.81
    qy = ay - 1.21
    d_panel = hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - 0.06
    in_panel = 1.0 - smoothstep(-aa, aa, d_panel)

    # 3. Botanical emerald damask substrate with organic silk waves
    dam_w = fbm(nx * 3.0 + 4.2, ny * 3.0 + 1.9)
    damask = 0.5 + 0.5 * sin(nx * 9.0 + sin(ny * 7.5 + dam_w * 2.5) * 1.4)
    rad_card = hypot(nx * 0.85, ny * 0.72)
    vig = 1.0 - smoothstep(0.30, 1.35, rad_card) * 0.34

    pan_r = (0.036 + damask * 0.028 + dam_w * 0.020) * vig
    pan_g = (0.195 + damask * 0.078 + dam_w * 0.045) * vig
    pan_b = (0.145 + damask * 0.052 + dam_w * 0.032) * vig

    # 4. Delicate gold wire lattice + concentric guilloche filigree waves
    in_field = 1.0 - smoothstep(-aa, aa, d_panel + 0.055)
    lat_a = abs(sin((nx + ny) * 24.0))
    lat_b = abs(sin((nx - ny) * 24.0))
    lat_line = max(1.0 - smoothstep(0.0, 0.085, lat_a), 1.0 - smoothstep(0.0, 0.085, lat_b))

    r_c = hypot(nx, ny)
    ang = atan2(ny, nx)
    guil = sin(r_c * 38.0 + sin(ang * 10.0) * 1.4)
    guil_line = (1.0 - smoothstep(0.0, 0.20, abs(guil))) * smoothstep(0.42, 0.54, r_c) * (1.0 - smoothstep(0.82, 1.05, r_c))

    gold_sh = 0.94 + 0.12 * sin((nx * 2.0 + ny * 2.0) * 2.4)
    gold_r = clamp(0.89 * gold_sh, 0.0, 1.0)
    gold_g = clamp(0.77 * gold_sh, 0.0, 1.0)
    gold_b = clamp(0.49 * gold_sh, 0.0, 1.0)

    pat_a = (lat_line * 0.17 + guil_line * 0.25) * in_field
    pan_r = mix(pan_r, gold_r, pat_a)
    pan_g = mix(pan_g, gold_g, pat_a)
    pan_b = mix(pan_b, gold_b, pat_a)

    # 5. Antique gold frame, corner 4-petal botanical florets & beaded inner rule
    b_outer = 1.0 - smoothstep(0.006, 0.015, abs(d_panel + 0.008))
    b_inner = 1.0 - smoothstep(0.003, 0.010, abs(d_panel + 0.052))
    bead_band = 1.0 - smoothstep(0.006, 0.013, abs(d_panel + 0.030))
    beads = bead_band * smoothstep(0.56, 0.80, 0.5 + 0.5 * sin(nx * 52.0) * sin(ny * 52.0))

    cx = ax - 0.72
    cy = ay - 1.12
    cr = hypot(cx, cy)
    ca = atan2(cy, cx)
    c_floret = cr - (0.062 + 0.022 * cos(ca * 4.0 + 0.785))
    c_fill = 1.0 - smoothstep(-aa, aa, c_floret)
    c_ring = 1.0 - smoothstep(0.004, 0.011, abs(c_floret))
    c_dot = 1.0 - smoothstep(0.015, 0.024, cr)

    pan_r = mix(pan_r, 0.032, c_fill * 0.85)
    pan_g = mix(pan_g, 0.165, c_fill * 0.85)
    pan_b = mix(pan_b, 0.125, c_fill * 0.85)

    trim = clamp(max(max(b_outer, b_inner * 0.85), max(beads * 0.82, max(c_ring, c_dot))), 0.0, 1.0)
    pan_r = mix(pan_r, gold_r, trim)
    pan_g = mix(pan_g, gold_g, trim)
    pan_b = mix(pan_b, gold_b, trim)

    # 6. Central Circular Sunburst Medallion & Embossed Club
    mrad = r_c / 0.45
    m_shadow = (1.0 - smoothstep(0.98, 1.15, mrad)) * smoothstep(0.96, 1.02, mrad) * 0.44
    pan_r = pan_r * (1.0 - m_shadow)
    pan_g = pan_g * (1.0 - m_shadow)
    pan_b = pan_b * (1.0 - m_shadow)

    in_med = 1.0 - smoothstep(0.985, 1.015, mrad)
    med_r = 0.035 + (1.0 - mrad) * 0.03
    med_g = 0.180 + (1.0 - mrad) * 0.08
    med_b = 0.135 + (1.0 - mrad) * 0.06

    sun = (0.5 + 0.5 * cos(ang * 20.0)) * smoothstep(0.28, 0.76, mrad) * (1.0 - smoothstep(0.75, 0.84, mrad)) * 0.15
    med_r = med_r + sun * 0.85
    med_g = med_g + sun * 0.75
    med_b = med_b + sun * 0.42

    pan_r = mix(pan_r, med_r, in_med)
    pan_g = mix(pan_g, med_g, in_med)
    pan_b = mix(pan_b, med_b, in_med)

    ring1 = 1.0 - smoothstep(0.015, 0.032, abs(mrad - 0.985))
    ring2 = 1.0 - smoothstep(0.010, 0.022, abs(mrad - 0.835))
    ring_m = clamp(max(ring1, ring2 * 0.9), 0.0, 1.0)
    pan_r = mix(pan_r, gold_r, ring_m)
    pan_g = mix(pan_g, gold_g, ring_m)
    pan_b = mix(pan_b, gold_b, ring_m)

    # Embossed Gold Club emblem via built-in SDF
    d_club = sdf_suit(nx * 3.05, ny * 3.05 + 0.02, 3)
    d_club_sh = sdf_suit((nx - 0.014) * 3.05, (ny - 0.014) * 3.05 + 0.02, 3)
    club_sh = (1.0 - smoothstep(-0.02, 0.05, d_club_sh)) * 0.45
    club_fill = 1.0 - smoothstep(-0.018, 0.018, d_club)
    club_hi = clamp(-d_club * 2.0, 0.0, 0.14)
    pan_r = pan_r * (1.0 - club_sh)
    pan_g = pan_g * (1.0 - club_sh)
    pan_b = pan_b * (1.0 - club_sh)
    pan_r = mix(pan_r, clamp(gold_r + club_hi, 0.0, 1.0), club_fill)
    pan_g = mix(pan_g, clamp(gold_g + club_hi, 0.0, 1.0), club_fill)
    pan_b = mix(pan_b, clamp(gold_b + club_hi * 0.7, 0.0, 1.0), club_fill)

    out_r = clamp(mix(ivory_r, pan_r, in_panel), 0.0, 1.0)
    out_g = clamp(mix(ivory_g, pan_g, in_panel), 0.0, 1.0)
    out_b = clamp(mix(ivory_b, pan_b, in_panel), 0.0, 1.0)
    return [out_r, out_g, out_b]
)EAT";

// Eatscript Bytecode Shader: Obsidian Slate & Baroque Damask Substrate
static constexpr const char* kObsidianSlateShader = R"EAT(
def obsidian_slate_substrate(u, v, nx, ny, in_r, in_g, in_b, px, py):
    rad = hypot(nx * 0.85, ny * 0.72)
    vig = 1.0 - smoothstep(0.25, 1.38, rad) * 0.45
    slate = fbm(nx * 4.0 + 1.1, ny * 4.0 + 2.7)
    damask = 0.5 + 0.5 * sin(nx * 8.0 + sin(ny * 6.5 + slate * 2.0) * 1.2)
    micro = (fbm(px * 0.28, py * 0.28) - 0.5) * 0.025
    r = (0.068 + slate * 0.032 + damask * 0.016 + micro) * vig
    g = (0.074 + slate * 0.034 + damask * 0.016 + micro) * vig
    b = (0.092 + slate * 0.042 + damask * 0.018 + micro) * vig
    return [clamp(r, 0.0, 1.0), clamp(g, 0.0, 1.0), clamp(b, 0.0, 1.0)]
)EAT";

} // namespace

CardBackRegistry& CardBackRegistry::instance() {
    static CardBackRegistry sInstance;
    return sInstance;
}

bool CardBackRegistry::bakeCanvasBack(
    CardTexture& outTex,
    int cardW,
    int cardH,
    const std::function<void(CardCanvas& canvas, int w, int h)>& drawFn
) {
    if (cardW <= 0 || cardH <= 0 || !drawFn) return false;
    CardCanvas canvas(cardW, cardH);
    drawFn(canvas, cardW, cardH);
    outTex.width = cardW;
    outTex.height = cardH;
    outTex.rgba = std::move(canvas.rgba());
    return true;
}

bool CardBackRegistry::loadBitmapBack(CardTexture& outTex, const std::string& filePath, int cardW, int cardH) {
    if (cardW <= 0 || cardH <= 0) return false;
    int srcW = 0, srcH = 0, channels = 0;
    stbi_uc* data = stbi_load(filePath.c_str(), &srcW, &srcH, &channels, 4);
    if (!data) return false;

    outTex.width = cardW;
    outTex.height = cardH;
    outTex.rgba.resize(static_cast<size_t>(cardW * cardH * 4));

    if (srcW == cardW && srcH == cardH) {
        std::memcpy(outTex.rgba.data(), data, outTex.rgba.size());
    } else {
        bilinearResample(data, srcW, srcH, outTex.rgba.data(), cardW, cardH);
    }
    stbi_image_free(data);
    return true;
}

bool CardBackRegistry::loadMemoryBack(CardTexture& outTex, const uint8_t* data, size_t size, const std::string& format, int cardW, int cardH) {
    if (!data || size == 0 || cardW <= 0 || cardH <= 0) return false;
    if (format == "svg") {
        std::string svgStr(reinterpret_cast<const char*>(data), size);
        return loadSvgBack(outTex, svgStr, cardW, cardH);
    }

    int srcW = 0, srcH = 0, channels = 0;
    stbi_uc* decoded = stbi_load_from_memory(data, static_cast<int>(size), &srcW, &srcH, &channels, 4);
    if (!decoded) return false;

    outTex.width = cardW;
    outTex.height = cardH;
    outTex.rgba.resize(static_cast<size_t>(cardW * cardH * 4));

    if (srcW == cardW && srcH == cardH) {
        std::memcpy(outTex.rgba.data(), decoded, outTex.rgba.size());
    } else {
        bilinearResample(decoded, srcW, srcH, outTex.rgba.data(), cardW, cardH);
    }
    stbi_image_free(decoded);
    return true;
}

bool CardBackRegistry::loadSvgBack(CardTexture& outTex, const std::string& svgSource, int cardW, int cardH) {
    if (svgSource.empty() || cardW <= 0 || cardH <= 0) return false;
    std::vector<char> xmlCopy(svgSource.begin(), svgSource.end());
    xmlCopy.push_back('\0');

    NSVGimage* image = nsvgParse(xmlCopy.data(), "px", 96.0f);
    if (!image) return false;

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) {
        nsvgDelete(image);
        return false;
    }

    float scaleX = (image->width > 0.0f) ? (static_cast<float>(cardW) / image->width) : 1.0f;
    float scaleY = (image->height > 0.0f) ? (static_cast<float>(cardH) / image->height) : 1.0f;
    float scale = std::min(scaleX, scaleY);

    outTex.width = cardW;
    outTex.height = cardH;
    outTex.rgba.assign(static_cast<size_t>(cardW * cardH * 4), 255);

    nsvgRasterize(rast, image, 0.0f, 0.0f, scale, outTex.rgba.data(), cardW, cardH, cardW * 4);

    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);
    return true;
}

void CardBackRegistry::initialize(int cardW, int cardH) {
    if (initialized_ && currentCardW_ == cardW && currentCardH_ == cardH) return;
    initialized_ = true;
    scriptBacksScanned_ = false;
    currentCardW_ = cardW;
    currentCardH_ = cardH;
    universalBacks_.clear();

    // 1. Classic Sapphire (Pure Eatscript Shader: Watered Silk, Lattice & Guilloche)
    {
        CardBackDescriptor d;
        d.id = "classic_sapphire";
        d.displayName = "Classic Sapphire";
        d.deckOrigin = "";
        d.type = CardBackType::ProceduralDraw;
        d.generator = [](CardTexture& tex, int w, int h) -> bool {
            return CardBackRegistry::bakeCanvasBack(tex, w, h, [](CardCanvas& ctx, int, int) {
                ctx.applyShader(nullptr, kClassicSapphireShader, 1, Suit::Spades, 1.0f);
            });
        };
        d.generator(d.baseTexture, cardW, cardH);
        universalBacks_.push_back(std::move(d));
    }

    // 2. Classic Crimson (Pure Eatscript Shader: Velvet Baize, Gold Filigree & Scalloped Medallion)
    {
        CardBackDescriptor d;
        d.id = "classic_crimson";
        d.displayName = "Classic Crimson";
        d.deckOrigin = "";
        d.type = CardBackType::ProceduralDraw;
        d.generator = [](CardTexture& tex, int w, int h) -> bool {
            return CardBackRegistry::bakeCanvasBack(tex, w, h, [](CardCanvas& ctx, int, int) {
                ctx.applyShader(nullptr, kClassicCrimsonShader, 1, Suit::Diamonds, 1.0f);
            });
        };
        d.generator(d.baseTexture, cardW, cardH);
        universalBacks_.push_back(std::move(d));
    }

    // 3. Obsidian Baroque (Hybrid: Obsidian Slate Shader + Baroque Relief & Faceted Gem)
    {
        CardBackDescriptor d;
        d.id = "obsidian_baroque";
        d.displayName = "Obsidian Baroque";
        d.deckOrigin = "";
        d.type = CardBackType::ProceduralDraw;
        d.generator = [](CardTexture& tex, int w, int h) -> bool {
            return CardBackRegistry::bakeCanvasBack(tex, w, h, [](CardCanvas& ctx, int cw, int ch) {
                const float fw = static_cast<float>(cw);
                const float fh = static_cast<float>(ch);
                const float pad = fw * 0.065f;
                const float cx = fw * 0.5f;
                const float cy = fh * 0.5f;

                // Textured obsidian slate damask substrate
                ctx.applyShader(nullptr, kObsidianSlateShader, 1, Suit::Diamonds, 1.0f);

                // Gold border, 4-way baroque corner flourishes, ogee arches & faceted ruby gem
                ctx.setMaterial("gold");
                ctx.setElevation(0.85f, 1.05f);
                ctx.strokeRoundedRect(pad, pad, fw - pad * 2.0f, fh - pad * 2.0f, 4.0f, 0xFFC5A869, 2.2f);
                ctx.repeatBeads(pad + 10.0f, 2.2f, 8.0f, 0xFFC5A869);

                ctx.beginSymmetry(true, true);
                ctx.drawBaroqueCorner(pad + 6.0f, pad + 6.0f, fw * 0.16f, 0xFFC5A869, 0xFFFFF3D1);
                ctx.endSymmetry();

                ctx.setElevation(1.0f, 1.15f);
                ctx.drawBaroqueArch(cx, cy, fw * 0.35f, fh * 0.38f, "ogee_spires", 0xFFC5A869, 0xFFFFF3D1, 2.4f);

                ctx.setMaterial("gem_faceted");
                ctx.setElevation(1.2f, 1.25f);
                ctx.drawGemPip(Suit::Diamonds, cx, cy, fw * 0.24f, 0xFFC91428, 0xFFC5A869, "faceted_gem");
                ctx.resetMaterial();
                ctx.resetElevation();

                ctx.applyFinish("relief", 135.0f, 48.0f, 1.25f, 2.8f, 0xFF0A0B0E, 0xFFFFF5D6, 0.04f, 0.0f);
            });
        };
        d.generator(d.baseTexture, cardW, cardH);
        universalBacks_.push_back(std::move(d));
    }

    // 4. Emerald Filigree (Pure Eatscript Shader: Botanical Damask, Gold Guilloche & Club Rosette)
    {
        CardBackDescriptor d;
        d.id = "emerald_filigree";
        d.displayName = "Emerald Filigree";
        d.deckOrigin = "";
        d.type = CardBackType::ProceduralDraw;
        d.generator = [](CardTexture& tex, int w, int h) -> bool {
            return CardBackRegistry::bakeCanvasBack(tex, w, h, [](CardCanvas& ctx, int, int) {
                ctx.applyShader(nullptr, kEmeraldFiligreeShader, 1, Suit::Clubs, 1.0f);
            });
        };
        d.generator(d.baseTexture, cardW, cardH);
        universalBacks_.push_back(std::move(d));
    }

    // 5-8. Seed-Randomized Low-Poly SolPoly Card Backs (re-generate dynamically from lowPolySeed)
    auto registerLowPolyBack = [&](const std::string& id, const std::string& displayName,
                                   LowPolyStyle style, LowPolyBg bg) {
        CardBackDescriptor d;
        d.id = id;
        d.displayName = displayName;
        d.deckOrigin = "";
        d.type = CardBackType::ProceduralDraw;
        d.supportsSeed = true;
        // Intentionally leave baseTexture empty so setActiveCardBack always invokes generator with current lowPolySeed
        d.generator = [style, bg](CardTexture& tex, int w, int h) -> bool {
            LowPolyDeckOptions opts{};
            opts.style = style;
            opts.bg = bg;
            opts.seed = CardTextureCache::instance().lowPolySeed();
            return LowPolyDeckGenerator::generateCardBackTexture(opts, w, h, tex);
        };
        universalBacks_.push_back(std::move(d));
    };

    registerLowPolyBack("solpoly_classic", "SolPoly Classic", LowPolyStyle::Clockwork, LowPolyBg::White);
    registerLowPolyBack("solpoly",         "SolPoly Alabaster", LowPolyStyle::Classic, LowPolyBg::White);
    registerLowPolyBack("solpoly_dark",    "SolPoly Obsidian",  LowPolyStyle::Classic, LowPolyBg::Dark);
    registerLowPolyBack("solpoly_mobile",  "SolPoly Mobile",    LowPolyStyle::Mobile,  LowPolyBg::White);
    registerLowPolyBack("solpoly_jumbo",   "SolPoly Jumbo",     LowPolyStyle::Jumbo,   LowPolyBg::White);
    registerLowPolyBack("scoundrel_prism", "Scoundrel Prism",   LowPolyStyle::Scoundrel, LowPolyBg::Dark);
}

void CardBackRegistry::registerUniversalBack(const CardBackDescriptor& desc) {
    for (auto& u : universalBacks_) {
        if (u.id == desc.id) {
            u = desc;
            return;
        }
    }
    universalBacks_.push_back(desc);
}

void CardBackRegistry::registerDeckBack(const CardBackDescriptor& desc) {
    for (auto& d : deckBacks_) {
        if (d.id == desc.id && d.deckOrigin == desc.deckOrigin) {
            d = desc;
            return;
        }
    }
    deckBacks_.push_back(desc);
}

void CardBackRegistry::clearDeckBacks(const std::string& deckOrigin) {
    deckBacks_.erase(
        std::remove_if(deckBacks_.begin(), deckBacks_.end(), [&](const CardBackDescriptor& d) {
            return d.deckOrigin == deckOrigin;
        }),
        deckBacks_.end()
    );
}

void CardBackRegistry::clearAllDeckBacks() {
    deckBacks_.clear();
}

std::vector<CardBackDescriptor> CardBackRegistry::getAvailableBacks(const std::string& deckOrigin) const {
    std::vector<CardBackDescriptor> result;

    // 1. Auto selection (dynamically resolves to deck's primary card back)
    const CardBackDescriptor* defBack = getDefaultBackForDeck(deckOrigin);
    std::string defName = defBack ? defBack->displayName : "Default";

    CardBackDescriptor autoDesc;
    autoDesc.id = "auto";
    autoDesc.displayName = "Auto (" + defName + ")";
    autoDesc.deckOrigin = deckOrigin;
    if (defBack) {
        autoDesc.baseTexture = defBack->baseTexture;
    }
    result.push_back(autoDesc);

    // 2. Deck-specific card backs (from the active deck)
    if (!deckOrigin.empty()) {
        for (const auto& d : deckBacks_) {
            if (d.deckOrigin == deckOrigin) {
                result.push_back(d);
            }
        }
    }

    // 3. Universal card backs (available across all decks)
    for (const auto& u : universalBacks_) {
        result.push_back(u);
    }

    return result;
}

const CardBackDescriptor* CardBackRegistry::findBack(const std::string& id, const std::string& deckOrigin) const {
    if (id == "auto" || id.empty()) {
        return getDefaultBackForDeck(deckOrigin);
    }

    // 1. Check deck-specific backs
    if (!deckOrigin.empty()) {
        for (const auto& d : deckBacks_) {
            if (d.id == id && d.deckOrigin == deckOrigin) {
                return &d;
            }
        }
    }

    // 2. Check universal backs
    for (const auto& u : universalBacks_) {
        if (u.id == id) {
            return &u;
        }
    }

    // Fallback: deck default or first universal back
    const auto* def = getDefaultBackForDeck(deckOrigin);
    if (def) return def;
    return universalBacks_.empty() ? nullptr : &universalBacks_.front();
}

const CardBackDescriptor* CardBackRegistry::getDefaultBackForDeck(const std::string& deckOrigin) const {
    // Check if deck has registered card backs
    if (!deckOrigin.empty()) {
        for (const auto& d : deckBacks_) {
            if (d.deckOrigin == deckOrigin) {
                return &d;
            }
        }
    }

    // Map known deck themes to their signature card back
    if (deckOrigin == "solpoly_classic") {
        for (const auto& u : universalBacks_) if (u.id == "solpoly_classic") return &u;
    } else if (deckOrigin == "solpoly") {
        for (const auto& u : universalBacks_) if (u.id == "solpoly") return &u;
    } else if (deckOrigin == "solpoly_dark") {
        for (const auto& u : universalBacks_) if (u.id == "solpoly_dark") return &u;
    } else if (deckOrigin == "solpoly_mobile") {
        for (const auto& u : universalBacks_) if (u.id == "solpoly_mobile") return &u;
    } else if (deckOrigin == "solpoly_jumbo") {
        for (const auto& u : universalBacks_) if (u.id == "solpoly_jumbo") return &u;
    } else if (deckOrigin == "scoundrel_lowpoly") {
        for (const auto& u : universalBacks_) if (u.id == "scoundrel_prism") return &u;
    } else if (deckOrigin.find("baroque") != std::string::npos) {
        for (const auto& u : universalBacks_) if (u.id == "obsidian_baroque") return &u;
    } else if (deckOrigin == "classic") {
        for (const auto& u : universalBacks_) if (u.id == "classic_sapphire") return &u;
    }

    // Default to Classic Sapphire or first universal
    for (const auto& u : universalBacks_) {
        if (u.id == "classic_sapphire") return &u;
    }
    return universalBacks_.empty() ? nullptr : &universalBacks_.front();
}

void CardBackRegistry::scanCompanionBacks(const std::string& decksDir, int cardW, int cardH) {
    std::error_code ec;
    if (!std::filesystem::exists(decksDir, ec) || !std::filesystem::is_directory(decksDir, ec)) {
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(decksDir, ec)) {
        if (!entry.is_regular_file()) continue;
        std::string filename = entry.path().filename().string();
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".svg") {
            continue;
        }

        // Look for pattern: <theme>_back_<name>.<ext>
        size_t backPos = filename.find("_back_");
        if (backPos != std::string::npos) {
            std::string theme = filename.substr(0, backPos);
            size_t dotPos = filename.rfind('.');
            std::string backName = filename.substr(backPos + 6, dotPos - (backPos + 6));

            CardBackDescriptor desc;
            desc.id = theme + "_" + backName;
            desc.displayName = cleanNameFromId(backName);
            desc.deckOrigin = theme;
            desc.type = (ext == ".svg") ? CardBackType::Svg : CardBackType::Bitmap;

            bool ok = false;
            if (ext == ".svg") {
                ok = loadSvgBack(desc.baseTexture, entry.path().string(), cardW, cardH);
            } else {
                ok = loadBitmapBack(desc.baseTexture, entry.path().string(), cardW, cardH);
            }
            if (ok) {
                registerDeckBack(desc);
                std::cout << "[SolEngine] Registered companion card back: " << desc.displayName
                          << " (" << desc.id << ") for deck " << theme << "\n";
            }
        }
    }
}

void CardBackRegistry::scanScriptBacks(const std::string& backsDir, int cardW, int cardH) {
    if (scriptBacksScanned_ && scannedBacksDir_ == backsDir &&
        currentCardW_ == cardW && currentCardH_ == cardH) {
        return;
    }

    std::error_code ec;
    const std::string searchPaths[] = {
        backsDir,
        "/" + backsDir,
        "../" + backsDir,
        "../../" + backsDir,
        "../../../" + backsDir,
        "assets/" + backsDir,
        "../assets/" + backsDir,
        "./" + backsDir
    };

    std::string targetDir;
    for (const auto& sp : searchPaths) {
        if (std::filesystem::exists(sp, ec) && std::filesystem::is_directory(sp, ec)) {
            targetDir = sp;
            break;
        }
    }

    scriptBacksScanned_ = true;
    scannedBacksDir_ = backsDir;
    if (targetDir.empty()) return;

    std::vector<std::filesystem::path> scriptPaths;
    for (const auto& entry : std::filesystem::directory_iterator(targetDir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".eats") {
            scriptPaths.push_back(entry.path());
        }
    }
    std::sort(scriptPaths.begin(), scriptPaths.end());

    struct ScriptBackOption {
        std::string id;
        std::string name;
        float u[4]{0.0f, 0.0f, 0.0f, 0.0f};
    };

    for (const auto& path : scriptPaths) {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open()) continue;
        std::ostringstream ss;
        ss << ifs.rdbuf();
        std::string source = ss.str();
        if (source.empty()) continue;

        std::string stem = path.stem().string();
        std::string baseId = stem;
        std::string baseName = cleanNameFromId(stem);
        bool scriptSupportsSeed = false;
        std::vector<ScriptBackOption> options;

        // Parse metadata & discover draw entrypoints without baking yet
        eatsbits::eatscript::Evaluator metaEval;
        eatsbits::eatscript::registerStandardLibrary(metaEval);
        CardCanvas dummyCanvas(8, 8);
        CardCanvas::bindToEvaluator(metaEval, &dummyCanvas);

        using HostArgs = const std::vector<eatsbits::eatscript::Value>&;
        using HostKwargs = const std::map<std::string, eatsbits::eatscript::Value>&;
        auto defineBackFn = [&](HostArgs args, HostKwargs kwargs) -> eatsbits::eatscript::Value {
            auto d = eatsbits::eatscript::resolveArg(
                args, kwargs, 0, "back",
                eatsbits::eatscript::Value(std::map<std::string, eatsbits::eatscript::Value>{})
            );
            if (d.isDict()) {
                if (d.hasKey("id")) {
                    std::string s = d.get("id").asString();
                    if (!s.empty()) baseId = s;
                }
                if (d.hasKey("name")) {
                    std::string s = d.get("name").asString();
                    if (!s.empty()) baseName = s;
                } else if (d.hasKey("display_name")) {
                    std::string s = d.get("display_name").asString();
                    if (!s.empty()) baseName = s;
                }
                if (d.hasKey("seeded")) {
                    scriptSupportsSeed = d.get("seeded").asBoolean();
                } else if (d.hasKey("supports_seed")) {
                    scriptSupportsSeed = d.get("supports_seed").asBoolean();
                }
                if (d.hasKey("options") && d.get("options").isList()) {
                    for (const auto& item : d.get("options").listVal) {
                        if (!item.isDict()) continue;
                        ScriptBackOption opt;
                        if (item.hasKey("id")) opt.id = item.get("id").asString();
                        if (item.hasKey("name")) opt.name = item.get("name").asString();
                        if (opt.name.empty() && !opt.id.empty()) opt.name = cleanNameFromId(opt.id);
                        if (item.hasKey("u") && item.get("u").isList()) {
                            for (size_t i = 0; i < 4 && i < item.get("u").listVal.size(); ++i) {
                                opt.u[i] = item.get("u").listVal[i].asFloat(0.0f);
                            }
                        }
                        if (!opt.id.empty()) options.push_back(std::move(opt));
                    }
                }
            }
            return eatsbits::eatscript::Value(true);
        };

        metaEval.setVariable("sol", eatsbits::eatscript::Value(std::map<std::string, eatsbits::eatscript::Value>{}));
        metaEval.registerMemberFunction("sol", "define_card_back", defineBackFn);
        metaEval.registerFunction("define_card_back", defineBackFn);
        metaEval.evaluateSource(source);
        if (!metaEval.getLastError().empty()) {
            std::cerr << "[SolEngine] Error loading card back script '" << path.string()
                      << "': " << metaEval.getLastError() << "\n";
            continue;
        }

        // Collect all on_draw_card_back* functions (or fallback to card_back_shader)
        std::vector<std::string> drawFuncs;
        for (const auto& fnName : metaEval.getFunctionNames()) {
            if (fnName.rfind("on_draw_card_back", 0) == 0) {
                drawFuncs.push_back(fnName);
            }
        }
        std::sort(drawFuncs.begin(), drawFuncs.end());
        const bool hasShaderEntry = metaEval.hasFunction("card_back_shader");
        if (drawFuncs.empty() && !hasShaderEntry) continue;

        auto makeGenerator = [](std::string scriptSrc, std::string fnName,
                                std::string optId, std::string optName,
                                std::array<float, 4> uniforms) {
            return [scriptSrc = std::move(scriptSrc), fnName = std::move(fnName),
                    optId = std::move(optId), optName = std::move(optName),
                    uniforms](CardTexture& outTex, int w, int h) -> bool {
                return CardBackRegistry::bakeCanvasBack(outTex, w, h, [&](CardCanvas& canvas, int cw, int ch) {
                    eatsbits::eatscript::Evaluator ev;
                    eatsbits::eatscript::registerStandardLibrary(ev);
                    CardCanvas::bindToEvaluator(ev, &canvas);

                    auto noop = [](HostArgs, HostKwargs) -> eatsbits::eatscript::Value {
                        return eatsbits::eatscript::Value(true);
                    };
                    ev.setVariable("sol", eatsbits::eatscript::Value(std::map<std::string, eatsbits::eatscript::Value>{}));
                    ev.registerMemberFunction("sol", "define_card_back", noop);
                    ev.registerFunction("define_card_back", noop);
                    ev.setVariable("option_id", eatsbits::eatscript::Value(optId));
                    ev.setVariable("option_name", eatsbits::eatscript::Value(optName));
                    const double curSeed = static_cast<double>(CardTextureCache::instance().lowPolySeed());
                    ev.setVariable("seed", eatsbits::eatscript::Value(curSeed));
                    ev.setVariable("low_poly_seed", eatsbits::eatscript::Value(curSeed));
                    ev.setVariable("u0", eatsbits::eatscript::Value(static_cast<double>(uniforms[0])));
                    ev.setVariable("u1", eatsbits::eatscript::Value(static_cast<double>(uniforms[1])));
                    ev.setVariable("u2", eatsbits::eatscript::Value(static_cast<double>(uniforms[2])));
                    ev.setVariable("u3", eatsbits::eatscript::Value(static_cast<double>(uniforms[3])));

                    ev.evaluateSource(scriptSrc);
                    if (!ev.getLastError().empty()) {
                        std::cerr << "[SolEngine] Card back script error: " << ev.getLastError() << "\n";
                        return;
                    }

                    if (!fnName.empty() && ev.hasFunction(fnName)) {
                        ev.callFunction(fnName, {
                            eatsbits::eatscript::Value("ctx"),
                            eatsbits::eatscript::Value(static_cast<double>(cw)),
                            eatsbits::eatscript::Value(static_cast<double>(ch))
                        });
                        if (!ev.getLastError().empty()) {
                            std::cerr << "[SolEngine] " << fnName << " error: " << ev.getLastError() << "\n";
                        }
                    } else if (ev.hasFunction("card_back_shader")) {
                        auto fnVal = ev.getVariable("card_back_shader");
                        if (fnVal.isFunction() && fnVal.funcDef) {
                            canvas.applyShader(fnVal.funcDef.get(), "", 1, Suit::Spades, 1.0f,
                                               uniforms[0], uniforms[1], uniforms[2], uniforms[3]);
                        }
                    }
                });
            };
        };

        if (!options.empty()) {
            std::string entryFn = drawFuncs.empty() ? "" : drawFuncs.front();
            for (const auto& opt : options) {
                CardBackDescriptor desc;
                desc.id = (options.size() == 1) ? baseId : (baseId + "_" + opt.id);
                desc.displayName = (options.size() == 1) ? baseName : (baseName + " — " + opt.name);
                desc.deckOrigin = "";
                desc.type = CardBackType::Eatscript;
                desc.supportsSeed = scriptSupportsSeed;
                std::array<float, 4> uArr = {opt.u[0], opt.u[1], opt.u[2], opt.u[3]};
                desc.generator = makeGenerator(source, entryFn, opt.id, opt.name, uArr);
                registerUniversalBack(desc);
                std::cout << "[SolEngine] Registered Eatscript card back: " << desc.displayName
                          << " (" << desc.id << ")\n";
            }
        } else if (!drawFuncs.empty()) {
            for (const auto& fn : drawFuncs) {
                std::string suffix = (fn == "on_draw_card_back") ? "" : fn.substr(17);
                if (!suffix.empty() && suffix[0] == '_') suffix = suffix.substr(1);

                CardBackDescriptor desc;
                desc.id = suffix.empty() ? baseId : (baseId + "_" + suffix);
                desc.displayName = suffix.empty() ? baseName : (baseName + " — " + cleanNameFromId(suffix));
                desc.deckOrigin = "";
                desc.type = CardBackType::Eatscript;
                desc.supportsSeed = scriptSupportsSeed;
                desc.generator = makeGenerator(source, fn, suffix, cleanNameFromId(suffix), {0.0f, 0.0f, 0.0f, 0.0f});
                registerUniversalBack(desc);
                std::cout << "[SolEngine] Registered Eatscript card back: " << desc.displayName
                          << " (" << desc.id << ")\n";
            }
        } else if (hasShaderEntry) {
            CardBackDescriptor desc;
            desc.id = baseId;
            desc.displayName = baseName;
            desc.deckOrigin = "";
            desc.type = CardBackType::Eatscript;
            desc.supportsSeed = scriptSupportsSeed;
            desc.generator = makeGenerator(source, "", "", "", {0.0f, 0.0f, 0.0f, 0.0f});
            registerUniversalBack(desc);
            std::cout << "[SolEngine] Registered Eatscript shader card back: " << desc.displayName
                      << " (" << desc.id << ")\n";
        }
    }
}

} // namespace solengine

