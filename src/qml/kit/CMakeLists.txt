# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Dish contributors.
#
# dish_setup_kit: a second compile of the chrome + design kit, linked only into
# dish-setup-ui.exe. Same module URI as the app's (Dish.Chrome), so not one of
# the ~40 kit files nor any import statement in the wizard changes (spec D3).
#
# WHY THIS IS ITS OWN DIRECTORY SCOPE, and not part of installer/CMakeLists.txt
# with every other installer target:
#
# qt_add_qml_module embeds the module's generated qmldir in a resource whose
# NAME it derives from the URI alone --- Qt6QmlMacros.cmake does
#     string(REPLACE "/" "_" qmldir_resource_name "qmake_${TARGET_PATH}")
# with TARGET_PATH defaulting to the URI --- and qt6_add_resources writes that
# resource to ${CMAKE_CURRENT_BINARY_DIR}/.qt/rcc/<name>.qrc. Two modules
# sharing one URI in one directory scope therefore share ONE generated .qrc and
# ONE qrc_*.cpp object: whichever call runs second wins the file, and the object
# is linked into BOTH binaries. The root CMakeLists include()s
# installer/CMakeLists.txt rather than add_subdirectory()ing it (for reasons
# spelled out at the include site), so before this file existed the setup kit's
# qmldir was the one compiled into dish.exe, and the deployed app died with
#     Module "Dish.Chrome" contains no type named "Main"
# while running from the build tree kept working, because Qt finds
# <builddir>/Dish/Chrome/qmldir on the applicationDirPath import path first.
#
# A directory scope of its own is what makes the two resources distinct
# (.qt/rcc/qmake_Dish_Chrome.qrc here vs the same name at the top level), using
# nothing but documented behaviour: no URI rename, no edits inside the kit or
# the wizard, and no reliance on qt_add_qml_module's TARGET_PATH, which Qt marks
# "Internal option only, it may be removed".
#
# It has to be THIS directory rather than a scope under installer/, because
# qmlcachegen names every generated .cpp after the QML file's path RELATIVE TO
# THE DECLARING DIRECTORY: from installer/ each one would be "../src/qml/..."
# and ask Windows for a directory whose name ends in a dot. Declaring the module
# from the directory the kit files live in gives plain basenames. The backing
# library moves with it because qt6_extract_metatypes emits
# add_custom_command(OUTPUT) rules CMake only materialises for consumers in the
# same directory --- splitting the target from its qml module breaks the
# metatypes step and qmltyperegistrar then fails on empty input.

# --- dish_setup_kit: a second compile of the chrome + design kit -------------
add_library(dish_setup_kit STATIC
    ${CMAKE_SOURCE_DIR}/src/qml/chrome/ThemeBridge.h
    ${CMAKE_SOURCE_DIR}/src/qml/chrome/ThemeBridge.cpp
    ${CMAKE_SOURCE_DIR}/src/qml/chrome/TokensBridge.h
    ${CMAKE_SOURCE_DIR}/src/qml/chrome/TokensBridge.cpp
    ${CMAKE_SOURCE_DIR}/src/qml/chrome/ChromeBridge.h
    ${CMAKE_SOURCE_DIR}/src/qml/chrome/ChromeBridge.cpp
    ${CMAKE_SOURCE_DIR}/src/qml/chrome/FramelessWindowChrome.h
    ${CMAKE_SOURCE_DIR}/src/qml/chrome/FramelessWindowChrome.cpp
    ${CMAKE_SOURCE_DIR}/src/qml/chrome/WindowHitTest.h
    # The LTCG-safe instance registration, replicated instead of shared so the
    # app's QmlEntryPoint.cpp stays untouched (spec D4).
    ${CMAKE_SOURCE_DIR}/src/qml/chrome/ChromeSingletons.h
    ${CMAKE_SOURCE_DIR}/src/qml/chrome/ChromeSingletons.cpp
    ${CMAKE_SOURCE_DIR}/src/UI/Theme.h
    ${CMAKE_SOURCE_DIR}/src/UI/Theme.cpp
    ${CMAKE_SOURCE_DIR}/src/UI/FontStacks.h
    ${CMAKE_SOURCE_DIR}/src/UI/FontStacks.cpp
    ${CMAKE_SOURCE_DIR}/src/UI/licenses/LicenseManifest.h
    ${CMAKE_SOURCE_DIR}/src/UI/licenses/LicenseManifest.cpp
    ${CMAKE_SOURCE_DIR}/src/Util/Localization.h
    ${CMAKE_SOURCE_DIR}/src/Util/Localization.cpp)
target_include_directories(dish_setup_kit PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    # qmltyperegistrar's generated .cpp includes the bridge headers by
    # BASENAME, so their directory has to be reachable.
    ${CMAKE_SOURCE_DIR}/src/qml/chrome)
target_link_libraries(dish_setup_kit
    PUBLIC Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
    PRIVATE dish_warnings dwmapi)

# DISH_KIT_QML_FILES_SHARED holds repo-root-relative paths, because the app
# module is declared at the root; re-anchored here, or they would resolve to
# src/qml/kit/src/qml/kit/*.qml. Relative and not absolute: Qt refuses an
# absolute path in a resource unless QT_RESOURCE_ALIAS is set on every file, and
# the relative form IS the alias we want. The kit therefore lands at
# :/qt/qml/Dish/Chrome/<Name>.qml in dish-setup-ui.exe against
# :/qt/qml/Dish/Chrome/src/qml/kit/<Name>.qml in dish.exe. Both layouts are
# self-consistent (each generated qmldir names the paths that module registered)
# and no kit file addresses another by URL.
#
# The FATAL_ERROR is the guard on that: a kit file added OUTSIDE this directory
# would silently get a "../" alias, which the resource system stores as a path
# the engine cannot resolve. Move it here, or give it its own module.
set(DISH_SETUP_KIT_QML_FILES "")
foreach(_kit_qml IN LISTS DISH_KIT_QML_FILES_SHARED)
    file(RELATIVE_PATH _kit_qml_rel
         "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/${_kit_qml}")
    if(_kit_qml_rel MATCHES "^\\.\\.")
        message(FATAL_ERROR
            "KitFiles.cmake lists ${_kit_qml}, which is outside "
            "${CMAKE_CURRENT_SOURCE_DIR}. The setup kit's QML module is declared "
            "here so its resource aliases stay inside the module.")
    endif()
    list(APPEND DISH_SETUP_KIT_QML_FILES "${_kit_qml_rel}")
endforeach()

qt_add_qml_module(dish_setup_kit
    URI Dish.Chrome
    VERSION 1.0
    DEPENDENCIES QtQuick
    # Not under ${CMAKE_BINARY_DIR}/Dish: CI runs `qmllint -I <builddir>`, and
    # two qmldirs for one URI under that root make resolution a coin flip.
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/setup-qml/Dish/Chrome"
    QML_FILES ${DISH_SETUP_KIT_QML_FILES})

# C4702 (unreachable code) fires inside Qt's own headers from the TUs
# qmlcachegen generates.
if(MSVC)
    target_compile_options(dish_setup_kit PRIVATE /wd4702)
endif()
set_property(TARGET dish_setup_kit PROPERTY INTERPROCEDURAL_OPTIMIZATION FALSE)
