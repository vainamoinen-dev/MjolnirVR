// core_test_support.h: the includes, the Check() harness (with the check and unit counters) and the shared
// helpers of the core test executables (tests/core_*_tests.cpp), moved unchanged from core_tests.cpp. Each
// executable includes it once; the harness is in an anonymous namespace, so each has its own counters.
#pragma once
#include "../src/common/weapon_profiles.h"
#include "../src/common/dual_reticle_logic.h"
#include "../src/common/titles/reach/reach_wind_replay.h"
int RunRoomscaleInputTests();
#include "../src/common/roomscale_logic.h"
#include "../src/common/controller_finger_input.h"
#include "../src/common/vr_interaction_refinement_logic.h"
#include "../src/common/menu_slider_logic.h"
#include "../src/common/anatomical_palette_logic.h"
#include "../src/common/titles/h4/halo4_runtime_weapon_bounds.h"
#include "../src/common/legacy_runtime_weapon_bounds.h"
#include "../src/common/titles/h2/halo2_snap_turn_logic.h"
#include "../src/common/titles/h2/halo2_vehicle_view.h"
#include "../src/common/titles/h2/halo2_datum_logic.h"
#include "../src/common/titles/h3/halo3_melee_selection_logic.h"
#include "../src/common/weapon_order_diagnostic_logic.h"
#include "../src/common/contact_melee_motion.h"
#include "../src/dll/core/contact_melee_queue.h"
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <iostream>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <windows.h>

#include "aim_servo_logic.h"
#include "config.h"
#include "log.h"
#include "visor_zoom_logic.h"
#include "installer_policy.h"
#include "weapon_model_catalog.h"
#include "coop_probe_logic.h"
#include "cutscene_theater_logic.h"
#include "../src/common/titles/h3/halo3_cinematic_facing.h"
#include "frame_pacing_logic.h"
#include "../src/common/titles/h3/halo3_theater_logic.h"
#include "../src/common/titles/h3/halo3_vehicle_logic.h"
#include "hud_layout_logic.h"
#include "input_logic.h"
#include "level_load_gate_logic.h"
#include "camera_recovery_logic.h"
#include "weapon_hand_logic.h"
#include "dual_weapon_aim_logic.h"
#include "legacy_weapon_collision_catalog.h"
#include "../src/common/titles/odst/odst_bringup_logic.h"
#include "../src/dll/core/sigscan.h"
#include "../src/common/titles/odst/odst_vehicle_logic.h"
#include "../src/dll/titles/h2/halo2_adapter.h"
#include "../src/common/titles/h2/halo2_hud_logic.h"
#include "../src/common/titles/h2/halo2_hud_shader_logic.h"
#include "../src/common/titles/h2/halo2_render_logic.h"
#include "../src/common/titles/h2/halo2_contact_melee_logic.h"
#include "../src/common/titles/h4/halo4_contact_melee_logic.h"
#include "../src/common/titles/h2/halo2_world_collision_logic.h"
#include "../src/common/titles/reach/reach_hand_shot_logic.h"
#include "../src/dll/titles/h4/halo4_adapter.h"
#include "../src/common/titles/h4/halo4_cui_reticle_logic.h"
#include "../src/common/titles/h4/halo4_helmet_shader_logic.h"
#include "../src/common/titles/h4/halo4_screen_effect_shader_logic.h"
#include "../src/common/titles/h4/halo4_hud_logic.h"
#include "../src/common/titles/h4/halo4_restoration_logic.h"
#include "../src/common/titles/h4/halo4_parity_trace_logic.h"
#include "../src/common/titles/h4/halo4_render_logic.h"
#include "../src/common/titles/h4/halo4_world_collision_logic.h"
#include "../src/dll/titles/reach/reach_adapter.h"
#include "../src/common/titles/reach/reach_chud_logic.h"
#include "../src/common/titles/reach/reach_observer_logic.h"
#include "../src/dll/titles/reach/reach_render_candidate.h"
#include "../src/dll/titles/reach/reach_render_preflight.h"
#include "../src/common/titles/reach/reach_render_logic.h"
#include "../src/common/titles/reach/reach_vehicle_logic.h"
#include "scope_logic.h"
#include "scope_log.h"
#include "scope_window_logic.h"
#include "title_registry.h"
#include "title_runtime_state.h"
#include "view_cache_logic.h"
#include "two_hand_ik_logic.h"
#include "../src/dll/core/vr.h"

#include <authored_reticle_logic.h>

namespace
{
    int g_failures = 0;
    // Checks run, in all and per unit of main() (E3: a split must run the same checks, unit by unit).
    int g_checks = 0;
    int g_unit = -1;
    int g_unitStart = 0;

    void Check(bool condition, const char* message)
    {
        ++g_checks;
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++g_failures;
        }
    }

    // Ends the previous unit's range (printing its count) and starts unit `unit`; -1 ends the last range.
    void CheckUnit(int unit)
    {
        if (g_unit >= 0)
            std::cout << "unit " << g_unit << ": " << (g_checks - g_unitStart) << " checks\n";
        g_unit = unit;
        g_unitStart = g_checks;
    }

    ReachRenderCandidateProof CompleteReachRenderCandidateProof()
    {
        ReachRenderCandidateProof proof{};
        proof.retailIdentity = true;
        proof.mainRenderViewMatchCount = 1;
        proof.mainRenderViewAtExpectedRva = true;
        proof.mainRenderViewBodyHash = true;
        proof.playerViewRenderMatchCount = 1;
        proof.playerViewRenderAtExpectedRva = true;
        proof.playerViewRenderBodyHash = true;
        proof.cameraStackCallbackMatchCount = 1;
        proof.cameraStackCallbackAtExpectedRva = true;
        proof.cameraStackCallbackBodyHash = true;
        proof.frustumHelperMatchCount = 1;
        proof.frustumHelperAtExpectedRva = true;
        proof.frustumHelperExecutableRange = true;
        proof.fpCameraRebuildMatchCount = 1;
        proof.fpCameraRebuildAtExpectedRva = true;
        proof.fpCameraRebuildBodyHash = true;
        proof.fpCameraUploadMatchCount = 1;
        proof.fpCameraUploadAtExpectedRva = true;
        proof.fpCameraUploadBodyHash = true;
        proof.fpCameraWrapperBodyHashes = true;
        proof.exactFpCameraFlowEdges = true;
        proof.exactOuterCallerEdges = true;
        proof.exactInnerCallerEdge = true;
        proof.fixedDataRanges = true;
        return proof;
    }

    ReachDisplaySurfaceProof CompleteReachDisplaySurfaceProof(
        const ReachModuleEpoch& epoch, const ReachPreflightToken& preflight, uint64_t resourceRevision = 1)
    {
        ReachDisplaySurfaceProof proof{};
        proof.continuity.epoch = epoch;
        proof.continuity.resourceRevision = resourceRevision;
        proof.continuity.lifecycleSerial = 7;
        proof.continuity.swapchainIdentity = 0x20000000;
        proof.continuity.buffer0Identity = 0x20001000;
        proof.continuity.surfaceArrayIdentity = 0x20002000;
        proof.continuity.record0RtvIdentity = 0x20003000;
        proof.continuity.record0SrvIdentity = 0x20004000;
        proof.continuity.selectedRtvIdentity = 0x20003000;
        proof.continuity.deviceIdentity = 0x20008000;
        proof.continuity.immediateContextIdentity = 0x20005000;
        proof.continuity.eyeResourceIdentities[0] = 0x20006000;
        proof.continuity.eyeResourceIdentities[1] = 0x20007000;
        proof.continuity.specializationCount = kReachDisplaySurfaceCount;
        proof.continuity.selectedSpecialization = 0;
        proof.preflight = preflight;
        proof.immediateContextIdentity = 0x20005000;
        proof.eyeResourceIdentities[0] = 0x20006000;
        proof.eyeResourceIdentities[1] = 0x20007000;
        proof.source = {1920, 1080, 1, 1, kReachDisplayFormatR8G8B8A8Unorm, 1, 0};
        proof.eyes[0] = proof.source;
        proof.eyes[1] = proof.source;
        proof.readyEyeMask = 0x3u;
        proof.engineSwapchainMatchesPresent = true;
        proof.selectedRtvMatchesRecord0 = true;
        proof.swapchainContract = true;
        proof.sameDevice = true;
        proof.immediateContext = true;
        return proof;
    }

    std::string ReadTextFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    // The byte-by-byte scan sig::Find used before it was made memchr-anchored.
    // Kept verbatim as the reference an optimisation must agree with: a faster
    // scan that finds a DIFFERENT address would silently hook the wrong code.
    uintptr_t ReferenceFind(
        const uint8_t* data, size_t size, const std::vector<uint8_t>& bytes, const std::vector<uint8_t>& wild)
    {
        const size_t n = bytes.size();
        if (!n || size < n)
            return 0;
        const size_t last = size - n;
        for (size_t i = 0; i <= last; i++)
        {
            size_t j = 0;
            for (; j < n; j++)
                if (!wild[j] && data[i + j] != bytes[j])
                    break;
            if (j == n)
                return reinterpret_cast<uintptr_t>(data) + i;
        }
        return 0;
    }

    size_t CountText(std::string_view text, std::string_view needle)
    {
        size_t count = 0;
        for (size_t pos = 0; (pos = text.find(needle, pos)) != std::string_view::npos; pos += needle.size())
            ++count;
        return count;
    }
}
