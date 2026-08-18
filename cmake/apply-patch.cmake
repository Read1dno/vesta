if(NOT DEFINED GIT_EXECUTABLE OR NOT DEFINED SOURCE_DIR OR NOT DEFINED PATCH_FILE)
    message(FATAL_ERROR "GIT_EXECUTABLE, SOURCE_DIR and PATCH_FILE are required")
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --check "${PATCH_FILE}"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE patch_can_apply
    OUTPUT_QUIET
    ERROR_QUIET
)

if(patch_can_apply EQUAL 0)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --whitespace=nowarn "${PATCH_FILE}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE patch_result
    )
    if(NOT patch_result EQUAL 0)
        message(FATAL_ERROR "Failed to apply ${PATCH_FILE}")
    endif()
    return()
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${PATCH_FILE}"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE patch_is_applied
    OUTPUT_QUIET
    ERROR_QUIET
)
if(NOT patch_is_applied EQUAL 0)
    message(FATAL_ERROR "${PATCH_FILE} neither applies cleanly nor is already applied")
endif()
