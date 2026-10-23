# Build-time source identity (runs on every build, `cmake -P`).
# Inputs: SRC (repo root), OUT (commit header), STAMP (build-stamp header), GIT (git executable, may be empty).
# OUT is written only when the commit/dirty text changes (its includers recompile only then).
# STAMP (commit + build time) is rewritten on every build; only dllmain.cpp (log line 1) includes it.
set(commit "unknown")
if(GIT)
    execute_process(
        COMMAND "${GIT}" -C "${SRC}" rev-parse HEAD
        OUTPUT_VARIABLE head
        RESULT_VARIABLE result
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(result EQUAL 0 AND NOT head STREQUAL "")
        set(commit "${head}")
        execute_process(
            COMMAND "${GIT}" -C "${SRC}" status --porcelain --untracked-files=normal
            OUTPUT_VARIABLE status
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(NOT status STREQUAL "")
            string(APPEND commit "-dirty")
        endif()
    endif()
endif()
set(text "// GENERATED at build time by cmake/GitVersion.cmake. Do not edit.\n#pragma once\n#ifndef HALOMCCVR_BUILD_COMMIT\n#define HALOMCCVR_BUILD_COMMIT \"${commit}\"\n#endif\n")
set(old "")
if(EXISTS "${OUT}")
    file(READ "${OUT}" old)
endif()
if(NOT old STREQUAL text)
    file(WRITE "${OUT}" "${text}")
    message(STATUS "Halo MCC VR source identity: ${commit}")
endif()
if(STAMP)
    string(TIMESTAMP built "%Y-%m-%dT%H:%M:%SZ" UTC)
    file(WRITE "${STAMP}" "// GENERATED on every build by cmake/GitVersion.cmake. Do not edit.
#pragma once
#define HALOMCCVR_BUILD_STAMP_COMMIT \"${commit}\"
#define HALOMCCVR_BUILD_TIME \"${built}\"
")
endif()
