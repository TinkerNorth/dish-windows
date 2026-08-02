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
    # core/AsyncState.h — the canonical Idle/Loading/Success/Error container every
    # async flow (catalog/discovery/pairing) binds through. Exhaustive pure
    # transitions + stale-while-revalidate retention + == (distinct-until-changed).
    test_async_state.cpp
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
    # Remembered-satellite reconnect/relearn (the "must rescan to reconnect" bug):
    # the net::ConnectionStore adapter seam the session manager calls on every
    # discovery completion to persist a moved satellite's IP, so autoReconnectAll
    # / scheduleRetry dial the current address and the paired key survives.
    test_remembered_reconnect.cpp
    test_gamepad_input_processor.cpp
    test_sdl_motion_convert.cpp
    # RAW-JOYSTICK fallback (feat/qml-fluent-migration) — the PURE default-layout
    # mapper for generic pads SDL classifies as a joystick but NOT a game
    # controller. Pins stick/trigger routing, face/shoulder/system buttons, all
    # 8 hat directions, and out-of-range axis/button safety. SDL-free, like the
    # motion-convert tests next to it.
    test_joystick_mapping.cpp
    # Pure capture→assignment layer for the "Configure controls" page: the
    # withAssignment reducer (one capture → a new JoystickRemap), the withInvert
    # toggle, and the capture-threshold predicates the bridge gates its emit on.
    # SDL-free — the bridge is never constructed (it would init SDL and hang).
    test_joystick_remap_assign.cpp
    # Pure input-capture LIFECYCLE FSM (core/input/CaptureMode.h) for "press a
    # button to assign it": every (phase x event) transition, the other-device
    # filter (the property AppViewModel's deviceId != capturingSlotId_ guard
    # enforces today, now a single-owner pure reducer), sub-threshold rejection,
    # Stop-from-any-phase, Start re-targeting. Reuses the bridge's capture
    # predicates; SDL/Qt-free (the bridge is never constructed).
    test_capture_mode.cpp
    # Per-VID:PID raw-joystick REMAP ("Configure controls" page): the durable
    # QSettings Repository (RepositoryContract) over a "joystick_remaps" key
    # disjoint from "usb_path_choices", wrapped in a reactive StateSource. Pure +
    # the QSettings seam — no SDL, no live manager.
    test_joystick_remap_repository.cpp
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
    # Reverse (host-initiated) Path-B pairing — the PURE poll-loop decision core
    # (nextReversePairingAction over every classifyApproval verdict x deadline
    # boundary) + the 4-digit clientPin shape helpers. Socket/clock-free: the live
    # manager poll methods open real Winsock the test process would join, so the
    # branching is tested here, not by driving the manager.
    test_reverse_pairing.cpp
    # Release-parity wave — tests for the five headers that landed without them:
    # the canonical BatteryUi projection + chip tokens (android MainUiState arm),
    # the binding-config blocker rule (all 13 android ConfigUiStateBlockerTest
    # cases), the pure ds4>mouse>off touchpad-mode resolve (android
    # TouchpadRouting.wireMode + the CapabilityResolver ds4-mode catalog gate),
    # and the per-satellite touchpad-mode repository (RepositoryContract) +
    # store (hydrate/persist/republish) pair.
    test_battery_ui.cpp
    test_config_blocker.cpp
    test_touchpad_mode_resolve.cpp
    test_touchpad_mode_repository.cpp
    test_touchpad_mode_store.cpp
    # Forward (Path A, user-types-a-PIN) pairing lifecycle — the PURE total FSM
    # (reducePairing over every PairPhase x PairEvent) giving forward pairing the
    # same first-class, typed, retained lifecycle reverse pairing already has.
    # Consumes the existing classifyPair verdict; Qt/socket-free.
    test_pairing_machine.cpp
    # Per-satellite SESSION lifecycle — the PURE total FSM (reduce over every
    # SessionPhase x SessionEvent) closing the audit gap the imperative
    # WifiConnectionManager left: Faltering is finally ENTERED at the 2-miss
    # threshold, Reconnecting + backoff is observable, and the failure reason is
    # retained + typed. Consumes classifyRest/classifyPair/closeActionForReason/
    # backoffDelayMs; clock-free + Qt/socket-free.
    test_satellite_session_machine.cpp
    test_udp_opcodes.cpp
    test_session_reconcile.cpp
    # Workstream 2b — connections composer/coordinator, session mappers, pacer
    test_satellite_link_state.cpp
    test_connection_rows.cpp
    test_resend_pacer.cpp
    test_late_slot_converge.cpp
    test_connections_composer.cpp
    test_connection_coordinator.cpp
    # FOUND-list one-spot rule (a box never renders as both FOUND and REMEMBERED)
    test_found_visibility.cpp
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
    # Heartbeat-RTT latency window (pure): ping-clock arming (in-flight guard +
    # loss reclaim), 64-sample slide + validity clamp, median/2 one-way estimate,
    # deterministic "~3.4 ms" formatting (android #138 parity).
    test_latency_window.cpp
    test_slot_live_stats.cpp
    test_motion_indicator_state.cpp
    test_winsock_init.cpp
    test_set_thread_execution_state_inhibitor.cpp
    test_satellite_client_rumble.cpp
    test_satellite_client_motion.cpp
    test_satellite_client_touchpad.cpp
    test_satellite_client_session.cpp
    test_satellite_client_lightbar.cpp
    test_satellite_client_send_counter.cpp
    test_wifi_connection_rekey.cpp
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
    # Workstream 2g USB-direct REAL: the per-model HID report decoders (DualShock4/
    # DualSense/Switch Pro/generic byte-layout vectors -> XUSB + IMU + touchpad)
    # and the twin-dedup arbitration (which SDL device is hidden by an active
    # USB-direct synthetic; claim->suppress, detach->restore).
    test_usb_report_parsers.cpp
    test_usb_twin_dedup.cpp
    # USB-vs-Bluetooth transport classification of a Windows HID interface path
    # (pure). Gates the raw-HID claim candidates (a BT pad never claims) and the
    # slot card's Bluetooth presentation.
    test_hid_transport.cpp
    # Workstream 3a/3b/3c/3d/3e — onboarding/theme/crash preference stores +
    # controllers, the donate-pill dismiss-window pure fn, and the license-manifest
    # parse + mapping + forward-compat. Windows-authored (most surfaces had no
    # android @Test); they pin the android class RULES, asserted via probes/fakes.
    test_onboarding_store.cpp
    test_license_manifest.cpp
    test_theme_store.cpp
    test_crash_reporting.cpp
    # Cross-cutting test-parity audit (PROMPT_05) — the one genuinely-fillable pure
    # gap: MainUiState's battery-display projection (fromWire + isLow). Pins
    # android's canonical rule via a tests-local pure fn (the live impl is trapped
    # in + diverges from SlotCard.cpp; see tests/PARITY.md "production code that
    # resisted testing").
    test_main_ui_state.cpp
    # QML migration (A0) — the PURE frameless-window hit-test region math
    # (caption/maximize/client/8 resize regions, half-open edge off-by-ones,
    # maximized suppression) + the Win11 build-number gate predicate. No Qt/Win32.
    test_window_hit_test.cpp
    # QML migration (A1) — the QML-facing role models. The SlotListModel adapter
    # (mirrors SlotCard's roles + the shared SlotLiveStats mapper) and the
    # ConnectionListModel adapter (mirrors composer::ConnectionRow + its render-key
    # tokens): roleNames coverage, row-count tracking, data() per role, and the
    # minimal dataChanged/rowsInserted/rowsRemoved on a state push.
    test_slot_list_model.cpp
    # QML migration (USB path surfacing) — the PURE slot-identity x controllers-map
    # -> path-fields mapper (routed reflects its controller; synthetic reports
    # Direct; an Xbox pad with no controller is unsupported) + the synthetic
    # slot-id (vpKey) parse the view model resolves (vid, pid) through. No live
    # UsbGamepadManager (it opens real USB/SDL and would hang).
    test_slot_path_fields.cpp
    test_connection_list_model.cpp
    # QML migration (A-ext) — the AppViewModel settings/about/deadzone/license
    # EXPOSURE mapping (qml/AppSettingsMaps): theme-mode int<->enum + store
    # round-trip, crash toggle forwarding, deadzone row folding (override + default
    # seed + per-device motion), license-row projection, onboarding gate +
    # markWelcomeCompleted. Driven via the real stores over isolated QSettings.
    test_app_view_model_maps.cpp
    # QML migration (C1) — the Connections C++ foundation AppViewModel exposes
    # reactively: the manager scan-start NOTIFY edge it folds (scanning), the
    # discovery-source label each FOUND row carries, and the de-raced stable-id
    # the id-based invokables (connectByServerId/pairByServerId/reconnect) resolve
    # on. The new coordinator commands (reconnect/disconnect) are exercised in
    # test_connection_coordinator.cpp via the coordinator Fixture.
    test_app_view_model_connections.cpp
    # QML migration (bind flow) — the bind-chooser availability exposure
    # (AppViewModel::availableConnectionsForSlot). The view model can't be
    # instantiated (owns a full AppModel), so this pins the reducer seam it folds:
    # the candidate filter (drop bound-elsewhere) + connectionsVisibleInPicker
    # holdover, asserting another slot's binding is excluded while an unbound-
    # available one and the slot's own offline holdover are included.
    test_app_view_model_bind_availability.cpp
    # v3 UI/UX redesign — the pure cores behind the new binding surfaces: the
    # four-layer capability solver (input x link x type x host -> verdict +
    # first failing layer, Pending whenever the type or host is unresolved),
    # the apply sequencer FSM (every phase x event, the skipped-connection
    # path, the 20 s Direct fallback that is a WARNING not a failure, and
    # cancel-only-during-connection), the font-stack probes behind
    # Tokens.sansFamily/monoFamily (a Consolas-bearing list never yields
    # Courier New), and the WCAG relative-luminance guard over BOTH palettes.
    test_capability_solver.cpp
    test_apply_binding_machine.cpp
    test_font_stacks.cpp
    test_theme_contrast.cpp)

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
            # dish_core links Qt6::Gui (theme palette + role models), so the
            # test exe needs its DLL on PATH too.
            $<TARGET_FILE_DIR:Qt6::Gui>)
else()
    catch_discover_tests(DishTests)
endif()
