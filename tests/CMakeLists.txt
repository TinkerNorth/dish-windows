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
    test_atomic_counter.cpp
    test_hex.cpp
    test_endian.cpp
    test_models.cpp
    test_beacon_parser.cpp
    test_gamepad_input_processor.cpp
    test_sdl_motion_convert.cpp
    test_pairing_client_classify.cpp
    test_screen_wake_controller.cpp
    test_winsock_init.cpp
    test_set_thread_execution_state_inhibitor.cpp
    test_satellite_client_rumble.cpp
    test_satellite_client_motion.cpp
    test_satellite_client_touchpad.cpp
    test_satellite_client_lightbar.cpp
    test_host_battery.cpp)

add_executable(DishTests ${DISH_TEST_SOURCES})
target_link_libraries(DishTests
    PRIVATE
        dish_core
        Catch2::Catch2WithMain
        dish_warnings)

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
            $<TARGET_FILE_DIR:Qt6::Network>)
else()
    catch_discover_tests(DishTests)
endif()
