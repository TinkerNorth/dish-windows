# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Dish contributors.
#
# Stages the COMPLETE install image that dish-setup.exe carries (spec 1.2).
# Run in script mode by the `dish_setup_image` custom target:
#
#   cmake -DIMAGE_DIR=... -DDISH_EXE=... -DUI_EXE=... -DHELPER_EXE=...
#         -DQT_BIN_DIR=... -DSRC_DIR=... -DDEPLOY_CONFIG=Release
#         -P cmake/DishSetupImage.cmake
#
# What lands in IMAGE_DIR is exactly what ends up on the user's disk, minus the
# two files the pack tool handles specially: dish-setup-ui.exe (staged, then
# copied to <install>/uninstall.exe under its manifest alias, spec D9) and
# manifest.json (written by the pack tool, describing everything else).
#
# The image is wiped and rebuilt every run. Incremental staging would let a
# file deleted from the build tree survive into a release, and the whole point
# of this directory is that it is a faithful snapshot.

cmake_minimum_required(VERSION 3.21)

foreach(_required IMAGE_DIR DISH_EXE UI_EXE HELPER_EXE QT_BIN_DIR SRC_DIR)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "DishSetupImage.cmake: -D${_required} is required")
    endif()
endforeach()

foreach(_input "${DISH_EXE}" "${UI_EXE}" "${HELPER_EXE}")
    if(NOT EXISTS "${_input}")
        message(FATAL_ERROR "DishSetupImage.cmake: missing input ${_input}")
    endif()
endforeach()

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
# to drop vc_redist.x64.exe next to the app, and this installer stages the
# five DLLs app-locally instead (spec D2) so nothing has to be installed
# system-wide. --qmldir covers BOTH src/qml and src/qml/setup, which is what
# makes the wizard's QML imports a subset of the installed runtime.
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

# --- 2b. The app's own QML module entry (Dish.Chrome) -------------------------
# The app and dish_setup_kit declare the SAME module URI, and Qt derives the
# generated qmldir resource's name from the URI alone. While both
# qt_add_qml_module calls lived in one CMake scope they therefore shared one
# `.qt/rcc/qmake_Dish_Chrome.qrc`: the second call won it, and dish.exe shipped
# with the kit's qmldir — ~40 kit types, `plugin dish_setup_kitplugin`, no Main
# — so the installed app died at startup with
#   Module "Dish.Chrome" contains no type named "Main"
# while the build tree kept working. src/qml/kit/CMakeLists.txt fixed that at
# the root by declaring the kit's module from a directory scope of its own, and
# dish.exe now carries the app's module entry compiled in.
#
# The file still travels with the image, because Qt searches
# <exedir>/Dish/Chrome/qmldir BEFORE the compiled-in copy — the very precedence
# that let the bug hide in the build tree. An image carrying the entry its own
# build generated is correct whichever copy the engine reaches for; one carrying
# the kit's is dead on arrival. It is only the module ENTRY: its
# `prefer :/qt/qml/Dish/Chrome/` line sends every type to the compiled-in copy,
# so no .qml source ships.
#
# The checks below are what make a regression in that CMake arrangement a build
# failure with a name on it, rather than a dead install.
get_filename_component(_app_build_dir "${DISH_EXE}" DIRECTORY)
set(_app_qmldir "${_app_build_dir}/Dish/Chrome/qmldir")
if(NOT EXISTS "${_app_qmldir}")
    message(FATAL_ERROR
        "DishSetupImage.cmake: no Dish.Chrome qmldir at ${_app_qmldir}. Without "
        "it the installed dish.exe cannot resolve its own QML module.")
endif()
file(READ "${_app_qmldir}" _app_qmldir_text)
# The one thing that must be true of it: it is the APP's module, not the kit's.
if(NOT _app_qmldir_text MATCHES "(^|\n)Main 1\\.0 ")
    message(FATAL_ERROR
        "DishSetupImage.cmake: ${_app_qmldir} does not declare the app's Main "
        "type, so it is the wrong Dish.Chrome qmldir (the setup kit's?).")
endif()
file(MAKE_DIRECTORY "${IMAGE_DIR}/Dish/Chrome")
configure_file("${_app_qmldir}" "${IMAGE_DIR}/Dish/Chrome/qmldir" COPYONLY)

# --- 3. The wizard binary, unioned into the same runtime ---------------------
# Expected delta: none. It is run anyway so a setup-only Qt module (a plugin
# the app never loads) cannot go missing from the image and take the whole
# wizard down on a machine with no Qt.
file(COPY "${UI_EXE}" DESTINATION "${IMAGE_DIR}")
get_filename_component(_ui_exe_name "${UI_EXE}" NAME)
execute_process(
    COMMAND "${WINDEPLOYQT}"
            ${_deploy_config_flag}
            --no-translations
            --no-system-d3d-compiler
            --no-opengl-sw
            --no-compiler-runtime
            --qmldir "${SRC_DIR}/src/qml"
            "${IMAGE_DIR}/${_ui_exe_name}"
    RESULT_VARIABLE _deploy_ui_result)
if(NOT _deploy_ui_result EQUAL 0)
    message(FATAL_ERROR "windeployqt failed on ${_ui_exe_name} (${_deploy_ui_result})")
endif()

# --- 4. Non-Qt runtime DLLs (libsodium, SDL2) -------------------------------
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

# --- 5. The uninstall janitor ------------------------------------------------
file(COPY "${HELPER_EXE}" DESTINATION "${IMAGE_DIR}")

# --- 6. App-local VC++ runtime ----------------------------------------------
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

# --- 7. Licence texts --------------------------------------------------------
# Identical staging to release.yml's "Stage licence texts": LGPLv3 s4(b) needs
# the licence to travel with the Combined Work, OFL-1.1 s2 the same for the
# four Inter faces compiled into the exe. The wizard's License page renders
# these same files.
file(MAKE_DIRECTORY "${IMAGE_DIR}/licenses")
configure_file("${SRC_DIR}/LICENSE"      "${IMAGE_DIR}/licenses/LICENSE.LGPL-3.0.txt" COPYONLY)
configure_file("${SRC_DIR}/COPYING.GPL3" "${IMAGE_DIR}/licenses/LICENSE.GPL-3.0.txt"  COPYONLY)
configure_file("${SRC_DIR}/THIRD_PARTY.md" "${IMAGE_DIR}/licenses/THIRD_PARTY.md"     COPYONLY)
configure_file("${SRC_DIR}/packaging/fonts/Inter-LICENSE.txt"
               "${IMAGE_DIR}/licenses/Inter-LICENSE.txt" COPYONLY)

# --- 8. Sanity -------------------------------------------------------------
# The pack tool refuses an image carrying a literal uninstall.exe (it is an
# alias entry, not a second copy) and one without dish-setup-ui.exe. Failing
# here names the cause instead of leaving that to a packing error.
if(EXISTS "${IMAGE_DIR}/uninstall.exe")
    message(FATAL_ERROR
        "The image contains a literal uninstall.exe. It must exist only as the "
        "manifest alias of dish-setup-ui.exe (spec D9).")
endif()
if(NOT EXISTS "${IMAGE_DIR}/${_ui_exe_name}")
    message(FATAL_ERROR "The image is missing ${_ui_exe_name}")
endif()

file(GLOB_RECURSE _image_files LIST_DIRECTORIES false "${IMAGE_DIR}/*")
set(_image_bytes 0)
foreach(_file IN LISTS _image_files)
    file(SIZE "${_file}" _size)
    math(EXPR _image_bytes "${_image_bytes} + ${_size}")
endforeach()
list(LENGTH _image_files _image_count)
math(EXPR _image_mib "${_image_bytes} / 1048576")
message(STATUS "Install image staged: ${_image_count} files, ${_image_mib} MiB uncompressed")
