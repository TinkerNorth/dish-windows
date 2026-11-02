# The input-path latency bench (README.md beside this file). Bench-only: nothing here reaches the
# app or the installer.

# ViGEmClient v1.21.222.0, the SDK version the satellite targets (its lib/VENDORED.md), pinned by
# commit. Only the bench's vigem mode uses it. SYSTEM keeps /W4 /WX off its headers.
include(FetchContent)
FetchContent_Declare(
    ViGEmClient
    GIT_REPOSITORY https://github.com/ViGEm/ViGEmClient.git
    GIT_TAG        9e842ba1c3a6efbb90d9b7e9346a55b1a3d10494
    SYSTEM)
FetchContent_MakeAvailable(ViGEmClient)
# The project turns Qt's generators on for every target; this one has nothing for them.
set_target_properties(ViGEmClient PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# A DISH_LOCK_STATS build compiles every TrackedMutex with counters, so its bench is the lockstats
# twin and the plain build's is the one whose latencies are reported.
if(DISH_LOCK_STATS)
    set(DISH_INPUT_PATH_BENCH bench_input_path_lockstats)
else()
    set(DISH_INPUT_PATH_BENCH bench_input_path)
endif()

add_executable(${DISH_INPUT_PATH_BENCH} input_path_bench.cpp)
target_include_directories(${DISH_INPUT_PATH_BENCH} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/tests)
# SDL.h renames main to SDL_main on Windows unless the program says it has its own.
target_compile_definitions(${DISH_INPUT_PATH_BENCH} PRIVATE SDL_MAIN_HANDLED)
target_link_libraries(${DISH_INPUT_PATH_BENCH}
    PRIVATE
        dish_core
        ViGEmClient
        setupapi
        xinput
        dish_warnings
        dish_strict)

if(DISH_BUILD_TESTS)
    add_test(NAME ${DISH_INPUT_PATH_BENCH}_smoke
        COMMAND ${DISH_INPUT_PATH_BENCH} --smoke
            --json ${CMAKE_CURRENT_BINARY_DIR}/${DISH_INPUT_PATH_BENCH}_smoke.json)
    set_tests_properties(${DISH_INPUT_PATH_BENCH}_smoke PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()
