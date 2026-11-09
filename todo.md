# Procedural Baroque & Sculptural Deck Pipeline: Implementation Tasks

> **Tracking Document:** `todo.md`  
> **Architecture Reference:** [PROCEDURAL_BAROQUE_ART_PIPELINE.md](file:///c:/git/solengine/PROCEDURAL_BAROQUE_ART_PIPELINE.md)  
> **Key Files:**  
> - [card_canvas.hpp](file:///c:/git/solengine/include/solengine/card_canvas.hpp) / [card_canvas.cpp](file:///c:/git/solengine/src/render/card_canvas.cpp)  
> - [field_ops.hpp](file:///c:/git/solengine/include/eatstextures/field_ops.hpp) / [field_ops.cpp](file:///c:/git/solengine/src/proctex/field_ops.cpp)  
> - [card_texture_cache.hpp](file:///c:/git/solengine/include/solengine/card_texture_cache.hpp) / [card_texture_cache.cpp](file:///c:/git/solengine/src/render/card_texture_cache.cpp)  
> - [decks/imperial_baroque.eats](file:///c:/git/solengine/decks/imperial_baroque.eats)  
> - [decks/engraved_intaglio.eats](file:///c:/git/solengine/decks/engraved_intaglio.eats)  

---

## Phase 1: Symmetrical Ornamental Vector Primitives

- [x] **1.1 Symmetry Scope Context (`ctx.symmetry`)**
  - [x] Implement symmetrical transform scope in `CardCanvas` supporting `mirror_x`, `mirror_y`, and `quad` (mirror both).
  - [x] Expose `ctx.begin_symmetry`, `ctx.end_symmetry`, and `ctx.with_symmetry(fn, mirror_x, mirror_y)` to Eatscript evaluator.
  - [x] Recording queue buffers and replays primitives across reflected coordinate spaces.

- [x] **1.2 Parametric Volute / Logarithmic Spiral (`ctx.draw_volute`)**
  - [x] Implement logarithmic spiral arc generator $r(\theta) = a \cdot e^{b\theta}$ with variable cubic-eased stroke thickness.
  - [x] Generate closed outline polygon / Bézier path for both fill and stroke operations.
  - [x] Expose `ctx.draw_volute(cx, cy, radius, turns, angle_start, color, width, profile)` in Eatscript.

- [x] **1.3 Acanthus Leaf & Frond Generator (`ctx.draw_acanthus`)**
  - [x] Implement guide-curve spline with parametric lobe fan generation.
  - [x] Add scalloped lobe geometry with curling tip geometry and central spine rib.
  - [x] Expose `ctx.draw_acanthus(from=[x1, y1], to=[x2, y2], lobes=5, curl_dir="left", color, stroke)` in Eatscript.

- [x] **1.4 Bead Chains & Path Repeaters (`ctx.repeat_beads`)**
  - [x] Implement perimeter sampling along card boundary with guide wire and specular gleam dots.
  - [x] Add `ctx.repeat_beads(inset, radius, spacing, color)` for rapid pearl/beaded borders in Eatscript.

- [x] **1.5 Baroque Cartouche & Medallion Outlines (`ctx.stroke_cartouche`, `ctx.fill_oval`, `ctx.stroke_oval`)**
  - [x] Implement 8-node pinched baroque cartouche with pinched crests and scrolled shoulder brackets.
  - [x] Expose `ctx.stroke_cartouche(cx, cy, rx, ry, style, color, line_width)`, `ctx.fill_oval`, and `ctx.stroke_oval` in Eatscript.

- [x] **1.6 Ornate Central Pip Infill (`ctx.draw_ornate_suit`)**
  - [x] Create ornate filigree infill overlays for Spades, Hearts, Diamonds, and Clubs.
  - [x] Expose `ctx.draw_ornate_suit(suit, cx, cy, size, body_color, filigree_color)` in Eatscript.

---

## Phase 2: Color Palette, Multi-Material Tagging & Clean Flat Rendering

- [x] **2.1 Multi-Material State in `CardCanvas`**
  - [x] Hex color parsing with full RGBA support.
  - [x] Presets for antique gold (`#D4AF37`), antique bone/ivory (`#EDE6D6`), slate ebony (`#18191E`), and imperial crimson (`#B81424`).
- [x] **2.2 Multi-Layer Composition & Flat Finish**
  - [x] High-precision subpixel anti-aliasing via NanoSVG.
  - [x] `"flat"` finish pass in `ctx.apply_finish(style="flat")` producing ultra-clean vector cards.
  - [x] Upgraded GPU atlas from 2048x2048 downscaled `mip1` (180x252) to 4096x4096 native `argb32` (360x504), eliminating card blurriness.

---

## Phase 3: Hand-Sketched & Copperplate Engraving Finish

- [x] **3.1 Procedural Paper Grain Noise**
  - [x] High-frequency pseudo-random cotton rag paper fiber luminance modulation.
- [x] **3.2 Micro-Capillary Ink Bleed**
  - [x] Softened stroke boundary edge perturbation simulating quill-and-burin ink bleed.
- [x] **3.3 Finish Binding**
  - [x] Expose `ctx.apply_finish(style="hand_sketched", paper_grain=0.18, ink_bleed=0.22)` in Eatscript.

---

## Phase 4: Bas-Relief Heightfield Pipeline

- [x] **4.1 Heightfield Derivation**
  - [x] Extract luminance and alpha into `eatstextures::FloatField`.
- [x] **4.2 Bevel Profile Shaping**
  - [x] Extrude 3D sculpted slopes using `eatstextures::opBevel`.
- [x] **4.3 Cavity Ambient Occlusion (AO)**
  - [x] Multi-tap horizon AO via `eatstextures::computeAmbientOcclusion` driving crevice patina / tea-stain grime.
- [x] **4.4 Curvature Analysis & Ridge Highlights**
  - [x] Discrete Laplacian via `eatstextures::computeCurvature` driving polished edge specular highlights.
- [x] **4.5 PBR Shading**
  - [x] Cook-Torrance GGX directional lighting via `eatstextures::shadePBR` with metallic, roughness, and normal mapping.
  - [x] Expose `ctx.apply_finish(style="relief", ...)` in Eatscript.

---

## Phase 5: Builtin Deck Templates & Verification

- [x] **5.1 Imperial Baroque Deck Recipe (`decks/imperial_baroque.eats`)**
  - [x] Production sample deck with dark slate, antique gold, and suit-themed accents using `style="relief"`.
- [x] **5.2 Vintage Intaglio Deck Recipe (`decks/engraved_intaglio.eats`)**
  - [x] Production sample deck with vintage copperplate engraving and paper grain using `style="hand_sketched"`.
- [x] **5.3 Build Verification**
  - [x] Verified full C++ compilation and linking of `solengine_render` and `solengine.exe` with zero errors.
- [ ] **5.4 Optional Future Enhancements**
  - [ ] WebGPU Compute Shader Jump Flood Algorithm (JFA) pass for real-time dynamic light tilt when dragging cards in 2.5D.
