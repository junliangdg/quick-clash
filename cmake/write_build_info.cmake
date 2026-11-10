cmake_minimum_required(VERSION 3.24)
if(NOT IS_DIRECTORY "${QUICKCLASH_SOURCE_DIR}" OR
   NOT IS_DIRECTORY "${QUICKCLASH_BINARY_DIR}" OR
   NOT QUICKCLASH_BASE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "Build identity requires source/build directories and a three-part base version.")
endif()

set(QUICKCLASH_GIT_COMMIT "")
set(QUICKCLASH_SOURCE_STATE "unknown")
set(QUICKCLASH_VERSION "${QUICKCLASH_BASE_VERSION}")
# A source archive inside another checkout must not inherit its parent's identity.
# .git may be either a directory or a worktree's indirection file.
if(EXISTS "${QUICKCLASH_SOURCE_DIR}/.git")
    find_package(Git QUIET)
    if(Git_FOUND)
        execute_process(COMMAND "${GIT_EXECUTABLE}" --no-optional-locks
            -C "${QUICKCLASH_SOURCE_DIR}" rev-parse --verify "HEAD^{commit}"
            RESULT_VARIABLE git_result OUTPUT_VARIABLE QUICKCLASH_GIT_COMMIT
            ERROR_VARIABLE git_error OUTPUT_STRIP_TRAILING_WHITESPACE TIMEOUT 15)
        string(LENGTH "${QUICKCLASH_GIT_COMMIT}" commit_length)
        if(NOT git_result STREQUAL "0" OR NOT commit_length EQUAL 40 OR
           NOT QUICKCLASH_GIT_COMMIT MATCHES "^[0-9a-fA-F]+$")
            message(FATAL_ERROR "Cannot identify the source commit: ${git_error}")
        endif()
        string(TOLOWER "${QUICKCLASH_GIT_COMMIT}" QUICKCLASH_GIT_COMMIT)
        execute_process(COMMAND "${GIT_EXECUTABLE}" --no-optional-locks
            -C "${QUICKCLASH_SOURCE_DIR}" status --porcelain=v1
            --untracked-files=normal --ignore-submodules=none
            RESULT_VARIABLE git_result OUTPUT_VARIABLE git_changes
            ERROR_VARIABLE git_error OUTPUT_STRIP_TRAILING_WHITESPACE TIMEOUT 15)
        if(NOT git_result STREQUAL "0")
            message(FATAL_ERROR "Cannot determine the source working tree state: ${git_error}")
        endif()
        set(QUICKCLASH_SOURCE_STATE "clean")
        if(NOT git_changes STREQUAL "")
            set(QUICKCLASH_SOURCE_STATE "dirty")
        endif()
    endif()
endif()

set(QUICKCLASH_CI_RUN_ID "")
set(QUICKCLASH_CI_RUN_ATTEMPT "")
if("$ENV{GITHUB_ACTIONS}" STREQUAL "true")
    set(QUICKCLASH_CI_RUN_ID "$ENV{GITHUB_RUN_ID}")
    set(QUICKCLASH_CI_RUN_ATTEMPT "$ENV{GITHUB_RUN_ATTEMPT}")
    if(NOT QUICKCLASH_CI_RUN_ID MATCHES "^[1-9][0-9]*$" OR
       NOT QUICKCLASH_CI_RUN_ATTEMPT MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "GitHub Actions build identity requires a numeric run ID and attempt.")
    endif()
endif()

file(MAKE_DIRECTORY "${QUICKCLASH_BINARY_DIR}/generated")
file(CONFIGURE OUTPUT "${QUICKCLASH_BINARY_DIR}/generated/quickclash_build_info.h" CONTENT [[
#pragma once
#define QUICKCLASH_BASE_VERSION "@QUICKCLASH_BASE_VERSION@"
#define QUICKCLASH_VERSION "@QUICKCLASH_VERSION@"
#define QUICKCLASH_GIT_COMMIT "@QUICKCLASH_GIT_COMMIT@"
#define QUICKCLASH_SOURCE_STATE "@QUICKCLASH_SOURCE_STATE@"
]] @ONLY NEWLINE_STYLE LF)
file(CONFIGURE OUTPUT "${QUICKCLASH_BINARY_DIR}/quickclash_build_info.json" CONTENT [[
{
  "schema_version": 1,
  "base_version": "@QUICKCLASH_BASE_VERSION@",
  "application_version": "@QUICKCLASH_VERSION@",
  "commit": "@QUICKCLASH_GIT_COMMIT@",
  "source_state": "@QUICKCLASH_SOURCE_STATE@",
  "ci_run_id": "@QUICKCLASH_CI_RUN_ID@",
  "ci_run_attempt": "@QUICKCLASH_CI_RUN_ATTEMPT@"
}
]] @ONLY NEWLINE_STYLE LF)
