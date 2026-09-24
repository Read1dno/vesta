cmake_minimum_required(VERSION 3.20)
project(vesta VERSION 1.1.7 LANGUAGES C CXX)

set(VESTA_OUTPUT_NAME "vesta")

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
    src/app/workers.cpp
    src/app/map_loader.cpp

    # config
    src/config/settings.cpp

    # core
    src/core/assets/kv3.cpp
    src/core/assets/resource.cpp
    src/core/assets/vpk.cpp
    src/core/input/bindings.cpp
	 src/core/input/hotkeys.cpp
    src/core/input/input.cpp
    src/core/math/interpolation.cpp
    src/core/math/random.cpp
    src/core/math/vector.cpp
    src/core/memory/addresses.cpp
    src/core/memory/convars.cpp
    src/core/memory/compatibility.cpp
    src/core/memory/entities.cpp
    src/core/memory/modules.cpp
    src/core/memory/process.cpp
    src/core/memory/schemas.cpp
    src/core/state/camera.cpp
    src/core/state/local.cpp
    src/core/state/pose.cpp
    src/core/state/world.cpp
    src/core/state/seed_players.cpp

    # simulation
    src/simulation/seed_diagnostics.cpp
    src/simulation/shot_trace.cpp
    src/simulation/shot_state.cpp
    src/simulation/ballistics.cpp
    src/simulation/seed_weapon.cpp
    src/simulation/blast.cpp
    src/simulation/blast_loader.cpp
    src/simulation/bounds.cpp
    src/simulation/collision.cpp
    src/simulation/grenade.cpp
    src/simulation/hitboxes.cpp
    src/simulation/map_collision.cpp
    src/simulation/penetration.cpp
    src/simulation/skeleton.cpp

    # features
    src/features/aimbot/aimbot.cpp
    src/features/aimbot/grenade_aim.cpp
    src/features/trigger/seed_trigger.cpp
    src/features/trigger/seed_plan.cpp
    src/features/trigger/seed_state.cpp
    src/features/visuals/bomb.cpp
    src/features/visuals/bullet_impacts.cpp
    src/features/visuals/crosshair.cpp
    src/features/visuals/crosshair_sync.cpp
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

    # render
    src/render/chams/animation.cpp
    src/render/chams/material.cpp
    src/render/chams/mesh_cache.cpp
    src/render/chams/mesh_extract.cpp
    src/render/chams/preview.cpp
    src/render/chams/renderer.cpp
    src/render/chams/texture.cpp
    src/render/menu/localization.cpp
    src/render/menu/menu.cpp
    src/render/menu/widgets.cpp
    src/render/menu/layout.cpp
    src/render/menu/popups.cpp
    src/render/menu/visual_editor.cpp
    src/render/menu/combat_page.cpp
    src/render/menu/combat_widgets.cpp
    src/render/menu/aimbot_page.cpp
    src/render/menu/triggerbot_page.cpp
    src/render/menu/visuals_page.cpp
    src/render/menu/misc_page.cpp
    src/render/overlay/input.cpp
    src/render/overlay/overlay.cpp
    src/render/overlay/hud.cpp
    src/render/overlay/ui.cpp
    src/render/overlay/window.cpp

    # system
    src/system/diagnostics.cpp
    src/system/frame_limiter.cpp
    src/system/performance.cpp
    src/system/storage.cpp

    # local Lua scripting (no network API is linked into the product)
    src/scripting/runtime.cpp
    src/scripting/radar_asset.cpp

    # build support and vendored UI
    src/stdafx.cpp
    src/external/imgui_vendor/imgui.cpp
    src/external/imgui_vendor/imgui_draw.cpp
    src/external/imgui_vendor/imgui_tables.cpp
    src/external/imgui_vendor/imgui_widgets.cpp
    src/external/imgui_vendor/misc/freetype/imgui_freetype.cpp
    src/external/imgui_vendor/backends/imgui_impl_dx11.cpp
    src/external/imgui_vendor/backends/imgui_impl_win32.cpp
)

option(VESTA_INSTALLED_UIACCESS "Signed, installed UIAccess executable; no self-elevation or relaunch" OFF)

option(VESTA_SEED_LOG "Instrument the seed trigger and force the console on" OFF)
option(VESTA_PERF_LOG "Write low-overhead aggregate performance telemetry" OFF)
option(VESTA_WINDOWED_PROFILE "Diagnostic 90-second ordinary-token windowed capture; never for distribution" OFF)
if(VESTA_WINDOWED_PROFILE AND (NOT VESTA_PERF_LOG OR VESTA_INSTALLED_UIACCESS))
    message(FATAL_ERROR "Windowed diagnostics require VESTA_PERF_LOG=ON and VESTA_INSTALLED_UIACCESS=OFF")
endif()


# The tracked configs/legit.cfg is the product default. Embed it at configure time
# so a clean installation does not depend on the source tree being present.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/configs/legit.cfg" VESTA_DEFAULT_PROFILE_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," VESTA_DEFAULT_PROFILE_BYTES
    "${VESTA_DEFAULT_PROFILE_HEX}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/config/default_profile.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/config/default_profile.hpp"
    @ONLY
)

add_executable(${PROJECT_NAME} ${vesta_sources})
if(VESTA_INSTALLED_UIACCESS)
    target_compile_definitions(${PROJECT_NAME} PRIVATE VESTA_INSTALLED_UIACCESS=1)
    set(VESTA_MANIFEST_LEVEL "asInvoker")
    set(VESTA_MANIFEST_UIACCESS "true")
else()
    target_sources(${PROJECT_NAME} PRIVATE src/render/overlay/ui_legacy.cpp)
    set(VESTA_MANIFEST_LEVEL "asInvoker")
    set(VESTA_MANIFEST_UIACCESS "false")
endif()
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${VESTA_OUTPUT_NAME}" PDB_NAME "${VESTA_OUTPUT_NAME}")

target_precompile_headers(${PROJECT_NAME} PRIVATE src/stdafx.hpp)

target_include_directories(${PROJECT_NAME} PRIVATE
	${CMAKE_CURRENT_BINARY_DIR}/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/external/zdraw/external/freetype
    ${CMAKE_CURRENT_SOURCE_DIR}/src/external/imgui_vendor
    ${CMAKE_CURRENT_SOURCE_DIR}/src/external/imgui_vendor/backends
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    # UC rejects binaries with generic shell/web launch paths. Vesta has no links
    # to open, so remove ImGui's otherwise-unused ShellExecuteW default handler.
    IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS
    # Release builds do not expose ImGui's metrics/debug windows. Removing them
    # also keeps their upstream FAQ link and diagnostic strings out of the EXE.
    IMGUI_DISABLE_DEBUG_TOOLS
    # Use the already-linked FreeType rasterizer. Each ESP glyph stores smooth
    # fill coverage and its one-pixel outline mask in one atlas rectangle.
    IMGUI_ENABLE_FREETYPE
    # Steam display names can contain emoji and symbols outside the BMP.
    IMGUI_USE_WCHAR32
    $<$<OR:$<CONFIG:Debug>,$<BOOL:${VESTA_SEED_LOG}>>:VESTA_ENABLE_CONSOLE=1>
    $<$<NOT:$<OR:$<CONFIG:Debug>,$<BOOL:${VESTA_SEED_LOG}>>>:VESTA_ENABLE_CONSOLE=0>
    $<$<BOOL:${VESTA_SEED_LOG}>:VESTA_SEED_LOG=1>
    $<$<BOOL:${VESTA_PERF_LOG}>:VESTA_PERF_LOG=1>
    $<$<BOOL:${VESTA_WINDOWED_PROFILE}>:VESTA_WINDOWED_PROFILE=1>
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
        $<$<CONFIG:Debug>:/DEBUG>
        $<$<CONFIG:Release>:/LTCG;/OPT:REF;/OPT:ICF;/DEBUG:FULL>
        $<$<AND:$<CONFIG:Release>,$<NOT:$<BOOL:${VESTA_SEED_LOG}>>>:/SUBSYSTEM:WINDOWS>
        $<$<AND:$<CONFIG:Release>,$<NOT:$<BOOL:${VESTA_SEED_LOG}>>>:/ENTRY:mainCRTStartup>
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
    GIT_TAG v1.2 # v0.21 only decodes vertex codec version 0; CS2 emits version 1 (see cs2-vmdl-mesh-extraction memory notes)
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

include(CTest)
if(BUILD_TESTING)
    foreach(test ray_capsule penetration_accuracy update_contracts crosshair_geometry)
        add_executable(vesta_${test}_test tests/${test}.cpp)
        target_include_directories(vesta_${test}_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        add_test(NAME ${test} COMMAND vesta_${test}_test)
    endforeach()
    add_executable(vesta_shot_trace_test tests/shot_trace.cpp src/simulation/shot_trace.cpp)
    target_include_directories(vesta_shot_trace_test PRIVATE tests/fixtures/shot_trace src)
    target_compile_definitions(vesta_shot_trace_test PRIVATE VESTA_TRACE_RAY_TEST=1)
    add_test(NAME shot_trace COMMAND vesta_shot_trace_test "${CMAKE_CURRENT_BINARY_DIR}/shot-trace-test")
    add_executable(vesta_seed_reader_test tests/seed_reader_restore.cpp src/core/state/seed_players.cpp)
    target_include_directories(vesta_seed_reader_test PRIVATE tests/fixtures/seed_reader src)
    add_test(NAME seed_reader COMMAND vesta_seed_reader_test)
    add_executable(vesta_seed_punch_snapshot_test tests/seed_punch_snapshot.cpp src/features/trigger/seed_state.cpp)
    target_include_directories(vesta_seed_punch_snapshot_test PRIVATE tests/fixtures/seed_punch src)
    add_test(NAME seed_punch_snapshot COMMAND vesta_seed_punch_snapshot_test)
    add_executable(vesta_native_accuracy_test tests/native_accuracy.cpp)
    target_include_directories(vesta_native_accuracy_test PRIVATE src)
    add_test(NAME native_accuracy COMMAND vesta_native_accuracy_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/shooting_accuracy.tsv")
    add_executable(vesta_seed_plan_test tests/seed_plan.cpp src/features/trigger/seed_plan.cpp)
    target_include_directories(vesta_seed_plan_test PRIVATE tests/fixtures/seed_plan src)
    add_test(NAME seed_plan COMMAND vesta_seed_plan_test)
    add_executable(vesta_seed_snapshot_test tests/seed_snapshot_restore.cpp src/simulation/seed_weapon.cpp src/core/math/vector.cpp)
    target_include_directories(vesta_seed_snapshot_test PRIVATE tests/fixtures/seed_snapshot src)
    add_test(NAME seed_snapshot COMMAND vesta_seed_snapshot_test)
    add_executable(vesta_seed_inaccuracy_test tests/seed_inaccuracy.cpp src/simulation/seed_weapon.cpp src/core/math/vector.cpp)
    target_include_directories(vesta_seed_inaccuracy_test PRIVATE tests/fixtures/seed_snapshot src)
    add_test(NAME seed_inaccuracy COMMAND vesta_seed_inaccuracy_test)
    add_executable(vesta_seed_integration_test tests/seed_integration.cpp)
    add_test(NAME seed_integration COMMAND vesta_seed_integration_test "${CMAKE_CURRENT_SOURCE_DIR}")
    add_executable(vesta_native_spread_test tests/native_spread.cpp src/core/math/random.cpp)
    target_include_directories(vesta_native_spread_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME native_spread COMMAND vesta_native_spread_test)

    add_executable(vesta_live_shot_replay_test tests/frida/replay.cpp src/core/math/random.cpp src/core/math/vector.cpp)
    target_include_directories(vesta_live_shot_replay_test PRIVATE tests/fixtures/seed_snapshot src)
    add_test(NAME live_shot_replay COMMAND vesta_live_shot_replay_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/live_shots_20260924.jsonl")

    add_executable(vesta_seed_window_test tests/seed_window.cpp src/core/math/random.cpp src/core/math/vector.cpp)
    target_include_directories(vesta_seed_window_test PRIVATE tests/fixtures/seed_snapshot src)
    add_test(NAME seed_window COMMAND vesta_seed_window_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/live_shots_20260924.jsonl")

    add_executable(vesta_native_rng_test tests/native_rng.cpp src/core/math/random.cpp)
    target_include_directories(vesta_native_rng_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME native_rng COMMAND vesta_native_rng_test)

    add_executable(vesta_shot_model_test tests/shot_model.cpp src/core/math/random.cpp)
    target_include_directories(vesta_shot_model_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME shot_model COMMAND vesta_shot_model_test)
    add_executable(vesta_spread_sampler_test tests/spread_sampler.cpp src/core/math/random.cpp)
    target_include_directories(vesta_spread_sampler_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME spread_sampler COMMAND vesta_spread_sampler_test)
    add_executable(vesta_spread_cache_test tests/spread_cache.cpp src/core/math/random.cpp)
    target_include_directories(vesta_spread_cache_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME spread_cache COMMAND vesta_spread_cache_test)
    add_executable(vesta_penetration_segments_test
        tests/penetration_segments.cpp)
    target_include_directories(vesta_penetration_segments_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME penetration_segments COMMAND vesta_penetration_segments_test)

	add_executable(vesta_swept_sphere_test
		tests/swept_sphere.cpp)
	target_include_directories(vesta_swept_sphere_test PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src)
	add_test(NAME swept_sphere COMMAND vesta_swept_sphere_test)

	add_executable(vesta_swept_hull_test
		tests/swept_hull.cpp)
	target_include_directories(vesta_swept_hull_test PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src)
	add_test(NAME swept_hull COMMAND vesta_swept_hull_test)

    foreach(test seed_receipt seed_epoch_guard shot_clock_evidence seed_reaction seed_weapon_guard entity_slot snapshot_epoch read_pair depth_bounds keyboard_binding index_run object_watch chunked_read present_policy bvh_capacity snapshot_pool mesh_leaves latest_job seed_timing frame_schedule menu_geometry key_encoding binding_names binding_layout)
        add_executable(vesta_${test}_test tests/${test}.cpp)
        target_include_directories(vesta_${test}_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(vesta_${test}_test PRIVATE user32)
        add_test(NAME ${test} COMMAND vesta_${test}_test)
    endforeach()

    add_executable(vesta_performance_test tests/performance.cpp src/system/performance.cpp)
    target_include_directories(vesta_performance_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(vesta_performance_test PRIVATE VESTA_PERF_LOG=1
        VESTA_PERF_TEST_OUTPUT="${CMAKE_CURRENT_BINARY_DIR}/performance-test.jsonl")
    target_link_libraries(vesta_performance_test PRIVATE user32)
    add_test(NAME performance COMMAND vesta_performance_test)

    add_executable(vesta_startup_contract_test tests/startup_contract.cpp)
    target_include_directories(vesta_startup_contract_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME startup_contract COMMAND vesta_startup_contract_test $<TARGET_FILE:vesta> ${VESTA_MANIFEST_LEVEL} ${VESTA_MANIFEST_UIACCESS})

    add_executable(vesta_pacing_benchmark tests/frame_pacing_benchmark.cpp)
    target_include_directories(vesta_pacing_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    add_executable(vesta_presentation_smoke_test tests/presentation_smoke.cpp)
    target_include_directories(vesta_presentation_smoke_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(vesta_presentation_smoke_test PRIVATE user32 d3d11 dxgi dcomp)
    add_test(NAME presentation_smoke COMMAND vesta_presentation_smoke_test)
    set_tests_properties(presentation_smoke PROPERTIES LABELS "gpu")

endif()
