# FetchContent fallback so contributors don't need a system Catch2 package.
# The tag is pinned: an unpinned FetchContent makes the test suite's result
# depend on whatever upstream tagged today.
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
    # Routes debug-CRT assertions to stderr+abort. Without it a failed assert
    # pops a modal dialog and hangs ctest on a headless runner.
    CrtAssertToStderr.cpp
    test_kernel.cpp
    test_async_state.cpp
    test_session_crypto.cpp
    test_atomic_counter.cpp
    test_hex.cpp
    test_endian.cpp
    test_models.cpp
    test_beacon_parser.cpp
    test_mdns_discovery.cpp
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
    test_remembered_reconnect.cpp
    test_gamepad_input_processor.cpp
    test_sdl_motion_convert.cpp
    test_joystick_mapping.cpp
    test_joystick_remap_assign.cpp
    test_capture_mode.cpp
    test_joystick_remap_repository.cpp
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
    test_reverse_pairing.cpp
    test_battery_ui.cpp
    test_config_blocker.cpp
    test_touchpad_mode_resolve.cpp
    test_touchpad_mode_repository.cpp
    test_touchpad_mode_store.cpp
    test_pairing_machine.cpp
    test_satellite_session_machine.cpp
    test_udp_opcodes.cpp
    test_session_reconcile.cpp
    test_satellite_link_state.cpp
    test_connection_rows.cpp
    test_resend_pacer.cpp
    test_late_slot_converge.cpp
    test_binding_presence.cpp
    test_pad_identity_seed.cpp
    test_connections_composer.cpp
    test_connection_coordinator.cpp
    test_found_visibility.cpp
    test_session_lifecycle.cpp
    test_session_manager.cpp
    test_catalog_repository.cpp
    test_catalog_dto.cpp
    test_controller_type_store.cpp
    test_picker_visibility.cpp
    test_screen_wake_controller.cpp
    test_wake_state_composer.cpp
    test_wake_state_controller.cpp
    test_dish_notifications.cpp
    test_input_rate_tracker.cpp
    test_input_rate_store.cpp
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
    test_rumble_routing.cpp
    test_rumble_helpers.cpp
    test_battery_routing.cpp
    test_battery_validator.cpp
    test_physical_battery_mapping.cpp
    test_touchpad_routing.cpp
    test_usb_path_machine.cpp
    test_usb_path_machine_edge_cases.cpp
    test_usb_poll_rate.cpp
    test_poll_rate_sampler.cpp
    test_path_choice.cpp
    test_usb_path_resolution.cpp
    test_usb_path_preference_store.cpp
    test_usb_gamepad_manager.cpp
    test_usb_report_parsers.cpp
    test_usb_hid_layout.cpp
    test_usb_twin_dedup.cpp
    test_hid_transport.cpp
    test_catalog_prewarm.cpp
    test_onboarding_store.cpp
    test_license_manifest.cpp
    test_theme_store.cpp
    test_crash_reporting.cpp
    test_main_ui_state.cpp
    test_window_hit_test.cpp
    test_slot_list_model.cpp
    test_slot_path_fields.cpp
    test_connection_list_model.cpp
    test_app_view_model_maps.cpp
    test_app_view_model_connections.cpp
    test_app_view_model_bind_availability.cpp
    test_capability_solver.cpp
    test_apply_binding_machine.cpp
    test_font_stacks.cpp
    test_theme_contrast.cpp
    # Installer engine. Listed here (not in a second exe) so `ctest -R
    # installer` is a filter over the one suite the Debug CI leg already runs.
    installer/FakeOps.h
    test_installer_machine.cpp
    test_uninstall_machine.cpp
    test_installer_plan.cpp
    test_installer_journal.cpp
    test_installer_manifest.cpp
    test_payload_format.cpp
    test_installer_registry.cpp
    test_installer_shortcuts.cpp
    test_installer_running_app.cpp
    test_installer_unicode_paths.cpp
    test_installer_cli.cpp
    test_installer_version_compare.cpp
    # Auto-updater.
    test_update_version.cpp
    test_update_manifest.cpp
    test_update_machine.cpp
    test_update_staging_store.cpp
    test_update_coordinator.cpp
    test_update_handoff.cpp
    test_update_preference_store.cpp
    test_translations.cpp)

# Per-target compile of the payload layer, exactly as the stub and the pack
# tool do it: PayloadFormat is deliberately Qt-free and lives in no library, so
# the pack -> extract round-trip test builds its own copy. miniz comes with it.
list(APPEND DISH_TEST_SOURCES
    ${CMAKE_SOURCE_DIR}/src/installer/PayloadFormat.cpp
    ${CMAKE_SOURCE_DIR}/third_party/miniz/miniz.c)
# Source properties are directory-scoped, so this repeats installer/CMakeLists.
if(MSVC)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/third_party/miniz/miniz.c
        PROPERTIES COMPILE_OPTIONS "/W3;/wd4005")
endif()

add_executable(DishTests ${DISH_TEST_SOURCES})
target_link_libraries(DishTests
    PRIVATE
        dish_core
        dish_installer_core
        Catch2::Catch2WithMain
        dish_warnings)

# The manifest is a resource of the Dish exe, not of dish_core, which is what
# DishTests links — so the test reads the source file by absolute path instead.
target_compile_definitions(DishTests PRIVATE
    DISH_LICENSES_JSON_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../assets/licenses/licenses.json")

# Same reason for the .qm files, which land beside the top-level binary dir.
target_compile_definitions(DishTests PRIVATE
    DISH_QM_DIR="${CMAKE_BINARY_DIR}"
    DISH_TS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../translations")
# The `release_translations` dependency is wired from the TOP-LEVEL CMakeLists:
# that target does not exist until this directory scope is finalized.

# Catch2's discovery step launches DishTests.exe, which needs Qt6's DLLs. vcpkg
# drops libsodium/SDL2 beside the exe at link time, but Qt is not vcpkg-managed,
# so without DL_PATHS discovery exits with STATUS_DLL_NOT_FOUND (0xc0000135).
if(WIN32)
    catch_discover_tests(DishTests
        DL_PATHS
            $<TARGET_FILE_DIR:Qt6::Core>
            $<TARGET_FILE_DIR:Qt6::Network>
            $<TARGET_FILE_DIR:Qt6::Gui>)
else()
    catch_discover_tests(DishTests)
endif()
