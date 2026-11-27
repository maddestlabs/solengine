# SolEngine WebGL2 Legacy Web Build Plan

## Goal
Add a **completely isolated WebGL2 (OpenGL ES 3.0) WebAssembly build pipeline** so SolEngine runs smoothly on older browsers and still-capable hardware without WebGPU (such as older iPads running iPadOS 15.2+ / Safari 15.2+ and older desktop/mobile browsers).

## Hard Constraints
- **Do NOT modify the existing build pipelines**:
  - Root `CMakeLists.txt` — untouched
  - `build.ps1` — untouched
  - `build-web.ps1` — untouched
  - `web/index.html` — untouched
  - All WebGPU code paths — untouched
- Any hooks added to shared C++ headers/sources must be strictly gated behind `#ifdef EATS_ENABLE_WEBGL2` (or backend-neutral `isGpuBackend()` helpers) so native and WebGPU builds compile identically.

---

## Architecture & File Layout

```
solengine/
├── build-webgl.ps1                    # Standalone Emscripten + WebGL2 build & optional serve script
├── web-webgl/
│   ├── CMakeLists.txt                 # Standalone CMake project referencing ../src and ../include
│   ├── webgl_batch_backend.cpp        # WebGL2 / GLES3 IBatchRenderBackend implementation + GLSL ES 3.00 shaders
│   └── index.html                     # WebGL2 shell HTML (no navigator.gpu or DecompressionStream required)
└── build-webgl/                       # Isolated output directory (solengine.html, index.html, .js, .wasm, .data)
```

---

## Minimal Shared C++ Hooks

1. **`include/eatsbits/ui/batch_renderer_2d.hpp`**
   - Add `WebGL2` to `enum class RenderBackendType` (lines 48–52).
   - Add inline helper:
     ```cpp
     [[nodiscard]] inline bool isGpuBackend(RenderBackendType type) noexcept {
         return type == RenderBackendType::WebGPU || type == RenderBackendType::WebGL2;
     }
     ```
   - Declare factory under `#ifdef EATS_ENABLE_WEBGL2`:
     ```cpp
     std::unique_ptr<IBatchRenderBackend> createWebGL2BatchBackend();
     ```

2. **`src/ui/batch_renderer_2d.cpp`**
   - In `BatchRenderer2D::initialize(...)`, under `#ifdef EATS_ENABLE_WEBGL2`, instantiate `createWebGL2BatchBackend()` when `RenderBackendType::WebGL2` is requested.
   - Ensure `EATS_ENABLE_WEBGPU` blocks remain cleanly skipped when only `EATS_ENABLE_WEBGL2` is defined.

3. **`src/render/table_renderer.cpp`**
   - Update the 8 `RenderBackendType::WebGPU` checks (lines ~932, 1229, 3672, 5125, 8811, 10813, 10815, 10830):
     - Replace capability/atlas checks with `eatsbits::ui::isGpuBackend(r.getBackendType())`.
     - For renderer initialization (lines ~10813–10815), select `RenderBackendType::WebGL2` when `#if defined(EATS_ENABLE_WEBGL2) && !defined(EATS_ENABLE_WEBGPU)`.

---

## Standalone Build Pipeline

### `web-webgl/CMakeLists.txt`
- Sets `set(SOLENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")`.
- Compiles the same `eats_script`, `solengine_core`, `eats_proctex_core`, and `solengine_render` source lists from `${SOLENGINE_ROOT}/src/...`, plus `${CMAKE_CURRENT_SOURCE_DIR}/webgl_batch_backend.cpp`.
- Defines `EATS_ENABLE_WEBGL2` (does **not** define `EATS_ENABLE_WEBGPU`, does **not** pass `--use-port=emdawnwebgpu`).
- Auto-discovers and preloads `${SOLENGINE_ROOT}/{decks,themes,card_backs,postfx,games,sounds,ambience,presets}/*.eats` into `solengine.data` (mirroring root `CMakeLists.txt`).
- **Older iPad / Safari 15.2+ compatible Emscripten flags**:
  - `-sUSE_WEBGL2=1` (and `-sUSE_GLFW=3` or `emscripten_webgl_create_context`)
  - **Omit `-msimd128`** (WASM SIMD requires Safari 16.4+; omitting opens support back to iPadOS 15.2+)
  - Evaluate `-fwasm-exceptions` (Safari 15.2+ supported; or `-fexceptions` if targeting 15.0–15.1)
  - Evaluate `-pthread` vs single-threaded fallback (older 2–3 GB iPads and non-COOP/COEP hosts are much more stable single-threaded or with conservative `-sINITIAL_MEMORY=67108864` + `-sALLOW_MEMORY_GROWTH=1`)
  - Export the same JS bridge functions: `['_main','_solengine_load_file_from_web','_solengine_load_game_from_web','_solengine_set_search_query','_solengine_on_web_resize']`

### `build-webgl.ps1`
- Mirrors `build-web.ps1` (`-Clean`, `-NoServe`, `-Serve`, `-Port 8081`, EmSDK auto-detection, `tools\generate_games_manifest.py`).
- Runs:
  ```powershell
  emcmake cmake -S web-webgl -B build-webgl -G Ninja
  cmake --build build-webgl
  ```
- Copies PWA/icon assets and serves `build-webgl/` when `-Serve` is active (without touching `build-web/`).

### `web-webgl/index.html`
- Clean WebGL2 shell based on `web/index.html` without:
  - `navigator.gpu` / `webgpuDevicePromise` initialization
  - Client-side `DecompressionStream` requirement (Safari 16.4+); rely on standard HTTP compression or uncompressed payloads with graceful fallback.
- Checks `canvas.getContext('webgl2')` and displays a clear error only if WebGL2 is unavailable.

---

## `WebGLBatchBackend` Implementation (`web-webgl/webgl_batch_backend.cpp`)

Implements `eatsbits::ui::IBatchRenderBackend` (`include/eatsbits/ui/batch_renderer_2d.hpp`, lines 67–158):

### Core GPU Resources
- WebGL2 context via `emscripten_webgl_create_context("#canvas", ...)` (antialias / alpha: false / preserveDrawingBuffer: false / powerPreference: high-performance).
- Dynamic VAO + VBO for `Vertex2D` (`x, y, u, v, color [uint32], mode [uint32], pad[2], localUV[2]`):
  - Use `glVertexAttribPointer` for floats and `glVertexAttribIPointer` for integer `color` / `mode` attributes (native to WebGL2 / GLSL ES 3.00).
- Uniform Buffer Objects (UBOs) or uniforms for:
  - Viewport size, camera transform (`invZoom`, `focusX`, `focusY`, `active`), shader time
  - Global light (`LightSource2D`, color, ambient, `sceneDarkness`, `lightStrength`)
  - Scene atmosphere (`goboType`, `goboSoftness`, `dustMotesIntensity`, steady light pos, `lightAngle`, `cloudOpacity`)
  - Table surface lighting (`lacquerStrength`, `bumpStrength`)
- Textures:
  - Font atlas (`GL_R8` / `GL_RGBA8`, updated via `glTexSubImage2D`)
  - Card RGBA atlas (`GL_RGBA8`, updated via `glTexSubImage2D` in `updateRgbaTexture`)
  - Background table texture (`GL_RGBA8`, invalidated via `invalidateBgTexture`)
  - Scratch/ad-hoc RGBA texture for `renderRgba` / `renderRgbaRegion`
- Framebuffers (FBOs):
  - Main scene offscreen FBO (supports `setInternalMaxResolution`, `setPixelArtMode` nearest/sharp-bilinear blit, and backdrop blur source)
  - Optional MSAA renderbuffer (`glRenderbufferStorageMultisample` + `glBlitFramebuffer`) when AA is enabled and not in pixel-art mode
  - Shadow layer FBO (`beginShadowLayer` / `endShadowLayer`)
  - Ping-pong FBO for `applyBackdropBlur`
- State:
  - Scissor rect (`setScissorRect` / `clearScissorRect` with Y-flip for OpenGL bottom-left origin)
  - Blend modes (`Normal`, `Multiply`, `Screen`, `Add`, `Overlay`, `SoftLight`)

### Shaders to Port (WGSL -> GLSL ES 3.00 `#version 300 es`)
Reference WGSL sources live in `src/ui/batch_renderer_2d.cpp` around lines 3638, 4430, 5162, 5219, and 5491:
1. **Main 2D Batch Shader**: handles all `VertexMode` values (`Color`, `FontAtlas`, `AnalyticalSdf`, `RgbaTexture`, `MonospaceAtlas`, `OpaqueMeshNoAA`, `BgTexture`), per-pixel lighting, shadow gobos, dust motes, and table bump/lacquer shading.
2. **Shadow Layer Composite Shader**: composites the shadow layer FBO onto the scene.
3. **Backdrop Blur Shader**: separable or box/kawase blur + dim factor for modal dialogs (`applyBackdropBlur`).
4. **Present / Resolution Blit Shader**: upscales internal FBO to canvas (smooth bilinear or crisp pixel-art mode).
5. **PostFX Pass (v1 vs v2)**:
   - **v1 (MVP)**: `setPostFxShader()` returns `false` (no-op graceful fallback); core game, lighting, gobos, cards, audio, and UI run at 100% fidelity.
   - **v2 (Optional follow-up)**: Add GLSL ES 3.00 emitter alongside `src/eatscript/wgsl_transpiler.cpp` for custom `.eats` PostFX shaders.

---

## Suggested Implementation Order (for Next Session)
1. Add the minimal `#ifdef EATS_ENABLE_WEBGL2` hooks in `batch_renderer_2d.hpp`, `batch_renderer_2d.cpp`, and `table_renderer.cpp` (verify native `Release` build still compiles cleanly with `-NoRun -NoLaunch`).
2. Create `web-webgl/CMakeLists.txt`, `web-webgl/index.html`, and `build-webgl.ps1` with a stub `webgl_batch_backend.cpp` to confirm end-to-end Emscripten linking and `.data` asset preloading first.
3. Implement `WebGLBatchBackend` core pass (VAO/VBO, main 2D GLSL ES 3.00 shader, font/card/bg textures, blend modes, scissor).
4. Add FBO passes (shadow layer, backdrop blur, internal resolution / pixel-art blit).
5. Port lighting, gobo, dust motes, and table bump math into the main GLSL ES 3.00 fragment shader.
6. Test on desktop browser (with WebGL2) and older iPad/Safari; tune memory/thread flags as needed.
