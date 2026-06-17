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
# OFF by default: with DISH_QML OFF the build is byte-for-byte the current
# Widgets app — no Quick libs linked, no QML compiled. ON swaps the app entry
# point to the parallel Qt Quick chrome (the Widgets UI still builds & runs).
option(DISH_QML             "Build the parallel Qt Quick UI entry point" OFF)

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
# Floor raised to 6.7 (the installed Qt) so the optional Quick components below
# resolve against the same version. The Widgets components are unchanged.
find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Widgets Network Svg)
# Qt Quick stack — only required when the DISH_QML entry point is built. Kept out
# of the default REQUIRED set so a Quick-less Qt still configures the Widgets app.
if(DISH_QML)
    find_package(Qt6 6.7 REQUIRED COMPONENTS Quick Qml QuickControls2)
    qt_standard_project_setup(REQUIRES 6.7)
endif()
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
    src/core/model/IdentityKey.h
    src/core/net/Tofu.h
    src/core/net/Tofu.cpp
    src/core/net/IpLiterals.h
    src/core/net/IpLiterals.cpp
    src/core/reducer/RestOutcome.h
    src/core/reducer/Backoff.h
    src/core/reducer/Reconcile.h
    src/core/reducer/CloseNotify.h
    # core/reducer — Workstream 2g USB-direct PURE reducers (Qt-free): the total
    # path-lifecycle FSM (reduce: phase x event -> next + effects-as-data), the
    # interrupt-endpoint poll-rate math, the URB-count delta sampler core, the
    # per-model path choice enum, and the direct-claim-failure reason enum.
    src/core/reducer/PathChoice.h
    src/core/reducer/DirectClaimFailure.h
    src/core/reducer/UsbPollRate.h
    src/core/reducer/PollRateSampler.h
    src/core/reducer/UsbPathMachine.h
    src/core/reducer/UsbPathMachine.cpp
    # Workstream 2g twin-dedup arbitration (pure, Qt-free): which SDL/framework
    # device ids are hidden by an active USB-direct synthetic of the same model,
    # so a pad visible to both paths streams via exactly one. Port of android's
    # ui/main/SyntheticTwinDedup.kt.
    src/core/reducer/UsbTwinDedup.h
    # core/reducer mappers — Workstream 2b (pure, Qt-free; the connection->row
    # decision logic + the descriptor-aware late-slot converge + the resend pacer).
    src/core/reducer/SatelliteLinkState.h
    src/core/reducer/ConnectionRows.h
    src/core/reducer/LateSlotConverge.h
    src/core/reducer/ResendPacer.h
    # core/reducer mapper — Workstream 2c (pure picker-visibility + catalog
    # forward-compat: which connections/types the Emulate picker offers).
    src/core/reducer/PickerVisibility.h
    # core/reducer — Workstream 2e return-path routing (pure, Qt-free): rumble
    # resolve + 16->8 magnitude / safe-duration helpers, battery lowest-pick +
    # validator + physical-capacity mapping (host fallback preserved), and the
    # touchpad forward assembly that threads eventTimeMs end-to-end.
    src/core/reducer/RumbleRouting.h
    src/core/reducer/BatteryRouting.h
    src/core/reducer/TouchpadRouting.h
    # core/reducer — Workstream 2f telemetry (pure, Qt-free): the per-stream
    # input-rate estimator (event-count delta -> 5 Hz-quantized rate) and the
    # motion-indicator precedence ladder + meter-visibility conjunctions.
    src/core/reducer/InputRateTracker.h
    src/core/reducer/MotionIndicatorState.h
    # core/input — pure, Qt-free XUSB/HID layout math + deadzone/scale + the
    # host-testable physical-motion convert/filter (Workstream 2d). Header-only.
    src/core/input/GamepadButtonLayouts.h
    src/core/input/Deadzones.h
    src/core/input/PhysicalMotionSource.h
    # Workstream 2g per-model HID input-report decoders (pure, Qt-free,
    # allocation-free): DualShock 4 / DualSense / Switch Pro / generic-HID byte
    # layouts mirrored 1:1 from dish-android usb_parsers.cpp, producing an XUSB
    # report (+ IMU / touchpad). WinHidGateway::readLoop calls these.
    src/core/input/UsbReportParsers.h
    src/Util/AtomicCounter.h
    src/Util/Hex.h
    src/Util/Hex.cpp
    src/Util/Endian.h
    src/Util/HostBattery.h
    src/Util/HostBattery.cpp
    src/Util/DisplaySleepInhibitor.h
    src/Util/DisplaySleepInhibitor.cpp
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
    # source/connection — discovery Gateways (relocated from Network/) + the
    # DiscoveryGateway merge/source-tag fuse (Workstream 2a).
    src/source/connection/LANDiscovery.h
    src/source/connection/LANDiscovery.cpp
    src/source/connection/MdnsDiscovery.h
    src/source/connection/MdnsDiscovery.cpp
    src/source/connection/DiscoveryGateway.h
    src/source/connection/DiscoveryGateway.cpp
    # source/http — the TOFU TLS verifier seam HTTPClient calls (Workstream 2a).
    src/source/http/SatelliteTlsVerifier.h
    src/source/http/SatelliteTlsVerifier.cpp
    # source/http — the catalog ETag caching gateway over HTTPClient::getCatalog
    # (Workstream 2c). A caching gateway, NOT a durable Repository.
    src/source/http/SatelliteCatalogRepository.h
    src/source/http/SatelliteCatalogRepository.cpp
    # source/store — the per-slot Emulate controller-type override StateSource
    # the picker writes + the connection layer reads (Workstream 2c).
    src/source/store/ControllerTypeStore.h
    src/source/store/ControllerTypeStore.cpp
    # source/store — motion enable (in-mem + repo bridge) + the satellite motion
    # backend-status decode store (Workstream 2d).
    src/source/store/MotionEnabledStore.h
    src/source/store/MotionEnabledStore.cpp
    src/source/store/SatelliteMotionBackendStatusStore.h
    src/source/store/SatelliteMotionBackendStatusStore.cpp
    # source/store — Workstream 2g per-VID:PID USB path preference: a durable
    # QSettings Repository (RepositoryContract) wrapped in a reactive StateSource.
    src/source/store/UsbPathPreferenceStore.h
    src/source/store/UsbPathPreferenceStore.cpp
    # source/store — per-VID:PID raw-joystick button/axis remap ("Configure
    # controls" page): a durable QSettings Repository (RepositoryContract) over a
    # "joystick_remaps" key disjoint from "usb_path_choices", wrapped in a
    # reactive StateSource. Overrides JoystickMapping's default DirectInput layout.
    src/source/store/JoystickRemapStore.h
    src/source/store/JoystickRemapStore.cpp
    # source/store — Workstream 3a/3d/3e preference StateSources: onboarding
    # welcome-seen flags, theme mode (dark/light/system), crash-reporting opt-out
    # (default-ON, D4). Each a StateSource<S> over QSettings, host-testable.
    src/source/store/OnboardingPreferenceStore.h
    src/source/store/OnboardingPreferenceStore.cpp
    src/source/store/ThemePreferenceStore.h
    src/source/store/ThemePreferenceStore.cpp
    src/source/store/CrashReportingStore.h
    src/source/store/CrashReportingStore.cpp
    # source/usb — Workstream 2g Windows raw-HID claim Gateway + the imperative
    # driver (the only USB-IO place). The gateway interface is the test seam; the
    # WinHidGateway is the SetupAPI/hid.dll body (hardware-validated manually).
    # XInput hides Xbox pads from raw HID -> this serves HID pads (DualSense/DS4/
    # 8BitDo); Xbox pads fall back to SDL/XInput.
    src/source/usb/UsbDeviceGateway.h
    src/source/usb/WinHidGateway.h
    src/source/usb/WinHidGateway.cpp
    src/source/usb/UsbGamepadManager.h
    src/source/usb/UsbGamepadManager.cpp
    # source/notification — the in-app notification event-channel source (two
    # bounded DROP_OLDEST channels, monotonic ids, severity->duration defaults)
    # under the UI NotificationQueue/ToastHost; the renderer keeps dedup (2f).
    src/source/notification/EventChannel.h
    src/source/notification/DishNotifications.h
    src/source/notification/DishNotifications.cpp
    # source/inputrate — the per-slot live-rate StateSource (samples the hot-path
    # event counters through the pure InputRateTracker) + the SlotInputRates value
    # the live-stats display reads (2f).
    src/source/inputrate/SlotInputRates.h
    src/source/inputrate/InputRateStore.h
    src/source/inputrate/InputRateStore.cpp
    # repository — trust+identity stores on the kernel Repository + the
    # ConnectionStore facade/Coordinator over them (Workstream 2a).
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
    # repository — per-device deadzone profiles + per-slot motion toggle stores
    # on the kernel Repository (Workstream 2d).
    src/repository/DeadzoneRepository.h
    src/repository/DeadzoneRepository.cpp
    src/repository/MotionPreferenceRepository.h
    src/repository/MotionPreferenceRepository.cpp
    # composer — the derived connections view (Composer) + the imperative
    # Coordinator that re-exposes it (Workstream 2b).
    src/composer/ConnectionsComposer.h
    src/composer/ConnectionsComposer.cpp
    src/composer/ConnectionCoordinator.h
    src/composer/ConnectionCoordinator.cpp
    # composer — the catalog → pickable-type-list projection (Workstream 2c).
    src/composer/CatalogComposer.h
    src/composer/CatalogComposer.cpp
    # composer — per-slot motion-capability derivation (gyro ∧ enabled ∧ live,
    # type→sink map, toCapBits) (Workstream 2d).
    src/composer/MotionCapabilityComposer.h
    src/composer/MotionCapabilityComposer.cpp
    # composer — the wake subsystem split onto the kernel (Workstream 2f): the
    # pure streaming-slot-count derivation, the WakeStateComposer (count +
    # keep-screen-on -> WakeState), and the WakeStateController that effects
    # SetThreadExecutionState via the DisplaySleepInhibitor on each transition.
    src/composer/StreamingSlotCount.h
    src/composer/WakeStateComposer.h
    src/composer/WakeStateController.h
    src/composer/WakeStateController.cpp
    # composer — Workstream 3d/3e effect controllers: ThemeController (resolve
    # SYSTEM -> palette, re-theme the live app off the ThemePreferenceStore), and
    # CrashReportingController (forward opt-in flips to a no-op backend seam; its
    # stop() is a deliberate no-op so the opt-in survives teardown, D4). Both live
    # here (host-testable via the kernel ControllerProbe + injected seams). The
    # Theme files move into dish_core with them so the palette data + the
    # full-light/system palette are reachable from the controller + the tests.
    src/composer/ThemeController.h
    src/composer/ThemeController.cpp
    src/composer/CrashReportingBackend.h
    src/composer/CrashReportingBackend.cpp
    src/composer/CrashReportingController.h
    src/composer/CrashReportingController.cpp
    # UI/Theme — the design-token palette + the full light/system palette
    # (Workstream 3d OWNS this). In dish_core so ThemeController + the theme tests
    # can reach the palettes; the widget rendering (applyDishTheme / QSS helpers)
    # links Qt6::Gui/Widgets (added to dish_core below).
    src/UI/Theme.h
    src/UI/Theme.cpp
    # ui/licenses — the license-manifest DTO + parse + pure mapping rules
    # (Workstream 3c). Pure (no QWidget), host-testable; LicensesView renders it.
    src/ui/licenses/LicenseManifest.h
    src/ui/licenses/LicenseManifest.cpp
    # ui/donate — the PURE donate-pill dismiss-window decision + persistence
    # helpers (Workstream 3b), split out of the widget so they're host-testable.
    # Only the logic .cpp is in dish_core; the DonatePill.h widget (Q_OBJECT) is
    # moc'd in the Dish exe (DISH_UI_SOURCES) to avoid a duplicate meta-object.
    src/ui/donate/DonatePillLogic.cpp
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
    # qml/chrome — PURE, Qt-free frameless-window hit-test math + the Win11
    # version gate (WindowHitTest.h). Header-only and in dish_core so the unit
    # tests link it without any Quick/Win32 dependency. The side-effecting Win32
    # chrome (FramelessWindowChrome) lives in the QML target, not here.
    src/qml/chrome/WindowHitTest.h
    # qml — the QML-facing exposure layer (the AppViewModel adapter + the two
    # role models). Plain QObject/QAbstractListModel — they need only Qt Core/Gui,
    # NOT Qml/Quick — so they live here and the unit tests link them directly. The
    # QML registration (context property + qmlRegisterUncreatableType) is in the
    # DISH_QML-only QmlEntryPoint, not here.
    src/qml/SlotListModel.h
    src/qml/SlotListModel.cpp
    src/qml/ConnectionListModel.h
    src/qml/ConnectionListModel.cpp
    src/qml/AppViewModel.h
    src/qml/AppViewModel.cpp
    # qml — pure settings/about/deadzone/license mapping helpers the AppViewModel
    # re-projects (split out so the mapping is unit-testable without the heavy
    # AppModel). Plain QVariant + borrowed store pointers; Qt Core/Gui only.
    src/qml/AppSettingsMaps.h
    src/qml/AppSettingsMaps.cpp)

add_library(dish_core STATIC ${DISH_CORE_SOURCES})
target_include_directories(dish_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# The build version string the About surface reads (AppViewModel::appVersion).
# Sourced from the single CMake project VERSION so it never drifts from dish.rc.
target_compile_definitions(dish_core PUBLIC DISH_VERSION="${PROJECT_VERSION}")
target_link_libraries(dish_core
    PUBLIC
        Qt6::Core Qt6::Network
        # Qt6::Gui/Widgets: the theme layer (UI/Theme.cpp + composer/ThemeController)
        # moved into dish_core for Workstream 3d so the palette data + the
        # full-light/system re-apply are reachable from the controller and the
        # theme tests. applyDishTheme/QSS helpers/QStyleHints need Gui + Widgets.
        Qt6::Gui Qt6::Widgets
        unofficial-sodium::sodium
        $<TARGET_NAME_IF_EXISTS:SDL2::SDL2> $<TARGET_NAME_IF_EXISTS:SDL2::SDL2-static>
        Threads::Threads
        # Winsock + Win32 power management
        ws2_32
        # Workstream 2g raw-HID claim Gateway: SetupAPI device enumeration +
        # the HID class API (HidD_*/HidP_*). hid.lib pulls in hidsdi/hidpi.
        setupapi
        hid
    PRIVATE
        dish_warnings
        dish_strict)

# --------------------------------------------------------------------------
# UI executable
# --------------------------------------------------------------------------
set(DISH_UI_SOURCES
    src/UI/DishLoaders.h
    src/UI/DishLoaders.cpp
    # Crash diagnostic: SetUnhandledExceptionFilter -> minidump + symbolized
    # crash.log under %LOCALAPPDATA%\Dish, plus the debug-CRT assert redirect.
    # Links dbghelp (MiniDumpWriteDump / StackWalk64 / SymFromAddr) below.
    src/UI/CrashHandler.h
    src/UI/CrashHandler.cpp
    src/UI/SlotCard.h
    src/UI/SlotCard.cpp
    src/UI/SlotLiveStats.h
    src/UI/BrandIcon.h
    src/UI/BrandIcon.cpp
    src/UI/PairingDialog.h
    src/UI/PairingDialog.cpp
    src/UI/ConnectionsDialog.h
    src/UI/ConnectionsDialog.cpp
    src/UI/EmulatePicker.h
    src/UI/EmulatePicker.cpp
    src/UI/SettingsView.h
    src/UI/SettingsView.cpp
    src/UI/DeadzoneSettingsView.h
    src/UI/DeadzoneSettingsView.cpp
    src/UI/SettingsDialog.h
    src/UI/SettingsDialog.cpp
    src/UI/NotificationQueue.h
    src/UI/NotificationQueue.cpp
    src/UI/NotificationToastHost.h
    src/UI/NotificationToastHost.cpp
    src/UI/MainWindow.h
    src/UI/MainWindow.cpp
    # Workstream 3a/3b/3c/3e UI screens. Onboarding (welcome pager + adapted
    # setup wizard + help/FAQ), donate (screen + dismissible pill), licenses
    # (list view over the bundled manifest), the crash-reporting toggle row
    # (3e-owned factory inserted into 3d's Settings column), and the shared
    # external-link helper.
    src/ui/common/ExternalLink.h
    src/ui/common/ExternalLink.cpp
    src/ui/onboarding/WelcomeDialog.h
    src/ui/onboarding/WelcomeDialog.cpp
    src/ui/onboarding/SetupWizardView.h
    src/ui/onboarding/SetupWizardView.cpp
    src/ui/onboarding/HelpView.h
    src/ui/onboarding/HelpView.cpp
    src/ui/donate/DonateView.h
    src/ui/donate/DonateView.cpp
    src/ui/donate/DonatePill.h
    src/ui/donate/DonatePill.cpp
    src/ui/licenses/LicensesView.h
    src/ui/licenses/LicensesView.cpp
    src/ui/settings/CrashReportingRow.h
    src/ui/settings/CrashReportingRow.cpp
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
# dbghelp powers the crash diagnostic in UI/CrashHandler.cpp: MiniDumpWriteDump
# (the .dmp), plus StackWalk64 / SymFromAddr / SymGetLineFromAddr64 (the
# symbolized crash.log stack). shell32 (SHGetKnownFolderPath) + ole32
# (CoTaskMemFree) resolve %LOCALAPPDATA%; both are in Qt's default link set but
# named explicitly so the crash path never depends on transitive linkage.
target_link_libraries(Dish PRIVATE dbghelp shell32 ole32)

# --------------------------------------------------------------------------
# Qt Quick UI (DISH_QML) — the parallel modern-chrome entry point. Compiled
# into the Dish exe only when DISH_QML is ON; otherwise none of this runs and
# the exe is the unchanged Widgets app. main.cpp branches on the DISH_QML
# define below.
# --------------------------------------------------------------------------
if(DISH_QML)
    target_compile_definitions(Dish PRIVATE DISH_QML)

    # The side-effecting Quick/Win32 glue: the native frameless chrome (Mica +
    # snap hit-test via the pure WindowHitTest in dish_core), the QML-facing
    # bridges (chrome state + the C++ Theme tokens), and the entry point.
    target_sources(Dish PRIVATE
        src/qml/QmlEntryPoint.h
        src/qml/QmlEntryPoint.cpp
        src/qml/chrome/FramelessWindowChrome.h
        src/qml/chrome/FramelessWindowChrome.cpp
        src/qml/chrome/ChromeBridge.h
        src/qml/chrome/ChromeBridge.cpp
        src/qml/chrome/ThemeBridge.h
        src/qml/chrome/ThemeBridge.cpp)

    # The QML module. URI Dish.Chrome ties the .qml files and the QML_ELEMENT
    # bridges into one import. The singletons (ChromeBridge/ThemeBridge) are
    # registered via their QML_SINGLETON macros; Main/WindowTitleBar are the
    # visual tree loaded by QmlEntryPoint::runQmlApp.
    qt_add_qml_module(Dish
        URI Dish.Chrome
        VERSION 1.0
        QML_FILES
            src/qml/Main.qml
            src/qml/WindowTitleBar.qml
            src/qml/AppShell.qml
            # Shared component kit (A2) — styled from Theme tokens on Basic.
            src/qml/kit/SectionHeader.qml
            src/qml/kit/KitButton.qml
            src/qml/kit/OutlineButton.qml
            src/qml/kit/Card.qml
            src/qml/kit/StatusDot.qml
            src/qml/kit/BrandGlyph.qml
            src/qml/kit/LabeledSwitch.qml
            src/qml/kit/KitTextField.qml
            src/qml/kit/Page.qml
            src/qml/kit/ContentDialog.qml
            # Nav destinations (placeholders; page agents replace bodies).
            src/qml/pages/ControllersPage.qml
            src/qml/pages/ConnectionsPage.qml
            src/qml/pages/SettingsPage.qml
            # Settings detail sub-pages (pushed onto the content StackView by
            # SettingsPage.openDetail via Qt.resolvedUrl — they must be in the
            # module so the resolved URL maps to a registered component).
            src/qml/pages/DeadzoneSettingsPage.qml
            src/qml/pages/DonatePage.qml
            src/qml/pages/LicensesPage.qml
            # Raw-joystick remap detail (pushed from ControllersPage's
            # "Configure controls" action on a remappable slot card).
            src/qml/pages/ControlsRemapPage.qml
            # EmulatePicker is referenced by bare type name from ControllersPage,
            # so it must register into the same Dish.Chrome module.
            src/qml/pages/EmulatePicker.qml
            # First-run onboarding flow (pushed full-screen onto appRoot). The
            # flow loads its screens by bare type name, so all five register here.
            src/qml/onboarding/OnboardingFlow.qml
            src/qml/onboarding/WelcomeScreen.qml
            src/qml/onboarding/SetupWizardScreen.qml
            src/qml/onboarding/HelpScreen.qml
            src/qml/onboarding/OnboardingNavBar.qml)

    # The qmltyperegistrar emits a registration .cpp that #includes the bridge
    # headers by BASENAME (ChromeBridge.h, ThemeBridge.h), so their directory
    # must be on the include path for that generated TU to compile.
    target_include_directories(Dish PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/qml/chrome)

    # qmlcachegen emits per-QML C++ TUs that instantiate Qt's qmetatype/qjsengine
    # templates; under MSVC those headers raise C4702 (unreachable code) which
    # dish_strict's /WX turns fatal. The warning is Qt's, not ours, and only the
    # generated cache TUs hit it — relax just C4702 for this target. Gated inside
    # DISH_QML so the Widgets build (DISH_QML OFF) keeps full /WX, byte-for-byte.
    if(MSVC)
        target_compile_options(Dish PRIVATE /wd4702)
    endif()

    target_link_libraries(Dish PRIVATE
        Qt6::Quick Qt6::Qml Qt6::QuickControls2
        # dwmapi: DwmSetWindowAttribute (Mica/dark mode) +
        # DwmExtendFrameIntoClientArea (shadow/resize frame).
        dwmapi)
endif()

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
