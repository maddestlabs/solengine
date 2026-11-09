# SolEngine Deck Pipeline & AI Generation Guide

> **End-to-End Asset Tooling, Multi-Format Deck Loading, Multimodal Gemini Conditioning, and Gameplay Color Enforcement**  
> *CLI Tool: [`tools/deck_pipeline.py`](file:///c:/git/solengine/tools/deck_pipeline.py) | Engine Loader: [`src/render/card_texture_cache.cpp`](file:///c:/git/solengine/src/render/card_texture_cache.cpp)*

---

## 1. Overview & Architecture

SolEngine features a dual-pipeline card engine supporting both **infinite vector scaling (SVG)** and **photorealistic painterly raster sheets (PNG Bitmaps)**:

```
[Base Template Deck]
  ├── decks/classic_svg.svg (9x6 Vector Sheet)
  └── decks/classic_svg.bin (Pre-baked fast binary cache)
        │
        ▼ (python deck_pipeline.py export-base)
[54 Sliced Card Templates] (assets/deck_cards/classic/png/)
        │
        ├───────────────────────────────────────────────────────┐
        │                                                       │
        ▼ (Multimodal Gemini 3 Pro Conditioning)                ▼
  [Layout Reference (5:7 ratio, index bounds)]          [Style Reference Anchor]
  [Explicit Red/Black Solitaire Constraints]            (--style-ref queen.png)
        │                                                       │
        └──────────────────────────┬────────────────────────────┘
                                   │
                                   ▼ (python deck_pipeline.py generate-ai)
                    [Generated Card PNGs] (assets/deck_cards/<theme>/png/)
                                   │
                    ┌──────────────┴──────────────┐
                    ▼ (assemble-bitmap)           ▼ (assemble vector)
           decks/<theme>.png             decks/<theme>_svg.svg
           (Master 9x6 Bitmap Sheet)     (Master 9x6 Vector SVG)
                    │                             │
                    └──────────────┬──────────────┘
                                   ▼
                   [Engine: CardTextureCache]
                     ├── Bilinear cell resampling
                     ├── Fast binary cache (.bin)
                     └── Procedural theme card back
```

### Why the Bitmap Pipeline?
- **Fidelity:** Modern diffusion and transformer models (`gemini-3-pro-image`) produce rich raster art with subtle lighting, mechanical filigree, paper grain, and brushwork. Vectorizing with `vtracer` quantizes images to 16 colors and inflates file size (~75 MB SVG).
- **Performance:** A 9x6 bitmap sheet (e.g. 3240x3024 PNG, ~20 MB) loads in milliseconds via `stb_image` and is cached as a flat GPU-ready binary (`.bin`), eliminating runtime XML parsing.
- **Coexistence:** The engine retains the CC0 Wikimedia SVG (`decks/classic_svg.svg`) as the vector rendering baseline while themed decks (such as Victorian Steampunk) leverage direct bitmap sheets.

---

## 2. 9x6 Grid Sheet Layout

All master decks adhere to a standard 9-column by 6-row matrix (54 cells total):

| Row | Suit / Content | Column 0 (Ace) | Columns 1–9 (2–10) | Column 10 (Jack) | Column 11 (Queen) | Column 12 (King) |
|---|---|---|---|---|---|---|
| **0** | **Spades** (♠) | `spades_01_ace` | `spades_02` .. `spades_10` | `spades_11_jack` | `spades_12_queen` | `spades_13_king` |
| **1** | **Hearts** (♥) | `hearts_01_ace` | `hearts_02` .. `hearts_10` | `hearts_11_jack` | `hearts_12_queen` | `hearts_13_king` |
| **2** | **Clubs** (♣) | `clubs_01_ace` | `clubs_02` .. `clubs_10` | `clubs_11_jack` | `clubs_12_queen` | `clubs_13_king` |
| **3** | **Diamonds** (♦) | `diamonds_01_ace` | `diamonds_02` .. `diamonds_10` | `diamonds_11_jack` | `diamonds_12_queen` | `diamonds_13_king` |
| **4–5** | **Jokers & Extra** | `joker_black` | `joker_red` | *(Blank / reserved)* | *(Blank / reserved)* | *(Blank / reserved)* |

Card indices are mapped in code via [`svgCellIndexForCard()`](file:///c:/git/solengine/src/render/card_texture_cache.cpp#L19-L35).

---

## 3. Gemini Prompt Engineering & Gameplay Rules

Solitaire mechanics strictly demand **alternating red and black card stacks**. AI diffusion models tend to homogenize stylized decks (e.g. Victorian Steampunk) into uniform sepia or brass tones. The pipeline enforces color contrast and layout integrity programmatically:

### A. Red vs. Black Suit Enforcement
Generated prompts in `get_card_prompt()` dynamically inject strict color rules:

- **Red Suits (Hearts & Diamonds):**
  > *"VIVID RED SUIT COLOR REQUIREMENT (CRITICAL FOR SOLITAIRE PLAYABILITY): This is a RED suit. The corner rank, corner pip, and all primary suit symbols MUST be bright, vivid crimson red (`#c8102e` / `#d01828`). Do NOT make the symbols black, brown, brass, gold, or dark monochrome. The red must be unmistakable, bold, and high-contrast against the antique parchment."*
- **Black Suits (Spades & Clubs):**
  > *"BLACK SUIT COLOR REQUIREMENT: This is a BLACK suit. The corner rank, corner pip, and primary suit symbols MUST be deep solid charcoal-black (`#111111`), accented with antique copper and brass metallic details."*

### B. Mandatory Full-Bleed Flat Texture & 5:7 Proportions (CRITICAL ANTI-SHADOW POLICY)

> [!CAUTION]
> **Strict Prohibition on Simulated Drop Shadows, Padding, and Tabletop Photography:**  
> AI diffusion and image models frequently misinterpret playing card requests as prompts to generate a 3D photograph of a physical card resting on a wooden desk, felt surface, or neutral tabletop, baking simulated drop shadows around the card borders and adding surrounding margins/padding.
>
> **Why post-crop fails:**
> 1. **Baked-in Shadows:** Simulated drop shadows cast onto the background bleed into the outer ornate frame bevels; cropping cannot remove the shadows without cutting into the rosettes or leaving an asymmetrical dark fringe.
> 2. **Aspect Ratio Distortion:** Squeezing unconditioned images (e.g. 1:1.49) into standard 5:7 playing card proportions ($360 \times 504$, 1:1.40) noticeably squashes the artwork vertically and introduces interpolation artifacts.
> 3. **Cascade Misalignment:** In Solitaire tableau cascades, cards overlap. Any baked drop shadows or outer padding break the planar alignment of the tableau and produce ugly dark borders inside the game.

**Mandatory Generation Rules for All Decks:**
1. **Texture-Only Output (Zero Perspective):** The generated image MUST be a flat digital 2D card texture blitted edge-to-edge. The 4 outer pixel borders of the image ($x=0, y=0, x=W-1, y=H-1$) ARE the card's physical perimeter.
2. **Strict Negative Prompts in Manifests:**
   All card prompt generators must append:  
   `"STRICT NEGATIVE CONSTRAINTS: flat digital card face texture only, edge-to-edge full bleed, zero external drop shadow, no tabletop, no background surface, no photograph of a card, no white/grey/dark border padding, no margin, no perspective tilt."`
3. **1:1 Structural Template Conditioning:**
   Every generated card MUST be conditioned against the corresponding sliced classic template ([`assets/deck_cards/classic/png/<card>.png`](file:///c:/git/solengine/assets/deck_cards/classic/png)) at exact $360 \times 504$ resolution (5:7 ratio).
4. **Style References:**
   When an external style reference image is provided (such as an engraving photograph), the prompt must explicitly state:  
   `"Extract ONLY the ornamental relief motifs, linework, and metallic shading from the style reference. DO NOT copy its aspect ratio, border margins, background surface, or external shadows. Render the design flat and full-bleed across the 5:7 card layout."`

### C. Corner Indices & Framing Instructions
Corner pips and rank letters are explicitly instructed to prevent AI models from cropping or generating empty card borders:
> *"Corner Indices: Clear, legible rank '{rank}' and suit symbol '{symbol}' in the top-left and inverted in the bottom-right corners, inside the card border."*

---

## 4. Multi-Image Style Conditioning (`--style-ref`)

To keep an entire deck visually harmonious, `deck_pipeline.py` supports **multimodal multi-image conditioning** using Google GenAI SDK (`gemini-3-pro-image`):

1. **Content Part 1 (Layout Anchor):** Sliced classic reference card (`assets/deck_cards/classic/png/<card>.png`). Enforces standard 5:7 playing card proportions and index placements.
2. **Content Part 2 (Style Anchor):** Passed via `--style-ref <path_to_card.png>` (e.g. `spades_12_queen.png`). Enforces matching linework, engraving shading, palette, and ornamentation.
3. **Content Part 3 (Prompt):** Instructs the model:
   > *"Style Reference: Match the exact artistic style, engraving line weight, and steampunk color palette of the second image."*

---

## 5. CLI Reference & Command Recipes

The pipeline tool [`tools/deck_pipeline.py`](file:///c:/git/solengine/tools/deck_pipeline.py) provides subcommands for every stage:

### 1. Slice Base Deck
Extracts all 54 cards from `decks/classic_svg.svg` into standalone PNGs and SVGs:
```bash
python tools/deck_pipeline.py export-base \
    --svg decks/classic_svg.svg \
    --bin decks/classic_svg.bin \
    --out assets/deck_cards/classic
```

### 2. Generate Prompt Manifest
Generates a JSON file containing customized Gemini prompts for all 54 cards:
```bash
python tools/deck_pipeline.py generate-prompts \
    --theme "Victorian Steampunk" \
    --out assets/victorian_steampunk_prompts.json
```

### 3. Generate AI Cards (Batch or Individual)
Requires `GEMINI_API_KEY` environment variable or `--api-key`:

```bash
# Generate only court cards (Jacks, Queens, Kings)
python tools/deck_pipeline.py generate-ai \
    --theme "Victorian Steampunk" \
    --cards court \
    --model gemini-3-pro-image

# Generate a single specific card with style conditioning
python tools/deck_pipeline.py generate-ai \
    --theme "Victorian Steampunk" \
    --cards "spades_13_king" \
    --style-ref "assets/deck_cards/victorian_steampunk/png/spades_12_queen.png" \
    --force

# Refresh all red suits (Hearts & Diamonds) with vivid crimson enforcement
python tools/deck_pipeline.py generate-ai \
    --theme "Victorian Steampunk" \
    --cards "hearts_01_ace,hearts_02,hearts_03,hearts_04,hearts_05,hearts_06,hearts_07,hearts_08,hearts_09,hearts_10,hearts_11_jack,hearts_12_queen,hearts_13_king,diamonds_01_ace,diamonds_02,diamonds_03,diamonds_04,diamonds_05,diamonds_06,diamonds_07,diamonds_08,diamonds_09,diamonds_10,diamonds_11_jack,diamonds_12_queen,diamonds_13_king" \
    --style-ref "assets/deck_cards/victorian_steampunk/png/spades_12_queen.png" \
    --force
```

### 4. Assemble Master 9x6 Bitmap Sheet (Recommended)
Combines 54 individual PNGs into a unified high-resolution master sheet and removes stale `.bin` caches:
```bash
python tools/deck_pipeline.py assemble-bitmap \
    --cards-dir assets/deck_cards/victorian_steampunk/png \
    --out decks/steampunk.png \
    --card-width 360 \
    --card-height 504
```

### 5. Assemble Master Vector Sheet (SVG Fallback)
Stitches individual SVGs into a single 9x6 grid SVG:
```bash
python tools/deck_pipeline.py assemble \
    --cards-dir assets/deck_cards/victorian_steampunk/svg \
    --base decks/classic_svg.svg \
    --out decks/steampunk_svg.svg
```

### 6. Hone & Surgical Alignment Pipeline (Baroque Noir & Themed Decks)
Deterministically normalizes geometry, inlays authentic background textures to full bleed (zero black letterbox borders), and calibrates gameplay contrast/bloom without altering face portraits or character engravings:
```bash
python tools/hone_baroque_noir.py
```
- **Geometry & Alignment:** Anchors scale and centering to the 2 of Clubs ($W=344, H=496, cx=180.0, cy=252.0$).
- **Full Bleed:** Replaces outer dark margins with seamless slate cardstock texture with authentic paper grain.
- **Red Bloom:** Calibrates radiant ruby halo on Hearts and Diamonds against the Ace of Hearts baseline.
- **Black Contrast:** Darkens Spades and Clubs pips to obsidian black (#111111) matching the Aces for mobile readability.

---

## 7. Engine Integration & Binary Caching

In [`src/render/card_texture_cache.cpp`](file:///c:/git/solengine/src/render/card_texture_cache.cpp):
1. **Timestamp Freshness:** When loading `decks/steampunk.png`, the engine checks for `decks/steampunk.bin`. If the `.bin` exists and is newer than the source image, it loads in $\sim 2\text{ ms}$. If the PNG has been reassembled, the cache is automatically invalidated and refreshed.
2. **Procedural Card Backs:** Each theme automatically synthesizes a themed card back (e.g. brass cogwork for Steampunk, gold baize filigree for Classic) via `bakeThemeCardBack()`.
3. **Resampling:** High-quality bilinear cell filtering adapts any source cell resolution to the engine's target card width and height.

