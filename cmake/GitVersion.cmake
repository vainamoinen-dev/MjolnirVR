# Build-time source identity (runs on every build, `cmake -P`).
# Inputs: SRC (repo root), OUT (commit header), STAMP (build-stamp header), VERSION (version header),
# GIT (git executable, may be empty).
# OUT is written only when the commit/dirty text changes (its includers recompile only then).
# STAMP (commit + build time) is rewritten on every build; only dllmain.cpp (log line 1) includes it.
# VERSION (the VERSIONINFO numbers for the DLL and launcher resource scripts) is written only when it changes.
set(commit "unknown")
set(describe "")
set(count 0)
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
        execute_process(
            COMMAND "${GIT}" -C "${SRC}" describe --tags --long --match "v[0-9]*"
            OUTPUT_VARIABLE describe
            RESULT_VARIABLE result
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(NOT result EQUAL 0)
            set(describe "")
        endif()
        execute_process(
            COMMAND "${GIT}" -C "${SRC}" rev-list --count HEAD
            OUTPUT_VARIABLE count
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(NOT count MATCHES "^[0-9]+$")
            set(count 0)
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
if(VERSION)
    # vMAJOR.MINOR[.PATCH][-label]-<commits since the tag>-g<hash> gives MAJOR.MINOR.PATCH.<commits>;
    # with no such tag, 0.0.0.<commit count>. Each field is a 16-bit number in VERSIONINFO.
    if(describe MATCHES "^v([0-9]+)\\.([0-9]+)(\\.([0-9]+))?.*-([0-9]+)-g[0-9a-f]+$")
        set(fields "${CMAKE_MATCH_1}" "${CMAKE_MATCH_2}" "${CMAKE_MATCH_4}" "${CMAKE_MATCH_5}")
        set(product "${describe}")
    else()
        set(fields 0 0 0 "${count}")
        set(product "")
    endif()
    set(numbers "")
    foreach(field IN LISTS fields)
        if(field STREQUAL "")
            set(field 0)
        endif()
        if(field GREATER 65535)
            set(field 65535)
        endif()
        list(APPEND numbers "${field}")
    endforeach()
    string(REPLACE ";" "," binary "${numbers}")
    string(REPLACE ";" "." dotted "${numbers}")
    if(product STREQUAL "")
        set(product "${dotted}")
    endif()
    if(commit MATCHES "-dirty$")
        string(APPEND product "-dirty")
    endif()
    set(text "// GENERATED at build time by cmake/GitVersion.cmake. Do not edit.\n#ifndef HALOMCCVR_VERSION_BINARY\n#define HALOMCCVR_VERSION_BINARY ${binary}\n#define HALOMCCVR_VERSION_TEXT \"${dotted}\"\n#define HALOMCCVR_PRODUCT_VERSION_TEXT \"${product}\"\n#endif\n")
    set(old "")
    if(EXISTS "${VERSION}")
        file(READ "${VERSION}" old)
    endif()
    if(NOT old STREQUAL text)
        file(WRITE "${VERSION}" "${text}")
        message(STATUS "MjolnirVR file version: ${dotted} (${product})")
    endif()
endif()
if(STAMP)
    string(TIMESTAMP built "%Y-%m-%dT%H:%M:%SZ" UTC)
    file(WRITE "${STAMP}" "// GENERATED on every build by cmake/GitVersion.cmake. Do not edit.
#pragma once
#define HALOMCCVR_BUILD_STAMP_COMMIT \"${commit}\"
#define HALOMCCVR_BUILD_TIME \"${built}\"
")
endif()
