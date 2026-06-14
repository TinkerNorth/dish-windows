cmake_minimum_required(VERSION 3.21)
project(Dish
    VERSION 0.1.0
    DESCRIPTION "Dish Windows client for the Satellite gamepad-over-LAN protocol"
    LANGUAGES CXX)

# --------------------------------------------------------------------------
# Global build settings
# --------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DISH_BUILD_TESTS     "Build the Dish test suite"          ON)
option(DISH_ENABLE_WARNINGS "Treat compiler warnings as errors"  ON)
option(DISH_ENABLE_LTO      "Enable LTO/IPO for Release builds"  ON)

if(DISH_ENABLE_LTO AND CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _ipo_supported OUTPUT _ipo_error)
    if(_ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    endif()
endif()

# --------------------------------------------------------------------------
# Common warning flags. Two targets:
#   * dish_warnings  — the lint set, applied everywhere (including tests).
#   * dish_strict    — adds /WX (MSVC) or -Werror (clang-cl), applied only
#                      to first-party code so Catch2's headers don't fail
#                      the build under "warnings as errors".
# --------------------------------------------------------------------------
add_library(dish_warnings INTERFACE)
add_library(dish_strict   INTERFACE)

if(MSVC)
    # /W4 is the MSVC equivalent of -Wall -Wextra. /permissive- enforces
    # C++ standard conformance (Microsoft's pre-C++17 default is lenient).
    # /Zc:__cplusplus reports the real __cplusplus value (MSVC silently
    # lies and reports 199711L by default). UNICODE / _UNICODE so the
    # Win32 API resolves to the wide-char variants by default.
    target_compile_options(dish_warnings INTERFACE
        /W4 /permissive- /Zc:__cplusplus /Zc:preprocessor
        /utf-8)
    target_compile_definitions(dish_warnings INTERFACE
        UNICODE _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        # Silence MSVC's "use the secure version" nags for the POSIX-style
        # functions Qt and libsodium both use internally.
        _CRT_SECURE_NO_WARNINGS
        _WINSOCK_DEPRECATED_NO_WARNINGS)
    if(DISH_ENABLE_WARNINGS)
        target_compile_options(dish_strict INTERFACE /WX)
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    # clang-cl / MinGW path. Mirrors the Linux flag set.
    target_compile_options(dish_warnings INTERFACE
        -Wall -Wextra -Wpedantic
        -Wshadow -Wnon-virtual-dtor -Wold-style-cast
        -Wcast-align -Wunused -Woverloaded-virtual
        -Wconversion -Wsign-conversion -Wnull-dereference
        -Wdouble-promotion -Wformat=2)
    if(DISH_ENABLE_WARNINGS)
        target_compile_options(dish_strict INTERFACE -Werror)
    endif()
endif()

# --------------------------------------------------------------------------
# Dependencies
# --------------------------------------------------------------------------
find_package(Threads REQUIRED)
# Qt6::Network carries the TLS/SSL API (QSslConfiguration, QSslSocket) the
# HTTP and pairing clients use to reach the satellite's HTTPS server. No extra
# package is needed: on Windows Qt 6.2+ ships the Schannel TLS backend by
# default (no OpenSSL DLLs to deploy). A build configured against a Qt with no
# TLS backend would fail at runtime with "TLS initialization failed"; the
# standard Qt for Windows binaries are fine.
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Network Svg)
# LinguistTools is OPTIONAL — the Qt Linguist .ts → .qm pipeline is only
# active when qttools is installed. When it's not present we still build a
# working app: every `tr("...")` falls back to the English source string.
# The translation catalogues under `translations/` are checked in and don't
# need to be rebuilt every time. Mirrors the dish-linux pattern.
find_package(Qt6 6.2 QUIET OPTIONAL_COMPONENTS LinguistTools)

# libsodium and SDL2 come from vcpkg on Windows (see vcpkg.json). The
# `unofficial-` prefix on sodium is vcpkg's "no upstream CMake config"
# convention — the dish-linux pkg-config path doesn't apply here.
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(SDL2 CONFIG REQUIRED)

# --------------------------------------------------------------------------
# Core library: everything except main.cpp & UI is reusable for tests
# --------------------------------------------------------------------------
set(DISH_CORE_SOURCES
    # Architecture kernel — pure, header-only, Qt-free (the StateFlow/Flow.combine
    # analogues every source/composer/controller is built on). See
    # src/architecture/README.md.
    src/architecture/Observable.h
    src/architecture/Combiner.h
    src/architecture/Composer.h
    src/architecture/StateSource.h
    src/architecture/Controller.h
    src/architecture/Repository.h
    # core/ — pure, Qt-free wire/crypto/model/reducer logic (host-testable)
    src/core/wire/SessionCrypto.h
    src/core/wire/SessionCrypto.cpp
    src/core/model/Protocol.h
    src/core/reducer/RestOutcome.h
    src/core/reducer/Backoff.h
    src/core/reducer/Reconcile.h
    src/core/reducer/CloseNotify.h
    src/Util/AtomicCounter.h
    src/Util/Hex.h
    src/Util/Hex.cpp
    src/Util/Endian.h
    src/Util/HostBattery.h
    src/Util/HostBattery.cpp
    src/Util/DisplaySleepInhibitor.h
    src/Util/DisplaySleepInhibitor.cpp
    src/Util/ScreenWakeController.h
    src/Util/ScreenWakeController.cpp
    src/Models/Models.h
    src/Models/Models.cpp
    src/Network/WinsockInit.h
    src/Network/WinsockInit.cpp
    src/Network/SatelliteClient.h
    src/Network/SatelliteClient.cpp
    src/Network/LANDiscovery.h
    src/Network/LANDiscovery.cpp
    src/Network/MdnsDiscovery.h
    src/Network/MdnsDiscovery.cpp
    src/Network/PairingClient.h
    src/Network/PairingClient.cpp
    src/Network/HTTPClient.h
    src/Network/HTTPClient.cpp
    src/Network/WifiConnection.h
    src/Network/WifiConnection.cpp
    src/Network/WifiConnectionManager.h
    src/Network/WifiConnectionManager.cpp
    src/Network/ConnectionHub.h
    src/Network/ConnectionHub.cpp
    src/Network/ConnectionStore.h
    src/Network/ConnectionStore.cpp
    src/Input/GamepadInputProcessor.h
    src/Input/GamepadInputProcessor.cpp
    src/Input/SdlMotionConvert.h
    src/Input/SdlMotionConvert.cpp
    src/Input/OutputCommandQueue.h
    src/Input/SDLGamepadBridge.h
    src/Input/SDLGamepadBridge.cpp
    src/FeatureSettings.h
    src/FeatureSettings.cpp
    src/LightbarRouting.h
    src/AppModel.h
    src/AppModel.cpp)

add_library(dish_core STATIC ${DISH_CORE_SOURCES})
target_include_directories(dish_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(dish_core
    PUBLIC
        Qt6::Core Qt6::Network
        unofficial-sodium::sodium
        $<TARGET_NAME_IF_EXISTS:SDL2::SDL2> $<TARGET_NAME_IF_EXISTS:SDL2::SDL2-static>
        Threads::Threads
        # Winsock + Win32 power management
        ws2_32
    PRIVATE
        dish_warnings
        dish_strict)

# --------------------------------------------------------------------------
# UI executable
# --------------------------------------------------------------------------
set(DISH_UI_SOURCES
    src/UI/Theme.h
    src/UI/Theme.cpp
    src/UI/DishLoaders.h
    src/UI/DishLoaders.cpp
    src/UI/SlotCard.h
    src/UI/SlotCard.cpp
    src/UI/BrandIcon.h
    src/UI/BrandIcon.cpp
    src/UI/PairingDialog.h
    src/UI/PairingDialog.cpp
    src/UI/ConnectionsDialog.h
    src/UI/ConnectionsDialog.cpp
    src/UI/SettingsView.h
    src/UI/SettingsView.cpp
    src/UI/SettingsDialog.h
    src/UI/SettingsDialog.cpp
    src/UI/NotificationQueue.h
    src/UI/NotificationQueue.cpp
    src/UI/NotificationToastHost.h
    src/UI/NotificationToastHost.cpp
    src/UI/MainWindow.h
    src/UI/MainWindow.cpp
    src/main.cpp
    # Windows resource file embeds the application icon + version metadata
    # so the .exe shows a real Dish icon in Explorer / taskbar.
    packaging/dish.rc
    # Qt resource bundle — embeds dish.ico for QApplication::setWindowIcon
    # in main.cpp. AUTORCC compiles this at build time.
    packaging/dish.qrc)

add_executable(Dish WIN32 ${DISH_UI_SOURCES})
set_target_properties(Dish PROPERTIES OUTPUT_NAME dish)
target_link_libraries(Dish PRIVATE dish_core Qt6::Widgets Qt6::Gui Qt6::Svg dish_warnings dish_strict)

# --------------------------------------------------------------------------
# i18n: compile the bundled .ts files into .qm at build time and embed them
# in the executable as Qt resources under :/i18n/. main.cpp picks the right
# .qm at startup off QLocale::system() and falls back to English.
#
# qt_add_translations creates an internal `Dish_translations` target the
# `Dish` executable depends on; the .qm files end up in
# ${CMAKE_CURRENT_BINARY_DIR}/.qm. The RESOURCE_PREFIX puts them at
# `:/i18n/dish_<locale>.qm` inside the binary.
# --------------------------------------------------------------------------
if(Qt6LinguistTools_FOUND)
    qt_add_translations(Dish
        TS_FILES
            translations/dish_bs.ts
            translations/dish_de.ts
            translations/dish_es.ts
            translations/dish_fr.ts
            translations/dish_pt_BR.ts
        RESOURCE_PREFIX "/i18n")
else()
    message(STATUS "Qt6LinguistTools not found — building without translation catalogues.")
endif()

# WIN32 above creates a windowed (non-console) subsystem binary. Qt provides
# a small Win32 entry-point shim (`qtmain` / WinMain via Qt6::EntryPoint)
# that forwards into the standard `int main(int, char**)` we use everywhere
# else. AUTOLINK on Qt6 wires it in automatically since Qt 6.0.

# Qt isn't vcpkg-managed, so Qt6Core/Gui/Widgets/Network.dll + the
# `platforms\qwindows.dll` plugin live at <qt>/bin and aren't next to
# dish.exe. windeployqt copies everything Qt needs into the output
# directory so dish.exe runs from a fresh shell / Explorer double-click.
if(WIN32)
    get_target_property(_qt_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_qmake}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET Dish POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --no-compiler-runtime
                    "$<IF:$<CONFIG:Debug>,--debug,--release>"
                    "$<TARGET_FILE:Dish>"
            COMMENT "Deploying Qt runtime next to $<TARGET_FILE_NAME:Dish>"
            VERBATIM)
    else()
        message(WARNING "windeployqt not found under ${_qt_bin_dir}; dish.exe will need Qt's bin/ on PATH at runtime.")
    endif()
endif()

install(TARGETS Dish RUNTIME DESTINATION bin)
# Headless service-like deployments don't need the resource file, but the
# .exe already embeds it via the AUTORCC pipeline above.

# --------------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------------
if(DISH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
