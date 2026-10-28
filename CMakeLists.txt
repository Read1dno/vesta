cmake_minimum_required(VERSION 3.20)
project(vesta VERSION 1.2.2 LANGUAGES C CXX)

# Lua (compiled as C) reports errors with longjmp through Vesta's C++ API functions. With /EHsc MSVC assumes that
# extern "C" functions never throw and skips the destructors of those frames, leaving locks held and memory leaked;
# /EHs keeps the unwind information so the longjmp destroys them like an exception would.
if(MSVC)
    string(REPLACE "/EHsc" "/EHs" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

# Dear ImGui, unmodified upstream v1.92.3, fetched at configure time into the dependency cache (build/_deps), not
# kept in the source tree. The tag is only a convenient name: the commit it resolves to is verified below.
include(FetchContent)
set(VESTA_IMGUI_COMMIT bf75bfec48fc00f532af8926130b70c0e26eb099)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.3
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)
find_package(Git REQUIRED)
execute_process(COMMAND ${GIT_EXECUTABLE} -C ${imgui_SOURCE_DIR} rev-parse HEAD
    OUTPUT_VARIABLE vesta_imgui_head OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE vesta_imgui_head_result)
if(NOT vesta_imgui_head_result EQUAL 0 OR NOT vesta_imgui_head STREQUAL VESTA_IMGUI_COMMIT)
    message(FATAL_ERROR "ImGui checkout is ${vesta_imgui_head}, expected ${VESTA_IMGUI_COMMIT} (v1.92.3)")
endif()
set(VESTA_IMGUI_DIR ${imgui_SOURCE_DIR})

if(NOT DEFINED VESTA_OUTPUT_NAME)
    set(VESTA_OUTPUT_NAME "vesta")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # Static CRT keeps the executable standalone.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0A00)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-DNOMINMAX)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

set(vesta_sources
    # app
    src/app/main.cpp
    src/app/runtime_session.cpp
    src/app/workers.cpp
    src/app/map_loader.cpp
    $<$<CONFIG:Debug>:src/app/diagnostic_reports.cpp>

    # config
    src/config/settings.cpp
    src/config/first_run.cpp
    src/config/config_file.cpp
    src/config/library.cpp

    # core
    src/assets/kv3.cpp
    src/assets/resource.cpp
    src/assets/vpk.cpp
    src/assets/workshop_maps.cpp
    src/game/input/bindings.cpp
	 src/config/hotkeys.cpp
    src/platform/windows/input.cpp
    src/foundation/math/interpolation.cpp
    src/foundation/math/random.cpp
    src/foundation/math/vector.cpp
    src/game/memory/addresses.cpp
    src/game/memory/bound_services.cpp
    src/game/memory/convars.cpp
    src/game/memory/compatibility.cpp
    src/game/memory/entities.cpp
    src/game/memory/modules.cpp
    src/platform/windows/process.cpp
    src/game/memory/schemas.cpp
    src/game/state/camera.cpp
    src/game/state/local.cpp
    src/game/state/pose.cpp
    src/game/state/world.cpp
    src/game/state/seed_players.cpp

    # simulation
    $<$<CONFIG:Debug>:src/simulation/seed_diagnostics.cpp>
    $<$<CONFIG:Debug>:src/simulation/shot_trace.cpp>
    $<$<CONFIG:Debug>:src/app/trace_session.cpp>
    src/simulation/shot_state.cpp
    src/simulation/ballistics.cpp
    src/simulation/seed_weapon.cpp
    src/simulation/blast.cpp
    src/simulation/blast_loader.cpp
    src/game/body/bounds.cpp
    src/simulation/collision.cpp
    src/simulation/grenade.cpp
    src/game/body/hitboxes.cpp
    src/simulation/map_collision.cpp
    src/simulation/nav_mesh.cpp
    src/simulation/penetration.cpp
    src/game/body/skeleton.cpp

    # features
    src/features/aimbot/aimbot.cpp
    src/features/aimbot/aimbot_seed.cpp
    src/features/aimbot/aimbot_target.cpp
    src/features/aimbot/aimbot_recoil.cpp
    src/features/aimbot/aimbot_trigger.cpp
    src/features/aimbot/aimbot_internal.cpp
    src/features/aimbot/command_ring.cpp
    src/features/aimbot/grenade_aim.cpp
    src/features/trigger/seed_trigger.cpp
    src/features/aimbot/seed_plan.cpp
    src/features/aimbot/seed_state.cpp
    src/features/visuals/bomb.cpp
    src/features/visuals/bullet_impacts.cpp
    src/features/visuals/crosshair.cpp
	 src/features/visuals/event_log.cpp
    src/features/visuals/grenade_prediction.cpp
	src/features/visuals/hitsound.cpp
    src/features/visuals/item.cpp
    src/features/visuals/player.cpp
    src/features/visuals/projectile.cpp
    src/features/visuals/radar.cpp
    src/features/visuals/sound.cpp
    src/features/misc/auto_accept.cpp
	 src/features/misc/auto_stop.cpp
    src/features/misc/bhop.cpp
    src/features/misc/nade_helper.cpp
    src/features/misc/walk_bot.cpp
    src/features/misc/walk_bot_path.cpp
    src/features/misc/walk_bot_drive.cpp
    src/features/misc/walk_bot_view.cpp
    src/features/misc/walk_bot_combat.cpp
    src/features/misc/walk_bot_extras.cpp
    src/features/misc/walk_bot_render.cpp
    src/features/misc/match_flow.cpp
    src/game/panorama/panorama.cpp

    # render
    src/features/chams/animation.cpp
    src/features/chams/material.cpp
    src/features/chams/mesh_cache.cpp
    src/features/chams/mesh_extract.cpp
    src/features/chams/preview.cpp
    src/features/chams/renderer.cpp
    src/features/chams/texture.cpp
    src/render/text/localization.cpp
    src/ui/menu/menu.cpp
    src/ui/menu/widgets.cpp
    src/ui/menu/layout.cpp
    src/ui/menu/popups.cpp
    src/ui/menu/visual_editor.cpp
    src/ui/menu/combat_page.cpp
    src/ui/menu/combat_widgets.cpp
    src/ui/menu/aimbot_page.cpp
    src/ui/menu/triggerbot_page.cpp
    src/ui/menu/visuals_page.cpp
    src/ui/menu/misc_page.cpp
    src/ui/menu/configs_page.cpp
    src/ui/menu/scripts_page.cpp
    src/ui/menu/library_widgets.cpp
    src/ui/menu/guide_page.cpp
    src/platform/windows/open_folder.cpp
    src/platform/windows/user_settings.cpp
    src/render/presentation.cpp
    src/ui/presentation.cpp
    src/ui/overlay/input.cpp
    src/ui/overlay/overlay.cpp
    src/ui/overlay/hud.cpp
    src/render/text/esp_text.cpp
    src/ui/overlay/window_band.cpp
    src/ui/overlay/window.cpp

    # system
    src/diagnostics/log.cpp
    src/diagnostics/performance.cpp

    # local Lua scripting (no network API is linked into the product)
    src/scripting/runtime.cpp
    src/scripting/sandbox.cpp
    src/scripting/native_files.cpp
    src/scripting/radar_asset.cpp

    # build support and vendored UI
    src/stdafx.cpp
    ${VESTA_IMGUI_DIR}/imgui.cpp
    ${VESTA_IMGUI_DIR}/imgui_draw.cpp
    ${VESTA_IMGUI_DIR}/imgui_tables.cpp
    ${VESTA_IMGUI_DIR}/imgui_widgets.cpp
    ${VESTA_IMGUI_DIR}/misc/freetype/imgui_freetype.cpp
    ${VESTA_IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    ${VESTA_IMGUI_DIR}/backends/imgui_impl_win32.cpp
)

option(VESTA_SEED_LOG "Instrument the seed trigger in Debug builds" OFF)
option(VESTA_PERF_LOG "Write low-overhead aggregate performance telemetry" OFF)
option(VESTA_WINDOWED_SESSION "Long-running ordinary-token windowed diagnostic session" OFF)
option(VESTA_WINDOWED_PROFILE "Diagnostic 90-second ordinary-token windowed capture; never for distribution" OFF)
if(VESTA_WINDOWED_PROFILE AND VESTA_WINDOWED_SESSION)
    message(FATAL_ERROR "Choose one diagnostic mode: profile or session")
endif()
if(VESTA_WINDOWED_PROFILE AND NOT VESTA_PERF_LOG)
    message(FATAL_ERROR "Windowed diagnostics require VESTA_PERF_LOG=ON")
endif()


# The tracked configs/legit.cfg is the product default.  Embed it at configure time
# so a clean installation does not depend on the source tree being present.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/configs/legit.cfg" VESTA_DEFAULT_PROFILE_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," VESTA_DEFAULT_PROFILE_BYTES
    "${VESTA_DEFAULT_PROFILE_HEX}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/config/default_profile.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/config/default_profile.hpp"
    @ONLY
)

# The web radar is the only Lua script allowed outside the sandbox: its digest (text with CRLF folded to LF) is
# compiled in, so an edited or different file runs restricted. Changing the script needs a new build.
set(VESTA_RADAR_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/lua/scripts/Vesta Web Radar.lua")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${VESTA_RADAR_SCRIPT}")
file(READ "${VESTA_RADAR_SCRIPT}" VESTA_RADAR_TEXT)
string(REPLACE "\r" "" VESTA_RADAR_TEXT "${VESTA_RADAR_TEXT}")
string(SHA256 VESTA_RADAR_SHA256 "${VESTA_RADAR_TEXT}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/scripting/trusted_scripts.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/scripting/trusted_scripts.hpp"
    @ONLY
)

# Diagnostic metadata is fixed at configure time, never taken from runtime text.
if(NOT DEFINED VESTA_BUILD_REVISION)
    execute_process(COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse --verify HEAD
        OUTPUT_VARIABLE VESTA_BUILD_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT VESTA_BUILD_REVISION)
        set(VESTA_BUILD_REVISION "unknown")
    else()
        execute_process(COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" diff --quiet HEAD
            RESULT_VARIABLE vesta_dirty ERROR_QUIET)
        if(NOT vesta_dirty EQUAL 0)
            string(APPEND VESTA_BUILD_REVISION "-dirty")
        endif()
    endif()
endif()
string(LENGTH "${VESTA_BUILD_REVISION}" vesta_revision_length)
if(NOT VESTA_BUILD_REVISION MATCHES "^[A-Za-z0-9._-]+$" OR vesta_revision_length GREATER 80)
    message(FATAL_ERROR "Diagnostic build revision must be a bounded identifier")
endif()
configure_file(src/diagnostics/build_identity.hpp.in generated/diagnostics/build_identity.hpp @ONLY)
add_library(vesta_diagnostics STATIC src/diagnostics/journal.cpp src/diagnostics/crash_record.cpp src/diagnostics/report.cpp src/diagnostics/log_directory.cpp)
target_include_directories(vesta_diagnostics PUBLIC src PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(vesta_diagnostics PUBLIC shell32 ole32 bcrypt)
target_compile_definitions(vesta_diagnostics PRIVATE VESTA_JOURNAL_CONFIGURATION="$<CONFIG>")
if(MSVC)
    target_compile_options(vesta_diagnostics PRIVATE /utf-8)
endif()

add_library(vesta_storage STATIC src/platform/windows/data_paths.cpp src/platform/windows/data_migration.cpp src/platform/windows/storage.cpp)
target_include_directories(vesta_storage PUBLIC src)
target_link_libraries(vesta_storage PUBLIC shell32 ole32)
if(MSVC)
    target_compile_options(vesta_storage PRIVATE /utf-8 /W4)
endif()

set_source_files_properties(src/features/chams/mesh_cache.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

add_library(vesta_mesh_file STATIC src/features/chams/mesh_file.cpp)
target_include_directories(vesta_mesh_file PUBLIC src)
target_link_libraries(vesta_mesh_file PUBLIC vesta_storage)
if(MSVC)
    target_compile_options(vesta_mesh_file PRIVATE /utf-8 /W4)
endif()

add_executable(${PROJECT_NAME} ${vesta_sources})
target_link_libraries(${PROJECT_NAME} PRIVATE vesta_diagnostics vesta_storage vesta_mesh_file)
# The product is an ordinary user process: no elevation request, no UIAccess, no token changes.
set(VESTA_MANIFEST_LEVEL "asInvoker")
set(VESTA_MANIFEST_UIACCESS "false")
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${VESTA_OUTPUT_NAME}" PDB_NAME "${VESTA_OUTPUT_NAME}")

target_precompile_headers(${PROJECT_NAME} PRIVATE src/stdafx.hpp)

target_include_directories(${PROJECT_NAME} PRIVATE
	${CMAKE_CURRENT_BINARY_DIR}/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${VESTA_IMGUI_DIR}
    ${VESTA_IMGUI_DIR}/backends
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    # Vesta opens no links or documents, so ImGui's generic ShellExecuteW "open anything" handler is left out of
    # the executable. The only thing it opens is its own data/log/script folders, through
    # platform::windows::open_folder, which starts explorer.exe on an existing directory and nothing else.
    IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS
    # Release builds do not expose ImGui's metrics/debug windows; this also keeps their upstream FAQ link and
    # diagnostic strings out of the executable.
    IMGUI_DISABLE_DEBUG_TOOLS
    # Use the already-linked FreeType rasterizer. Each ESP glyph stores smooth
    # fill coverage and its one-pixel outline mask in one atlas rectangle.
    IMGUI_ENABLE_FREETYPE
    # Steam display names can contain emoji and symbols outside the BMP.
    IMGUI_USE_WCHAR32
    # ImVec2/ImVec4 arithmetic operators (upstream's own opt-in macro, previously set by editing imconfig.h).
    IMGUI_DEFINE_MATH_OPERATORS
    $<$<CONFIG:Debug>:VESTA_ENABLE_CONSOLE=1>
    $<$<NOT:$<CONFIG:Debug>>:VESTA_ENABLE_CONSOLE=0>
    $<$<CONFIG:Debug>:VESTA_SHOT_TRACE_ENABLED=1>
    $<$<NOT:$<CONFIG:Debug>>:VESTA_SHOT_TRACE_ENABLED=0>
    $<$<AND:$<CONFIG:Debug>,$<BOOL:${VESTA_SEED_LOG}>>:VESTA_SEED_LOG=1>
    $<$<BOOL:${VESTA_PERF_LOG}>:VESTA_PERF_LOG=1>
    $<$<BOOL:${VESTA_WINDOWED_PROFILE}>:VESTA_WINDOWED_PROFILE=1>
    $<$<BOOL:${VESTA_WINDOWED_SESSION}>:VESTA_WINDOWED_SESSION=1>
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE d3d11 d3dcompiler windowscodecs ole32 dxgi dwmapi dcomp winmm xaudio2 ntdll comdlg32 advapi32 shell32)
endif()

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:/Od;/Zi;/MP>
        $<$<CONFIG:Release>:/O2;/GL;/MP;/arch:AVX>
    )
    target_link_options(${PROJECT_NAME} PRIVATE
        "/MANIFESTUAC:level='${VESTA_MANIFEST_LEVEL}' uiAccess='${VESTA_MANIFEST_UIACCESS}'"
        # Imported DLLs are looked up in System32 only (LOAD_LIBRARY_SEARCH_SYSTEM32), not next to the executable.
        /DEPENDENTLOADFLAG:0x800
        $<$<CONFIG:Debug>:/DEBUG>
        $<$<CONFIG:Release>:/LTCG;/OPT:REF;/OPT:ICF;/DEBUG:FULL>
        $<$<CONFIG:Release>:/SUBSYSTEM:WINDOWS>
        $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>
        $<$<CONFIG:Release>:/PDBALTPATH:vesta.pdb>
    )
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:-O0;-g>
        $<$<CONFIG:Release>:-O3;-march=native>
    )
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/bin")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_SOURCE_DIR}/build/bin")
endforeach()

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/bin"
    PDB_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/bin"
)
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_SOURCE_DIR}/build/bin"
        PDB_OUTPUT_DIRECTORY_${config} "${CMAKE_SOURCE_DIR}/build/bin"
    )
endforeach()

include(FetchContent)

FetchContent_Declare(
    lua
    URL https://www.lua.org/ftp/lua-5.4.8.tar.gz
    URL_HASH SHA256=4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(lua)
file(GLOB lua_sources CONFIGURE_DEPENDS "${lua_SOURCE_DIR}/src/*.c")
list(FILTER lua_sources EXCLUDE REGEX "/(lua|luac|onelua)\\.c$")
add_library(vesta_lua STATIC ${lua_sources})
target_include_directories(vesta_lua PUBLIC ${lua_SOURCE_DIR}/src)
target_link_libraries(${PROJECT_NAME} PRIVATE vesta_lua)

FetchContent_Declare(
    freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG VER-2-13-2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(freetype)
target_link_libraries(${PROJECT_NAME} PRIVATE freetype)

FetchContent_Declare(
    meshoptimizer
    GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
    GIT_TAG v1.2 # v0.21 only decodes vertex codec version 0; CS2 emits version 1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(meshoptimizer)
target_link_libraries(${PROJECT_NAME} PRIVATE meshoptimizer)

FetchContent_Declare(
    lz4
    GIT_REPOSITORY https://github.com/lz4/lz4.git
    GIT_TAG v1.9.4
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR build/cmake
)
set(LZ4_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(LZ4_BUILD_LEGACY_LZ4C OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(lz4)
target_link_libraries(${PROJECT_NAME} PRIVATE lz4_static)
target_include_directories(${PROJECT_NAME} PRIVATE ${lz4_SOURCE_DIR}/lib)

FetchContent_Declare(
    zstd
    GIT_REPOSITORY https://github.com/facebook/zstd.git
    GIT_TAG v1.5.6
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR build/cmake
)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(zstd)
target_link_libraries(${PROJECT_NAME} PRIVATE libzstd_static)
target_include_directories(${PROJECT_NAME} PRIVATE ${zstd_SOURCE_DIR}/lib)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)
target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)
