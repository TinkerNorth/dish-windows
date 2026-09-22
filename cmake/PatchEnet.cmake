# Applies cmake/enet-msvc-c5287.patch to the fetched ENet checkout, once.
#
# This is FetchContent's patch step for `enet`. ExternalProject re-runs it
# whenever the update step runs, so a tree that already carries the patch is
# recognised and left alone rather than failing the second application.
if(NOT ENET_SOURCE_DIR OR NOT PATCH_FILE)
    message(FATAL_ERROR "PatchEnet.cmake needs ENET_SOURCE_DIR and PATCH_FILE")
endif()

find_package(Git REQUIRED)

execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${PATCH_FILE}"
    WORKING_DIRECTORY "${ENET_SOURCE_DIR}"
    RESULT_VARIABLE already_applied
    OUTPUT_QUIET
    ERROR_QUIET)
if(already_applied EQUAL 0)
    return()
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply "${PATCH_FILE}"
    WORKING_DIRECTORY "${ENET_SOURCE_DIR}"
    RESULT_VARIABLE apply_result)
if(NOT apply_result EQUAL 0)
    message(FATAL_ERROR "ENet patch did not apply: ${PATCH_FILE}")
endif()
