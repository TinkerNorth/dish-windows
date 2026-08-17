# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Dish contributors.
#
# Stages the COMPLETE install image that dish-setup.exe carries. Run in script
# mode by the `dish_setup_image` custom target:
#
#   cmake -DIMAGE_DIR=... -DDISH_EXE=... -DQMLDIR_FILE=... -DQT_BIN_DIR=...
#         -DSRC_DIR=... -DDEPLOY_CONFIG=Release
#         -P cmake/DishSetupImage.cmake
#
# What lands in IMAGE_DIR is exactly what ends up on the user's disk:
# installer.iss ships the directory verbatim ([Files] Source: "{#ImageDir}\*")
# and enumerates nothing itself, so this script is the one reviewed place that
# decides what an installed Dish consists of.
#
# The image is wiped and rebuilt every run. Incremental staging would let a
# file deleted from the build tree survive into a release, and the whole point
# of this directory is that it is a faithful snapshot.

cmake_minimum_required(VERSION 3.21)

foreach(_required IMAGE_DIR DISH_EXE QMLDIR_FILE QT_BIN_DIR SRC_DIR)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "DishSetupImage.cmake: -D${_required} is required")
    endif()
endforeach()

if(NOT EXISTS "${DISH_EXE}")
    message(FATAL_ERROR "DishSetupImage.cmake: missing input ${DISH_EXE}")
endif()

if(NOT DEFINED DEPLOY_CONFIG OR "${DEPLOY_CONFIG}" STREQUAL "")
    set(DEPLOY_CONFIG "Release")
endif()
if(DEPLOY_CONFIG STREQUAL "Debug")
    # A debug image is a developer convenience only: it pairs debug Qt DLLs
    # with the RELEASE app-local CRT staged below, so it must never be
    # published. Release is what release.yml and windows-ci.yml build.
    set(_deploy_config_flag "--debug")
    message(WARNING
        "Staging a DEBUG install image. It is not redistributable: the Qt DLLs "
        "are debug builds and the staged CRT is the release redistributable.")
else()
    set(_deploy_config_flag "--release")
endif()

find_program(WINDEPLOYQT windeployqt HINTS "${QT_BIN_DIR}" NO_DEFAULT_PATH)
if(NOT WINDEPLOYQT)
    message(FATAL_ERROR "DishSetupImage.cmake: windeployqt not found under ${QT_BIN_DIR}")
endif()

# --- 1. Empty image directory ------------------------------------------------
file(REMOVE_RECURSE "${IMAGE_DIR}")
file(MAKE_DIRECTORY "${IMAGE_DIR}")
message(STATUS "Install image: ${IMAGE_DIR}")

# --- 2. The app, plus its Qt runtime -----------------------------------------
file(COPY "${DISH_EXE}" DESTINATION "${IMAGE_DIR}")
get_filename_component(_dish_exe_name "${DISH_EXE}" NAME)

# Same flags release.yml uses, plus the two "don't ship the fallbacks" ones.
# --no-compiler-runtime on purpose: windeployqt's idea of shipping the CRT is
# to drop vc_redist.x64.exe next to the app, and this image stages the five
# DLLs app-locally instead so nothing has to be installed system-wide.
execute_process(
    COMMAND "${WINDEPLOYQT}"
            ${_deploy_config_flag}
            --no-translations
            --no-system-d3d-compiler
            --no-opengl-sw
            --no-compiler-runtime
            --qmldir "${SRC_DIR}/src/qml"
            "${IMAGE_DIR}/${_dish_exe_name}"
    RESULT_VARIABLE _deploy_result)
if(NOT _deploy_result EQUAL 0)
    message(FATAL_ERROR "windeployqt failed on ${_dish_exe_name} (${_deploy_result})")
endif()

# --- 3. The app's OWN QML module entry ---------------------------------------
# windeployqt deploys the QT modules the imports resolve to, and Dish.Chrome
# is not one of Qt's; dish.exe carries a compiled-in copy. Staged anyway
# because Qt searches <exedir>/Dish/Chrome/qmldir BEFORE the compiled-in copy,
# so if anything ever puts a file there it had better be the app's own. The
# asserts fail the staging, named, if the build's CMake arrangement regresses
# and the generated qmldir stops declaring the app's Main type. Same staging,
# and the same asserts, as release.yml's portable bundle.
#
# (Historical note: while the QML setup wizard existed, the image deliberately
# shipped NO qmldir — the wizard's identically-named module resolved through
# the same on-disk entry with an incompatible resource layout. The wizard is
# gone; the image and the portable zip now stage the identical file.)
if(NOT EXISTS "${QMLDIR_FILE}")
    message(FATAL_ERROR
        "No Dish.Chrome qmldir at ${QMLDIR_FILE} - the staged dish.exe could "
        "not resolve its own QML module")
endif()
file(READ "${QMLDIR_FILE}" _qmldir_text)
if(NOT _qmldir_text MATCHES "\nMain 1\\.0 |^Main 1\\.0 ")
    message(FATAL_ERROR
        "${QMLDIR_FILE} does not declare the app's Main type, so it is the "
        "wrong Dish.Chrome qmldir")
endif()
file(MAKE_DIRECTORY "${IMAGE_DIR}/Dish/Chrome")
file(COPY "${QMLDIR_FILE}" DESTINATION "${IMAGE_DIR}/Dish/Chrome")

# --- 4. Non-Qt runtime DLLs (libsodium, SDL2) --------------------------------
# windeployqt walks Qt's own dependency graph and nothing else, so the two
# vcpkg libraries dish.exe imports directly would be missing from the image and
# the installed app would fail to start with 0xc0000135 before drawing a pixel.
# vcpkg's applocal step drops them beside the freshly linked exe, which is
# where this picks them up: anything already staged (every Qt6*.dll and the
# graphics fallbacks) is skipped, so a new dependency is staged automatically
# instead of silently going missing.
get_filename_component(_build_bin_dir "${DISH_EXE}" DIRECTORY)
file(GLOB _build_dlls "${_build_bin_dir}/*.dll")
set(_extra_dlls "")
foreach(_dll IN LISTS _build_dlls)
    get_filename_component(_dll_name "${_dll}" NAME)
    if(NOT EXISTS "${IMAGE_DIR}/${_dll_name}")
        file(COPY "${_dll}" DESTINATION "${IMAGE_DIR}")
        list(APPEND _extra_dlls "${_dll_name}")
    endif()
endforeach()
if(_extra_dlls)
    list(JOIN _extra_dlls ", " _extra_dlls_text)
    message(STATUS "Staged non-Qt runtime DLLs: ${_extra_dlls_text}")
endif()

# --- 5. App-local VC++ runtime -----------------------------------------------
# "Runs on a machine where NOTHING is installed" is a hard requirement, and
# app-local deployment is the only redistribution form that needs no installer
# of its own. THIRD_PARTY.md documents the redist terms.
if(NOT DEFINED ENV{VCToolsRedistDir} OR "$ENV{VCToolsRedistDir}" STREQUAL "")
    message(FATAL_ERROR
        "VCToolsRedistDir is not set: run this from a Visual Studio developer "
        "environment (vcvars64.bat / the MSVC dev-cmd CI step). The install "
        "image cannot ship without the app-local CRT.")
endif()
set(_crt_dir "$ENV{VCToolsRedistDir}/x64/Microsoft.VC143.CRT")
if(NOT IS_DIRECTORY "${_crt_dir}")
    message(FATAL_ERROR "No Microsoft.VC143.CRT under ${_crt_dir}")
endif()
foreach(_crt_dll msvcp140.dll msvcp140_1.dll msvcp140_2.dll
                 vcruntime140.dll vcruntime140_1.dll)
    if(NOT EXISTS "${_crt_dir}/${_crt_dll}")
        message(FATAL_ERROR "Missing ${_crt_dll} in ${_crt_dir}")
    endif()
    file(COPY "${_crt_dir}/${_crt_dll}" DESTINATION "${IMAGE_DIR}")
endforeach()

# --- 6. Licence texts --------------------------------------------------------
# Identical staging to release.yml's "Stage licence texts": LGPLv3 s4(b) needs
# the licence to travel with the Combined Work, OFL-1.1 s2 the same for the
# four Inter faces compiled into the exe. installer.iss also points its
# wizard's licence page at the LGPL text staged here.
file(MAKE_DIRECTORY "${IMAGE_DIR}/licenses")
configure_file("${SRC_DIR}/LICENSE"      "${IMAGE_DIR}/licenses/LICENSE.LGPL-3.0.txt" COPYONLY)
configure_file("${SRC_DIR}/COPYING.GPL3" "${IMAGE_DIR}/licenses/LICENSE.GPL-3.0.txt"  COPYONLY)
configure_file("${SRC_DIR}/THIRD_PARTY.md" "${IMAGE_DIR}/licenses/THIRD_PARTY.md"     COPYONLY)
configure_file("${SRC_DIR}/packaging/fonts/Inter-LICENSE.txt"
               "${IMAGE_DIR}/licenses/Inter-LICENSE.txt" COPYONLY)

# --- 7. Summary --------------------------------------------------------------
file(GLOB_RECURSE _image_files LIST_DIRECTORIES false "${IMAGE_DIR}/*")
set(_image_bytes 0)
foreach(_file IN LISTS _image_files)
    file(SIZE "${_file}" _size)
    math(EXPR _image_bytes "${_image_bytes} + ${_size}")
endforeach()
list(LENGTH _image_files _image_count)
math(EXPR _image_mib "${_image_bytes} / 1048576")
message(STATUS "Install image staged: ${_image_count} files, ${_image_mib} MiB uncompressed")
