# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Dish contributors.
#
# Everything dish-setup.exe is made of. Pulled in unconditionally from the root
# CMakeLists (spec D2): there is no DISH_BUILD_INSTALLER option, because the
# installer carries user-facing strings and lupdate must see the same set for
# every contributor. Only the two packaging custom targets at the bottom
# (dish_setup_image, dish_setup_exe) are outside ALL — they run windeployqt and
# zip ~100 MB, which no ordinary build should pay for.
#
# NOTE ON SCOPE: the root include()s this file rather than add_subdirectory()ing
# it, so everything below evaluates with CMAKE_CURRENT_SOURCE_DIR at the REPO
# ROOT. That is deliberate and load-bearing — see the comment at the include()
# site — and it is why the QML_FILES lists below are plain repo-relative paths
# exactly like the app module's, and why ${CMAKE_CURRENT_BINARY_DIR} here means
# the top-level build directory.
#
# The artifact is [stub PE][payload.zip][32-byte trailer] (spec 1.1):
#
#   dish_setup_exe ── DishPayloadPack ── dish_setup_image ── Dish
#                  └─ DishSetupStub                       ├─ DishSetupUi
#                                                         └─ DishUninstallHelper
#
# Runtime-library split matters here: DishSetupStub and DishUninstallHelper are
# /MT because they must run before anything is installed; every other target
# stays /MD and runs against the CRT DLLs the payload stages beside them.

# All four installer binaries land beside dish.exe in the top-level build dir
# rather than in ${CMAKE_CURRENT_BINARY_DIR}. That is what lets a developer run
# dish-setup-ui.exe straight out of the build tree against the Qt runtime
# windeployqt already put there, and it keeps the packaging commands and
# scripts/test-installer-roundtrip.ps1 on one predictable path.
set(DISH_SETUP_BIN_DIR "${CMAKE_BINARY_DIR}")

# --- Version resources -------------------------------------------------------
# configure_file from PROJECT_VERSION so there is no fourth hand-synced version
# site. packaging/dish.rc stays hand-written (and release.yml now asserts it).
set(DISH_SETUP_ICON "${CMAKE_SOURCE_DIR}/packaging/dish.ico")
configure_file(${CMAKE_SOURCE_DIR}/packaging/setup/setup-stub.rc.in
               ${CMAKE_CURRENT_BINARY_DIR}/setup-stub.rc @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/packaging/setup/setup-ui.rc.in
               ${CMAKE_CURRENT_BINARY_DIR}/setup-ui.rc @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/packaging/setup/helper.rc.in
               ${CMAKE_CURRENT_BINARY_DIR}/helper.rc @ONLY)

set(DISH_SETUP_MANIFEST ${CMAKE_SOURCE_DIR}/packaging/setup/setup.manifest)

# miniz is vendored C, not ours: /W3 and never /WX, and it defines
# WIN32_LEAN_AND_MEAN itself, which collides with dish_warnings' definition.
# Source properties are directory-scoped, so tests/CMakeLists.txt repeats this.
if(MSVC)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/third_party/miniz/miniz.c
        PROPERTIES COMPILE_OPTIONS "/W3;/wd4005")
endif()

# --- dish_installer_core: the engine, no Quick ------------------------------
# Silent mode and the whole test suite stay headless, so nothing in here may
# touch Qt Quick. The two coordinators are QObjects; AUTOMOC is on from the
# root scope.
add_library(dish_installer_core STATIC
    ${CMAKE_SOURCE_DIR}/src/installer/Errors.h
    ${CMAKE_SOURCE_DIR}/src/installer/InstallPlan.h
    ${CMAKE_SOURCE_DIR}/src/installer/Manifest.h
    ${CMAKE_SOURCE_DIR}/src/installer/Manifest.cpp
    ${CMAKE_SOURCE_DIR}/src/installer/VersionCompare.h
    ${CMAKE_SOURCE_DIR}/src/installer/VersionCompare.cpp
    ${CMAKE_SOURCE_DIR}/src/installer/Journal.h
    ${CMAKE_SOURCE_DIR}/src/installer/Journal.cpp
    ${CMAKE_SOURCE_DIR}/src/installer/CliOptions.h
    ${CMAKE_SOURCE_DIR}/src/installer/CliOptions.cpp
    ${CMAKE_SOURCE_DIR}/src/installer/Logger.h
    ${CMAKE_SOURCE_DIR}/src/installer/Logger.cpp
    ${CMAKE_SOURCE_DIR}/src/installer/InstallMachine.h
    ${CMAKE_SOURCE_DIR}/src/installer/InstallMachine.cpp
    ${CMAKE_SOURCE_DIR}/src/installer/UninstallMachine.h
    ${CMAKE_SOURCE_DIR}/src/installer/UninstallMachine.cpp
    ${CMAKE_SOURCE_DIR}/src/installer/InstallCoordinator.h
    ${CMAKE_SOURCE_DIR}/src/installer/InstallCoordinator.cpp
    ${CMAKE_SOURCE_DIR}/src/installer/UninstallCoordinator.h
    ${CMAKE_SOURCE_DIR}/src/installer/UninstallCoordinator.cpp
    ${CMAKE_SOURCE_DIR}/src/installer/SilentRunner.h
    ${CMAKE_SOURCE_DIR}/src/installer/SilentRunner.cpp
    ${CMAKE_SOURCE_DIR}/src/installer/UpdateApply.h
    ${CMAKE_SOURCE_DIR}/src/installer/UpdateApply.cpp
    ${CMAKE_SOURCE_DIR}/src/installer/ops/FileOps.h
    ${CMAKE_SOURCE_DIR}/src/installer/ops/Win32FileOps.h
    ${CMAKE_SOURCE_DIR}/src/installer/ops/Win32FileOps.cpp
    ${CMAKE_SOURCE_DIR}/src/installer/ops/RegistryOps.h
    ${CMAKE_SOURCE_DIR}/src/installer/ops/Win32RegistryOps.h
    ${CMAKE_SOURCE_DIR}/src/installer/ops/Win32RegistryOps.cpp
    ${CMAKE_SOURCE_DIR}/src/installer/ops/ShortcutOps.h
    ${CMAKE_SOURCE_DIR}/src/installer/ops/Win32ShortcutOps.h
    ${CMAKE_SOURCE_DIR}/src/installer/ops/Win32ShortcutOps.cpp
    ${CMAKE_SOURCE_DIR}/src/installer/ops/ProcessOps.h
    ${CMAKE_SOURCE_DIR}/src/installer/ops/Win32ProcessOps.h
    ${CMAKE_SOURCE_DIR}/src/installer/ops/Win32ProcessOps.cpp
    ${CMAKE_SOURCE_DIR}/src/installer/ops/KnownFolders.h
    ${CMAKE_SOURCE_DIR}/src/installer/ops/KnownFolders.cpp)
# The engine includes itself as "installer/...", the same rooted-at-src form
# the rest of the tree uses.
target_include_directories(dish_installer_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
# Same single source as dish_core's: PROJECT_VERSION. Without it --version and
# the ARP DisplayVersion fall back to the "0.0.0" the sources compile in.
target_compile_definitions(dish_installer_core PUBLIC DISH_VERSION="${PROJECT_VERSION}")
target_link_libraries(dish_installer_core
    PUBLIC
        Qt6::Core
    PRIVATE
        dish_warnings
        dish_strict
        # BCrypt SHA-256, ARP registry, known folders, IShellLinkW, process
        # enumeration. EnumProcessModules resolves out of kernel32 under the
        # default PSAPI_VERSION=2, so no psapi.
        bcrypt advapi32 shell32 ole32 uuid user32)
# The installer is small and cold-started; LTCG buys nothing and costs minutes.
set_property(TARGET dish_installer_core PROPERTY INTERPROCEDURAL_OPTIMIZATION FALSE)

# --- dish_setup_kit: NOT here, and that is deliberate ------------------------
# The wizard's copy of the chrome + design kit is declared by
# src/qml/kit/CMakeLists.txt, added from the root just above the include() of
# this file. It is the ONE installer target that cannot live in this file: it
# declares a qt_add_qml_module with the same URI as the app's, and Qt names the
# generated qmldir resource after the URI alone, so two such modules in one
# directory scope collide on a single .qt/rcc/qmake_Dish_Chrome.qrc and one
# binary silently gets the other's module. That file explains the whole
# mechanism. Nothing else about the kit changed: same URI, same
# OUTPUT_DIRECTORY, same file list out of KitFiles.cmake.

# --- DishSetupUi: the wizard, and (byte-copied) the uninstaller --------------
qt_add_executable(DishSetupUi WIN32
    ${CMAKE_SOURCE_DIR}/src/installer/app/SetupMain.cpp
    ${CMAKE_SOURCE_DIR}/src/installer/app/SetupController.h
    ${CMAKE_SOURCE_DIR}/src/installer/app/SetupController.cpp
    ${CMAKE_SOURCE_DIR}/src/UI/CrashHandler.h
    ${CMAKE_SOURCE_DIR}/src/UI/CrashHandler.cpp
    # Fonts, brand SVGs, licenses.json and dish.ico. The same .qrc AUTORCC
    # compiles into dish.exe: two independent AUTORCC runs, one file.
    ${CMAKE_SOURCE_DIR}/packaging/dish.qrc
    ${CMAKE_CURRENT_BINARY_DIR}/setup-ui.rc
    ${DISH_SETUP_MANIFEST})
set_target_properties(DishSetupUi PROPERTIES
    OUTPUT_NAME dish-setup-ui
    RUNTIME_OUTPUT_DIRECTORY "${DISH_SETUP_BIN_DIR}")

qt_add_qml_module(DishSetupUi
    URI Dish.Setup
    VERSION 1.0
    DEPENDENCIES QtQuick
    # Under ${CMAKE_BINARY_DIR} so `qmllint -I <builddir>` resolves
    # `import Dish.Setup` the same way it resolves Dish.Chrome.
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Dish/Setup"
    QML_FILES
        src/qml/setup/SetupRoot.qml
        src/qml/setup/SetupTitleBar.qml
        src/qml/setup/SetupHeroRail.qml
        src/qml/setup/SetupStageBar.qml
        src/qml/setup/SetupFooter.qml
        src/qml/setup/WelcomePage.qml
        src/qml/setup/LocationPage.qml
        src/qml/setup/ShortcutsPage.qml
        src/qml/setup/InstallingPage.qml
        src/qml/setup/DonePage.qml
        src/qml/setup/UninstallConfirmPage.qml
        src/qml/setup/UninstallProgressPage.qml
        src/qml/setup/UninstallDonePage.qml
        src/qml/setup/BlockerCloseDialog.qml)

# qmltyperegistrar's generated .cpp includes SetupController.h by BASENAME,
# the same reason the Dish target puts src/qml/chrome on its include path.
target_include_directories(DishSetupUi PRIVATE ${CMAKE_SOURCE_DIR}/src/installer/app)

target_link_libraries(DishSetupUi PRIVATE
    dish_installer_core
    dish_setup_kit
    # The static QML module's plugin: without it the kit's types never
    # register and every Kit.* reference is an unknown component.
    dish_setup_kitplugin
    Qt6::Gui Qt6::Svg Qt6::Quick Qt6::Qml Qt6::QuickControls2
    dish_warnings dish_strict
    # CrashHandler.cpp: dbghelp writes the minidump, shell32 + ole32 resolve
    # %LOCALAPPDATA%. Named rather than left to Qt's transitive link set.
    dbghelp shell32 ole32)
if(MSVC)
    target_compile_options(DishSetupUi PRIVATE /wd4702)
endif()
set_property(TARGET DishSetupUi PROPERTY INTERPROCEDURAL_OPTIMIZATION FALSE)

# --- DishSetupStub: /MT, no Qt, the only binary that runs on a bare machine --
add_executable(DishSetupStub WIN32
    ${CMAKE_SOURCE_DIR}/src/installer/stub/StubMain.cpp
    ${CMAKE_SOURCE_DIR}/src/installer/stub/StubUi.h
    ${CMAKE_SOURCE_DIR}/src/installer/stub/StubUi.cpp
    ${CMAKE_SOURCE_DIR}/src/installer/stub/StubStrings.h
    # Per-target compile: this same source is also built /MD into the pack
    # tool and DishTests.
    ${CMAKE_SOURCE_DIR}/src/installer/PayloadFormat.h
    ${CMAKE_SOURCE_DIR}/src/installer/PayloadFormat.cpp
    ${CMAKE_SOURCE_DIR}/third_party/miniz/miniz.c
    ${CMAKE_CURRENT_BINARY_DIR}/setup-stub.rc
    ${DISH_SETUP_MANIFEST})
set_target_properties(DishSetupStub PROPERTIES
    OUTPUT_NAME dish-setup-stub
    RUNTIME_OUTPUT_DIRECTORY "${DISH_SETUP_BIN_DIR}"
    # The whole point: a machine with no VC++ redistributable must still be
    # able to run this.
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    # AUTOMOC/AUTORCC would only add Qt tooling to a deliberately Qt-free exe.
    AUTOMOC OFF
    AUTORCC OFF
    AUTOUIC OFF)
# Exactly the five libraries spec 1.3 sanctions; kernel32 comes for free.
# comctl32 is what TaskDialogIndirect lives in, which is why setup.manifest
# MUST keep its Common-Controls 6.0.0.0 dependency: without it the import
# cannot bind and dish-setup.exe fails to load at all.
target_link_libraries(DishSetupStub PRIVATE comctl32 user32 shell32 advapi32)
# dish_warnings, but NOT dish_strict: miniz is third-party and never gates.
target_link_libraries(DishSetupStub PRIVATE dish_warnings)

# --- DishUninstallHelper: the /MT janitor that outlives uninstall.exe --------
add_executable(DishUninstallHelper WIN32
    ${CMAKE_SOURCE_DIR}/src/installer/helper/HelperMain.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/helper.rc
    ${DISH_SETUP_MANIFEST})
set_target_properties(DishUninstallHelper PROPERTIES
    OUTPUT_NAME uninstall-helper
    RUNTIME_OUTPUT_DIRECTORY "${DISH_SETUP_BIN_DIR}"
    # It deletes the install directory it was copied out of, and on a machine
    # scope uninstall the CRT DLLs are among the files already gone.
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    AUTOMOC OFF
    AUTORCC OFF
    AUTOUIC OFF)
target_link_libraries(DishUninstallHelper PRIVATE
    advapi32 shell32 user32 dish_warnings dish_strict)

# --- DishPayloadPack: the host tool that welds stub + zip + trailer ----------
# Qt-free on purpose: PackMain writes manifest.json with its own emitter and
# hashes through BCrypt, so this tool runs from any shell without Qt's DLLs
# beside it or on PATH. Spec 6.2 also lists Manifest.cpp + Qt6::Core here; they
# are dropped because nothing in the tool references them (Manifest.cpp would
# additionally drag in VersionCompare.cpp and Qt6Core.dll for dead code). The
# Qt-side Manifest.cpp still parses what this writes — from the engine.
add_executable(DishPayloadPack
    ${CMAKE_SOURCE_DIR}/src/installer/tools/PackMain.cpp
    ${CMAKE_SOURCE_DIR}/src/installer/PayloadFormat.h
    ${CMAKE_SOURCE_DIR}/src/installer/PayloadFormat.cpp
    ${CMAKE_SOURCE_DIR}/third_party/miniz/miniz.c)
set_target_properties(DishPayloadPack PROPERTIES
    OUTPUT_NAME dish-payload-pack
    RUNTIME_OUTPUT_DIRECTORY "${DISH_SETUP_BIN_DIR}"
    AUTOMOC OFF
    AUTORCC OFF
    AUTOUIC OFF)
target_include_directories(DishPayloadPack PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(DishPayloadPack PRIVATE dish_warnings)

# --- Developer convenience: a runnable dish-setup-ui.exe in the build tree ---
# The SHIPPED runtime comes from the payload image, never from here. This only
# makes `build/dish-setup-ui.exe --help` and a hand-run wizard work. Because
# the binary sits next to dish.exe, the Dish target's own deployment already
# covers it in a normal build; the pass is kept so a configure that builds only
# DishSetupUi still produces something runnable.
#
# The dependency on Dish is what keeps the two POST_BUILD deployments off each
# other's toes: they write the same Qt DLLs into the same directory, and under
# parallel Ninja one would eventually copy over a file the other has open.
# Ordering them costs nothing that a full build was not already paying.
add_dependencies(DishSetupUi Dish)

get_target_property(_dish_setup_qmake Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_dish_setup_qt_bin "${_dish_setup_qmake}" DIRECTORY)

if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_dish_setup_qt_bin}")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET DishSetupUi POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --no-compiler-runtime
                    --qmldir "${CMAKE_SOURCE_DIR}/src/qml"
                    "$<IF:$<CONFIG:Debug>,--debug,--release>"
                    "$<TARGET_FILE:DishSetupUi>"
            COMMENT "Deploying Qt runtime next to $<TARGET_FILE_NAME:DishSetupUi>"
            VERBATIM)
    endif()
endif()

# --- Packaging custom targets (deliberately NOT in ALL) ---------------------
# Staging the image runs windeployqt over ~100 MB and the pack step hashes and
# deflates all of it; both are release chores, not build chores.
add_custom_target(dish_setup_image
    COMMAND ${CMAKE_COMMAND}
        -DIMAGE_DIR=${CMAKE_BINARY_DIR}/setup-image
        -DDISH_EXE=$<TARGET_FILE:Dish>
        -DUI_EXE=$<TARGET_FILE:DishSetupUi>
        -DHELPER_EXE=$<TARGET_FILE:DishUninstallHelper>
        -DQT_BIN_DIR=${_dish_setup_qt_bin}
        -DSRC_DIR=${CMAKE_SOURCE_DIR}
        -DDEPLOY_CONFIG=$<CONFIG>
        -P ${CMAKE_SOURCE_DIR}/cmake/DishSetupImage.cmake
    DEPENDS Dish DishSetupUi DishUninstallHelper
    COMMENT "Staging the install image into ${CMAKE_BINARY_DIR}/setup-image"
    VERBATIM)

add_custom_target(dish_setup_exe
    COMMAND $<TARGET_FILE:DishPayloadPack>
        --stub $<TARGET_FILE:DishSetupStub>
        --image ${CMAKE_BINARY_DIR}/setup-image
        --out ${CMAKE_BINARY_DIR}/dish-setup.exe
        --version ${PROJECT_VERSION}
    DEPENDS dish_setup_image DishSetupStub DishPayloadPack
    BYPRODUCTS ${CMAKE_BINARY_DIR}/dish-setup.exe
    COMMENT "Packing ${CMAKE_BINARY_DIR}/dish-setup.exe"
    VERBATIM)
