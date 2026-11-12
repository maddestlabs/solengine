cmake_minimum_required(VERSION 3.20)
project(solengine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if(MSVC)
    add_compile_options(/W4 /O2 /permissive- /wd4244 /wd4267 /wd4324 /openmp)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
elseif(EMSCRIPTEN)
    add_compile_options(-Wall -Wextra -O3 -fexceptions)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    add_compile_options(-Wall -Wextra -O3)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# 1. Bundled GLFW 3.4 Windowing
if(NOT EMSCRIPTEN)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/glfw EXCLUDE_FROM_ALL)
endif()

# 2. Standalone Eatscript Scripting & Host ABI Library
add_library(eats_script STATIC
    src/eatscript/lexer.cpp
    src/eatscript/parser.cpp
    src/eatscript/vm.cpp
    src/eatscript/transpiler.cpp
    src/eatscript/evaluator.cpp
    src/eatscript/host_abi.cpp
    src/eatscript/stdlib.cpp
    src/eatscript/repl.cpp
    src/eatscript/dispatch_scanner.cpp
)
target_include_directories(eats_script PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 3. SolEngine Core Library (Headless Solitaire Rules, Procedural 2D Batch Renderer, SVG Path & Foley)
add_library(solengine_core STATIC
    src/game/solitaire_board.cpp
    src/game/solitaire_solver.cpp
    src/game/auto_play_controller.cpp
    src/audio/audio_foley.cpp
    src/ui/batch_renderer_2d.cpp
    src/ui/svg_path.cpp
    src/ui/base64.cpp
    src/ui/monospace_font_8x16.cpp
    src/ui/theme.cpp
    src/ui/glyph/ebf_font.cpp
    src/ui/glyph/vector_font.cpp
    src/ui/glyph/glyph_packs.cpp
    third_party/stb/stb_image.c
)
target_include_directories(solengine_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
)
if(NOT EMSCRIPTEN)
    target_include_directories(solengine_core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wgpu/include
    )
endif()
target_compile_definitions(solengine_core PUBLIC EATS_ENABLE_WEBGPU)
target_link_libraries(solengine_core PUBLIC eats_script)

if(WIN32 AND NOT EMSCRIPTEN)
    set(WGPU_NATIVE_LIB "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wgpu/lib/wgpu_native.dll.lib")
    target_link_libraries(solengine_core PUBLIC glfw ole32 winmm user32 gdi32 "${WGPU_NATIVE_LIB}")
elseif(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    target_link_libraries(solengine_core PUBLIC pthread dl m)
endif()

# 3b. EatsTextures Procedural Synthesis Core (Headless SIMD Synthesis)
add_library(eats_proctex_core STATIC
    src/proctex/field_ops.cpp
    src/proctex/proctex_runtime.cpp
    src/proctex/png_io.cpp
)
target_include_directories(eats_proctex_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
)
target_link_libraries(eats_proctex_core PUBLIC eats_script)
if(NOT EMSCRIPTEN)
    if(MSVC)
        target_compile_options(eats_proctex_core PRIVATE /openmp)
    else()
        target_compile_options(eats_proctex_core PRIVATE -fopenmp)
        target_link_libraries(eats_proctex_core PUBLIC -fopenmp)
    endif()
endif()

# 4. SolEngine Procedural Table Renderer Library
add_library(solengine_render STATIC
    src/render/table_renderer.cpp
    src/render/dynamic_light.cpp
    src/render/card_texture_cache.cpp
    src/render/card_canvas.cpp
    src/render/card_back.cpp
    src/render/procedural_background.cpp
    src/render/proctex_api.cpp
    src/game/settings_manager.cpp
    src/game/session_manager.cpp
    src/game/game_definition.cpp
    src/ui/svg_icon_renderer.cpp
    src/ui/settings_dialog.cpp
    src/ui/seed_dialog.cpp
    src/ui/loading_dialog.cpp
    src/ui/game_browser_dialog.cpp
    src/ui/clockwork_theme.cpp
)
target_include_directories(solengine_render PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nanosvg
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw/deps
)
if(NOT EMSCRIPTEN)
    target_include_directories(solengine_render PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wgpu/include
    )
endif()

if(EMSCRIPTEN)
    target_compile_options(solengine_core PUBLIC --use-port=emdawnwebgpu)
    target_compile_options(solengine_render PUBLIC --use-port=emdawnwebgpu)
    target_link_libraries(solengine_render PUBLIC solengine_core eats_proctex_core)

    # 5. Main SolEngine WebAssembly & WebGPU Application
    add_executable(solengine_web src/main.cpp)
    target_compile_options(solengine_web PRIVATE -O3 -msimd128 -fexceptions --use-port=emdawnwebgpu)
    target_compile_definitions(solengine_web PRIVATE NDEBUG EATS_ENABLE_WEBGPU)
    target_link_libraries(solengine_web PRIVATE solengine_render)
    set_target_properties(solengine_web PROPERTIES
        OUTPUT_NAME "solengine"
        SUFFIX ".html"
        LINK_FLAGS "-O3 -msimd128 -sNO_EXIT_RUNTIME=1 -fexceptions --use-port=emdawnwebgpu -sUSE_GLFW=3 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8388608 -sINITIAL_MEMORY=67108864 -sEXPORTED_FUNCTIONS=['_main','_solengine_load_file_from_web','_solengine_set_search_query'] -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS'] --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/decks/solenshade.eats@/decks/solenshade.eats --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/decks/classic.eats@/decks/classic.eats --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/decks/minimal.eats@/decks/minimal.eats --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/decks/royal_casino.eats@/decks/royal_casino.eats --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/decks/engraved_intaglio.eats@/decks/engraved_intaglio.eats --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/decks/imperial_baroque.eats@/decks/imperial_baroque.eats --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/decks/tavern_klondike.eats@/decks/tavern_klondike.eats --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/decks/baroque_arches.eats@/decks/baroque_arches.eats --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/themes@/themes --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/games@/games --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/sounds@/sounds --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/web/index.html"
    )
    add_custom_command(TARGET solengine_web POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/solengine.html"
            "${CMAKE_BINARY_DIR}/index.html"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/web/manifest.json"
            "${CMAKE_BINARY_DIR}/manifest.json"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/web/sw.js"
            "${CMAKE_BINARY_DIR}/sw.js"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/web/app_icon.png"
            "${CMAKE_BINARY_DIR}/app_icon.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/web/favicon.ico"
            "${CMAKE_BINARY_DIR}/favicon.ico"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/web/icon-192.png"
            "${CMAKE_BINARY_DIR}/icon-192.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/web/icon-512.png"
            "${CMAKE_BINARY_DIR}/icon-512.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/web/solengine_logo.svg"
            "${CMAKE_BINARY_DIR}/solengine_logo.svg"
        COMMENT "Copying PWA manifest, service worker, icons, and index.html to web root"
    )
else()
    target_link_libraries(solengine_render PUBLIC solengine_core eats_proctex_core glfw)
    if(WIN32)
        set(WGPU_NATIVE_LIB "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wgpu/lib/wgpu_native.dll.lib")
        target_link_libraries(solengine_render PUBLIC
            user32 gdi32 shell32 opengl32 "${WGPU_NATIVE_LIB}"
        )
    endif()

    # 5. Main SolEngine Executable
    if(WIN32)
        add_executable(solengine WIN32 src/main.cpp src/solengine.rc)
        if(MSVC)
            set_target_properties(solengine PROPERTIES
                LINK_FLAGS "/ENTRY:mainCRTStartup"
            )
        endif()
    else()
        add_executable(solengine src/main.cpp)
    endif()
    target_link_libraries(solengine PRIVATE solengine_render)

    if(WIN32)
        add_custom_command(TARGET solengine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wgpu/lib/wgpu_native.dll"
                "$<TARGET_FILE_DIR:solengine>/wgpu_native.dll"
        )
    endif()
endif()
