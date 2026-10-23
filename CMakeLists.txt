cmake_minimum_required(VERSION 3.24)
project(halomccvr LANGUAGES C CXX RC)

if(MSVC)
    enable_language(ASM_MASM)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static CRT so users do not need a separate Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
include(CTest)

# Source identity for the first log line: regenerated on EVERY build (not at
# configure time, which left staged DLLs naming an old commit) by
# cmake/GitVersion.cmake into generated/halomccvr_build_commit.h. The header is
# only rewritten when the commit or dirty state changes.
find_package(Git QUIET)

# Every title line is permanent in the one cumulative build. The former
# compile-time options (ODST/Reach bring-up, Reach render candidate, Halo 4
# camera, Halo 2 cold observation/stereo/observer/Anniversary) shipped ON and are
# now unconditional code; the rejected Halo 2 temporal stereo and the Halo 2
# render probe are deleted. The build identity strings keep their shipped values
# so log line 1 is unchanged.
set(HALOMCCVR_BUILD_ODST "ON")
set(HALOMCCVR_BUILD_REACH "ON")
set(HALOMCCVR_BUILD_REACH_RENDER "ON")
set(HALOMCCVR_BUILD_HALO4 "ON")
set(HALOMCCVR_BUILD_HALO2 "OBSERVER_6DOF_BOTH_RENDERERS")

message(STATUS
    "Halo MCC VR build identity: commit stamped at build time, ODST=${HALOMCCVR_BUILD_ODST}, Reach=${HALOMCCVR_BUILD_REACH}, ReachRender=${HALOMCCVR_BUILD_REACH_RENDER}, Halo4=${HALOMCCVR_BUILD_HALO4}, Halo2=${HALOMCCVR_BUILD_HALO2}")

# Exact dependency commits used by the accepted release source. Shallow clones
# avoid pulling entire third-party histories into the ignored build tree.
set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(openxr
    GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
    GIT_TAG 5267613edf3d937e3d77556a106a65c2f82b25c6
    GIT_SHALLOW TRUE)

FetchContent_Declare(minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG c3fcafdc10146beb5919319d0683e44e3c30d537
    GIT_SHALLOW TRUE)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG 8936b58fe26e8c3da834b8f60b06511d537b4c63
    GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(openxr minhook imgui)

# vr.h exposes the sampled OpenXR head pose in aim receipts. Keep that public
# header usable by the DLL's focused fixtures as well as the runtime target.
include_directories("${openxr_SOURCE_DIR}/include")

# Keep upstream pinned and untouched on disk. The local translation unit adds
# a no-write retirement path for title DLLs MCC has already unmapped.
get_target_property(mccvr_minhook_sources minhook SOURCES)
list(FILTER mccvr_minhook_sources EXCLUDE REGEX "(^|/)hook\\.c$")
set_property(TARGET minhook PROPERTY SOURCES ${mccvr_minhook_sources}
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/minhook_lifecycle.c")

# Generate, validate, and embed the pinned official SMAA shaders. Bytecode is
# compiled at configure time so selecting SMAA cannot invoke the HLSL optimizer
# on the VR render thread. The installed mod has no loose shader dependency.
set(HALOMCCVR_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${HALOMCCVR_GENERATED_DIR}")
set(HALOMCCVR_BUILD_COMMIT_HEADER "${HALOMCCVR_GENERATED_DIR}/halomccvr_build_commit.h")
set(HALOMCCVR_BUILD_STAMP_HEADER "${HALOMCCVR_GENERATED_DIR}/halomccvr_build_stamp.h")
add_custom_target(halomccvr_git_version ALL
    COMMAND "${CMAKE_COMMAND}" "-DSRC=${CMAKE_CURRENT_SOURCE_DIR}" "-DOUT=${HALOMCCVR_BUILD_COMMIT_HEADER}"
        "-DSTAMP=${HALOMCCVR_BUILD_STAMP_HEADER}"
        "-DGIT=${GIT_EXECUTABLE}" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GitVersion.cmake"
    BYPRODUCTS "${HALOMCCVR_BUILD_COMMIT_HEADER}" "${HALOMCCVR_BUILD_STAMP_HEADER}"
    COMMENT "Stamping the source commit"
    VERBATIM)
include(cmake/MenuAssets.cmake)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/third_party/smaa/SMAA.hlsl"
    HALOMCCVR_SMAA_UPSTREAM_SOURCE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/smaa/SMAA.hlsl")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/dll/core/smaa_runtime.hlsl.in"
    "${HALOMCCVR_GENERATED_DIR}/smaa_runtime.hlsl"
    @ONLY)

get_filename_component(HALOMCCVR_WINDOWS_SDK_BIN "${CMAKE_RC_COMPILER}" DIRECTORY)
find_program(HALOMCCVR_FXC NAMES fxc.exe fxc
    HINTS
        "${HALOMCCVR_WINDOWS_SDK_BIN}"
        "$ENV{WindowsSdkVerBinPath}/x64"
        "C:/Program Files (x86)/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64")
if(NOT HALOMCCVR_FXC)
    file(GLOB HALOMCCVR_FXC_CANDIDATES
        "C:/Program Files (x86)/Windows Kits/10/bin/*/x64/fxc.exe")
    list(SORT HALOMCCVR_FXC_CANDIDATES COMPARE NATURAL ORDER DESCENDING)
    if(HALOMCCVR_FXC_CANDIDATES)
        list(GET HALOMCCVR_FXC_CANDIDATES 0 HALOMCCVR_FXC)
    endif()
endif()
if(NOT HALOMCCVR_FXC)
    message(FATAL_ERROR "fxc is required to build the embedded SMAA shaders")
endif()

function(halomccvr_compile_smaa entry profile output_name)
    execute_process(
        COMMAND "${HALOMCCVR_FXC}" /nologo /Ges /WX /O3
            /T "${profile}" /E "${entry}"
            /Fo "${HALOMCCVR_GENERATED_DIR}/${output_name}"
            "${HALOMCCVR_GENERATED_DIR}/smaa_runtime.hlsl"
        RESULT_VARIABLE HALOMCCVR_FXC_RESULT
        OUTPUT_VARIABLE HALOMCCVR_FXC_OUTPUT
        ERROR_VARIABLE HALOMCCVR_FXC_ERROR)
    if(NOT HALOMCCVR_FXC_RESULT EQUAL 0)
        message(FATAL_ERROR
            "SMAA shader ${entry} failed to compile:\n"
            "${HALOMCCVR_FXC_OUTPUT}${HALOMCCVR_FXC_ERROR}")
    endif()
endfunction()

halomccvr_compile_smaa(vs_smaa_edges vs_5_0 smaa_edges_vs.cso)
halomccvr_compile_smaa(vs_smaa_weights vs_5_0 smaa_weights_vs.cso)
halomccvr_compile_smaa(vs_smaa_neighborhood vs_5_0 smaa_neighborhood_vs.cso)
halomccvr_compile_smaa(ps_smaa_edges ps_5_0 smaa_edges_ps.cso)
halomccvr_compile_smaa(ps_smaa_weights ps_5_0 smaa_weights_ps.cso)
halomccvr_compile_smaa(ps_smaa_neighborhood ps_5_0 smaa_neighborhood_ps.cso)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/dll/core/smaa_resources.rc.in"
    "${HALOMCCVR_GENERATED_DIR}/smaa_resources.rc"
    @ONLY)

# OpenXR requests the dynamic CRT for its static loader; keep it consistent with
# the mod's static-CRT build.
set_target_properties(openxr_loader PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends)

add_executable(menu_style_preview EXCLUDE_FROM_ALL tools/menu-style-preview.cpp)
target_include_directories(menu_style_preview PRIVATE src/dll "${HALOMCCVR_GENERATED_DIR}")
target_compile_definitions(menu_style_preview PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(menu_style_preview PRIVATE imgui_lib d3d11 dxgi d3dcompiler)

add_library(mjolnirvr SHARED
    ${HALOMCCVR_GENERATED_DIR}/smaa_resources.rc
    src/common/log.cpp
    src/common/config.cpp
    src/common/input_logic.cpp
    src/common/scope_logic.cpp
    src/common/title_registry.cpp
    src/common/title_runtime_state.cpp
    src/dll/core/dllmain.cpp
    src/dll/core/native_fault_probe.cpp
    src/dll/core/telemetry_recorder.cpp
    src/dll/core/d3d11_hook.cpp
    src/dll/core/vr.cpp
    src/dll/core/vr_controllers.cpp
    src/dll/core/vr_compose.cpp
    src/dll/core/vr_scope.cpp
    src/dll/core/vr_telemetry.cpp
    src/dll/core/weapon_accessory_renderer.cpp
    src/dll/core/native_reload_policy.cpp
    src/dll/core/native_vehicle_first_person.cpp
    src/dll/core/menu.cpp
    src/dll/core/native_menu_pointer.cpp
    src/dll/core/sigscan.cpp
    src/dll/core/hook_install_check.cpp
    src/dll/core/game.cpp
    src/dll/core/ik.cpp
    src/dll/core/input.cpp
    src/dll/titles/reach/reach_action_test_guard.cpp
    src/dll/core/roomscale.cpp
    src/dll/core/title_adapter.cpp
    src/dll/core/title_reentry_probe.cpp
    src/dll/titles/reach/reach_adapter.cpp
    src/dll/titles/h2/halo2_adapter.cpp
    src/dll/titles/h2/halo2_cold_observation.cpp
    src/dll/titles/h2/halo2_stereo_core.cpp
    src/dll/titles/h2/halo2_observer_6dof.cpp
    src/dll/titles/h2/halo2_render_mode_guard.cpp
    src/dll/titles/h2/halo2_look_tests.cpp
    src/dll/titles/h4/halo4_look_tests.cpp
    src/dll/titles/h2/halo2_saber_camera.cpp
    src/dll/titles/ce/haloce_native_bindings.cpp
    src/dll/titles/ce/haloce_eye_cache.cpp
    src/dll/titles/ce/haloce_stereo_core.cpp
    src/dll/titles/ce/haloce_first_person.cpp
    src/dll/titles/ce/haloce_contact.cpp
    src/dll/titles/ce/haloce_hud.cpp
    src/dll/titles/ce/haloce_hud_layout.cpp
    src/dll/titles/ce/haloce_hud_target.cpp
    src/dll/titles/ce/haloce_controls.cpp
    src/dll/titles/ce/haloce_comfort.cpp
    src/dll/titles/ce/haloce_orientation.cpp
    src/dll/titles/ce/haloce_unit_control.cpp
    src/dll/titles/ce/haloce_network_input.cpp
    src/dll/titles/ce/haloce_runtime.cpp
    src/dll/titles/ce/haloce_frame_source.cpp
    src/dll/titles/h2/halo2_anniversary_stereo.cpp
    src/dll/titles/h4/halo4_adapter.cpp
    src/dll/titles/h4/halo4_cold_observation.cpp
    src/dll/titles/h4/halo4_camera_core.cpp
    src/dll/titles/h4/halo4_frame_source.cpp
    src/dll/titles/odst/odst_camera_core.cpp
    src/dll/titles/h3/halo3_runtime.cpp
    src/dll/titles/h3/halo3_camera_install.cpp
    src/dll/titles/h2/halo2_runtime.cpp
    src/dll/titles/h2/halo2_frame_source.cpp)
target_sources(mjolnirvr PRIVATE src/dll/titles/h4/halo4_restoration.asm)
set_target_properties(mjolnirvr PROPERTIES OUTPUT_NAME "HaloMCCVR")
# Crash triage: a PDB and a linker map for every Release DLL. /DEBUG turns
# /OPT:REF and /OPT:ICF off by default; they are named again so the code is
# the same as a build without symbols. Never shipped: the dist/launcher
# install rules list their files explicitly; package-release.ps1 copies both
# to builds\symbols\<version>.
target_compile_options(mjolnirvr PRIVATE $<$<CONFIG:Release>:/Zi>)
target_link_options(mjolnirvr PRIVATE $<$<CONFIG:Release>:/DEBUG:FULL /OPT:REF /OPT:ICF /MAP>)
target_include_directories(mjolnirvr PRIVATE
    ${HALOMCCVR_GENERATED_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dll
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dll/core
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/smaa/Textures)
target_sources(mjolnirvr PRIVATE
    src/dll/titles/reach/reach_render_candidate.cpp
    src/dll/titles/reach/reach_display_worker.cpp
    src/dll/titles/reach/reach_camera_core.cpp
    src/dll/titles/reach/reach_frame_source.cpp
    src/dll/titles/reach/reach_render_preflight.cpp)
target_compile_definitions(mjolnirvr PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    XR_USE_GRAPHICS_API_D3D11 XR_USE_PLATFORM_WIN32
    HALOMCCVR_BUILD_ODST="${HALOMCCVR_BUILD_ODST}"
    HALOMCCVR_BUILD_REACH="${HALOMCCVR_BUILD_REACH}"
    HALOMCCVR_BUILD_REACH_RENDER="${HALOMCCVR_BUILD_REACH_RENDER}"
    HALOMCCVR_BUILD_HALO4="${HALOMCCVR_BUILD_HALO4}"
    HALOMCCVR_BUILD_HALO2="${HALOMCCVR_BUILD_HALO2}")
add_dependencies(mjolnirvr halomccvr_git_version)
target_link_libraries(mjolnirvr PRIVATE
    openxr_loader minhook imgui_lib
    d3d11 dxgi d3dcompiler gdi32)
# Every MinHook create/remove in the DLL goes through the install-time detour
# check by name (MCCVR_CreateHookChecked / MCCVR_RemoveHookChecked in
# src/dll/core/hook_install_check.h); tools/check_raw_minhook_calls.py guards it.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    # Python check: no raw MH_CreateHook / MH_RemoveHook call under src/dll (fails the build).
    add_custom_target(halomccvr_raw_minhook_check
        COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_raw_minhook_calls.py" --self-test
        COMMENT "Checking for raw MinHook create/remove calls in the DLL"
        VERBATIM)
    add_dependencies(mjolnirvr halomccvr_raw_minhook_check)
else()
    message(WARNING "Python 3 not found: the raw MinHook call check (tools/check_raw_minhook_calls.py) does not run in this build")
endif()
target_sources(mjolnirvr PRIVATE src/dll/core/native_subtitles.cpp)
target_sources(mjolnirvr PRIVATE src/dll/core/physical_crouch_camera.cpp)
target_sources(mjolnirvr PRIVATE src/dll/core/bloom_override.cpp)
target_link_libraries(mjolnirvr PRIVATE windowscodecs)
target_link_libraries(mjolnirvr PRIVATE bcrypt)

include(cmake/Dlss.cmake)

add_executable(mjolnirvr_launcher WIN32 src/launcher/launcher.cpp
    src/launcher/installer.cpp src/launcher/menu.cpp src/launcher/launcher.rc)
set_target_properties(mjolnirvr_launcher PROPERTIES
    OUTPUT_NAME "HaloMCCVRLauncher")
target_compile_definitions(mjolnirvr_launcher PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
# IApplicationActivationManager: the Microsoft Store edition is a packaged app
# and can only be started through packaged activation.
target_link_libraries(mjolnirvr_launcher PRIVATE ole32 bcrypt advapi32 shell32 uxtheme)
if(MSVC)
    # Runs as the invoking user: no UAC prompt on every launch, and MCC is not
    # started elevated. (Upstream's later requireAdministrator was dropped.)
    target_link_options(mjolnirvr_launcher PRIVATE "/MANIFESTUAC:level='asInvoker' uiAccess='false'")
endif()

# Opt-in reverse-engineering helper; never shipped or built by the default
# preset. Some diagnostic modes write to a running game process.
add_executable(camscan EXCLUDE_FROM_ALL tools/camscan.cpp)
target_compile_definitions(camscan PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(camscan PRIVATE psapi)

# Opt-in, external Reach observer. It opens MCC with query/read rights only and
# is deliberately excluded from product/default builds.
add_executable(reach_runtime_observer EXCLUDE_FROM_ALL
    tools/reach_runtime_observer.cpp)
target_include_directories(reach_runtime_observer PRIVATE src/common)
target_compile_definitions(reach_runtime_observer PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(reach_runtime_observer PRIVATE ${HALOMCCVR_GENERATED_DIR})
add_dependencies(reach_runtime_observer halomccvr_git_version)
target_link_libraries(reach_runtime_observer PRIVATE bcrypt ntdll)
set_target_properties(reach_runtime_observer PROPERTIES
    OUTPUT_NAME "reach-runtime-observer")

if(BUILD_TESTING)
    add_executable(halomccvr_bloom_override_tests tests/bloom_override_tests.cpp src/dll/core/bloom_override.cpp src/common/config.cpp)
    target_compile_definitions(halomccvr_bloom_override_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_bloom_override_tests PRIVATE d3d11)
    add_test(NAME halomccvr_bloom_override_tests COMMAND halomccvr_bloom_override_tests)
    add_executable(halomccvr_muzzle_suppression_tests tests/muzzle_suppression_tests.cpp)
    target_compile_definitions(halomccvr_muzzle_suppression_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_muzzle_suppression_tests COMMAND halomccvr_muzzle_suppression_tests)
    add_executable(halomccvr_physical_crouch_tests tests/physical_crouch_tests.cpp)
    add_executable(halomccvr_hud_reveal_tests tests/hud_reveal_tests.cpp)
    add_test(NAME halomccvr_hud_reveal_tests COMMAND halomccvr_hud_reveal_tests)
    add_executable(body_visibility_switch_tests tests/body_visibility_switch_tests.cpp)
    add_test(NAME body_visibility_switch COMMAND body_visibility_switch_tests)
    add_executable(halomccvr_halo4_arm_ik_tests tests/halo4_arm_ik_tests.cpp src/dll/core/ik.cpp)
    target_compile_definitions(halomccvr_halo4_arm_ik_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo4_arm_ik_tests COMMAND halomccvr_halo4_arm_ik_tests)
    add_executable(halomccvr_halo4_body_ik_tests tests/halo4_body_ik_logic_tests.cpp)
    add_executable(finger_joint_identity_tests tests/finger_joint_identity_tests.cpp)
    target_compile_features(finger_joint_identity_tests PRIVATE cxx_std_20)
    add_test(NAME finger_joint_identity COMMAND finger_joint_identity_tests)
    add_executable(window_focus_policy_tests tests/window_focus_policy_tests.cpp)
    target_compile_definitions(window_focus_policy_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME window_focus_policy COMMAND window_focus_policy_tests)
    target_compile_definitions(halomccvr_halo4_body_ik_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo4_body_ik_tests COMMAND halomccvr_halo4_body_ik_tests)
    add_executable(reach_contact_backend_tests tests/reach_contact_backend_tests.cpp)
    target_compile_features(reach_contact_backend_tests PRIVATE cxx_std_20)
    target_compile_options(reach_contact_backend_tests PRIVATE /W4 /EHsc)
    add_test(NAME reach_contact_backend COMMAND reach_contact_backend_tests)
    add_executable(halo4_contact_backend_tests tests/halo4_contact_backend_tests.cpp)
    target_compile_features(halo4_contact_backend_tests PRIVATE cxx_std_20)
    target_compile_options(halo4_contact_backend_tests PRIVATE /W4 /EHsc)
    add_test(NAME halo4_contact_backend COMMAND halo4_contact_backend_tests)
    add_test(NAME halomccvr_physical_crouch_tests COMMAND halomccvr_physical_crouch_tests)
    add_executable(halomccvr_physical_crouch_camera_tests tests/physical_crouch_camera_tests.cpp src/dll/core/physical_crouch_camera.cpp)
    add_test(NAME halomccvr_physical_crouch_camera_tests COMMAND halomccvr_physical_crouch_camera_tests)
    add_executable(halomccvr_gesture_binding_reader_tests tests/gesture_binding_reader_tests.cpp)
    target_compile_definitions(halomccvr_gesture_binding_reader_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_gesture_binding_reader_tests COMMAND halomccvr_gesture_binding_reader_tests)
    add_executable(halomccvr_installer_tests tests/installer_tests.cpp src/launcher/installer.cpp)
    target_compile_definitions(halomccvr_installer_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    target_link_libraries(halomccvr_installer_tests PRIVATE bcrypt advapi32)
    add_test(NAME halomccvr_installer_tests COMMAND halomccvr_installer_tests)
    add_executable(halomccvr_vr_mapping_tests tests/vr_action_mapping_tests.cpp src/common/config.cpp src/common/log.cpp src/common/scope_logic.cpp)
    target_compile_definitions(halomccvr_vr_mapping_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_vr_mapping_tests COMMAND halomccvr_vr_mapping_tests)
    add_executable(halomccvr_halo4_pause_lifetime_tests tests/halo4_pause_lifetime_tests.cpp)
    target_compile_definitions(halomccvr_halo4_pause_lifetime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo4_pause_lifetime_tests COMMAND halomccvr_halo4_pause_lifetime_tests)
    add_executable(halomccvr_reach_hud_height_tests tests/reach_hud_height_tests.cpp)
    target_compile_definitions(halomccvr_reach_hud_height_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_reach_hud_height_tests COMMAND halomccvr_reach_hud_height_tests)
    add_executable(halomccvr_hook_lifecycle_tests tests/minhook_lifecycle_tests.cpp)
    target_link_libraries(halomccvr_hook_lifecycle_tests PRIVATE minhook)
    add_test(NAME halomccvr_hook_lifecycle_tests COMMAND halomccvr_hook_lifecycle_tests)
    add_executable(halomccvr_haloce_vehicle_frame_tests tests/haloce_vehicle_frame_tests.cpp)
    add_test(NAME halomccvr_haloce_vehicle_frame_tests COMMAND halomccvr_haloce_vehicle_frame_tests)
    add_executable(halomccvr_reach_frame_cap_tests tests/reach_frame_cap_tests.cpp)
    add_test(NAME halomccvr_reach_frame_cap_tests COMMAND halomccvr_reach_frame_cap_tests)
    add_executable(halomccvr_reach_action_test_guard_tests tests/reach_action_test_guard_tests.cpp)
    add_test(NAME halomccvr_reach_action_test_guard_tests COMMAND halomccvr_reach_action_test_guard_tests)
    add_executable(halomccvr_weapon_frame_transfer_tests tests/weapon_frame_transfer_tests.cpp)
    add_test(NAME halomccvr_weapon_frame_transfer_tests COMMAND halomccvr_weapon_frame_transfer_tests)
    add_executable(halomccvr_xinput_packet_tests tests/xinput_packet_tests.cpp)
    target_compile_definitions(halomccvr_xinput_packet_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_xinput_packet_tests COMMAND halomccvr_xinput_packet_tests)
    add_executable(halomccvr_hook_registry_tests tests/hook_registry_tests.cpp)
    target_include_directories(halomccvr_hook_registry_tests PRIVATE "${minhook_SOURCE_DIR}/include")
    add_test(NAME halomccvr_hook_registry_tests COMMAND halomccvr_hook_registry_tests)
    add_executable(halomccvr_title_traits_tests tests/title_traits_tests.cpp)
    add_test(NAME halomccvr_title_traits_tests COMMAND halomccvr_title_traits_tests)
    add_executable(halomccvr_weapon_profile_tests tests/weapon_profile_tests.cpp)
    target_compile_definitions(halomccvr_weapon_profile_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_weapon_profile_tests COMMAND halomccvr_weapon_profile_tests)
    add_executable(halomccvr_qa_give_tests tests/qa_give_tests.cpp)
    add_test(NAME halomccvr_qa_give_tests COMMAND halomccvr_qa_give_tests)
    add_executable(halomccvr_reserve_weapon_tests tests/reserve_weapon_tests.cpp)
    add_test(NAME halomccvr_reserve_weapon_tests COMMAND halomccvr_reserve_weapon_tests)
    add_executable(halomccvr_halo4_visor_tests tests/halo4_visor_tests.cpp)
    add_test(NAME halomccvr_halo4_visor_tests COMMAND halomccvr_halo4_visor_tests)
    add_executable(halomccvr_halo2_hud_element_tests tests/halo2_hud_element_tests.cpp)
    add_test(NAME halomccvr_halo2_hud_element_tests COMMAND halomccvr_halo2_hud_element_tests)
    add_executable(halomccvr_hud_element_tests tests/hud_element_tests.cpp)
    add_test(NAME halomccvr_hud_element_tests COMMAND halomccvr_hud_element_tests)
    add_executable(halomccvr_odst_drop_pod_tests tests/odst_drop_pod_tests.cpp)
    add_test(NAME halomccvr_odst_drop_pod_tests COMMAND halomccvr_odst_drop_pod_tests)
    add_executable(halomccvr_grab_pickup_tests tests/grab_pickup_tests.cpp)
    add_test(NAME halomccvr_grab_pickup_tests COMMAND halomccvr_grab_pickup_tests)
    add_executable(halomccvr_holster_logic_tests tests/holster_logic_tests.cpp)
    add_test(NAME halomccvr_holster_logic_tests COMMAND halomccvr_holster_logic_tests)
    add_executable(halomccvr_weapon_inventory_tests tests/weapon_inventory_tests.cpp)
    add_test(NAME halomccvr_weapon_inventory_tests COMMAND halomccvr_weapon_inventory_tests)
    add_executable(halomccvr_contact_melee_drain_tests tests/contact_melee_drain_tests.cpp)
    add_test(NAME halomccvr_contact_melee_drain_tests COMMAND halomccvr_contact_melee_drain_tests)
    # Exhaustive before/after proof that the shared vehicle_family logic matches each title's old tables.
    add_executable(halomccvr_vehicle_family_tests tests/vehicle_family_tests.cpp)
    add_test(NAME halomccvr_vehicle_family_tests COMMAND halomccvr_vehicle_family_tests)
    # Golden: the H3/ODST physical-crouch camera Apply.
    add_executable(halomccvr_physical_crouch_apply_golden_tests tests/physical_crouch_apply_golden_tests.cpp src/dll/core/sigscan.cpp)
    target_compile_definitions(halomccvr_physical_crouch_apply_golden_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_physical_crouch_apply_golden_tests COMMAND halomccvr_physical_crouch_apply_golden_tests)
    # Goldens: the look-test feed, builder and report per title.
    add_executable(halomccvr_halo2_look_tests_golden_tests tests/halo2_look_tests_golden_tests.cpp src/dll/core/sigscan.cpp)
    target_include_directories(halomccvr_halo2_look_tests_golden_tests PRIVATE "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halomccvr_halo2_look_tests_golden_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo2_look_tests_golden COMMAND halomccvr_halo2_look_tests_golden_tests)
    add_executable(halomccvr_halo4_look_tests_golden_tests tests/halo4_look_tests_golden_tests.cpp src/dll/core/sigscan.cpp)
    target_include_directories(halomccvr_halo4_look_tests_golden_tests PRIVATE "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halomccvr_halo4_look_tests_golden_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo4_look_tests_golden COMMAND halomccvr_halo4_look_tests_golden_tests)
    add_executable(halomccvr_scope_window_tests tests/scope_window_tests.cpp)
    add_test(NAME halomccvr_scope_window_tests COMMAND halomccvr_scope_window_tests)
    add_executable(halomccvr_scope_render_scale_tests tests/scope_render_scale_tests.cpp)
    add_test(NAME halomccvr_scope_render_scale_tests COMMAND halomccvr_scope_render_scale_tests)
    add_executable(halomccvr_glass_reprojection_tests tests/glass_reprojection_tests.cpp)
    add_test(NAME halomccvr_glass_reprojection_tests COMMAND halomccvr_glass_reprojection_tests)
    add_executable(halomccvr_scope_parallax_tests tests/scope_parallax_tests.cpp)
    add_test(NAME halomccvr_scope_parallax_tests COMMAND halomccvr_scope_parallax_tests)
    add_executable(halomccvr_lens_anchor_tests tests/lens_anchor_tests.cpp)
    add_test(NAME halomccvr_lens_anchor_tests COMMAND halomccvr_lens_anchor_tests)
    add_executable(halomccvr_fist_melee_tests tests/fist_melee_tests.cpp)
    add_test(NAME halomccvr_fist_melee_tests COMMAND halomccvr_fist_melee_tests)
    add_executable(halomccvr_native_binding_tests tests/native_binding_tests.cpp src/dll/core/sigscan.cpp)
    add_test(NAME halomccvr_native_binding_tests COMMAND halomccvr_native_binding_tests)
    # These three guards are not registered without Python; the two log guards are not registered
    # without docs/audit/LOG-STRING-MAP.txt (the public export has no docs/audit) and are skipped (77)
    # without git history. Changing a guarded LOG line on purpose: make that change in its own commit,
    # then move --base to that commit (the guard compares against --base, so the new wording becomes
    # the reference).
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/docs/audit/LOG-STRING-MAP.txt")
            # Python check: muzzle and contact LOG lines stay byte-identical to --base, with typed arguments
            # (their rows reach LOG through a template, outside printf checking).
            add_test(NAME halomccvr_muzzle_log_guard
                COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_log_literals.py"
                    --map "${CMAKE_CURRENT_SOURCE_DIR}/docs/audit/LOG-STRING-MAP.txt"
                    --base ab121dfd --paths "src/dll/*muzzle*" "src/dll/*contact*" "src/dll/titles/reach/*" "src/dll/titles/h4/*" "src/dll/titles/odst/*" "src/dll/titles/h3/*" "src/dll/titles/h2/*" "src/dll/titles/ce/haloce_runtime.cpp"
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            set_tests_properties(halomccvr_muzzle_log_guard PROPERTIES SKIP_RETURN_CODE 77)
            # Python check: weapon-haptic LOG lines stay byte-identical to --base (the main before the haptic fold).
            add_test(NAME halomccvr_haptic_log_guard
                COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_log_literals.py"
                    --map "${CMAKE_CURRENT_SOURCE_DIR}/docs/audit/LOG-STRING-MAP.txt"
                    --base 3471ffb --paths "src/dll/*haptic*" "src/dll/core/muzzle_hook_set.h"
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            set_tests_properties(halomccvr_haptic_log_guard PROPERTIES SKIP_RETURN_CODE 77)
        endif()
        # Python check: the offline frame-cap check's signatures and RVAs stay those of game.cpp.
        add_test(NAME halomccvr_frame_cap_sites_pin
            COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_frame_cap_sites.py" --self-check
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    # Packaging runs this tiny program against a nonexistent path to generate
    # the current source tree's exact default config. It is intentionally not
    # installed by CMake: package-candidate stages and hashes its output while
    # install-candidate preserves every already-existing live config.
    add_executable(halomccvr_config_defaults
        tools/config_defaults.cpp
        src/common/config.cpp
        src/common/log.cpp)
    target_include_directories(halomccvr_config_defaults PRIVATE src/common)
    target_compile_definitions(halomccvr_config_defaults PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    set_target_properties(halomccvr_config_defaults PROPERTIES
        OUTPUT_NAME "halomccvr-config-defaults")

    # The core suite split by subject (E3): one executable and one ctest entry per subject, each with the
    # original target's sources, include directories, definitions and stack reserve. The 4 MiB reserve is
    # for the ReachRollbackGate unit (core_reach_preflight_hud), whose frame is about 1.05 MiB, over MSVC's
    # 1 MiB default; every subject keeps it so no test runs differently.
    foreach(halomccvr_core_subject IN ITEMS halo4_contact_bounds platform weapons_contact aim_scope_hud reach_preflight_hud title_runtime h3_odst_ce reach_identity configured halo4_camera_render halo2)
        set(halomccvr_core_target halomccvr_core_${halomccvr_core_subject}_tests)
        add_executable(${halomccvr_core_target}
            tests/core_${halomccvr_core_subject}_tests.cpp
            tests/roomscale_input_tests.cpp
            src/common/input_logic.cpp
            src/common/scope_logic.cpp
            src/common/title_registry.cpp
            src/common/title_runtime_state.cpp
            src/common/config.cpp
            src/common/log.cpp
            src/dll/core/sigscan.cpp
            src/dll/titles/reach/reach_adapter.cpp
            src/dll/titles/h2/halo2_adapter.cpp
            src/dll/titles/h4/halo4_adapter.cpp
            src/dll/titles/reach/reach_render_candidate.cpp
            src/dll/titles/reach/reach_render_preflight.cpp)
        target_link_libraries(${halomccvr_core_target} PRIVATE bcrypt)
        target_include_directories(${halomccvr_core_target} PRIVATE src/common src/dll)
        target_compile_definitions(${halomccvr_core_target} PRIVATE
            UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
        if(MSVC)
            target_link_options(${halomccvr_core_target} PRIVATE "/STACK:4194304")
        endif()
        add_test(NAME ${halomccvr_core_target} COMMAND ${halomccvr_core_target})
    endforeach()
    # Exercise shipping XInput/OpenXR haptic functions with captured XR output.
    # This also checks the real title masks, ownership and short-pulse retention.
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    set(HALOMCCVR_PACKAGE_PYTHON "${Python3_EXECUTABLE}"
        CACHE FILEPATH "Python interpreter for package analyser validation" FORCE)
    add_executable(halomccvr_virtual_stock_tests tests/virtual_stock_tests.cpp)
    target_include_directories(halomccvr_virtual_stock_tests PRIVATE src/common)
    target_compile_definitions(halomccvr_virtual_stock_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_virtual_stock_tests COMMAND halomccvr_virtual_stock_tests)
    # Two-Handed Lab tranche 1: dependency-light PURE anchor/authority/damping
    # logic. Header-only; no runtime integration yet.
    add_executable(halomccvr_two_hand_lab_tests tests/two_hand_lab_logic_tests.cpp)
    target_include_directories(halomccvr_two_hand_lab_tests PRIVATE src/common)
    target_compile_definitions(halomccvr_two_hand_lab_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_two_hand_lab_tests COMMAND halomccvr_two_hand_lab_tests)
    # Persistent support grip: pure policy headers only, no
    # runtime wiring yet. Title-aware owner identity, tri-state evidence,
    # durable relationship/writer policy, pending-acquisition freshness and
    # the presented-ray qualification rules. The suite links the existing
    # header-only hold/admission helpers so the acquisition-order case steps
    # the production equation.
    add_executable(halomccvr_support_grip_logic_tests
        tests/support_grip_logic_tests.cpp src/common/input_logic.cpp)
    target_include_directories(halomccvr_support_grip_logic_tests PRIVATE src/common)
    target_compile_definitions(halomccvr_support_grip_logic_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_support_grip_logic_tests COMMAND halomccvr_support_grip_logic_tests)
    # Support-grab acquisition predicate: acquisition zone independent of the
    # support wrist orientation.
    add_executable(halomccvr_vehicle_view_tests tests/vehicle_view_tests.cpp)
    target_compile_definitions(halomccvr_vehicle_view_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_vehicle_view_tests COMMAND halomccvr_vehicle_view_tests)
    add_executable(halomccvr_turret_handles_tests tests/turret_handles_tests.cpp)
    target_compile_definitions(halomccvr_turret_handles_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_turret_handles_tests COMMAND halomccvr_turret_handles_tests)
    add_executable(halomccvr_turret_gyro_tests tests/turret_gyro_tests.cpp)
    target_compile_definitions(halomccvr_turret_gyro_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_turret_gyro_tests COMMAND halomccvr_turret_gyro_tests)
    add_executable(halomccvr_flight_stick_tests tests/flight_stick_tests.cpp)
    target_compile_definitions(halomccvr_flight_stick_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_flight_stick_tests COMMAND halomccvr_flight_stick_tests)
    add_executable(halomccvr_legacy_floating_hands_tests tests/legacy_floating_hands_tests.cpp)
    target_compile_definitions(halomccvr_legacy_floating_hands_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_legacy_floating_hands_tests COMMAND halomccvr_legacy_floating_hands_tests)
    add_executable(halomccvr_fp_rig_coverage_tests tests/fp_rig_coverage_tests.cpp)
    target_compile_definitions(halomccvr_fp_rig_coverage_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_fp_rig_coverage_tests COMMAND halomccvr_fp_rig_coverage_tests)
    add_executable(halomccvr_pause_debounce_tests tests/pause_debounce_tests.cpp)
    add_test(NAME halomccvr_pause_debounce_tests COMMAND halomccvr_pause_debounce_tests)
    add_executable(halomccvr_scope_log_tests tests/scope_log_tests.cpp)
    add_test(NAME halomccvr_scope_log_tests COMMAND halomccvr_scope_log_tests)
    add_executable(halomccvr_vehicle_monitors_tests tests/vehicle_monitors_tests.cpp)
    add_test(NAME halomccvr_vehicle_monitors_tests COMMAND halomccvr_vehicle_monitors_tests)
    add_executable(halomccvr_cockpit_screen_tests tests/cockpit_screen_tests.cpp)
    target_compile_definitions(halomccvr_cockpit_screen_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_cockpit_screen_tests COMMAND halomccvr_cockpit_screen_tests)
    add_executable(halomccvr_vehicle_comfort_tests tests/vehicle_comfort_tests.cpp)
    target_compile_definitions(halomccvr_vehicle_comfort_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_vehicle_comfort_tests COMMAND halomccvr_vehicle_comfort_tests)
    add_executable(halomccvr_vehicle_wheel_tests tests/vehicle_wheel_tests.cpp)
    target_compile_definitions(halomccvr_vehicle_wheel_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_vehicle_wheel_tests COMMAND halomccvr_vehicle_wheel_tests)
    add_executable(halomccvr_visor_zoom_tests tests/visor_zoom_tests.cpp src/common/scope_logic.cpp)
    target_compile_definitions(halomccvr_visor_zoom_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_visor_zoom_tests COMMAND halomccvr_visor_zoom_tests)
    add_executable(halomccvr_guard_count_tests tests/guard_count_tests.cpp)
    target_compile_definitions(halomccvr_guard_count_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_guard_count_tests COMMAND halomccvr_guard_count_tests)
    add_executable(halomccvr_scope_sight_tests tests/scope_sight_tests.cpp)
    target_compile_definitions(halomccvr_scope_sight_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_scope_sight_tests COMMAND halomccvr_scope_sight_tests)
    add_executable(halomccvr_grenade_throw_tests tests/grenade_throw_tests.cpp)
    target_include_directories(halomccvr_grenade_throw_tests PRIVATE src/common)
    target_compile_definitions(halomccvr_grenade_throw_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_grenade_throw_tests COMMAND halomccvr_grenade_throw_tests)
    add_executable(halomccvr_support_grab_tests tests/support_grab_tests.cpp)
    target_include_directories(halomccvr_support_grab_tests PRIVATE src/common)
    target_compile_definitions(halomccvr_support_grab_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_support_grab_tests COMMAND halomccvr_support_grab_tests)
    # Halo 4 on-foot owner-evidence fallback: stage decision plus the per-pair
    # finalization policy (A003).
    add_executable(halomccvr_halo4_owner_evidence_tests tests/halo4_owner_evidence_tests.cpp)
    target_compile_definitions(halomccvr_halo4_owner_evidence_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo4_owner_evidence_tests COMMAND halomccvr_halo4_owner_evidence_tests)
    # Pure grab/release aim-continuity state machine (fixed 200 ms smoothstep
    # for both directions) shared by Standard and Plus Virtual Stock.
    # Header-only and dependency-free, so it gets its own deterministic
    # standalone suite.
    add_executable(halomccvr_virtual_stock_aim_continuity_tests
        tests/virtual_stock_aim_continuity_tests.cpp)
    target_include_directories(halomccvr_virtual_stock_aim_continuity_tests
        PRIVATE src/common)
    target_compile_definitions(halomccvr_virtual_stock_aim_continuity_tests
        PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_virtual_stock_aim_continuity_tests
        COMMAND halomccvr_virtual_stock_aim_continuity_tests)
    set(HALOMCCVR_STOCK_FIXTURE "${HALOMCCVR_GENERATED_DIR}/virtual_stock_fixture.inl")
    add_custom_command(OUTPUT "${HALOMCCVR_STOCK_FIXTURE}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_virtual_stock_fixture.py"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}" --output "${HALOMCCVR_STOCK_FIXTURE}"
        DEPENDS tools/generate_virtual_stock_fixture.py tools/generate_haptics_runtime_fixture.py
            src/dll/core/vr.cpp src/dll/core/vr_controllers.cpp src/dll/core/vr_internal.h src/dll/core/vr_format_helpers.h src/dll/core/virtual_stock_aim.inl VERBATIM)
    add_executable(halomccvr_virtual_stock_aim_tests tests/virtual_stock_aim_tests.cpp "${HALOMCCVR_STOCK_FIXTURE}")
    target_include_directories(halomccvr_virtual_stock_aim_tests PRIVATE
        "${HALOMCCVR_GENERATED_DIR}" "${openxr_SOURCE_DIR}/include")
    add_test(NAME halomccvr_virtual_stock_aim_tests COMMAND halomccvr_virtual_stock_aim_tests)
    # End-to-end live seam for grab/release aim continuity: the test includes
    # the production runtime fragment against the generated production aim
    # solver, so the asserted latch-frame anchor contract, the 200 ms ease and
    # the release continuity are the shipping code, not a model of it. It
    # builds both Standard (rear reference 0/1) and Plus (rear reference 3)
    # inputs.
    add_executable(halomccvr_virtual_stock_aim_continuity_live_tests
        tests/virtual_stock_aim_continuity_live_tests.cpp
        "${HALOMCCVR_STOCK_FIXTURE}")
    target_include_directories(
        halomccvr_virtual_stock_aim_continuity_live_tests PRIVATE
        "${HALOMCCVR_GENERATED_DIR}" "${openxr_SOURCE_DIR}/include")
    add_test(NAME halomccvr_virtual_stock_aim_continuity_live_tests
        COMMAND halomccvr_virtual_stock_aim_continuity_live_tests)
    # Python check: the aim-continuity layer is invalidated on exactly its nine exceptional lifecycle
    # paths (definition plus nine call sites) and never by the routine prepared-frame retire
    # (ResetPreparedFrame). --self-test runs the checker on synthetic sources first.
    add_test(NAME halomccvr_aim_continuity_lifecycle_guard
        COMMAND "${Python3_EXECUTABLE}" -O
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_aim_continuity_lifecycle.py"
            --self-test)
    # Python check: the Two-Handed Lab stays hidden from normal sidebar navigation like the Virtual Stock Lab
    # (no Advanced doorway, no Back button) while its page block and test suites stay registered; presets
    # are complete deterministic builder states; the global reset also resets the Lab while the Virtual
    # Stock reset never touches it; the status line uses numerical wording. --self-test runs first.
    add_test(NAME halomccvr_two_hand_lab_patch_guard
        COMMAND "${Python3_EXECUTABLE}" -O
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_two_hand_lab_patch.py"
            --self-test)
    # Python check: the held gun's profile is applied on the render thread, once, just before
    # SubmitPreparedFrame, and never by the title supervisor. --self-test runs first.
    add_test(NAME halomccvr_weapon_profile_thread_guard
        COMMAND "${Python3_EXECUTABLE}" -O
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_weapon_profile_thread.py"
            --self-test)
    # Python check: AutoVrTickOdst clears the scope only when ODST is not the active title (forcing it
    # off every frame reset the R3 toggle and marked the glass image stale). --self-test runs first.
    add_test(NAME halomccvr_odst_scope_clear_guard
        COMMAND "${Python3_EXECUTABLE}" -O
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_odst_scope_clear.py"
            --self-test)
    # Python check: the Menu resume is decided in vr.cpp on the raw Menu edge, above the exclusive
    # filter that clears Menu while the laser owns input.
    add_test(NAME halomccvr_menu_resume_site_guard
        COMMAND "${Python3_EXECUTABLE}" -O
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_menu_resume_site.py"
            --self-test)
    # Python check: Reach contact melee retires as its own hook set, from RemoveReachCameraCore before
    # the camera table, not as table rows.
    add_test(NAME halomccvr_reach_contact_retire_site_guard
        COMMAND "${Python3_EXECUTABLE}" -O
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_reach_contact_retire_site.py"
            --self-test)
    # Python check: the persistent support grip wiring stays intact: the config key stays default-off
    # with its parse/save branches; the applicability predicate is consulted by both the durable writer
    # and the assembly qualification; the PG-off latch branch keeps its base stores verbatim; the reticle
    # reseed and the owner-break invalidation pair stay single; no donor leftover or retired sway symbol
    # enters the new paths; the acquisition predicate stays gated behind the feature. --self-test runs first.
    add_test(NAME halomccvr_persistent_support_grip_guard
        COMMAND "${Python3_EXECUTABLE}" -O
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_persistent_support_grip.py"
            --self-test)
    set(HALOMCCVR_HAPTICS_FIXTURE "${HALOMCCVR_GENERATED_DIR}/haptics_runtime_functions.inl")
    add_custom_command(OUTPUT "${HALOMCCVR_HAPTICS_FIXTURE}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_haptics_runtime_fixture.py"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}" --output "${HALOMCCVR_HAPTICS_FIXTURE}"
        DEPENDS tools/generate_haptics_runtime_fixture.py src/dll/core/input.cpp src/dll/core/vr.cpp src/dll/core/vr_controllers.cpp
            src/dll/core/game.cpp src/dll/core/halo3_family_internal.h src/dll/core/vr_inline_ops.h src/dll/titles/ce/haloce_stereo_core.cpp
        VERBATIM)
    add_executable(halomccvr_haptics_runtime_tests tests/haptics_runtime_tests.cpp
        "${HALOMCCVR_HAPTICS_FIXTURE}" src/common/input_logic.cpp
        src/common/title_registry.cpp src/common/title_runtime_state.cpp
        src/common/config.cpp src/common/log.cpp)
    target_include_directories(halomccvr_haptics_runtime_tests PRIVATE
        "${HALOMCCVR_GENERATED_DIR}" "${openxr_SOURCE_DIR}/include")
    target_compile_definitions(halomccvr_haptics_runtime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_haptics_runtime_tests COMMAND halomccvr_haptics_runtime_tests)
    add_executable(halomccvr_dpad_input_tests tests/dpad_input_tests.cpp src/common/input_logic.cpp)
    target_include_directories(halomccvr_dpad_input_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_dpad_input_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_dpad_input_tests COMMAND halomccvr_dpad_input_tests)
    add_executable(halomccvr_weapon_interaction_tests tests/weapon_interaction_tests.cpp)
    add_executable(halomccvr_native_reload_tests tests/native_reload_policy_tests.cpp src/common/config.cpp)
    add_executable(halomccvr_native_vehicle_fp_tests tests/native_vehicle_first_person_tests.cpp src/common/config.cpp)
    target_include_directories(halomccvr_native_vehicle_fp_tests PRIVATE "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halomccvr_native_vehicle_fp_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_native_vehicle_fp_tests COMMAND halomccvr_native_vehicle_fp_tests)
    add_executable(halomccvr_odst_seat_lifetime_tests tests/odst_seat_lifetime_tests.cpp)
    target_compile_definitions(halomccvr_odst_seat_lifetime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_odst_seat_lifetime_tests COMMAND halomccvr_odst_seat_lifetime_tests)
    add_executable(halomccvr_halo3_seat_lifetime_tests tests/halo3_seat_lifetime_tests.cpp)
    target_compile_definitions(halomccvr_halo3_seat_lifetime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo3_seat_lifetime_tests COMMAND halomccvr_halo3_seat_lifetime_tests)
    add_executable(halomccvr_halo4_vehicle_input_tests tests/halo4_vehicle_input_tests.cpp)
    target_compile_definitions(halomccvr_halo4_vehicle_input_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo4_vehicle_input_tests COMMAND halomccvr_halo4_vehicle_input_tests)
    target_include_directories(halomccvr_native_reload_tests PRIVATE "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halomccvr_native_reload_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_native_reload_tests COMMAND halomccvr_native_reload_tests)
    add_test(NAME halomccvr_weapon_interaction_tests COMMAND halomccvr_weapon_interaction_tests)
    add_executable(halomccvr_weapon_accessory_tests tests/weapon_accessory_tests.cpp src/dll/core/weapon_accessory_renderer.cpp)
    target_compile_definitions(halomccvr_weapon_accessory_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_weapon_accessory_tests PRIVATE d3d11 d3dcompiler)
    add_test(NAME halomccvr_weapon_accessory_tests COMMAND halomccvr_weapon_accessory_tests)
    set(HALOMCCVR_RELOAD_IDENTITY_FIXTURE "${HALOMCCVR_GENERATED_DIR}/reload_identity_functions.inl")
    add_custom_command(OUTPUT "${HALOMCCVR_RELOAD_IDENTITY_FIXTURE}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_reload_identity_fixture.py"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}" --output "${HALOMCCVR_RELOAD_IDENTITY_FIXTURE}"
        DEPENDS tools/generate_reload_identity_fixture.py tools/generate_haptics_runtime_fixture.py
            src/dll/core/game.cpp src/dll/titles/h2/halo2_observer_6dof.cpp VERBATIM)
    add_executable(halomccvr_reload_identity_tests tests/reload_identity_tests.cpp "${HALOMCCVR_RELOAD_IDENTITY_FIXTURE}")
    target_include_directories(halomccvr_reload_identity_tests PRIVATE "${HALOMCCVR_GENERATED_DIR}")
    target_compile_definitions(halomccvr_reload_identity_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_reload_identity_tests COMMAND halomccvr_reload_identity_tests)
    set(HALOMCCVR_DPAD_ACTION_FIXTURE "${HALOMCCVR_GENERATED_DIR}/dpad_action_functions.inl")
    add_custom_command(OUTPUT "${HALOMCCVR_DPAD_ACTION_FIXTURE}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_dpad_action_fixture.py"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}" --output "${HALOMCCVR_DPAD_ACTION_FIXTURE}"
        DEPENDS tools/generate_dpad_action_fixture.py tools/generate_haptics_runtime_fixture.py src/dll/core/vr.cpp src/dll/core/vr_controllers.cpp
        VERBATIM)
    add_executable(halomccvr_dpad_action_tests tests/dpad_action_tests.cpp "${HALOMCCVR_DPAD_ACTION_FIXTURE}"
        src/common/config.cpp src/common/log.cpp)
    target_include_directories(halomccvr_dpad_action_tests PRIVATE "${HALOMCCVR_GENERATED_DIR}" "${openxr_SOURCE_DIR}/include")
    target_compile_definitions(halomccvr_dpad_action_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_dpad_action_tests COMMAND halomccvr_dpad_action_tests)
    set(HALOMCCVR_GRIP_POSE_CAPTURE_FIXTURE "${HALOMCCVR_GENERATED_DIR}/grip_pose_capture_functions.inl")
    add_custom_command(OUTPUT "${HALOMCCVR_GRIP_POSE_CAPTURE_FIXTURE}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_grip_pose_fixture.py"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}" --output "${HALOMCCVR_GRIP_POSE_CAPTURE_FIXTURE}"
        DEPENDS tools/generate_grip_pose_fixture.py tools/generate_haptics_runtime_fixture.py src/dll/core/vr.cpp src/dll/core/vr_controllers.cpp
        VERBATIM)
    add_executable(halomccvr_grip_pose_capture_tests tests/grip_pose_capture_tests.cpp
        "${HALOMCCVR_GRIP_POSE_CAPTURE_FIXTURE}")
    target_include_directories(halomccvr_grip_pose_capture_tests PRIVATE
        "${HALOMCCVR_GENERATED_DIR}" "${openxr_SOURCE_DIR}/include" src/common)
    target_compile_definitions(halomccvr_grip_pose_capture_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_grip_pose_capture_tests COMMAND halomccvr_grip_pose_capture_tests)
    set(HALOMCCVR_AIM_POSE_FIXTURE "${HALOMCCVR_GENERATED_DIR}/aim_pose_functions.inl")
    add_custom_command(OUTPUT "${HALOMCCVR_AIM_POSE_FIXTURE}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_aim_pose_fixture.py"
            --source-root "${CMAKE_CURRENT_SOURCE_DIR}" --output "${HALOMCCVR_AIM_POSE_FIXTURE}"
        DEPENDS tools/generate_aim_pose_fixture.py tools/generate_haptics_runtime_fixture.py
            src/common/virtual_stock_logic.h src/common/virtual_stock_diagnostic.h
            src/common/virtual_stock_settings.h src/common/virtual_stock_test_profiles.h
            src/common/virtual_stock_test_profiles_experiments.inc
            src/common/config.h src/dll/core/aim_pose_trace.h src/dll/core/vr.cpp src/dll/core/vr_controllers.cpp
            src/dll/core/vr_internal.h
            src/dll/core/virtual_stock_aim.inl
        VERBATIM)
    add_executable(halomccvr_aim_pose_tests tests/aim_pose_tests.cpp
        "${HALOMCCVR_AIM_POSE_FIXTURE}")
    target_include_directories(halomccvr_aim_pose_tests PRIVATE
        "${HALOMCCVR_GENERATED_DIR}" "${openxr_SOURCE_DIR}/include" src/common)
    target_compile_definitions(halomccvr_aim_pose_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_aim_pose_tests COMMAND halomccvr_aim_pose_tests)
    # The XInput coherent-sample tests compile the same atomic packet,
    # seqlock reader, identity/freshness selector and solver-input mapping that
    # the shipping VR_GetAimPoseWithSupportProvenance path calls.
    add_executable(halomccvr_committed_aim_sample_tests
        tests/committed_aim_sample_tests.cpp "${HALOMCCVR_AIM_POSE_FIXTURE}")
    target_include_directories(halomccvr_committed_aim_sample_tests PRIVATE
        "${HALOMCCVR_GENERATED_DIR}" "${openxr_SOURCE_DIR}/include" src/common)
    target_compile_definitions(halomccvr_committed_aim_sample_tests PRIVATE
        NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_committed_aim_sample_tests
        COMMAND halomccvr_committed_aim_sample_tests)
    add_executable(halomccvr_aim_servo_identity_tests
        tests/aim_servo_identity_tests.cpp)
    target_include_directories(halomccvr_aim_servo_identity_tests PRIVATE
        src/common)
    target_compile_definitions(halomccvr_aim_servo_identity_tests PRIVATE
        NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_aim_servo_identity_tests
        COMMAND halomccvr_aim_servo_identity_tests)
    # Two-Handed Lab tranche 2A: live VS-off solver integration (runtime
    # settings snapshot, anchor matrix, offhand influence, soft agreement,
    # single-source D/S, diagnostics) against the same generated aim fixture.
    add_executable(halomccvr_two_hand_lab_solver_tests
        tests/two_hand_lab_solver_tests.cpp "${HALOMCCVR_AIM_POSE_FIXTURE}")
    target_include_directories(halomccvr_two_hand_lab_solver_tests PRIVATE
        "${HALOMCCVR_GENERATED_DIR}" "${openxr_SOURCE_DIR}/include" src/common)
    target_compile_definitions(halomccvr_two_hand_lab_solver_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_two_hand_lab_solver_tests COMMAND halomccvr_two_hand_lab_solver_tests)
    # Two-Handed Lab tranche 2B: live temporal experiment modes (production
    # fragment against the generated aim fixture) — Continuity200ms latch
    # law, constant/adaptive damping, mode exclusivity, resets, fail-open.
    add_executable(halomccvr_two_hand_lab_temporal_tests
        tests/two_hand_lab_temporal_tests.cpp "${HALOMCCVR_AIM_POSE_FIXTURE}")
    target_include_directories(halomccvr_two_hand_lab_temporal_tests PRIVATE
        "${HALOMCCVR_GENERATED_DIR}" "${openxr_SOURCE_DIR}/include" src/common)
    target_compile_definitions(halomccvr_two_hand_lab_temporal_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_two_hand_lab_temporal_tests COMMAND halomccvr_two_hand_lab_temporal_tests)
    set(HALOMCCVR_TELEMETRY_TEST_JSONL
        "${HALOMCCVR_GENERATED_DIR}/telemetry_recorder_test.jsonl")
    add_executable(halomccvr_telemetry_tests
        tests/telemetry_recorder_tests.cpp
        "${HALOMCCVR_AIM_POSE_FIXTURE}"
        src/dll/core/telemetry_recorder.cpp
        src/common/log.cpp)
    set(HALOMCCVR_TELEMETRY_ANALYSER_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/telemetry_analyser/analyse_mccvr_telemetry.py")
    set(HALOMCCVR_TELEMETRY_ANALYSER_SMOKE_DIR
        "${HALOMCCVR_GENERATED_DIR}/telemetry_analyser_smoke")
    set(HALOMCCVR_TELEMETRY_ANALYSER_SMOKE_SCRIPT
        "${HALOMCCVR_TELEMETRY_ANALYSER_SMOKE_DIR}/TelemetryAnalyser/analyse_mccvr_telemetry.py")
    add_custom_command(
        OUTPUT "${HALOMCCVR_TELEMETRY_ANALYSER_SMOKE_SCRIPT}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${HALOMCCVR_TELEMETRY_ANALYSER_SMOKE_DIR}/TelemetryAnalyser"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${HALOMCCVR_TELEMETRY_ANALYSER_SOURCE}"
            "${HALOMCCVR_TELEMETRY_ANALYSER_SMOKE_SCRIPT}"
        DEPENDS "${HALOMCCVR_TELEMETRY_ANALYSER_SOURCE}"
        VERBATIM)
    add_custom_target(halomccvr_telemetry_analyser_smoke_stage
        DEPENDS "${HALOMCCVR_TELEMETRY_ANALYSER_SMOKE_SCRIPT}")
    add_dependencies(halomccvr_telemetry_tests
        halomccvr_telemetry_analyser_smoke_stage)
    target_include_directories(halomccvr_telemetry_tests PRIVATE
        "${HALOMCCVR_GENERATED_DIR}" "${openxr_SOURCE_DIR}/include" src/common src/dll)
    target_compile_definitions(halomccvr_telemetry_tests PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
        HALOMCCVR_TELEMETRY_TESTING)
    add_test(NAME halomccvr_telemetry_tests
        COMMAND halomccvr_telemetry_tests "${HALOMCCVR_TELEMETRY_TEST_JSONL}")
    set_tests_properties(halomccvr_telemetry_tests PROPERTIES
        FIXTURES_SETUP halomccvr_telemetry_jsonl)
    # Python check: the recorder's JSONL output matches the telemetry schema and fixture.
    add_test(NAME halomccvr_telemetry_jsonl_tests
        COMMAND "${Python3_EXECUTABLE}" -O
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_telemetry_jsonl.py"
            "${HALOMCCVR_TELEMETRY_TEST_JSONL}"
            --expect-fixture --self-test)
    set_tests_properties(halomccvr_telemetry_jsonl_tests PROPERTIES
        FIXTURES_REQUIRED halomccvr_telemetry_jsonl)
    add_test(NAME halomccvr_telemetry_analyser_smoke
        COMMAND halomccvr_telemetry_tests --analyser-smoke
            "${HALOMCCVR_TELEMETRY_ANALYSER_SMOKE_DIR}"
            "${HALOMCCVR_TELEMETRY_TEST_JSONL}")
    set_tests_properties(halomccvr_telemetry_analyser_smoke PROPERTIES
        FIXTURES_REQUIRED halomccvr_telemetry_jsonl)
    add_executable(halomccvr_left_hand_h2_tests tests/left_hand_h2_tests.cpp)
    target_include_directories(halomccvr_left_hand_h2_tests PRIVATE src/common)
    add_test(NAME halomccvr_left_hand_h2_tests COMMAND halomccvr_left_hand_h2_tests)
    add_executable(halomccvr_left_hand_shared_tests tests/left_hand_shared_tests.cpp)
    target_include_directories(halomccvr_left_hand_shared_tests PRIVATE src/common)
    add_test(NAME halomccvr_left_hand_shared_tests COMMAND halomccvr_left_hand_shared_tests)
    add_executable(halomccvr_ce_render_tests tests/haloce_render_tests.cpp)
    target_include_directories(halomccvr_ce_render_tests PRIVATE src/common)
    add_test(NAME halomccvr_ce_render_tests COMMAND halomccvr_ce_render_tests)
    add_executable(halomccvr_ce_view_pair_tests tests/haloce_view_pair_tests.cpp)
    target_include_directories(halomccvr_ce_view_pair_tests PRIVATE src/common)
    add_test(NAME halomccvr_ce_view_pair_tests COMMAND halomccvr_ce_view_pair_tests)
    add_executable(halomccvr_ce_classic_view_pair_tests tests/haloce_classic_view_pair_tests.cpp)
    target_include_directories(halomccvr_ce_classic_view_pair_tests PRIVATE src/common)
    add_test(NAME halomccvr_ce_classic_view_pair_tests COMMAND halomccvr_ce_classic_view_pair_tests)
    add_executable(halomccvr_ce_first_person_tests tests/haloce_first_person_tests.cpp src/dll/core/ik.cpp)
    target_include_directories(halomccvr_ce_first_person_tests PRIVATE src/common)
    add_test(NAME halomccvr_ce_first_person_tests COMMAND halomccvr_ce_first_person_tests)
    add_executable(halomccvr_ce_contact_tests tests/haloce_contact_tests.cpp src/dll/core/ik.cpp)
    target_include_directories(halomccvr_ce_contact_tests PRIVATE src/common)
    add_test(NAME halomccvr_ce_contact_tests COMMAND halomccvr_ce_contact_tests)
    add_executable(halomccvr_ce_contact_runtime_tests tests/haloce_contact_runtime_tests.cpp
        src/dll/titles/ce/haloce_native_bindings.cpp src/dll/core/ik.cpp)
    target_include_directories(halomccvr_ce_contact_runtime_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_ce_contact_runtime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_contact_runtime_tests PRIVATE minhook)
    add_test(NAME halomccvr_ce_contact_runtime_tests COMMAND halomccvr_ce_contact_runtime_tests)
    # Runs through tools/re/test_ce_fp_shader_native.py with locally extracted,
    # hash-pinned CE shader bytecode. Proprietary fixtures are not in the repo.
    add_executable(halomccvr_ce_first_person_shader_tests tests/haloce_first_person_shader_tests.cpp)
    target_compile_definitions(halomccvr_ce_first_person_shader_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_first_person_shader_tests PRIVATE d3d11 d3dcompiler)
    # External, pinned mesh/sprite particle shaders; see CE muzzle evidence.
    add_executable(halomccvr_ce_particle_shader_tests tests/haloce_particle_shader_tests.cpp)
    target_compile_definitions(halomccvr_ce_particle_shader_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_particle_shader_tests PRIVATE d3d11 d3dcompiler)
    add_executable(halomccvr_ce_bindings_tests
        tests/haloce_bindings_tests.cpp src/dll/titles/ce/haloce_native_bindings.cpp)
    target_include_directories(halomccvr_ce_bindings_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_ce_bindings_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_bindings_tests PRIVATE minhook)
    add_test(NAME halomccvr_ce_bindings_tests COMMAND halomccvr_ce_bindings_tests)
    add_executable(halomccvr_ce_eye_cache_tests
        tests/haloce_eye_cache_tests.cpp src/dll/titles/ce/haloce_eye_cache.cpp)
    target_include_directories(halomccvr_ce_eye_cache_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_ce_eye_cache_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_eye_cache_tests PRIVATE d3d11)
    add_test(NAME halomccvr_ce_eye_cache_tests COMMAND halomccvr_ce_eye_cache_tests)
    add_executable(halomccvr_ce_desktop_mirror_tests tests/haloce_desktop_mirror_tests.cpp)
    target_compile_definitions(halomccvr_ce_desktop_mirror_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_desktop_mirror_tests PRIVATE d3d11 d3dcompiler)
    add_test(NAME halomccvr_ce_desktop_mirror_tests COMMAND halomccvr_ce_desktop_mirror_tests)
    add_executable(halomccvr_ce_runtime_tests tests/haloce_runtime_tests.cpp
        src/dll/titles/ce/haloce_native_bindings.cpp src/dll/titles/ce/haloce_eye_cache.cpp src/dll/titles/ce/haloce_hud_target.cpp tests/haloce_hud_layout_runtime_fixture.cpp)
    target_include_directories(halomccvr_ce_runtime_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_ce_runtime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_runtime_tests PRIVATE d3d11 d3dcompiler minhook)
    add_test(NAME halomccvr_ce_runtime_tests COMMAND halomccvr_ce_runtime_tests)
    add_executable(halomccvr_ce_classic_runtime_tests tests/haloce_classic_runtime_tests.cpp
        src/dll/titles/ce/haloce_native_bindings.cpp src/dll/titles/ce/haloce_eye_cache.cpp src/dll/titles/ce/haloce_hud_target.cpp src/dll/titles/ce/haloce_hud_layout.cpp)
    target_include_directories(halomccvr_ce_classic_runtime_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_ce_classic_runtime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_classic_runtime_tests PRIVATE d3d11 minhook)
    add_test(NAME halomccvr_ce_classic_runtime_tests COMMAND halomccvr_ce_classic_runtime_tests)
    add_executable(halomccvr_ce_hud_tests tests/haloce_hud_tests.cpp src/dll/titles/ce/haloce_native_bindings.cpp)
    target_include_directories(halomccvr_ce_hud_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_ce_hud_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_hud_tests PRIVATE minhook d3d11)
    add_test(NAME halomccvr_ce_hud_tests COMMAND halomccvr_ce_hud_tests)
    add_executable(halomccvr_ce_hud_layout_tests tests/haloce_hud_layout_tests.cpp src/dll/titles/ce/haloce_native_bindings.cpp)
    target_include_directories(halomccvr_ce_hud_layout_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_ce_hud_layout_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_hud_layout_tests PRIVATE minhook d3d11)
    add_test(NAME halomccvr_ce_hud_layout_tests COMMAND halomccvr_ce_hud_layout_tests)
    add_executable(halomccvr_ce_reticle_redirect_tests tests/haloce_reticle_redirect_tests.cpp)
    target_compile_definitions(halomccvr_ce_reticle_redirect_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_reticle_redirect_tests PRIVATE d3d11)
    add_test(NAME halomccvr_ce_reticle_redirect_tests COMMAND halomccvr_ce_reticle_redirect_tests)
    add_executable(halomccvr_ce_reticle_pixels_tests tests/haloce_reticle_pixels_tests.cpp)
    target_compile_definitions(halomccvr_ce_reticle_pixels_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_reticle_pixels_tests PRIVATE d3d11 d3dcompiler)
    add_test(NAME halomccvr_ce_reticle_pixels_tests COMMAND halomccvr_ce_reticle_pixels_tests)
    add_executable(halomccvr_ce_hud_target_tests tests/haloce_hud_target_tests.cpp)
    target_include_directories(halomccvr_ce_hud_target_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_ce_hud_target_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_hud_target_tests PRIVATE d3d11)
    add_test(NAME halomccvr_ce_hud_target_tests COMMAND halomccvr_ce_hud_target_tests)
    add_executable(halomccvr_ce_controls_tests tests/haloce_controls_tests.cpp)
    target_include_directories(halomccvr_ce_controls_tests PRIVATE src)
    add_test(NAME halomccvr_ce_controls_tests COMMAND halomccvr_ce_controls_tests)
    add_executable(halomccvr_ce_controls_runtime_tests tests/haloce_controls_runtime_tests.cpp
        src/dll/core/physical_crouch_camera.cpp src/dll/titles/ce/haloce_native_bindings.cpp)
    target_include_directories(halomccvr_ce_controls_runtime_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_ce_controls_runtime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_controls_runtime_tests PRIVATE minhook)
    add_test(NAME halomccvr_ce_controls_runtime_tests COMMAND halomccvr_ce_controls_runtime_tests)
    add_executable(halomccvr_ce_comfort_tests tests/haloce_comfort_tests.cpp src/dll/titles/ce/haloce_native_bindings.cpp)
    target_include_directories(halomccvr_ce_comfort_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_ce_comfort_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_comfort_tests PRIVATE minhook)
    add_test(NAME halomccvr_ce_comfort_tests COMMAND halomccvr_ce_comfort_tests)
    add_executable(halomccvr_ce_orientation_runtime_tests tests/haloce_orientation_runtime_tests.cpp
        src/dll/titles/ce/haloce_native_bindings.cpp)
    target_include_directories(halomccvr_ce_orientation_runtime_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_ce_orientation_runtime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_orientation_runtime_tests PRIVATE minhook)
    add_test(NAME halomccvr_ce_orientation_runtime_tests COMMAND halomccvr_ce_orientation_runtime_tests)
    add_executable(halomccvr_ce_unit_control_runtime_tests tests/haloce_unit_control_runtime_tests.cpp)
    target_include_directories(halomccvr_ce_unit_control_runtime_tests PRIVATE src/common src/dll "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halomccvr_ce_unit_control_runtime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_ce_unit_control_runtime_tests COMMAND halomccvr_ce_unit_control_runtime_tests)
    add_executable(halomccvr_ce_network_input_tests tests/haloce_network_input_tests.cpp)
    target_include_directories(halomccvr_ce_network_input_tests PRIVATE src/common src/dll "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halomccvr_ce_network_input_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_ce_network_input_tests COMMAND halomccvr_ce_network_input_tests)
    add_executable(halomccvr_ce_first_person_runtime_tests tests/haloce_first_person_runtime_tests.cpp
        src/dll/titles/ce/haloce_native_bindings.cpp src/dll/core/ik.cpp)
    target_include_directories(halomccvr_ce_first_person_runtime_tests PRIVATE src/common src/dll)
    target_compile_definitions(halomccvr_ce_first_person_runtime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(halomccvr_ce_first_person_runtime_tests PRIVATE minhook)
    add_test(NAME halomccvr_ce_first_person_runtime_tests COMMAND halomccvr_ce_first_person_runtime_tests)
    add_executable(halomccvr_ce_muzzle_lifecycle_tests tests/haloce_muzzle_lifecycle_tests.cpp)
    target_include_directories(halomccvr_ce_muzzle_lifecycle_tests PRIVATE ${minhook_SOURCE_DIR}/include)
    target_compile_definitions(halomccvr_ce_muzzle_lifecycle_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_ce_muzzle_lifecycle_tests COMMAND halomccvr_ce_muzzle_lifecycle_tests)
    add_executable(halomccvr_reach_display_admission_tests tests/reach_display_admission_tests.cpp)
    add_test(NAME halomccvr_reach_display_admission_tests COMMAND halomccvr_reach_display_admission_tests)
    add_executable(manual_vr_recovery_tests tests/manual_vr_recovery_tests.cpp)
    target_compile_features(manual_vr_recovery_tests PRIVATE cxx_std_20)
    add_test(NAME manual_vr_recovery COMMAND manual_vr_recovery_tests)
    add_executable(halomccvr_title_reentry_probe_tests tests/title_reentry_probe_tests.cpp src/common/title_registry.cpp)
    target_compile_definitions(halomccvr_title_reentry_probe_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_title_reentry_probe_tests COMMAND halomccvr_title_reentry_probe_tests)
    add_executable(halomccvr_halo2_anniversary_cleanup_tests tests/halo2_anniversary_cleanup_tests.cpp)
    target_link_libraries(halomccvr_halo2_anniversary_cleanup_tests PRIVATE minhook)
    target_compile_definitions(halomccvr_halo2_anniversary_cleanup_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo2_anniversary_cleanup_tests COMMAND halomccvr_halo2_anniversary_cleanup_tests)
    add_executable(halomccvr_halo2_observer_cleanup_tests tests/halo2_observer_cleanup_tests.cpp)
    target_include_directories(halomccvr_halo2_observer_cleanup_tests PRIVATE "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halomccvr_halo2_observer_cleanup_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo2_observer_cleanup_tests COMMAND halomccvr_halo2_observer_cleanup_tests)
    add_executable(halomccvr_hud_extra_draws_tests tests/hud_extra_draws_tests.cpp)
    target_include_directories(halomccvr_hud_extra_draws_tests PRIVATE "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halomccvr_hud_extra_draws_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_hud_extra_draws_tests COMMAND halomccvr_hud_extra_draws_tests)

    add_executable(halomccvr_halo3_independent_shots_tests tests/halo3_independent_shots_tests.cpp)
    add_executable(halomccvr_halo3_weapon_haptics_tests tests/halo3_weapon_haptics_tests.cpp)
    add_executable(halo3_avatar_logic_tests tests/halo3_avatar_logic_tests.cpp)
    target_compile_definitions(halo3_avatar_logic_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halo3_avatar_logic COMMAND halo3_avatar_logic_tests)
    add_executable(halo3_avatar_runtime_tests tests/halo3_avatar_runtime_tests.cpp)
    target_compile_definitions(halo3_avatar_runtime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(halo3_avatar_runtime_tests PRIVATE /W4 /EHsc)
    add_test(NAME halo3_avatar_runtime COMMAND halo3_avatar_runtime_tests)
    add_executable(odst_finger_pose_tests tests/odst_finger_pose_tests.cpp)
    target_compile_definitions(odst_finger_pose_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(odst_finger_pose_tests PRIVATE /W4 /EHsc)
    add_test(NAME odst_finger_pose COMMAND odst_finger_pose_tests)
    add_test(NAME halomccvr_halo3_weapon_haptics_tests COMMAND halomccvr_halo3_weapon_haptics_tests)
    add_executable(halomccvr_halo3_weapon_haptics_backend_tests tests/halo3_weapon_haptics_backend_tests.cpp)
    target_compile_definitions(halomccvr_halo3_weapon_haptics_backend_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(halomccvr_halo3_weapon_haptics_backend_tests PRIVATE /W4 /EHsc)
    add_test(NAME halomccvr_halo3_weapon_haptics_backend COMMAND halomccvr_halo3_weapon_haptics_backend_tests)
    add_executable(halomccvr_halo3_collision_dispatch_tests tests/halo3_collision_dispatch_tests.cpp)
    add_executable(halomccvr_native_fault_probe_tests tests/native_fault_probe_tests.cpp)
    target_compile_definitions(halomccvr_halo3_collision_dispatch_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_definitions(halomccvr_native_fault_probe_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo3_collision_dispatch_tests COMMAND halomccvr_halo3_collision_dispatch_tests)
    add_test(NAME halomccvr_native_fault_probe_tests COMMAND halomccvr_native_fault_probe_tests)
    target_include_directories(halomccvr_halo3_independent_shots_tests PRIVATE "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halomccvr_halo3_independent_shots_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_options(halomccvr_halo3_independent_shots_tests PRIVATE /MAP)
    add_test(NAME halomccvr_halo3_independent_shots_tests COMMAND halomccvr_halo3_independent_shots_tests)
    add_executable(halomccvr_halo2_independent_shots_tests tests/halo2_independent_shots_tests.cpp)
    target_include_directories(halomccvr_halo2_independent_shots_tests PRIVATE "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halomccvr_halo2_independent_shots_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo2_independent_shots_tests COMMAND halomccvr_halo2_independent_shots_tests)
    add_executable(halomccvr_halo2_haptic_runtime_tests tests/halo2_haptic_runtime_tests.cpp)
    target_compile_definitions(halomccvr_halo2_haptic_runtime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo2_haptic_runtime_tests COMMAND halomccvr_halo2_haptic_runtime_tests)
    add_executable(halo4_weapon_haptics_tests tests/halo4_weapon_haptics_tests.cpp)
    target_compile_definitions(halo4_weapon_haptics_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(halo4_weapon_haptics_tests PRIVATE /W4 /EHsc)
    add_test(NAME halo4_weapon_haptics COMMAND halo4_weapon_haptics_tests)
    add_executable(reach_weapon_haptics_logic_tests tests/reach_weapon_haptics_logic_tests.cpp)
    add_test(NAME reach_weapon_haptics_logic COMMAND reach_weapon_haptics_logic_tests)
    add_executable(halomccvr_reach_finger_pose_tests tests/reach_finger_pose_tests.cpp)
    target_compile_features(halomccvr_reach_finger_pose_tests PRIVATE cxx_std_20)
    add_test(NAME halomccvr_reach_finger_pose_tests COMMAND halomccvr_reach_finger_pose_tests)
    add_executable(halo4_body_packet_backend_tests tests/halo4_body_packet_backend_tests.cpp)
    target_compile_features(halo4_body_packet_backend_tests PRIVATE cxx_std_20)
    add_test(NAME halo4_body_packet_backend_tests COMMAND halo4_body_packet_backend_tests)
    add_executable(odst_weapon_haptics_tests tests/odst_weapon_haptics_tests.cpp)
    target_compile_definitions(odst_weapon_haptics_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(odst_weapon_haptics_tests PRIVATE /W4 /EHsc)
    add_test(NAME odst_weapon_haptics COMMAND odst_weapon_haptics_tests)
    # Golden fixtures: pin each title's current hook lifecycle so a shared fold must match it exactly.
    add_executable(odst_weapon_haptics_lifecycle_golden_tests tests/odst_weapon_haptics_lifecycle_golden_tests.cpp src/dll/core/sigscan.cpp)
    target_include_directories(odst_weapon_haptics_lifecycle_golden_tests PRIVATE "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(odst_weapon_haptics_lifecycle_golden_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_features(odst_weapon_haptics_lifecycle_golden_tests PRIVATE cxx_std_20)
    add_test(NAME odst_weapon_haptics_lifecycle_golden COMMAND odst_weapon_haptics_lifecycle_golden_tests)
    add_executable(halo4_weapon_haptics_lifecycle_golden_tests tests/halo4_weapon_haptics_lifecycle_golden_tests.cpp src/dll/core/sigscan.cpp)
    target_include_directories(halo4_weapon_haptics_lifecycle_golden_tests PRIVATE "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halo4_weapon_haptics_lifecycle_golden_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_features(halo4_weapon_haptics_lifecycle_golden_tests PRIVATE cxx_std_20)
    add_test(NAME halo4_weapon_haptics_lifecycle_golden COMMAND halo4_weapon_haptics_lifecycle_golden_tests)
    add_executable(reach_weapon_haptics_lifecycle_golden_tests tests/reach_weapon_haptics_lifecycle_golden_tests.cpp src/dll/core/sigscan.cpp)
    target_include_directories(reach_weapon_haptics_lifecycle_golden_tests PRIVATE "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(reach_weapon_haptics_lifecycle_golden_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_features(reach_weapon_haptics_lifecycle_golden_tests PRIVATE cxx_std_20)
    add_test(NAME reach_weapon_haptics_lifecycle_golden COMMAND reach_weapon_haptics_lifecycle_golden_tests)
    add_executable(halo3_weapon_haptics_lifecycle_golden_tests tests/halo3_weapon_haptics_lifecycle_golden_tests.cpp src/dll/core/sigscan.cpp)
    target_include_directories(halo3_weapon_haptics_lifecycle_golden_tests PRIVATE "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halo3_weapon_haptics_lifecycle_golden_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_features(halo3_weapon_haptics_lifecycle_golden_tests PRIVATE cxx_std_20)
    add_test(NAME halo3_weapon_haptics_lifecycle_golden COMMAND halo3_weapon_haptics_lifecycle_golden_tests)
    add_executable(halo2_weapon_haptics_lifecycle_golden_tests tests/halo2_weapon_haptics_lifecycle_golden_tests.cpp src/dll/core/sigscan.cpp)
    target_include_directories(halo2_weapon_haptics_lifecycle_golden_tests PRIVATE "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halo2_weapon_haptics_lifecycle_golden_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_features(halo2_weapon_haptics_lifecycle_golden_tests PRIVATE cxx_std_20)
    add_test(NAME halo2_weapon_haptics_lifecycle_golden COMMAND halo2_weapon_haptics_lifecycle_golden_tests)
    add_executable(halo3_contact_melee_runtime_golden_tests tests/halo3_contact_melee_runtime_golden_tests.cpp src/dll/core/sigscan.cpp)
    target_include_directories(halo3_contact_melee_runtime_golden_tests PRIVATE "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halo3_contact_melee_runtime_golden_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_features(halo3_contact_melee_runtime_golden_tests PRIVATE cxx_std_20)
    add_test(NAME halo3_contact_melee_runtime_golden COMMAND halo3_contact_melee_runtime_golden_tests)
    add_executable(odst_contact_melee_runtime_golden_tests tests/odst_contact_melee_runtime_golden_tests.cpp src/dll/core/sigscan.cpp)
    target_include_directories(odst_contact_melee_runtime_golden_tests PRIVATE "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(odst_contact_melee_runtime_golden_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_features(odst_contact_melee_runtime_golden_tests PRIVATE cxx_std_20)
    add_test(NAME odst_contact_melee_runtime_golden COMMAND odst_contact_melee_runtime_golden_tests)
    add_executable(halo2_contact_melee_runtime_golden_tests tests/halo2_contact_melee_runtime_golden_tests.cpp src/dll/core/sigscan.cpp)
    target_include_directories(halo2_contact_melee_runtime_golden_tests PRIVATE "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halo2_contact_melee_runtime_golden_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_features(halo2_contact_melee_runtime_golden_tests PRIVATE cxx_std_20)
    add_test(NAME halo2_contact_melee_runtime_golden COMMAND halo2_contact_melee_runtime_golden_tests)
    add_executable(halo4_contact_melee_runtime_golden_tests tests/halo4_contact_melee_runtime_golden_tests.cpp src/dll/core/sigscan.cpp)
    target_include_directories(halo4_contact_melee_runtime_golden_tests PRIVATE "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(halo4_contact_melee_runtime_golden_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_features(halo4_contact_melee_runtime_golden_tests PRIVATE cxx_std_20)
    add_test(NAME halo4_contact_melee_runtime_golden COMMAND halo4_contact_melee_runtime_golden_tests)
    add_executable(reach_contact_melee_runtime_golden_tests tests/reach_contact_melee_runtime_golden_tests.cpp src/dll/core/sigscan.cpp)
    target_include_directories(reach_contact_melee_runtime_golden_tests PRIVATE "${minhook_SOURCE_DIR}/include")
    target_compile_definitions(reach_contact_melee_runtime_golden_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_features(reach_contact_melee_runtime_golden_tests PRIVATE cxx_std_20)
    add_test(NAME reach_contact_melee_runtime_golden COMMAND reach_contact_melee_runtime_golden_tests)
    add_executable(halomccvr_weapon_muzzle_tests tests/weapon_muzzle_tests.cpp)
    add_test(NAME halomccvr_weapon_muzzle_tests COMMAND halomccvr_weapon_muzzle_tests)
    add_executable(halomccvr_odst_muzzle_tests tests/odst_muzzle_tests.cpp)
    add_executable(halomccvr_odst_scope_tests tests/odst_scope_tests.cpp src/common/scope_logic.cpp)
    target_compile_definitions(halomccvr_odst_scope_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_odst_scope_tests COMMAND halomccvr_odst_scope_tests)
    add_executable(halomccvr_reach_scope_tests tests/reach_scope_tests.cpp src/common/scope_logic.cpp)
    target_compile_definitions(halomccvr_reach_scope_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_reach_scope_tests COMMAND halomccvr_reach_scope_tests)
    add_executable(halomccvr_halo4_scope_tests tests/halo4_scope_tests.cpp src/common/scope_logic.cpp)
    target_compile_definitions(halomccvr_halo4_scope_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo4_scope_tests COMMAND halomccvr_halo4_scope_tests)
    add_executable(halomccvr_halo2_scope_tests tests/halo2_scope_tests.cpp src/common/scope_logic.cpp)
    target_compile_definitions(halomccvr_halo2_scope_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo2_scope_tests COMMAND halomccvr_halo2_scope_tests)
    add_executable(halomccvr_reach_muzzle_tests tests/reach_muzzle_tests.cpp)
    add_executable(halomccvr_halo4_muzzle_tests tests/halo4_muzzle_tests.cpp)
    target_compile_definitions(halomccvr_halo4_muzzle_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_include_directories(halomccvr_halo4_muzzle_tests PRIVATE ${minhook_SOURCE_DIR}/include)
    add_test(NAME halomccvr_halo4_muzzle_tests COMMAND halomccvr_halo4_muzzle_tests)
    add_executable(halomccvr_visual_hand_offset_tests tests/visual_hand_offset_tests.cpp src/common/config.cpp src/common/log.cpp src/dll/core/ik.cpp)
    target_compile_definitions(halomccvr_visual_hand_offset_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_visual_hand_offset_tests COMMAND halomccvr_visual_hand_offset_tests)
    add_executable(halomccvr_vehicle_turn_tests tests/vehicle_turn_tests.cpp src/common/config.cpp src/common/log.cpp)
    target_compile_definitions(halomccvr_vehicle_turn_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_vehicle_turn_tests COMMAND halomccvr_vehicle_turn_tests)
    target_compile_definitions(halomccvr_reach_muzzle_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_include_directories(halomccvr_reach_muzzle_tests PRIVATE ${minhook_SOURCE_DIR}/include)
    add_test(NAME halomccvr_reach_muzzle_tests COMMAND halomccvr_reach_muzzle_tests)
    target_compile_definitions(halomccvr_odst_muzzle_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_include_directories(halomccvr_odst_muzzle_tests PRIVATE ${minhook_SOURCE_DIR}/include)
    add_test(NAME halomccvr_odst_muzzle_tests COMMAND halomccvr_odst_muzzle_tests)
    add_executable(halomccvr_halo3_muzzle_publication_tests tests/halo3_muzzle_publication_tests.cpp)
    target_compile_definitions(halomccvr_halo3_muzzle_publication_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_halo3_muzzle_publication_tests COMMAND halomccvr_halo3_muzzle_publication_tests)
    add_executable(halomccvr_menu_slider_tests tests/menu_slider_tests.cpp)
    target_link_libraries(halomccvr_menu_slider_tests PRIVATE imgui_lib)
    add_test(NAME halomccvr_menu_slider_tests COMMAND halomccvr_menu_slider_tests)
    add_executable(halomccvr_game_menu_pointer_tests tests/game_menu_pointer_tests.cpp src/common/config.cpp)
    target_include_directories(halomccvr_game_menu_pointer_tests PRIVATE src/common src/dll "${openxr_SOURCE_DIR}/include")
    target_compile_definitions(halomccvr_game_menu_pointer_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    add_test(NAME halomccvr_game_menu_pointer_tests COMMAND halomccvr_game_menu_pointer_tests)
endif()

# The DLL, the launcher and the tools compile at /W4, and every target
# built from this repo's sources treats a compiler warning as an error, in
# every configuration. imgui_lib is third-party source and keeps its defaults.
foreach(halomccvr_target IN ITEMS mjolnirvr mjolnirvr_launcher camscan
        reach_runtime_observer menu_style_preview dlss_gpu_smoke)
    if(TARGET ${halomccvr_target})
        target_compile_options(${halomccvr_target} PRIVATE $<$<COMPILE_LANGUAGE:C,CXX>:/W4>)
    endif()
endforeach()
get_property(halomccvr_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
# Every other executable is a test, and compiles at /W4 too (E3). Third-party
# headers (angle-bracket includes) are external and keep quiet on tests only.
foreach(halomccvr_target IN LISTS halomccvr_targets)
    get_target_property(halomccvr_type ${halomccvr_target} TYPE)
    if(halomccvr_type STREQUAL "EXECUTABLE" AND NOT halomccvr_target MATCHES
            "^(mjolnirvr_launcher|camscan|reach_runtime_observer|menu_style_preview|dlss_gpu_smoke)$")
        target_compile_options(${halomccvr_target} PRIVATE
            "$<$<COMPILE_LANGUAGE:C,CXX>:/W4;/external:anglebrackets;/external:W0>")
    endif()
endforeach()
foreach(halomccvr_target IN LISTS halomccvr_targets)
    get_target_property(halomccvr_type ${halomccvr_target} TYPE)
    if(halomccvr_type MATCHES "^(EXECUTABLE|SHARED_LIBRARY|STATIC_LIBRARY)$" AND
            NOT halomccvr_target STREQUAL "imgui_lib")
        target_compile_options(${halomccvr_target} PRIVATE $<$<COMPILE_LANGUAGE:C,CXX>:/WX>)
    endif()
endforeach()

# Stage an unaccepted local candidate only. Release-specific hashes and metadata
# must never be copied into a newly rebuilt package.
install(TARGETS mjolnirvr
    RUNTIME DESTINATION .
    COMPONENT dist)
install(TARGETS mjolnirvr_launcher
    RUNTIME DESTINATION .
    COMPONENT launcher)
install(FILES
    LICENSE
    installer/MANUAL-README.txt
    installer/THIRD-PARTY-LICENSES.txt
    DESTINATION .
    COMPONENT dist)
install(DIRECTORY src/launcher/assets/ DESTINATION assets COMPONENT dist)
install(FILES
    tools/telemetry_analyser/analyse_mccvr_telemetry.py
    DESTINATION TelemetryAnalyser
    COMPONENT dist)
