cmake_minimum_required(VERSION 3.21)
project(Dish
    VERSION 1.1.0
    DESCRIPTION "Dish Windows client for the Satellite gamepad-over-LAN protocol"
    # C is enabled for the vendored ENet fork (Moonlight control stream), whose
    # backend is pure C; everything first-party is C++.
    LANGUAGES C CXX)

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
# OFF so a contributor without qttools still gets a working build; CI and the
# release workflow turn it ON, where shipping an English-only binary silently
# is not acceptable.
option(DISH_REQUIRE_TRANSLATIONS
       "Fail configuration if the Qt Linguist tools are missing"  OFF)

if(DISH_ENABLE_LTO AND CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _ipo_supported OUTPUT _ipo_error)
    if(_ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    endif()
endif()

# dish_warnings is applied everywhere including tests; dish_strict adds
# /WX (-Werror) and is applied only to first-party code, so Catch2's headers
# can't fail the build.
add_library(dish_warnings INTERFACE)
add_library(dish_strict   INTERFACE)

if(MSVC)
    # /Zc:__cplusplus because MSVC otherwise reports __cplusplus as 199711L.
    # UNICODE/_UNICODE so the Win32 API resolves to the wide-char variants.
    target_compile_options(dish_warnings INTERFACE
        /W4 /permissive- /Zc:__cplusplus /Zc:preprocessor
        /utf-8)
    target_compile_definitions(dish_warnings INTERFACE
        UNICODE _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        # Silences the "use the secure version" nags for POSIX-style calls
        # inside Qt and libsodium.
        _CRT_SECURE_NO_WARNINGS
        _WINSOCK_DEPRECATED_NO_WARNINGS)
    if(DISH_ENABLE_WARNINGS)
        target_compile_options(dish_strict INTERFACE /WX)
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
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

# --- Dependencies ---
find_package(Threads REQUIRED)
# No TLS package is needed: Qt 6.2+ on Windows ships the Schannel backend, so
# the HTTPS clients get QSsl* with no OpenSSL DLLs to deploy. A Qt built with
# no TLS backend fails at runtime with "TLS initialization failed".
find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Network Svg Quick Qml QuickControls2)
qt_standard_project_setup(REQUIRES 6.7)
# Optional: without qttools the .ts → .qm step is skipped and every tr() falls
# back to its English source string, which still builds a working app.
find_package(Qt6 6.2 QUIET OPTIONAL_COMPONENTS LinguistTools)

# vcpkg (see vcpkg.json). The `unofficial-` prefix is vcpkg's convention for a
# port with no upstream CMake config.
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(SDL2 CONFIG REQUIRED)
# OpenSSL backs the Moonlight (GameStream) path only: it needs AES-128-ECB,
# AES-128-GCM, RSA-2048 sign/verify and X.509 generation, none of which libsodium
# offers. The Satellite protocol-1 crypto (core/wire/SessionCrypto) stays on
# libsodium. See THIRD_PARTY.md.
find_package(OpenSSL REQUIRED)

# --- ENet (Moonlight control stream, MIT) ------------------------------------
# The Moonlight control channel runs over the cgutman ENet fork (IPv4+IPv6),
# vendored via FetchContent and pinned to a commit for reproducibility, the same
# way tests/ pins Catch2.
#
# We fetch the SOURCE only and compile it as our own `enet` static target rather
# than add_subdirectory'ing the fork's CMakeLists: that file declares
# cmake_minimum_required well below CMake's current floor, so its project() call
# fails to inherit this build's MSVC toolchain and the compiler check dies on
# kernel32.lib. Building the handful of .c files directly sidesteps all of that.
# See THIRD_PARTY.md.
include(FetchContent)
# SOURCE_SUBDIR points at a path with no CMakeLists.txt, so MakeAvailable
# POPULATES the source (reliably setting enet_SOURCE_DIR across re-configures)
# but never add_subdirectory's the fork's ancient CMakeLists.
FetchContent_Declare(
    enet
    GIT_REPOSITORY https://github.com/cgutman/enet.git
    GIT_TAG        44c85e16279553d9c052e572bcbfcd745fb74abf
    SOURCE_SUBDIR  _dish_no_such_subdir)
FetchContent_MakeAvailable(enet)

if(NOT enet_SOURCE_DIR OR NOT EXISTS "${enet_SOURCE_DIR}/callbacks.c")
    message(FATAL_ERROR "ENet source was not populated at '${enet_SOURCE_DIR}'")
endif()

# The platform backend: win32.c on Windows, unix.c elsewhere. The rest are
# portable.
set(_enet_sources
    ${enet_SOURCE_DIR}/callbacks.c
    ${enet_SOURCE_DIR}/compress.c
    ${enet_SOURCE_DIR}/host.c
    ${enet_SOURCE_DIR}/list.c
    ${enet_SOURCE_DIR}/packet.c
    ${enet_SOURCE_DIR}/peer.c
    ${enet_SOURCE_DIR}/protocol.c)
if(WIN32)
    list(APPEND _enet_sources ${enet_SOURCE_DIR}/win32.c)
else()
    list(APPEND _enet_sources ${enet_SOURCE_DIR}/unix.c)
endif()

add_library(enet STATIC ${_enet_sources})
# Pure-C target: keep Qt's autogen machinery off it.
set_target_properties(enet PROPERTIES AUTOMOC OFF AUTORCC OFF AUTOUIC OFF)
target_include_directories(enet SYSTEM PUBLIC ${enet_SOURCE_DIR}/include)
if(WIN32)
    target_link_libraries(enet PUBLIC ws2_32 winmm)
endif()

# --- Core library: everything except main.cpp and the UI, so tests can link it ---
set(DISH_CORE_SOURCES
    src/architecture/Observable.h
    src/architecture/Combiner.h
    src/architecture/Composer.h
    src/architecture/StateSource.h
    src/architecture/Controller.h
    src/architecture/Repository.h
    src/core/wire/SessionCrypto.h
    src/core/wire/SessionCrypto.cpp
    # Moonlight (GameStream) protocol core: control-stream codec, pairing
    # crypto, RTSP text and the session FSM. Pure logic; sockets live in
    # src/Network, persistence in src/repository.
    src/core/moonlight/MoonlightControl.h
    src/core/moonlight/MoonlightControl.cpp
    src/core/moonlight/MoonlightCrypto.h
    src/core/moonlight/MoonlightCrypto.cpp
    src/core/moonlight/MoonlightIdentity.h
    src/core/moonlight/MoonlightIdentity.cpp
    src/core/moonlight/MoonlightPairing.h
    src/core/moonlight/MoonlightPairing.cpp
    src/core/moonlight/MoonlightRtsp.h
    src/core/moonlight/MoonlightRtsp.cpp
    src/core/moonlight/MoonlightPadSlots.h
    src/core/moonlight/MoonlightSessionMachine.h
    src/core/model/Protocol.h
    src/core/model/IdentityKey.h
    src/core/net/Tofu.h
    src/core/net/Tofu.cpp
    src/core/net/IpLiterals.h
    src/core/net/IpLiterals.cpp
    # Auto-update: the release manifest grammar and the one version comparison
    # the updater is allowed to use.
    src/core/update/UpdateVersion.h
    src/core/update/UpdateVersion.cpp
    src/core/update/UpdateManifest.h
    src/core/update/UpdateManifest.cpp
    src/core/reducer/RestOutcome.h
    src/core/reducer/Backoff.h
    src/core/reducer/KeepAwake.h
    src/core/reducer/Reconcile.h
    src/core/reducer/CloseNotify.h
    src/core/reducer/PathChoice.h
    src/core/reducer/DirectClaimFailure.h
    src/core/reducer/UsbPollRate.h
    src/core/reducer/PollRateSampler.h
    src/core/reducer/UsbPathMachine.h
    src/core/reducer/UsbPathMachine.cpp
    src/core/reducer/UsbTwinDedup.h
    src/core/reducer/SatelliteLinkState.h
    src/core/reducer/ConnectionRows.h
    src/core/reducer/LateSlotConverge.h
    src/core/reducer/ResendPacer.h
    src/core/reducer/BindingPresence.h
    src/core/reducer/PickerVisibility.h
    src/core/reducer/RumbleRouting.h
    src/core/reducer/BatteryRouting.h
    src/core/reducer/TouchpadRouting.h
    src/core/reducer/InputRateTracker.h
    src/core/reducer/MotionIndicatorState.h
    src/core/reducer/LatencyWindow.h
    src/core/reducer/CapabilitySolver.h
    src/core/reducer/CapabilitySolver.cpp
    src/core/reducer/ApplyBindingMachine.h
    src/core/reducer/UpdateMachine.h
    src/core/reducer/UpdateMachine.cpp
    src/core/input/GamepadButtonLayouts.h
    src/core/input/Deadzones.h
    src/core/input/PhysicalMotionSource.h
    # Byte layouts mirrored 1:1 from dish-android's usb_parsers.cpp /
    # usb_hid_descriptor.cpp; keep the two in step when either side gains a
    # model.
    src/core/input/UsbReportParsers.h
    src/core/input/UsbHidLayout.h
    src/core/reducer/CatalogPrewarm.h
    src/Util/AtomicCounter.h
    src/Util/Hex.h
    src/Util/Hex.cpp
    src/Util/Endian.h
    src/Util/HostBattery.h
    src/Util/HostBattery.cpp
    # In dish_core so test_translations exercises the real locale-fallback path
    # rather than a copy of it.
    src/Util/Localization.h
    src/Util/Localization.cpp
    src/Models/Models.h
    src/Models/Models.cpp
    src/Network/WinsockInit.h
    src/Network/WinsockInit.cpp
    src/Network/SatelliteClient.h
    src/Network/SatelliteClient.cpp
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
    # Moonlight (GameStream) IO layer: the HTTP/HTTPS control API, the plaintext
    # RTSP handshake, the ENet control channel and the session coordinator.
    src/Network/MoonlightHost.h
    src/Network/MoonlightHost.cpp
    src/Network/MoonlightHttpClient.h
    src/Network/MoonlightHttpClient.cpp
    src/Network/MoonlightRtspClient.h
    src/Network/MoonlightRtspClient.cpp
    src/Network/MoonlightControlChannel.h
    src/Network/MoonlightControlChannel.cpp
    src/Network/MoonlightSession.h
    src/Network/MoonlightSession.cpp
    src/Network/MoonlightManager.h
    src/Network/MoonlightManager.cpp
    src/source/connection/NvstreamDiscovery.h
    src/source/connection/NvstreamDiscovery.cpp
    src/repository/MoonlightHostRepository.h
    src/repository/MoonlightHostRepository.cpp
    src/source/connection/LANDiscovery.h
    src/source/connection/LANDiscovery.cpp
    src/source/connection/MdnsDiscovery.h
    src/source/connection/MdnsDiscovery.cpp
    src/source/connection/DiscoveryGateway.h
    src/source/connection/DiscoveryGateway.cpp
    src/source/http/SatelliteTlsVerifier.h
    src/source/http/SatelliteTlsVerifier.cpp
    # An ETag cache over HTTPClient::getCatalog, not a durable Repository,
    # despite the name.
    src/source/http/SatelliteCatalogRepository.h
    src/source/http/SatelliteCatalogRepository.cpp
    src/source/store/ControllerTypeStore.h
    src/source/store/KeepAwakePreferenceStore.h
    src/source/store/ControllerTypeStore.cpp
    src/source/store/MotionEnabledStore.h
    src/source/store/MotionEnabledStore.cpp
    src/source/store/SatelliteMotionBackendStatusStore.h
    src/source/store/SatelliteMotionBackendStatusStore.cpp
    src/source/store/UsbPathPreferenceStore.h
    src/source/store/UsbPathPreferenceStore.cpp
    # Keyed under "joystick_remaps", deliberately disjoint from the
    # "usb_path_choices" key above.
    src/source/store/JoystickRemapStore.h
    src/source/store/JoystickRemapStore.cpp
    src/source/store/OnboardingPreferenceStore.h
    src/source/store/OnboardingPreferenceStore.cpp
    src/source/store/ThemePreferenceStore.h
    src/source/store/ThemePreferenceStore.cpp
    src/source/store/CrashReportingStore.h
    src/source/store/CrashReportingStore.cpp
    # Windows-shell-only prefs, so the default QSettings() hive
    # (HKCU\Software\TinkerNorth\Dish) rather than the cross-client one.
    src/source/store/UpdatePreferenceStore.h
    # XInput hides Xbox pads from raw HID, so this path serves DualSense/DS4/
    # 8BitDo and Xbox pads fall back to SDL/XInput.
    src/source/usb/UsbDeviceGateway.h
    src/source/usb/WinHidGateway.h
    src/source/usb/WinHidGateway.cpp
    src/source/usb/UsbGamepadManager.h
    src/source/usb/UsbGamepadManager.cpp
    src/source/notification/EventChannel.h
    src/source/notification/DishNotifications.h
    src/source/notification/DishNotifications.cpp
    src/source/input/ControllerActivitySource.h
    src/source/inputrate/SlotInputRates.h
    src/source/inputrate/InputRateStore.h
    src/source/inputrate/InputRateStore.cpp
    # Win32-only; returns {false,false} elsewhere, which renders the safe
    # "no adapter" copy rather than claiming Bluetooth is off.
    src/source/system/BluetoothRadioProbe.h
    src/source/system/BluetoothRadioProbe.cpp
    src/source/system/WakeInhibitor.h
    src/source/system/WakeInhibitor.cpp
    # Auto-update runtime: gateways, staging store, coordinator and the boot
    # handoff. In dish_core (not the exe) because main.cpp's boot gate runs
    # before the composition root and the tests link this library, not the exe.
    src/update/UpdatePorts.h
    src/update/HttpGateways.h
    src/update/HttpGateways.cpp
    src/update/FileStagingStore.h
    src/update/FileStagingStore.cpp
    src/update/UpdateCoordinator.h
    src/update/UpdateCoordinator.cpp
    src/update/UpdateHandoff.h
    src/update/UpdateHandoff.cpp
    src/repository/SettingsKeys.h
    src/repository/RememberedWifiEq.h
    src/repository/SatellitePinRepository.h
    src/repository/SatellitePinRepository.cpp
    src/repository/SatelliteSharedKeyRepository.h
    src/repository/SatelliteSharedKeyRepository.cpp
    src/repository/RememberedSatelliteRepository.h
    src/repository/RememberedSatelliteRepository.cpp
    src/repository/ConnectionStore.h
    src/repository/ConnectionStore.cpp
    src/repository/DeadzoneRepository.h
    src/repository/DeadzoneRepository.cpp
    src/repository/MotionPreferenceRepository.h
    src/repository/MotionPreferenceRepository.cpp
    src/composer/ConnectionsComposer.h
    src/composer/ConnectionsComposer.cpp
    src/composer/ConnectionCoordinator.h
    src/composer/ConnectionCoordinator.cpp
    src/composer/CatalogComposer.h
    src/composer/CatalogComposer.cpp
    src/composer/MotionCapabilityComposer.h
    src/composer/MotionCapabilityComposer.cpp
    src/composer/StreamingSlotCount.h
    src/composer/WakeStateComposer.h
    src/composer/WakeStateController.h
    src/composer/WakeStateController.cpp
    src/composer/ThemeController.h
    src/composer/ThemeController.cpp
    src/composer/CrashReportingBackend.h
    src/composer/CrashReportingBackend.cpp
    src/composer/CrashReportingController.h
    src/composer/CrashReportingController.cpp
    # Theme and FontStacks are in dish_core, not the exe, so ThemeController,
    # ThemeBridge/TokensBridge and the tests can all reach them; the tests
    # cannot link the exe target.
    src/UI/Theme.h
    src/UI/Theme.cpp
    src/UI/FontStacks.h
    src/UI/FontStacks.cpp
    src/UI/SlotLiveStats.h
    src/UI/licenses/LicenseManifest.h
    src/UI/licenses/LicenseManifest.cpp
    src/Input/GamepadInputProcessor.h
    src/Input/GamepadInputProcessor.cpp
    src/Input/SdlMotionConvert.h
    src/Input/SdlMotionConvert.cpp
    src/Input/JoystickMapping.h
    src/Input/JoystickMapping.cpp
    src/Input/OutputCommandQueue.h
    src/Input/SDLGamepadBridge.h
    src/Input/SDLGamepadBridge.cpp
    src/FeatureSettings.h
    src/FeatureSettings.cpp
    src/LightbarRouting.h
    src/AppModel.h
    src/AppModel.cpp
    # The pure hit-test math only. The side-effecting Win32 chrome
    # (FramelessWindowChrome) is in the exe target below.
    src/qml/chrome/WindowHitTest.h
    # QObject/QAbstractListModel only, no Qml/Quick, so the tests link them
    # directly. Registration into QML happens in the exe's QmlEntryPoint.
    src/qml/RenderTokens.h
    src/qml/SlotListModel.h
    src/qml/SlotListModel.cpp
    src/qml/ConnectionListModel.h
    src/qml/ConnectionListModel.cpp
    src/qml/AppViewModel.h
    src/qml/AppViewModel.cpp
    # Split out of AppViewModel so the mapping is testable without AppModel.
    src/qml/AppSettingsMaps.h
    src/qml/AppSettingsMaps.cpp)

add_library(dish_core STATIC ${DISH_CORE_SOURCES})
target_include_directories(dish_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# The cgutman ENet CMake adds its include dir only to its own target, so
# consumers name it explicitly. SYSTEM keeps /W4 /WX off its C headers.
target_include_directories(dish_core SYSTEM PUBLIC ${enet_SOURCE_DIR}/include)
# From the project VERSION so the About screen can't drift from dish.rc.
target_compile_definitions(dish_core PUBLIC DISH_VERSION="${PROJECT_VERSION}")
target_link_libraries(dish_core
    PUBLIC
        Qt6::Core Qt6::Network
        # For the theme palette (QColor + QStyleHints) and the role models.
        Qt6::Gui
        unofficial-sodium::sodium
        # Moonlight path only; see the find_package comment above. Just libcrypto:
        # the Moonlight crypto uses EVP / RSA / X.509 (all in libcrypto), and TLS
        # rides Qt's Schannel backend, so libssl is never linked or shipped.
        OpenSSL::Crypto
        # The vendored cgutman ENet fork (Moonlight control stream). Its CMake
        # exports no INTERFACE include dir, so it is added explicitly below.
        enet
        $<TARGET_NAME_IF_EXISTS:SDL2::SDL2> $<TARGET_NAME_IF_EXISTS:SDL2::SDL2-static>
        Threads::Threads
        ws2_32
        # ENet's Win32 backend uses timeGetTime.
        winmm
        # Raw-HID claim path: SetupAPI enumeration + the HID class API.
        setupapi
        hid
        # BluetoothRadioProbe. Its header also carries a #pragma comment(lib)
        # under MSVC; naming it here keeps the link set owned by one file.
        bthprops
    PRIVATE
        dish_warnings
        dish_strict)

# --- UI executable ---
set(DISH_UI_SOURCES
    src/UI/CrashHandler.h
    src/UI/CrashHandler.cpp
    src/UI/common/ExternalLink.h
    src/UI/common/ExternalLink.cpp
    src/main.cpp
    # Embeds the app icon + version metadata so Explorer/taskbar show them.
    packaging/dish.rc
    # dish.ico + the brand SVG glyphs, compiled by AUTORCC.
    packaging/dish.qrc)

add_executable(Dish WIN32 ${DISH_UI_SOURCES})
set_target_properties(Dish PROPERTIES OUTPUT_NAME dish)
target_link_libraries(Dish PRIVATE dish_core Qt6::Gui Qt6::Svg dish_warnings dish_strict)
# CrashHandler.cpp: dbghelp writes the minidump and symbolizes the stack;
# shell32 + ole32 resolve %LOCALAPPDATA%. The latter two are already in Qt's
# link set, but named here so the crash path never relies on transitive linkage.
target_link_libraries(Dish PRIVATE dbghelp shell32 ole32)

# --- Qt Quick UI: the frameless Win32 chrome and the Dish.Chrome QML module ---
target_sources(Dish PRIVATE
    src/qml/QmlEntryPoint.h
    src/qml/QmlEntryPoint.cpp
    src/qml/chrome/FramelessWindowChrome.h
    src/qml/chrome/FramelessWindowChrome.cpp
    src/qml/chrome/ChromeBridge.h
    src/qml/chrome/ChromeBridge.cpp
    src/qml/chrome/ThemeBridge.h
    src/qml/chrome/ThemeBridge.cpp
    src/qml/chrome/TokensBridge.h
    src/qml/chrome/TokensBridge.cpp)

# DISH_KIT_QML_FILES: the canonical design-kit file list, kept in its own
# file so the kit reads as a unit rather than as lines lost in this one.
include(src/qml/kit/KitFiles.cmake)

qt_add_qml_module(Dish
    URI Dish.Chrome
    VERSION 1.0
    # So qmllint/qmlcachegen load QtQuick's type database when analysing this
    # module; without it the singletons' QColor properties don't resolve.
    DEPENDENCIES QtQuick
    QML_FILES
        src/qml/Main.qml
        src/qml/WindowTitleBar.qml
        src/qml/AppShell.qml
        src/qml/UpdatePill.qml
        src/qml/UpdatePopover.qml
        ${DISH_KIT_QML_FILES}
        src/qml/shared/BindingDraft.qml
        src/qml/pages/HomePage.qml
        src/qml/pages/ControllersPage.qml
        src/qml/pages/ConnectionsPage.qml
        src/qml/pages/MoonlightHostsPage.qml
        src/qml/pages/SettingsPage.qml
        # Detail pages pushed via Qt.resolvedUrl, so they must be listed in the
        # module or the resolved URL maps to no registered component.
        src/qml/pages/DeadzoneSettingsPage.qml
        src/qml/pages/DonatePage.qml
        src/qml/pages/LicensesPage.qml
        src/qml/pages/ControlsRemapPage.qml
        src/qml/pages/ConfigureBindingPage.qml
        src/qml/pages/PairingDialog.qml
        src/qml/onboarding/OnboardingFlow.qml
        src/qml/onboarding/WelcomeScreen.qml
        src/qml/onboarding/HelpScreen.qml
        src/qml/wizard/SetupWizardPage.qml
        src/qml/wizard/WizardInputPage.qml
        src/qml/wizard/WizardDestinationPage.qml
        src/qml/wizard/WizardTypePage.qml
        src/qml/wizard/WizardFeelPage.qml
        src/qml/wizard/WizardReviewPage.qml)

# qmltyperegistrar's generated .cpp #includes the bridge headers by BASENAME,
# so their directory has to be on the include path for it to compile.
target_include_directories(Dish PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/qml/chrome)

# C4702 (unreachable code) fires inside Qt's own headers from the TUs
# qmlcachegen generates, and dish_strict's /WX makes it fatal.
if(MSVC)
    target_compile_options(Dish PRIVATE /wd4702)
endif()

target_link_libraries(Dish PRIVATE
    Qt6::Quick Qt6::Qml Qt6::QuickControls2
    # Mica/dark mode + the shadow and resize frame.
    dwmapi)

# --- Install image: the payload dish-setup.exe carries -----------------------
# The installer itself is Inno Setup (installer.iss at the repo root); CMake's
# whole contribution is this staging target. installer.iss ships
# ${CMAKE_BINARY_DIR}/setup-image verbatim, and cmake/DishSetupImage.cmake is
# the ONE place that decides what an installed Dish consists of (dish.exe, the
# windeployqt output, libsodium/SDL2, the five app-local CRT DLLs, the
# Dish.Chrome module entry, licenses/). Outside ALL because it re-runs
# windeployqt over ~100 MB, which is a release chore, not a build chore.
get_target_property(_dish_image_qmake Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_dish_image_qt_bin "${_dish_image_qmake}" DIRECTORY)
add_custom_target(dish_setup_image
    COMMAND ${CMAKE_COMMAND}
        -DIMAGE_DIR=${CMAKE_BINARY_DIR}/setup-image
        -DDISH_EXE=$<TARGET_FILE:Dish>
        -DQMLDIR_FILE=${CMAKE_BINARY_DIR}/Dish/Chrome/qmldir
        -DQT_BIN_DIR=${_dish_image_qt_bin}
        -DSRC_DIR=${CMAKE_SOURCE_DIR}
        -DDEPLOY_CONFIG=$<CONFIG>
        -P ${CMAKE_SOURCE_DIR}/cmake/DishSetupImage.cmake
    DEPENDS Dish
    COMMENT "Staging the install image into ${CMAKE_BINARY_DIR}/setup-image"
    VERBATIM)

# --- i18n: .ts -> .qm at build time, embedded at :/i18n/ ---
# English is a catalogue like any other, not the untranslated fallback: a %n
# message has one source string but needs one form per plural category, so
# without dish_en.qm "1 slots free" ships. Non-plural English messages are left
# untranslated on purpose and lrelease drops them, so dish_en.qm ends up holding
# only the forms the source string cannot express.
#
# TARGETS names the binary that embeds the compiled .qm; SOURCE_TARGETS names
# everything lupdate scans. Passing SOURCE_TARGETS makes qt_add_translations
# run IMMEDIATELY instead of deferring to the end of this scope, so it has to
# stay below the targets it names.
#
# dish_core is in SOURCE_TARGETS even though it embeds no catalogue of its own:
# five of its translation units call tr() (AppModel, FeatureSettings,
# WifiConnectionManager, AppViewModel, ThemePreferenceStore). Omitting it makes
# this target scan disagree with the DIRECTORY scan in
# scripts/check-translations.ps1 by 26 strings, so `update_translations` would
# delete exactly what the CI gate then demands back.
if(Qt6LinguistTools_FOUND)
    qt_add_translations(
        TARGETS Dish
        SOURCE_TARGETS Dish dish_core
        TS_FILES
            translations/dish_en.ts
            translations/dish_bs.ts
            translations/dish_de.ts
            translations/dish_es.ts
            translations/dish_fr.ts
            translations/dish_pt_BR.ts
        RESOURCE_PREFIX "/i18n"
        # Must stay identical to $lupdateArgs in scripts/check-translations.ps1,
        # or the staleness gate fails on formatting instead of content.
        # -locations none: the catalogues are tracked and the gate fails a stale
        # PR, so the files must change only when a STRING changes. With line
        # numbers in them, one inserted blank line rewrites hundreds of
        # <location> tags and the gate cries wolf.
        # -no-obsolete: drop messages whose source is gone rather than hoard.
        LUPDATE_OPTIONS -locations none -no-obsolete)
else()
    if(DISH_REQUIRE_TRANSLATIONS)
        message(FATAL_ERROR
            "Qt6LinguistTools not found and DISH_REQUIRE_TRANSLATIONS is ON. "
            "Install the Qt Linguist tools (qttools) — a build without them "
            "ships English-only with no other signal that anything is missing.")
    endif()
    message(WARNING "Qt6LinguistTools not found — building WITHOUT translation "
                    "catalogues. The app will be English-only, and %n plural "
                    "forms will fall back to their source spelling.")
endif()

# The WIN32 (windowed subsystem) target still defines a plain main(): Qt6
# autolinks its WinMain shim, so no explicit Qt6::EntryPoint link is needed.

# Qt isn't vcpkg-managed, so its DLLs and the platforms/qwindows.dll plugin
# live under <qt>/bin rather than next to dish.exe. windeployqt copies them
# into the output dir so the exe runs from a fresh shell or a double-click.
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
                    # Walks our sources so only the QML modules the app really
                    # imports get shipped.
                    --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/src/qml"
                    "$<IF:$<CONFIG:Debug>,--debug,--release>"
                    "$<TARGET_FILE:Dish>"
            COMMENT "Deploying Qt runtime next to $<TARGET_FILE_NAME:Dish>"
            VERBATIM)
    else()
        message(WARNING "windeployqt not found under ${_qt_bin_dir}; dish.exe will need Qt's bin/ on PATH at runtime.")
    endif()
endif()

install(TARGETS Dish RUNTIME DESTINATION bin)

if(DISH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)

    # test_translations.cpp asserts against the COMPILED .qm, so it must build
    # after them or it silently checks whatever an earlier build left on disk.
    # This cannot move into tests/CMakeLists.txt: qt_add_translations creates
    # `release_translations` only when THIS scope is finalized, which is after
    # add_subdirectory(tests) has run, so an if(TARGET) guard over there is
    # always false and the dependency vanishes. The DEFER is what makes it work.
    if(Qt6LinguistTools_FOUND)
        cmake_language(DEFER CALL add_dependencies DishTests release_translations)
    endif()
endif()
