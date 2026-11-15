# SolEngine Performance & Architecture Audit (Web / WebGPU)

This document captures the findings from a full codebase audit focused on WebAssembly + WebGPU performance, unnecessary per-frame work, memory usage, and architectural bottlenecks.

Items are ordered so **Quick Wins** come first, followed by **Memory & Asset Streaming**, **WebGPU Render Pass Batching**, and **Longer-Term Web Architecture**. Each item includes **Overlaps & Friction Notes** so changes don't conflict with one another.

---

## Phase 1: Quick Wins (High ROI, Low Risk)

### [x] 1.1 Fix Emscripten Compiler Flags Across All Targets (`CMakeLists.txt`)
- **Finding:**
  - `-msimd128` is only applied to `solengine_web` (`src/main.cpp`), **not** to `eats_script`, `solengine_core`, `eats_proctex_core`, or `solengine_render` ([`CMakeLists.txt`](CMakeLists.txt#L12-L145)). Procedural texture synthesis, card rasterization, audio mixing, and batch vertex math are all running without WASM SIMD.
  - `-fexceptions` is enabled globally on Emscripten ([`CMakeLists.txt`](CMakeLists.txt#L13)) without `-fwasm-exceptions`, which forces Emscripten to generate JS `invoke_*` try/catch trampolines around C++ calls—bloating WASM size and slowing down tight loops.
  - Release web builds do not enable `-flto` (Link-Time Optimization).
- **Action:**
  - Add `-msimd128` and `-fwasm-exceptions` (or `-fno-exceptions` if no runtime exceptions are thrown) to the global `EMSCRIPTEN` compile & link options so every static library and `solengine_web` shares the same flags.
  - Test `-flto` on Release web builds.
- **Overlap / Friction:**
  - **ABI consistency:** All CMake targets (`eats_script`, `solengine_core`, `eats_proctex_core`, `solengine_render`, `solengine_web`) and `LINK_FLAGS` must use the *same* exception flag (`-fwasm-exceptions` or `-fno-exceptions`). Mixing `-fexceptions` and `-fwasm-exceptions` across targets will fail at link time.

### [x] 1.2 Cache `SolitaireBoard::gameStatus()` Instead of Running Eatscript Every Frame
- **Finding:**
  - In [`SolitaireBoard::gameStatus`](src/game/solitaire_board.cpp#L2558-L2564) and [`SolitaireBoard::isVictory`](src/game/solitaire_board.cpp#L2599-L2603), scripted games invoke `scriptEngine_.invokeGetStatus(ctx)`.
  - Every frame at 60 FPS, `updateAnimations()` calls `isVictory()` ([`solitaire_board.cpp`](src/game/solitaire_board.cpp#L1403)), and `renderBoard()` calls `board.isVictory()` (inside the Foundation pile loop and at line 2404) plus `board.gameStatus()` ([`table_renderer.cpp`](src/render/table_renderer.cpp#L2285-L2501)).
  - Because [`Evaluator`](src/eatscript/evaluator.cpp#L458-L509) is a tree-walking AST interpreter that allocates `std::vector<Value>` and `std::map<std::string, Value>` on every call, this runs the interpreter **3+ times per frame** even when the board hasn't changed.
- **Action:**
  - Cache `cachedGameStatus_` (and a `statusDirty_` flag) on `SolitaireBoard`. Only re-evaluate `get_status` when a move, stock click, deal, undo, redo, or blackboard mutation occurs.
- **Overlap / Friction:**
  - Ensure `captureSnapshot()` / `restoreSnapshot()` ([`solitaire_board.cpp`](src/game/solitaire_board.cpp#L2581-L2597)) also mark `statusDirty_ = true` so solver/auto-play rollouts never read a stale cached status.

### [x] 1.3 Replace Per-Frame `EM_ASM_DOUBLE` DOM Layout Queries with `ResizeObserver`
- **Finding:**
  - [`InteractiveContext::updateAndRender`](src/render/table_renderer.cpp#L4756-L4767) executes 3 separate `EM_ASM_DOUBLE` calls every frame, reading `container.clientWidth` and `container.clientHeight`. Reading `clientWidth`/`clientHeight` can force a browser layout reflow, and crossing the WASM ↔ JS boundary 3× per frame is unnecessary.
- **Action:**
  - Register a `ResizeObserver` (and `devicePixelRatio` media query listener) once at startup in JS (`web/index.html` or `EM_ASM` init) that pushes updated CSS/DPR dimensions into C++ only when a resize actually occurs.
- **Overlap / Friction:**
  - If you also add a **DPR cap** (e.g., capping `dpr <= 2.0` on mobile/4K displays to reduce 4× MSAA fill-rate), apply the cap in one place (inside the resize callback) so `fbWidth`/`fbHeight`, `msaaTexture_`, and `postFxSceneTexture_` stay in sync.

### [x] 1.4 Replace `sin`/`cos` in Perlin Noise `hashGrad2D` with a Gradient Lookup Table
- **Finding:**
  - [`hashGrad2D`](src/proctex/field_ops.cpp#L52-L56) computes `std::cos(angle)` and `std::sin(angle)` for all 4 lattice corners on every pixel and every octave of Perlin noise. On Web (single-threaded, no OpenMP), this makes procedural theme/texture baking noticeably slower.
- **Action:**
  - Replace the trig calls with a precomputed 16-entry (or 8-entry) unit-gradient `constexpr` array indexed by `hashU32(x, y, seed) & 15u`.
- **Overlap / Friction:**
  - Changing gradient selection slightly alters procedural noise patterns for existing seeds. Verify visually against a couple of `.eats` themes (e.g., `themes/leather.eats`, `themes/marble.eats`) if exact pixel-for-pixel determinism with old baked caches matters.

### [x] 1.5 Stop Redundant `flush()` in `BatchRenderer2D::setBlendMode()` on WebGPU
- **Finding:**
  - [`BatchRenderer2D::setBlendMode`](src/ui/batch_renderer_2d.cpp#L4128-L4135) unconditionally calls `flush()` before forwarding to `backend_->setBlendMode(mode)`. However, `WebGPURenderBackend` does not override `setBlendMode()` (so it's a no-op). As a result, [`drawStaticTableChrome`](src/render/table_renderer.cpp#L1440-L1459) forces two extra render passes + 4× MSAA resolves every frame for `BlendMode::Multiply` → `BlendMode::Normal` with zero actual pipeline change.
- **Action:**
  - Either skip `flush()` when the active backend doesn't support/change pipelines for that blend mode, or implement a second `WGPURenderPipeline` for `BlendMode::Multiply` (and only flush/switch pipelines inside a single render pass—see Phase 3).
- **Overlap / Friction:**
  - Directly tied to **Phase 3 (Single-Pass Batching)**. Once Phase 3 keeps a single render pass open, `setBlendMode` (if implemented on WebGPU) only needs a `wgpuRenderPassEncoderSetPipeline` call rather than ending and restarting the render pass.

### [x] 1.6 Avoid Per-Frame Shader String Comparison & Vector Allocations
- **Finding:**
  - [`InteractiveContext::updateAndRender`](src/render/table_renderer.cpp#L5456) calls `renderer.setPostFxShader(pfx->wgslSource)` every frame, which compares the full WGSL string against `cachedPostFxWgsl_` byte-by-byte ([`batch_renderer_2d.cpp`](src/ui/batch_renderer_2d.cpp#L3064)).
  - [`TableRenderer::renderBoard`](src/render/table_renderer.cpp#L2256-L2276) constructs and destroys `animatingCards` and `restingIndices` `std::vector`s every frame (and per pile).
  - [`TableRenderer::renderBoard`](src/render/table_renderer.cpp#L2187-L2199) calls `ProceduralBackgroundSystem::instance().invalidateCache()` when `board.variant()` or `showMarkings` changes, even if the theme and dimensions did not change.
- **Action:**
  - Compare `postFxId` / `postFxOption` (or a pointer/version ID) instead of the full WGSL string.
  - Reuse persistent scratch vectors (`.clear()`) in `renderBoard`.
  - Only invalidate `ProceduralBackgroundSystem` when theme, tint, option, or dimensions change.

---

## Phase 2: Memory Usage & WASM Heap Stability

### [x] 2.1 Stream Card Slots Directly into the GPU Atlas (`CardTextureCache::buildGpuAtlas`)
- **Finding:**
  - [`CardTextureCache::buildGpuAtlas`](src/render/card_texture_cache.cpp#L2971-L2976) allocates a temporary **64 MB** `std::vector<uint32_t> atlasRgba(4096 * 4096, 0)` on the WASM heap to assemble the atlas before calling `wgpuQueueWriteTexture`.
- **Action:**
  - Upload each card slot directly to its `(slotX, slotY)` sub-rectangle in `rgbaTexture_` via `wgpuQueueWriteTexture`, eliminating the 64 MB temporary buffer entirely.
- **Overlap / Friction:**
  - **Must be done before or together with 2.2!** Currently `buildGpuAtlas` reads from `tex.argb32` / `tex.mip1` and swizzles ARGB32 → RGBA8. Update `buildGpuAtlas` to read directly from `tex.rgba` (already in RGBA8 layout!), which removes both the swizzle loop and the dependency on `argb32`/`mip1`.

### [x] 2.2 Drop Redundant Software Buffers (`argb32`, `mip1`, `mip2`) on WebGPU
- **Finding:**
  - [`CardTexture`](include/solengine/card_texture_cache.hpp#L27-L51) keeps `rgba`, `argb32`, `mip1`, and `mip2` in CPU RAM for all 53+ cards (~89 MB at 360×504) even on WebGPU, where `argb32`/`mip1`/`mip2` are only used by the software rasterizer.
- **Action:**
  - When running on WebGPU (and not in headless `--render-png` mode), skip generating/retaining `argb32`, `mip1`, and `mip2`. Also release `rgba` after `buildGpuAtlas` uploads it if card conditioning isn't re-applied from `rgba` (keep `baseTextures_` or re-load on wear/stock change only if needed).
- **Overlap / Friction:**
  - Depends on **2.1** (`buildGpuAtlas` reading `tex.rgba` instead of `tex.argb32`).
  - Verify `renderToPng` / software backend fallback still generates `argb32`/`mip1`/`mip2` when needed.

### [x] 2.3 Tune `-sINITIAL_MEMORY` in `CMakeLists.txt`
- **Finding:**
  - [`CMakeLists.txt`](CMakeLists.txt#L149) sets `-sINITIAL_MEMORY=67108864` (64 MB) with `-sALLOW_MEMORY_GROWTH=1`. Because `CardTextureCache` + the 64 MB `atlasRgba` + baked backgrounds exceed 64 MB immediately, Emscripten is forced to grow and resize the WASM linear memory multiple times during startup and deck switches.
- **Action:**
  - Raise `-sINITIAL_MEMORY` (e.g., `134217728` / 128 MB) to eliminate startup memory-growth stalls.
- **Overlap / Friction:**
  - After **2.1** and **2.2** land, re-measure peak WASM heap usage so `INITIAL_MEMORY` isn't set higher than necessary (important for mobile browser tab memory limits).

---

## Phase 3: WebGPU Single-Pass Command & Vertex Batching Revamp

> **Why this matters:** Right now, [`WebGPURenderBackend::renderBatch`](src/ui/batch_renderer_2d.cpp#L2819-L2927) creates a new `WGPUCommandEncoder`, begins a new `WGPURenderPassEncoder`, calls `wgpuQueueWriteBuffer` to **offset `0` of `vertexBuffer_`**, ends the pass, and calls `wgpuQueueSubmit` **on every `flush()`**. With 4× MSAA enabled, every mid-frame flush forces a full-screen MSAA resolve, a `LoadOp_Load` on the next pass, a buffer-0 overwrite hazard, and a WASM → JS → GPU-process IPC round-trip.

### [x] 3.1 Accumulate All Frame Draws into One Command Encoder & Render Pass
- **Action:**
  - Open **one** `WGPUCommandEncoder` and **one** `WGPURenderPassEncoder` at the start of the frame (clearing once with `WGPULoadOp_Clear` and resolving MSAA once at the end).
  - Instead of flushing into separate encoders/submissions on scissor changes (`pushScissor`/`popScissor`), pipeline/blend-mode changes, or bitmap draws, record `DrawCommand { uint32_t vertexOffset, vertexCount; Rect2D scissor; BlendMode blend; }` entries into a per-frame command list.
  - At `endFrame()` (or before `applyPostFxPass()`), upload all accumulated vertices for the frame in **one** `wgpuQueueWriteBuffer` call into a bump/ring-allocated vertex buffer, then replay the `DrawCommand` list inside the single `WGPURenderPassEncoder` (`SetScissorRect`, `SetPipeline`, `Draw(count, 1, offset, 0)`), and call `wgpuQueueSubmit` **once** for the frame.
- **Overlap / Friction (Critical to get right!):**
  - **Texture writes (`renderRgbaRegion` / `updateFontTexture`):** Under a deferred/single-pass model, `wgpuQueueWriteTexture` to `bgTexture_` or `rgbaTexture_` *cannot* happen mid-pass if an earlier draw in the same pass also reads that texture (the `writeTexture` would overwrite the texture before the GPU executes the earlier draw).
    - **Rule:** Perform all texture uploads (`bgTexture_`, `fontTexture_`, `rgbaTexture_`) *before* encoding the render pass, or give `bgTexture_` and `rgbaTexture_` separate bindings (already partially separated via `bgTexture_` vs `rgbaTexture_`) and avoid mutating the same texture twice in one frame.
  - **PostFX pass (`applyPostFxPass`):** When PostFX is active, the main pass renders into `postFxSceneView_`, ends, and then a second render pass draws the fullscreen PostFX quad into `currentBackBufferView_`. Both passes can still be recorded onto the **same `WGPUCommandEncoder`** and submitted in a single `wgpuQueueSubmit` at `endPass()`.
  - **UI rendered after PostFX (`!activeCfg.postFxAffectUi`):** [`updateAndRender`](src/render/table_renderer.cpp#L5527-L5576) calls `renderer.applyPostFxPass()` and then draws dialogs/menus on top. That naturally splits into 2 passes (Scene → `postFxSceneView_`, then PostFX + UI → `currentBackBufferView_`) on the **same `WGPUCommandEncoder`**, still resulting in **1 `wgpuQueueSubmit`** per frame.

### [~] 3.2 Offscreen `WGPUTexture` Caching for Static Table Chrome on WebGPU (Deferred / Low ROI)
- **Finding:**
  - [`BatchRenderer2D::beginChromeRecording` / `endChromeRecording`](src/ui/batch_renderer_2d.cpp#L4361-L4389) only snapshots pixels on the software backend (`snapshotFramebuffer`), so `drawStaticTableChrome` runs every frame on WebGPU.
- **Note:**
  - Because `dynamicLight` sways continuously and `ProceduralBackgroundSystem` renders the full-screen background with `uniforms.lightPos` in the shader, baking static chrome into an offscreen texture cannot skip the full-screen lit background pass without freezing table lighting. With **3.1** already batching `drawStaticTableChrome` into the single render pass, its CPU cost is negligible.
- **Additional Optimizations Landed Instead:**
  - Precomputed column UV/wrapping indices in [`bakePbrSurface`](src/render/procedural_background.cpp#L515-L565) outside the row loop to eliminate `bakeW * bakeH` floors/modulos during theme baking.
  - Enabled `-flto` across all Emscripten targets in [`CMakeLists.txt`](CMakeLists.txt#L12-L149).

---

## Phase 4: Deeper Web Architecture & Threading

### [x] 4.1 Move Heavy Baking & Solver Work Off the Browser Main Thread
- **Finding:**
  - `std::thread` is disabled under `__EMSCRIPTEN__` in [`table_renderer.cpp`](src/render/table_renderer.cpp#L3278-L3947) and [`auto_play_controller.cpp`](src/game/auto_play_controller.cpp#L32-L74), causing deck conditioning, procedural theme baking, and solver/odds analysis to freeze the browser UI thread.
- **Action:**
  - Either enable Emscripten pthreads (`-pthread` + `-sPTHREAD_POOL_SIZE`, note: `serve.py` / `web/sw.js` already mention COOP/COEP headers in `build-web.ps1`, which `SharedArrayBuffer` requires) **or** time-slice / yield heavy loops and run solver/baking in a dedicated Web Worker.
- **Overlap / Friction:**
  - Enabling `-pthread` affects **all** targets in `CMakeLists.txt` (every static lib must be compiled with `-pthread`) and requires strict COOP/COEP headers on every hosting environment. Check deployment constraints before flipping `-pthread` globally.

### [x] 4.2 Configurable MSAA & DPR Scaling for Web (DPR Cap Landed)
- **Finding:**
  - `renderer.setAntiAliasingMode(4)` ([`table_renderer.cpp`](src/render/table_renderer.cpp#L5718)) + uncapped `window.devicePixelRatio` can push 4× MSAA at 3840×2160+ on high-DPI laptops/phones, stressing WebGPU fragment shaders (especially `baseMode == 3u` card conditioning + lighting).
- **Action:**
  - Capped Web DPR to `2.0f` (`std::clamp(..., 1.0f, 2.0f)`) in [`table_renderer.cpp`](src/render/table_renderer.cpp#L4770) while preserving 4× MSAA. (Optional: expose an MSAA toggle later if needed for low-end mobile GPUs.)

---

## Quick Overlap & Conflict Reference

| Change | Interacts With | Rule to Avoid Friction |
| :--- | :--- | :--- |
| **1.1** `-fwasm-exceptions` / `-msimd128` | **4.1** `-pthread` | Apply compiler/ABI flags globally to *all* CMake targets, never only `solengine_web`. |
| **1.2** Cached `gameStatus()` | Solver / `AutoPlayController` | Invalidate `statusDirty_` inside `restoreSnapshot()` as well as all move/deal/blackboard mutators. |
| **1.3** `ResizeObserver` | **4.2** DPR cap | Apply any DPR cap in the resize handler before updating `fbWidth`/`fbHeight` and `renderer.resize()`. |
| **2.1** Direct `wgpuQueueWriteTexture` in `buildGpuAtlas` | **2.2** Dropping `argb32`/`mip1`/`mip2` | Land **2.1** (reading `tex.rgba`) *before* **2.2**, or `buildGpuAtlas` will have no source pixels on WebGPU. |
| **2.3** Raise `-sINITIAL_MEMORY` | **2.1 & 2.2** Heap reductions | Bump `INITIAL_MEMORY` now as a stopgap; re-tune downward after **2.1 & 2.2** remove ~120+ MB of peak allocations. |
| **3.1** Single-pass WebGPU batching | **1.5** `setBlendMode`, **3.2** Chrome FBO, PostFX | Upload textures *before* opening the render pass; keep PostFX + post-FX UI as passes on the *same* `WGPUCommandEncoder` with one `wgpuQueueSubmit`. |
