cmake_minimum_required(VERSION 3.20)
project(vesta VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)

    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0A00)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-DNOMINMAX)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

include(FetchContent)
find_package(Git REQUIRED)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG bf75bfec48fc00f532af8926130b70c0e26eb099
    GIT_SHALLOW FALSE
    PATCH_COMMAND
        "${CMAKE_COMMAND}"
        "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
        "-DSOURCE_DIR=<SOURCE_DIR>"
        "-DPATCH_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/imgui-vesta.patch"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/apply-patch.cmake"
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG 55f93686c01528224f448c19128836e7df245f72
    GIT_SHALLOW FALSE
)
FetchContent_Declare(
    xorstr
    GIT_REPOSITORY https://github.com/JustasMasiulis/xorstr.git
    GIT_TAG 066c64eea5104f4e3cfbc49e39031400e086425a
    GIT_SHALLOW FALSE
)
FetchContent_MakeAvailable(imgui nlohmann_json xorstr)

set(vesta_sources

    src/app/main.cpp
    src/app/workers.cpp

    src/config/settings.cpp

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
    src/core/memory/entities.cpp
    src/core/memory/modules.cpp
    src/core/memory/process.cpp
    src/core/memory/schemas.cpp
    src/core/state/camera.cpp
    src/core/state/local.cpp
    src/core/state/pose.cpp
    src/core/state/world.cpp

    src/simulation/ballistics.cpp
    src/simulation/blast.cpp
    src/simulation/blast_loader.cpp
    src/simulation/bounds.cpp
    src/simulation/collision.cpp
    src/simulation/grenade.cpp
    src/simulation/hitboxes.cpp
    src/simulation/map_collision.cpp
    src/simulation/penetration.cpp
    src/simulation/skeleton.cpp

    src/features/aimbot/aimbot.cpp
    src/features/trigger/seed_trigger.cpp
    src/features/visuals/bomb.cpp
    src/features/visuals/bullet_impacts.cpp
    src/features/visuals/crosshair.cpp
	 src/features/visuals/event_log.cpp
    src/features/visuals/grenade_prediction.cpp
	src/features/visuals/hitsound.cpp
    src/features/visuals/item.cpp
    src/features/visuals/no_flash.cpp
    src/features/visuals/player.cpp
    src/features/visuals/projectile.cpp
    src/features/visuals/radar.cpp
    src/features/visuals/sound.cpp
    src/features/misc/auto_accept.cpp
	 src/features/misc/auto_stop.cpp
    src/features/misc/bhop.cpp
    src/features/misc/nade_helper.cpp

    src/render/chams/animation.cpp
    src/render/chams/material.cpp
    src/render/chams/mesh_cache.cpp
    src/render/chams/mesh_extract.cpp
    src/render/chams/preview.cpp
    src/render/chams/renderer.cpp
    src/render/chams/texture.cpp
    src/render/menu/localization.cpp
    src/render/menu/menu.cpp
    src/render/overlay/input.cpp
    src/render/overlay/overlay.cpp
    src/render/overlay/ui.cpp
    src/render/overlay/window.cpp

    src/system/diagnostics.cpp
    src/system/frame_limiter.cpp
    src/system/performance.cpp
    src/system/storage.cpp

    src/scripting/runtime.cpp
    src/scripting/radar_asset.cpp

    src/stdafx.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
)

option(VESTA_SEED_LOG "Instrument the seed trigger and force the console on" OFF)
option(VESTA_PERF_LOG "Write low-overhead aggregate performance telemetry" OFF)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/configs/legit.cfg" VESTA_DEFAULT_PROFILE_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," VESTA_DEFAULT_PROFILE_BYTES
    "${VESTA_DEFAULT_PROFILE_HEX}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/config/default_profile.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/config/default_profile.hpp"
    @ONLY
)

add_executable(${PROJECT_NAME} ${vesta_sources})

target_precompile_headers(${PROJECT_NAME} PRIVATE src/stdafx.hpp)

target_include_directories(${PROJECT_NAME} PRIVATE
	${CMAKE_CURRENT_BINARY_DIR}/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${xorstr_SOURCE_DIR}/include
)

target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    IMGUI_DEFINE_MATH_OPERATORS

    IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS

    IMGUI_DISABLE_DEBUG_TOOLS

    IMGUI_ENABLE_FREETYPE

    IMGUI_USE_WCHAR32
    $<$<OR:$<CONFIG:Debug>,$<BOOL:${VESTA_SEED_LOG}>>:VESTA_ENABLE_CONSOLE=1>
    $<$<NOT:$<OR:$<CONFIG:Debug>,$<BOOL:${VESTA_SEED_LOG}>>>:VESTA_ENABLE_CONSOLE=0>
    $<$<BOOL:${VESTA_SEED_LOG}>:VESTA_SEED_LOG=1>
    $<$<BOOL:${VESTA_PERF_LOG}>:VESTA_PERF_LOG=1>
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE d3d11 d3dcompiler windowscodecs ole32 dwmapi dcomp winmm xaudio2 ntdll comdlg32 advapi32 shell32)
endif()

if(MSVC)

    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:/Od;/Zi;/MP>
        $<$<CONFIG:Release>:/O2;/GL;/MP;/arch:AVX>
    )
    target_link_options(${PROJECT_NAME} PRIVATE
		"/MANIFESTUAC:level='asInvoker' uiAccess='false'"

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

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin
    PDB_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    PDB_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin
    PDB_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin
)

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
    GIT_TAG v1.2
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
    add_executable(vesta_penetration_segments_test
        tests/penetration_segments.cpp)
    target_include_directories(vesta_penetration_segments_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME penetration_segments COMMAND vesta_penetration_segments_test)

endif()
