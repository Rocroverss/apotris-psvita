cmake_minimum_required(VERSION 3.20)

# UNVERIFIED DRAFT -- adapted from a .so-loader project's CMakeLists.txt as
# a starting point, but everything loader-specific (FalsoJNI, dynlib/patch/
# java shims, the classic.bin+launcher dual-eboot setup, vitaGL/vitashark
# GL-translation, DATA_PATH/SO_PATH) has been removed. This instead builds
# Apotris's own C++ source directly. I have no VitaSDK toolchain available
# to actually compile this, so treat it as a first draft to iterate on
# against real build errors, not a known-working file.
#
# This is an ALTERNATIVE to the project's normal meson build
# (meson/vita.ini), not a replacement for it -- meson is still the
# officially supported way to build Apotris for every other platform.
# The main reason to use CMake here instead is that it can link directly
# against VitaSDK's own prebuilt SDL2/SDL2_mixer packages (installed via
# vdpm), which are a Vita-patched fork -- whereas meson's subprojects/sdl2
# wrap fetches vanilla upstream SDL2 sources that have no Vita target at
# all. See PORTING_NOTES_VITA.md for the full breakdown of what's verified
# vs. not.

if (NOT DEFINED ENV{VITASDK} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  message(FATAL_ERROR "Please define VITASDK env var to point to your SDK path or set CMAKE_TOOLCHAIN_FILE.")
endif ()

set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Toolchain file")

project(Apotris C CXX)

include("${VITASDK}/share/vita.cmake" REQUIRED)

# ---------------------------------------------------------------------------
# Vita application configuration
# ---------------------------------------------------------------------------
set(VITA_APP_NAME "Apotris")
set(VITA_TITLEID "APOT00001")
set(VITA_VPKNAME "Apotris")
set(VITA_VERSION "04.20")

# Optional: used by the "send"/"reboot" dev-deploy targets below.
set(PSVITAIP "192.168.0.198" CACHE STRING "PSVita IP (for FTP access)")

# Matches Apotris's own version scheme (see meson.build's project(version:
# ...)); substituted into include/version.h.in below the same way meson's
# configure_file() does.
set(version "4.2.0a")

# ---------------------------------------------------------------------------
# Platform / feature defines
# ---------------------------------------------------------------------------
# This is the flag every shared source file (platform.hpp, liba_pc.h,
# scene.hpp, main.cpp, etc.) branches on for this port -- see
# PORTING_NOTES_VITA.md / apotris-vita-port.zip for where it's used.
add_definitions(-DVITA)

# makes sincos, sincosf, etc. visible (kept from upstream vitasdk examples;
# harmless either way)
add_definitions(-D_GNU_SOURCE -D__POSIX_VISIBLE=999999)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -mfloat-abi=softfp -std=gnu11 -Wno-deprecated")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,-q -mfloat-abi=softfp -std=gnu++20 -Wno-write-strings -Wno-psabi")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O1 -g3")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O1 -g3")
else ()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -g0 -ffast-math")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -g0 -ffast-math")
endif()

# ---------------------------------------------------------------------------
# Generated version header (mirrors meson.build's configure_file() step)
# ---------------------------------------------------------------------------
configure_file(
    "${CMAKE_SOURCE_DIR}/include/version.h.in"
    "${CMAKE_BINARY_DIR}/generated/version.h"
    @ONLY
)

include_directories(
    ${CMAKE_SOURCE_DIR}/source
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/data
    ${CMAKE_BINARY_DIR}/generated
)

# ---------------------------------------------------------------------------
# Third-party dependencies that must build for Vita: SDL2/SDL2_mixer come
# from VitaSDK's own vdpm package repo (install with `vdpm sdl2
# sdl2_mixer`) rather than being cross-compiled from source, since VitaSDK's
# SDL2 is a Vita-specific fork with no equivalent in vanilla upstream SDL2.
#
# UNVERIFIED: Tilengine, SoLoud, and openmpt are NOT standard vdpm
# packages. Apotris pulls them in as git submodules (subprojects/Tilengine,
# subprojects/SoLoud, subprojects/openmpt -- see .gitmodules) built from
# source by meson. Whether their own build systems can cross-compile for
# Vita at all is genuinely unknown to me -- I don't have network access to
# inspect gitea.com/Apotris/{Tilengine,SoLoud}.git from this environment.
# `find_package` below will simply fail loudly if they aren't installed/
# buildable yet, which is the right failure mode until that's sorted out.
# ---------------------------------------------------------------------------
find_package(SDL2 REQUIRED)
find_package(SDL2_mixer REQUIRED)

# Tilengine's fork CMakeLists.txt builds the library fine, but its bundled
# sample programs (mode7, etc.) fail to link -- they need Tilengine's own
# SDL-windowed demo mode, which Apotris doesn't use (Apotris does its own
# windowing via liba_vita.cpp and uses Tilengine purely as an offscreen
# "slave renderer"). It also appears to define no install() rule at all
# ("make install" -> "No rule to make target 'install'"), so chasing that
# further isn't worth it. Same fix as SoLoud below: compile its library
# sources directly ourselves and skip find_package()/its CMakeLists
# entirely.
#
# UNVERIFIED: Tilengine is written as portable C99 and upstream keeps
# everything flat under src/ (no subdirectories) -- this glob assumes
# that's still true in this fork. Per Tilengine's own README, it also
# requires libpng in addition to SDL2 to build.
set(TILENGINE_DIR ${CMAKE_SOURCE_DIR}/subprojects/Tilengine)
if (NOT EXISTS ${TILENGINE_DIR}/src)
    message(FATAL_ERROR "subprojects/Tilengine looks empty -- run: git submodule update --init subprojects/Tilengine")
endif ()

file(GLOB TILENGINE_SOURCES ${TILENGINE_DIR}/src/*.c)
if (NOT TILENGINE_SOURCES)
    message(FATAL_ERROR "No .c files found directly under subprojects/Tilengine/src -- its layout doesn't match what this assumes. Run `ls subprojects/Tilengine/src` and tell me what's actually there.")
endif ()

find_package(PNG REQUIRED)

add_library(Tilengine STATIC ${TILENGINE_SOURCES})
target_include_directories(Tilengine PUBLIC ${TILENGINE_DIR}/include)
target_link_libraries(Tilengine PUBLIC SDL2::SDL2 PNG::PNG)

# The prebuilt VitaSDK SoLoud package turned out to be missing
# SoLoud::Queue entirely (confirmed by the actual build error), and
# liba_sdl_audio.cpp's code expects soloud.h itself to pull Queue in --
# which only happens in Apotris's own SoLoud fork's headers, not vanilla
# upstream. Rather than mix the prebuilt static lib with one extra source
# file compiled fresh (real risk of an ABI/struct-layout mismatch between
# two different SoLoud versions -- would link fine and misbehave at
# runtime), compile all of SoLoud from the submodule consistently, the
# same way Tilengine is built above.
#
# Confirmed real paths in this fork: src/core/soloud_queue.cpp,
# include/soloud_queue.h, and src/backend/sdl2_static/soloud_sdl2_static.cpp
# (the "-static" backend variant, matching how everything else here is
# linked statically). src/audiosource and src/filter are assumed to match
# upstream's flat-ish layout the same way src/core did -- GLOB_RECURSE
# below tolerates subdirectories either way, and the FATAL_ERROR guard
# catches a total mismatch, though it can't catch a partial one (e.g. one
# missing class file) the way a real build system would.
set(SOLOUD_DIR ${CMAKE_SOURCE_DIR}/subprojects/SoLoud)
if (NOT EXISTS ${SOLOUD_DIR}/src/core/soloud_queue.cpp)
    message(FATAL_ERROR "subprojects/SoLoud looks empty or restructured -- run: git submodule update --init subprojects/SoLoud")
endif ()

file(GLOB SOLOUD_CORE_SOURCES ${SOLOUD_DIR}/src/core/*.cpp)
file(GLOB_RECURSE SOLOUD_AUDIOSOURCE_SOURCES ${SOLOUD_DIR}/src/audiosource/*.cpp ${SOLOUD_DIR}/src/audiosource/*.c)
file(GLOB_RECURSE SOLOUD_FILTER_SOURCES ${SOLOUD_DIR}/src/filter/*.cpp)
set(SOLOUD_BACKEND_SOURCE ${SOLOUD_DIR}/src/backend/sdl2_static/soloud_sdl2_static.cpp)

if (NOT SOLOUD_AUDIOSOURCE_SOURCES)
    message(FATAL_ERROR "No sources found under subprojects/SoLoud/src/audiosource -- layout doesn't match what this assumes. Run `ls -R subprojects/SoLoud/src/audiosource` and tell me what's there.")
endif ()

# soloud_openmpt.cpp (wherever it lands via the audiosource glob above)
# wraps the external libopenmpt C API, so linking it needs the real
# libopenmpt -- separate from libmodplug/libxmp, which are SDL2_mixer's
# own, unrelated mod-tracker decoders. REQUIRED now since we're compiling
# SoLoud's openmpt wrapper ourselves rather than relying on however the
# prebuilt package handled it. If this isn't a vdpm package, you'll need
# to build subprojects/openmpt (its own submodule -- same drill).
find_library(OPENMPT_LIBRARY NAMES openmpt REQUIRED)

add_library(SoLoud STATIC
    ${SOLOUD_CORE_SOURCES}
    ${SOLOUD_AUDIOSOURCE_SOURCES}
    ${SOLOUD_FILTER_SOURCES}
    ${SOLOUD_BACKEND_SOURCE}
)
target_include_directories(SoLoud PUBLIC ${SOLOUD_DIR}/include)
# soloud_openmpt.h does `#include <libopenmpt.h>` unqualified -- the real
# header lives at .../include/libopenmpt/libopenmpt.h (confirmed via
# `find`), so that subdirectory itself needs to be on the include path,
# not just its parent (which is already covered by the toolchain's
# default sysroot search).
find_path(LIBOPENMPT_INCLUDE_SUBDIR libopenmpt.h PATH_SUFFIXES libopenmpt REQUIRED)
target_include_directories(SoLoud PUBLIC ${LIBOPENMPT_INCLUDE_SUBDIR})
target_compile_definitions(SoLoud PUBLIC WITH_SDL2_STATIC)
target_link_libraries(SoLoud PUBLIC SDL2::SDL2 ${OPENMPT_LIBRARY})

# ---------------------------------------------------------------------------
# Generated asset headers/sources (mirrors meson's bin2s custom_target
# loop in meson.build, using the project's own tools/bin2s.py +
# tools/callBin2S.py -- same scripts, not reimplemented).
#
# For every data/*.bin file this produces a `<name>_bin.h` (extern uint8_t
# array declaration) and a `<name>_bin.cpp` (the actual byte array, built
# via `xxd -i`) into ${CMAKE_BINARY_DIR}/generated_assets. Game source
# files (menu.h etc.) #include these by bare filename, e.g.
# "font1tiles_bin.h" -- that's the error you hit.
#
# IMPORTANT: callBin2S.py's xxd-generated symbol name is derived from
# whatever path string is passed for the input file, then has "data_" and
# "___" stripped to match bin2s.py's header, which only strips the
# basename. That only lines up if the path passed in is the *relative*
# form "data/whatever.bin" -- an absolute path would mangle to a
# different, longer symbol name and the header/definition would silently
# mismatch (a link error, not a configure error). So this runs with
# WORKING_DIRECTORY set to the repo root and passes the relative path,
# matching what meson does.
# ---------------------------------------------------------------------------
find_program(XXD_EXECUTABLE xxd REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# callBin2S.py invokes bin2s.py as a direct executable (relying on its
# shebang), not via the python interpreter -- make sure that still works
# regardless of how permissions came through git/zip/etc.
file(CHMOD ${CMAKE_SOURCE_DIR}/tools/bin2s.py
     PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                 GROUP_READ GROUP_EXECUTE
                 WORLD_READ WORLD_EXECUTE)

set(GENERATED_ASSETS_DIR ${CMAKE_BINARY_DIR}/generated_assets)
file(MAKE_DIRECTORY ${GENERATED_ASSETS_DIR})

file(GLOB DATA_BIN_FILES ${CMAKE_SOURCE_DIR}/data/*.bin)
if (NOT DATA_BIN_FILES)
    message(FATAL_ERROR "No .bin files found under data/ -- expected ~225 (see data/meson.build).")
endif ()

set(GENERATED_ASSET_SOURCES "")
foreach(binfile ${DATA_BIN_FILES})
    file(RELATIVE_PATH bin_rel_path ${CMAKE_SOURCE_DIR} ${binfile})
    get_filename_component(bin_basename ${binfile} NAME_WE)
    set(output_h ${GENERATED_ASSETS_DIR}/${bin_basename}_bin.h)
    set(output_cpp ${GENERATED_ASSETS_DIR}/${bin_basename}_bin.cpp)

    add_custom_command(
        OUTPUT ${output_h} ${output_cpp}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/callBin2S.py
                ${CMAKE_SOURCE_DIR}/tools/bin2s.py
                ${bin_rel_path}
                ${output_h}
                ${output_cpp}
                gcc
                ${XXD_EXECUTABLE}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS ${binfile} ${CMAKE_SOURCE_DIR}/tools/bin2s.py ${CMAKE_SOURCE_DIR}/tools/callBin2S.py
        VERBATIM
    )
    list(APPEND GENERATED_ASSET_SOURCES ${output_cpp})
endforeach()

include_directories(${GENERATED_ASSETS_DIR} ${SOLOUD_DIR}/include)

# ---------------------------------------------------------------------------
# Apotris sources
#
# This list is kept in sync with the `sources = files(...)` block in
# meson.build. All of the liba_*.cpp platform backends are included
# unconditionally, same as meson does -- each one is a no-op translation
# unit unless its own #ifdef (VITA/SWITCH/PC/...) matches.
# ---------------------------------------------------------------------------
add_executable(${CMAKE_PROJECT_NAME}
               ${GENERATED_ASSET_SOURCES}
               source/audio_files.cpp
               source/blockEngine.cpp
               source/bot.cpp
               source/descriptions.cpp
               source/detectEmulators.c
               source/flashSaves.cpp
               source/game.cpp
               source/gbp_logo.cpp
               source/interrupt.cpp
               source/liba_gba.cpp
               source/liba_mm.cpp
               source/liba_pc.cpp
               source/liba_switch.cpp
               source/liba_vita.cpp
               source/liba_portmaster.cpp
               source/liba_web.cpp
               source/liba_window.cpp
               source/liba_android.cpp
               source/liba_sdl_audio.cpp
               source/LinkWireless.cpp
               source/main.cpp
               source/menuSystem.cpp
               source/menus.cpp
               source/multiplayer.cpp
               source/multiplayerClasses.cpp
               source/music.cpp
               source/prng.cpp
               source/qrcodegen.c
               source/rumblePatterns.cpp
               source/saving.cpp
               source/scenario.cpp
               source/scene.cpp
               source/screenAudio.cpp
               source/screenControls.cpp
               source/screenCredits.cpp
               source/screenException.cpp
               source/screenMultiplayerResults.cpp
               source/screenGraphics.cpp
               source/screenHandling.cpp
               source/screenLinks.cpp
               source/screenPlayOptions.cpp
               source/screenSkinEditor.cpp
               source/screenColorEditor.cpp
               source/skinFile.cpp
               source/stb_impl.cpp
               source/screenTitle.cpp
               source/sprites.cpp
               source/tetromino.cpp
               source/text.cpp
               source/nanotime.c
               source/shader.cpp
               source/gl.c
               source/gles2.c
               source/xoshiro128plusplus.c
)

# ---------------------------------------------------------------------------
# Link libraries
#
# Removed entirely vs. the loader template: OpenSLES, sndfile+codec
# find_library() loop, vitaGL, vitashark, SceShaccCg(Ext), taihen_stub,
# kubridge_stub, mathneon, ScePgf_stub, SceMotion_stub, SceHid_stub,
# SceKernelDmacMgr_stub, SceVshBridge_stub, and the whole SceLibcBridge
# subdirectory -- all of those existed specifically to run a prebuilt
# Android OpenGL ES binary, which is not what's happening here.
# ---------------------------------------------------------------------------
target_link_libraries(${CMAKE_PROJECT_NAME}
                      -Wl,--whole-archive pthread -Wl,--no-whole-archive
                      SDL2::SDL2
                      SDL2_mixer::SDL2_mixer-static
                      Tilengine
                      SoLoud
                      z
                      m
                      SceCtrl_stub
                      SceTouch_stub
                      SceDisplay_stub
                      SceGxm_stub
                      SceAudio_stub
                      SceAudioIn_stub
                      SceAppMgr_stub
                      SceAppUtil_stub
                      SceCommonDialog_stub
                      SceSysmodule_stub
                      ScePower_stub
                      )

# ---------------------------------------------------------------------------
# Packaging: eboot.bin + param.sfo + vpk
#
# TODO: real sce_sys/livearea assets (icon0.png, bg0.png, startup.png,
# template.xml) -- sprites/favicon32.bmp isn't sized/formatted for this,
# so the vpk below ships with whatever vita_create_vpk()'s defaults are
# until that's made.
# ---------------------------------------------------------------------------
vita_create_self(eboot.bin ${CMAKE_PROJECT_NAME} UNSAFE)
vita_create_vpk(${VITA_VPKNAME}.vpk ${VITA_TITLEID} eboot.bin
                VERSION ${VITA_VERSION}
                NAME ${VITA_APP_NAME}
                )

add_custom_target(copy_release_vpk ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/${VITA_VPKNAME}.vpk"
            "${CMAKE_SOURCE_DIR}/${VITA_VPKNAME}.vpk"
    DEPENDS ${VITA_VPKNAME}.vpk-vpk
    COMMENT "Updating root ${VITA_VPKNAME}.vpk"
    VERBATIM)

# ---------------------------------------------------------------------------
# Dev-deploy helpers (adapted, not removed -- these are generic vitasdk
# workflow, not loader-specific). Requires ftpvitalib/VitaShell's FTP+cmd
# server running on-device.
# ---------------------------------------------------------------------------
add_custom_target(send
                  COMMAND echo destroy | nc ${PSVITAIP} 1338
                  COMMAND curl -T eboot.bin ftp://${PSVITAIP}:1337/ux0:/app/${VITA_TITLEID}/
                  COMMAND echo launch ${VITA_TITLEID} | nc ${PSVITAIP} 1338
                  DEPENDS ${VITA_VPKNAME}.vpk-vpk
                  )

add_custom_target(reboot
                  COMMAND echo reboot | nc ${PSVITAIP} 1338
                  )
