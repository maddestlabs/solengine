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
    add_compile_options(-Wall -Wextra -O3 -msimd128 -fwasm-exceptions -flto -pthread)
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
    src/eatscript/wgsl_transpiler.cpp
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
    src/game/script_rule_engine.cpp
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
    src/render/lowpoly_deck_generator.cpp
    src/render/card_canvas.cpp
    src/render/vector_binary.cpp
    src/render/card_back.cpp
    src/render/procedural_background.cpp
    src/render/postfx_system.cpp
    src/render/proctex_api.cpp
    src/game/settings_manager.cpp
    src/game/session_manager.cpp
    src/game/game_definition.cpp
    src/game/eats_package_loader.cpp
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
    target_compile_options(solengine_web PRIVATE -O3 -msimd128 -fwasm-exceptions -flto -pthread --use-port=emdawnwebgpu)
    target_compile_definitions(solengine_web PRIVATE NDEBUG EATS_ENABLE_WEBGPU)
    target_link_libraries(solengine_web PRIVATE solengine_render)
    # Auto-discover all *.eats assets across content directories for Emscripten preloading.
    # CONFIGURE_DEPENDS re-runs CMake when .eats files are added/removed/renamed;
    # LINK_DEPENDS re-links solengine_web (regenerating solengine.data) when any .eats file is edited.
    set(SOLENGINE_ASSET_DIRS decks themes postfx games sounds ambience presets)
    set(SOLENGINE_PRELOAD_DEPS "")
    set(SOLENGINE_PRELOAD_FLAGS "")
    set(SOLENGINE_TOTAL_ASSET_BYTES 0)

    foreach(_asset_dir IN LISTS SOLENGINE_ASSET_DIRS)
        file(GLOB _dir_eats CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${_asset_dir}/*.eats")
        list(SORT _dir_eats)
        list(LENGTH _dir_eats _dir_count)
        set(_dir_bytes 0)
        foreach(_eats_file IN LISTS _dir_eats)
            file(SIZE "${_eats_file}" _file_bytes)
            math(EXPR _dir_bytes "${_dir_bytes} + ${_file_bytes}")
            get_filename_component(_eats_name "${_eats_file}" NAME)
            list(APPEND SOLENGINE_PRELOAD_DEPS "${_eats_file}")
            list(APPEND SOLENGINE_PRELOAD_FLAGS "--preload-file \"${_eats_file}@/${_asset_dir}/${_eats_name}\"")
        endforeach()
        math(EXPR SOLENGINE_TOTAL_ASSET_BYTES "${SOLENGINE_TOTAL_ASSET_BYTES} + ${_dir_bytes}")
        math(EXPR _dir_kib "${_dir_bytes} / 1024")
        message(STATUS "[SolEngine Web Assets] /${_asset_dir}: ${_dir_count} .eats file(s), ${_dir_kib} KiB (${_dir_bytes} bytes)")
    endforeach()

    math(EXPR _total_kib "${SOLENGINE_TOTAL_ASSET_BYTES} / 1024")
    list(LENGTH SOLENGINE_PRELOAD_DEPS _total_count)
    message(STATUS "[SolEngine Web Assets] Total preloaded into solengine.data: ${_total_count} .eats file(s), ${_total_kib} KiB (${SOLENGINE_TOTAL_ASSET_BYTES} bytes)")
    string(JOIN " " SOLENGINE_PRELOAD_FLAGS_STR ${SOLENGINE_PRELOAD_FLAGS})

    set_target_properties(solengine_web PROPERTIES
        OUTPUT_NAME "solengine"
        SUFFIX ".html"
        LINK_FLAGS "-O3 -msimd128 -flto -pthread -sPTHREAD_POOL_SIZE=2 -sNO_EXIT_RUNTIME=1 -fwasm-exceptions --use-port=emdawnwebgpu -sUSE_GLFW=3 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=2097152 -sINITIAL_MEMORY=134217728 -sEXPORTED_FUNCTIONS=['_main','_solengine_load_file_from_web','_solengine_load_game_from_web','_solengine_set_search_query','_solengine_on_web_resize'] -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS'] ${SOLENGINE_PRELOAD_FLAGS_STR} --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/web/index.html"
        LINK_DEPENDS "${SOLENGINE_PRELOAD_DEPS};${CMAKE_CURRENT_SOURCE_DIR}/web/index.html"
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
            "${CMAKE_CURRENT_SOURCE_DIR}/web/icon-192-maskable.png"
            "${CMAKE_BINARY_DIR}/icon-192-maskable.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/web/icon-512-maskable.png"
            "${CMAKE_BINARY_DIR}/icon-512-maskable.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/web/apple-touch-icon.png"
            "${CMAKE_BINARY_DIR}/apple-touch-icon.png"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/web/solengine_logo.svg"
            "${CMAKE_BINARY_DIR}/solengine_logo.svg"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/web/CNAME"
            "${CMAKE_BINARY_DIR}/CNAME"
        COMMENT "Copying PWA manifest, service worker, icons, CNAME, and index.html to web root"
    )
else()
    target_link_libraries(solengine_render PUBLIC solengine_core eats_proctex_core glfw)
    if(WIN32)
        set(WGPU_NATIVE_LIB "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wgpu/lib/wgpu_native.dll.lib")
        target_link_libraries(solengine_render PUBLIC
            user32 gdi32 shell32 winhttp opengl32 "${WGPU_NATIVE_LIB}"
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

    # 6. Pure-CPU Headless Solver & Seed Audit CLI (Zero GPU / Audio / Windowing)
    add_executable(solcli
        src/tools/solcli.cpp
        src/game/game_definition.cpp
        src/game/solitaire_board.cpp
        src/game/solitaire_solver.cpp
        src/game/script_rule_engine.cpp
    )
    target_compile_definitions(solcli PRIVATE SOLENGINE_HEADLESS_CLI)
    target_include_directories(solcli PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(solcli PRIVATE eats_script)
    if(NOT MSVC)
        target_compile_options(solcli PRIVATE -fopenmp)
        target_link_libraries(solcli PRIVATE -fopenmp)
    endif()
endif()
