# Eatscript Custom Deck Architecture: Retained Vector Display Lists

> **Document:** `EATSCRIPT_CARD_CUSTOMIZATION.md`  
> **Topic:** User-Scripted Procedural Card Rendering via Eatscript & Retained Vector Display Lists  
> **Engine Base:** [Eatsbits](file:///c:/git/solengine/BLUEPRINT.md) C++20 / WebGPU / [BatchRenderer2D](file:///c:/git/solengine/include/eatsbits/ui/batch_renderer_2d.hpp) / [Eatscript](file:///c:/git/solengine/include/eatsbits/eatscript/evaluator.hpp)  
> **Design Strategy:** Strategy 1 (Pre-Baked Vertex Display Lists / Retained Vector Cache)

---

## 1. Executive Summary & Design Goals

SolEngine avoids pre-rendered bitmap textures in favor of 100% procedural vector graphics. To allow players and modders to author custom card faces, backs, and pip iconography using **Eatscript**, we must balance scripting flexibility with strict real-time performance:

1. **Zero Runtime Script Overhead:** Eatscript code **never runs during the per-frame render loop**. Dynamic VM/interpreter execution for 52 cards every 16ms would hurt frame pacing and introduce memory allocations.
2. **Zero Bitmap Bloat (Pure Vector Fidelity):** Instead of rasterizing user scripts into fixed-resolution pixel bitmaps, Eatscript drawing commands compile directly into **retained 2D vertex display lists** (`std::vector<Vertex2D>`).
3. **Resolution Independence:** Cards retain perfect analytical curves and crisp edges whether rendered on a 720p handheld or an 8K display.
4. **Live Hot-Reloading:** Authors can edit `.eat` script files in any text editor and see their changes reflected on the game board in milliseconds without restarting.
5. **Fault-Tolerant Sandboxing:** If a user script contains syntax errors or exceptions, the engine logs the diagnostic info and falls back cleanly to the default Hardwood 3.0 deck.

---

## 2. Architecture Overview & Lifecycle

```mermaid
flowchart TD
    subgraph Authoring ["1. Authoring & Loading"]
        Script["custom_deck.eat (User Script)"]
        Parser["Eatscript Lexer & AST Parser"]
        Script -->|Load / Hot-Reload| Parser
    end

    subgraph Bake ["2. Pre-Bake Compilation (Run Once)"]
        VM["Eatscript Evaluator / VM"]
        Builder["CardMeshBuilder (Host Drawing Canvas)"]
        Cache["CompiledDeckMesh (Retained Vector Display Lists)"]
        
        Parser --> VM
        VM -->|Calls on_draw_card_back & on_draw_card_face| Builder
        Builder -->|Emits Vertex2D Geometry| Cache
    end

    subgraph Runtime ["3. Real-Time Render Loop (60+ FPS)"]
        Board["SolitaireBoard State"]
        Renderer["TableRenderer & BatchRenderer2D"]
        Screen["Screen Output (Native / WebGPU)"]

        Board --> Renderer
        Cache -->|Direct Vertex Batch Injection (Offset + 2.5D Scale)| Renderer
        Renderer --> Screen
    end
```

### Why Retained Vector Display Lists Outperform Texture Atlases

| Criterion | Retained Vector Display List (Strategy 1) | Offscreen RGBA Texture Atlas |
| :--- | :--- | :--- |
| **VRAM / RAM Footprint** | **~50 KB – 150 KB** total for all 53 card meshes (card back + 52 faces). | **16 MB – 64 MB** VRAM for high-DPI (4K) card atlas sheets. |
| **Edge Sharpness** | **Infinite vector precision.** Uses the engine's analytical anti-aliasing (2x continuous or 4x RGSS). | Fixed pixel grid. Blurs or sparkles when cards scale during 2.5D flips and elevation. |
| **GPU Cache & State Changes** | Single vertex buffer stream; zero texture-binding or sampler state switches. | Requires texture binding, texture coordinate calculations, and mipmap filtering. |
| **Compile / Reload Latency** | **< 5 milliseconds** to compile 53 vector cards from Eatscript. | 50–200 ms to rasterize, blit, and upload 53 high-res pixel targets to GPU. |

---

## 3. Eatscript Deck API Specification

Deck authors create a script file (e.g., `decks/mystic_sanctum.eat`). The engine invokes two mandatory entry-point functions and passes a drawing context (`ctx`):

```python
# =============================================================================
# Mystic Sanctum Custom Deck (custom_deck.eat)
# =============================================================================

# Card Dimensions: w = 100.0, h = 145.0 (Normalized aspect ratio: ~1 : 1.45)
# Coordinates are local: (0, 0) is top-left, (w, h) is bottom-right.

def on_draw_card_back(ctx, w, h):
    # 1. Base card plate with rounded corners
    ctx.fill_rounded_rect(0, 0, w, h, radius=7.0, color="#121824")
    
    # 2. Ornate interior panel with two-tone gradient
    margin = 5.0
    ctx.fill_rounded_rect_gradient(
        margin, margin, w - margin * 2, h - margin * 2,
        radius=5.0,
        color_top="#1d263b",
        color_bottom="#0f141f"
    )
    
    # 3. Gold filigree outline
    ctx.stroke_rounded_rect(margin, margin, w - margin * 2, h - margin * 2,
                            radius=5.0, color="#d4af37", width=1.2)
    
    # 4. Central occult sigil (Diamond + Arcane Eye)
    cx = w * 0.5
    cy = h * 0.5
    ctx.fill_circle(cx, cy, radius=18.0, color="#121824")
    ctx.stroke_circle(cx, cy, radius=18.0, color="#d4af37", width=1.5)
    ctx.draw_svg_path("M 0 -12 L 12 0 L 0 12 L -12 0 Z", cx=cx, cy=cy, color="#48cae4")


def on_draw_card_face(ctx, rank, suit, w, h):
    # 1. Ivory parchment card surface
    ctx.fill_rounded_rect(0, 0, w, h, radius=7.0, color="#fdfbf7")
    ctx.stroke_rounded_rect(0, 0, w, h, radius=7.0, color="#e0dacf", width=1.0)
    
    # 2. Suit color selection
    is_red = (suit == "hearts" or suit == "diamonds")
    pip_color = "#b22222" if is_red else "#1a1a1a"
    
    # 3. Corner Index: Rank text and suit pip (Top-Left)
    ctx.draw_text(rank, x=8.0, y=18.0, size=15.0, color=pip_color)
    ctx.draw_suit_pip(suit, cx=13.0, cy=32.0, radius=6.5, color=pip_color)
    
    # 4. Inverted Corner Index (Bottom-Right)
    ctx.draw_text(rank, x=w - 18.0, y=h - 8.0, size=15.0, color=pip_color, rotate_180=True)
    ctx.draw_suit_pip(suit, cx=w - 13.0, cy=h - 32.0, radius=6.5, color=pip_color)
    
    # 5. Face / Court Card Art or Center Pip Field
    if rank == "J" or rank == "Q" or rank == "K":
        draw_court_frame(ctx, rank, suit, w, h, pip_color)
    else:
        draw_spot_pips(ctx, rank, suit, w, h, pip_color)
```

### Supported Canvas Primitives

| Eatscript API Method | Description |
| :--- | :--- |
| `ctx.fill_rect(x, y, w, h, color)` | Solid color rectangle. |
| `ctx.fill_rounded_rect(x, y, w, h, r, color)` | Rounded rectangle with corner radius `r`. |
| `ctx.stroke_rounded_rect(x, y, w, h, r, color, width)` | Outlined rounded rectangle. |
| `ctx.fill_rounded_rect_gradient(...)` | Vertical linear gradient within a rounded box. |
| `ctx.fill_circle(cx, cy, radius, color)` | Smooth analytical circle. |
| `ctx.stroke_circle(cx, cy, radius, color, width)` | Analytical ring/outline. |
| `ctx.fill_triangle(x1, y1, x2, y2, x3, y3, color)` | Direct 2D polygon triangle. |
| `ctx.draw_svg_path(path_d, cx, cy, color, scale)` | Vector Bézier path (SVG format) tessellated to batch vertices. |
| `ctx.draw_suit_pip(suit, cx, cy, radius, color)` | Standard procedural suit pip (`hearts`, `diamonds`, `spades`, `clubs`). |
| `ctx.draw_text(text, x, y, size, color, rotate_180)` | Vector glyph typography via `eatsbits::ui::glyph`. |

---

## 4. Host C++ Engine Integration

### 4.1 Retained Mesh Storage

Each card is stored as a lightweight sequence of local-space 2D vertices:

```cpp
namespace solengine {

struct CardMesh {
    std::vector<eatsbits::ui::Vertex2D> vertices;
    float baseWidth{100.0f};
    float baseHeight{145.0f};
};

struct CompiledDeckMesh {
    CardMesh cardBack;
    // 52 card faces indexed by [suit * 13 + (rank - 1)]
    std::array<CardMesh, 52> cardFaces;
    bool isValid{false};
    std::string scriptPath;
};

} // namespace solengine
```

### 4.2 The `CardMeshBuilder` Canvas

The builder implements the drawing methods called by Eatscript. Instead of rendering to a screen or framebuffer, it appends vertices directly into a target `CardMesh`:

```cpp
class CardMeshBuilder {
public:
    explicit CardMeshBuilder(CardMesh& targetMesh) : mesh_(targetMesh) {}

    void fillRoundedRect(float x, float y, float w, float h, float radius, uint32_t color) {
        // Mode 2: Analytical SDF rounded rectangle recognized by BatchRenderer2D
        eatsbits::ui::Vertex2D v0{x, y, radius, 0.0f, color, eatsbits::ui::VertexMode::AnalyticalSdf};
        eatsbits::ui::Vertex2D v1{x + w, y, radius, 0.0f, color, eatsbits::ui::VertexMode::AnalyticalSdf};
        eatsbits::ui::Vertex2D v2{x, y + h, radius, 0.0f, color, eatsbits::ui::VertexMode::AnalyticalSdf};
        eatsbits::ui::Vertex2D v3{x + w, y + h, radius, 0.0f, color, eatsbits::ui::VertexMode::AnalyticalSdf};

        mesh_.vertices.push_back(v0);
        mesh_.vertices.push_back(v1);
        mesh_.vertices.push_back(v2);
        mesh_.vertices.push_back(v1);
        mesh_.vertices.push_back(v3);
        mesh_.vertices.push_back(v2);
    }

    void drawSvgPath(const std::string& pathD, float cx, float cy, float scale, uint32_t color) {
        eatsbits::ui::SvgPath svg(pathD);
        svg.tessellateToMesh(mesh_.vertices, cx, cy, scale, color);
    }

private:
    CardMesh& mesh_;
};
```

### 4.3 Zero-Overhead Real-Time Rendering

During gameplay, rendering a card requires zero matrix multiplication libraries and zero texture binds. The engine simply iterates over the card's pre-compiled vertex list, transforming coordinates with the card's screen position and 2.5D elevation/flip scale:

```cpp
void renderCompiledCard(
    eatsbits::ui::BatchRenderer2D& r,
    const CardMesh& mesh,
    float screenX,
    float screenY,
    float scaleX,
    float scaleY
) {
    auto& outBatch = r.acquireBatchBuffer();
    const size_t startIdx = outBatch.size();
    outBatch.reserve(startIdx + mesh.vertices.size());

    for (const auto& v : mesh.vertices) {
        eatsbits::ui::Vertex2D tv = v;
        // Apply 2.5D flip squish, scale, and screen placement
        tv.x = screenX + v.x * scaleX;
        tv.y = screenY + v.y * scaleY;
        outBatch.push_back(tv);
    }
}
```

---

## 5. Script Hot-Reloading & Error Handling

To make deck creation seamless, the desktop client includes an automatic file-watch mechanism:

1. **File Watcher:** On each tick of `runInteractiveWindow`, compare the last write timestamp of the active `.eat` script with `std::filesystem::last_write_time`.
2. **Atomic Recompile:** If modified:
   ```cpp
   CompiledDeckMesh newDeck = compileDeckFromEatscript(activeScriptPath);
   if (newDeck.isValid) {
       activeDeckMesh = std::move(newDeck);
       std::cout << "[SolEngine] Hot-reloaded custom deck: " << activeScriptPath << std::endl;
   } else {
       std::cerr << "[SolEngine] Script compilation error; retaining previous deck." << std::endl;
   }
   ```
3. **Safety Fallback:** If the script encounters syntax or runtime errors:
   - The engine logs the offending line and error string to the terminal and HUD notification banner.
   - Gameplay continues uninterrupted using the built-in procedural C++ cards.

---

## 6. Implementation Milestones

1. **Phase 1: Host ABI Canvas Bindings**
   - Bind `CardMeshBuilder` methods to Eatscript's [`HostFunction`](file:///c:/git/solengine/include/eatsbits/eatscript/evaluator.hpp#L15) registry (`ctx.fill_rounded_rect`, `ctx.draw_svg_path`, `ctx.draw_text`).
2. **Phase 2: One-Time Pre-Bake Pipeline**
   - Implement `compileDeckFromEatscript(scriptPath)` to invoke `on_draw_card_back` and `on_draw_card_face` for all 52 combinations, outputting `CompiledDeckMesh`.
3. **Phase 3: TableRenderer Integration**
   - In [`TableRenderer::renderBoard`](file:///c:/git/solengine/src/render/table_renderer.cpp#L480), check if a custom deck mesh is active; dispatch to `renderCompiledCard()` instead of hardcoded procedural card drawing.
4. **Phase 4: CLI & UI Hot-Reload**
   - Add CLI parameter `--deck <path.eat>` in `main.cpp`.
   - Add file timestamp polling in `runInteractiveWindow` for live editing.

