# --------------------------------------------------------------------------
# Catch2 v3 (FetchContent fallback so contributors don't need a system pkg)
# --------------------------------------------------------------------------
find_package(Catch2 3 QUIET)
if(NOT Catch2_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.4)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()

include(Catch)

set(DISH_TEST_SOURCES
    # Route debug-CRT assertions to stderr+abort so a bad assert fails the test
    # instead of popping a modal dialog that hangs ctest / headless runs.
    CrtAssertToStderr.cpp
    test_kernel.cpp
    test_session_crypto.cpp
    test_atomic_counter.cpp
    test_hex.cpp
    test_endian.cpp
    test_models.cpp
    test_beacon_parser.cpp
    test_mdns_discovery.cpp
    # Workstream 2a — discovery mapping/merge, TOFU, identity, trust repos
    test_stable_key.cpp
    test_tofu.cpp
    test_ip_literals.cpp
    test_mdns_mapping.cpp
    test_discovery_gateway.cpp
    test_satellite_tls_verifier.cpp
    test_pin_repository.cpp
    test_shared_key_repository.cpp
    test_remembered_satellite_repository.cpp
    test_connection_store_identity.cpp
    test_gamepad_input_processor.cpp
    test_sdl_motion_convert.cpp
    # Workstream 2d — pure XUSB/HID layout math, deadzone pure layer, per-device
    # deadzone + per-slot motion repos, motion enable/backend stores, the motion
    # capability composer, and the physical-motion convert/filter/probe.
    test_gamepad_button_layouts.cpp
    test_deadzones.cpp
    test_deadzone_repository.cpp
    test_motion_preference_repository.cpp
    test_motion_enabled_store.cpp
    test_motion_backend_status_store.cpp
    test_motion_capability_composer.cpp
    test_physical_motion_source.cpp
    test_pairing_client_classify.cpp
    test_rest_control_plane.cpp
    test_udp_opcodes.cpp
    test_session_reconcile.cpp
    # Workstream 2b — connections composer/coordinator, session mappers, pacer
    test_satellite_link_state.cpp
    test_connection_rows.cpp
    test_resend_pacer.cpp
    test_late_slot_converge.cpp
    test_connections_composer.cpp
    test_connection_coordinator.cpp
    test_session_lifecycle.cpp
    test_session_manager.cpp
    # Workstream 2c — catalog cache (ETag/304/stale), controller-type store,
    # picker-visibility mapper, catalog forward-compat.
    test_catalog_repository.cpp
    test_catalog_dto.cpp
    test_controller_type_store.cpp
    test_picker_visibility.cpp
    test_screen_wake_controller.cpp
    # Workstream 2f — wake split (composer + controller), the DishNotifications
    # event-channel source, the per-stream input-rate tracker + store, and the
    # motion-indicator precedence ladder + meter-visibility conjunctions.
    test_wake_state_composer.cpp
    test_wake_state_controller.cpp
    test_dish_notifications.cpp
    test_input_rate_tracker.cpp
    test_input_rate_store.cpp
    test_motion_indicator_state.cpp
    test_winsock_init.cpp
    test_set_thread_execution_state_inhibitor.cpp
    test_satellite_client_rumble.cpp
    test_satellite_client_motion.cpp
    test_satellite_client_touchpad.cpp
    test_satellite_client_session.cpp
    test_satellite_client_lightbar.cpp
    test_output_command_queue.cpp
    test_lightbar_routing.cpp
    test_feature_settings.cpp
    test_host_battery.cpp
    # Workstream 2e — return-path routing reducers (pure): rumble resolve +
    # 16->8 magnitude/safe-duration helpers, battery lowest-pick + validator +
    # physical-capacity mapping (host fallback preserved), touchpad forward
    # routing carrying eventTimeMs.
    test_rumble_routing.cpp
    test_rumble_helpers.cpp
    test_battery_routing.cpp
    test_battery_validator.cpp
    test_physical_battery_mapping.cpp
    test_touchpad_routing.cpp
    # Workstream 2g — USB-direct input path. PURE reducers (path FSM totality +
    # exact effect lists, poll-rate spec math, path choice/resolution) ported
    # 1:1; ADAPT the URB-count delta sampler, the per-VID:PID path store (+the
    # RepositoryContract), and the claim driver's open/claim/attach outcome
    # classification + auto-Direct gating against a fake gateway.
    test_usb_path_machine.cpp
    test_usb_path_machine_edge_cases.cpp
    test_usb_poll_rate.cpp
    test_poll_rate_sampler.cpp
    test_path_choice.cpp
    test_usb_path_resolution.cpp
    test_usb_path_preference_store.cpp
    test_usb_gamepad_manager.cpp
    # Workstream 3a/3b/3c/3d/3e — onboarding/theme/crash preference stores +
    # controllers, the donate-pill dismiss-window pure fn, and the license-manifest
    # parse + mapping + forward-compat. Windows-authored (most surfaces had no
    # android @Test); they pin the android class RULES, asserted via probes/fakes.
    test_onboarding_store.cpp
    test_donate_pill.cpp
    test_license_manifest.cpp
    test_theme_store.cpp
    test_crash_reporting.cpp
    # Cross-cutting test-parity audit (PROMPT_05) — the one genuinely-fillable pure
    # gap: MainUiState's battery-display projection (fromWire + isLow). Pins
    # android's canonical rule via a tests-local pure fn (the live impl is trapped
    # in + diverges from SlotCard.cpp; see tests/PARITY.md "production code that
    # resisted testing").
    test_main_ui_state.cpp)

add_executable(DishTests ${DISH_TEST_SOURCES})
target_link_libraries(DishTests
    PRIVATE
        dish_core
        Catch2::Catch2WithMain
        dish_warnings)

# The shipped license manifest is a Qt resource only in the Dish exe (not in
# dish_core, which DishTests links), so the manifest smoke test reads the source
# file directly. Hand it the absolute path at compile time.
target_compile_definitions(DishTests PRIVATE
    DISH_LICENSES_JSON_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../assets/licenses/licenses.json")

# Tests don't need -Werror on the third-party header path; keep our own at -Werror.
#
# On Windows, Catch2's test-discovery step launches DishTests.exe to
# enumerate cases for ctest. The exe transitively depends on Qt6Core.dll
# and Qt6Network.dll via dish_core; vcpkg's applocal.ps1 already drops
# libsodium.dll and SDL2.dll next to the exe at link time, but Qt isn't
# vcpkg-managed — its DLLs live at <qt-prefix>/bin and are nowhere near
# the runtime search path that catch_discover_tests inherits. Without
# DL_PATHS the discovery step exits with STATUS_DLL_NOT_FOUND (0xc0000135).
# Catch2 prepends DL_PATHS entries to PATH for both discovery and the
# test runs themselves, so ctest works regardless of whether the user
# launched it from cmd, PowerShell, or an IDE.
if(WIN32)
    catch_discover_tests(DishTests
        DL_PATHS
            $<TARGET_FILE_DIR:Qt6::Core>
            $<TARGET_FILE_DIR:Qt6::Network>
            # dish_core now links Qt6::Gui/Widgets (the theme layer moved in for
            # Workstream 3d), so the test exe needs their DLLs on PATH too.
            $<TARGET_FILE_DIR:Qt6::Gui>
            $<TARGET_FILE_DIR:Qt6::Widgets>)
else()
    catch_discover_tests(DishTests)
endif()
