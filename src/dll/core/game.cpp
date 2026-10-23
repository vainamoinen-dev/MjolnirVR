// game.cpp: the title supervisor. It detects which MCC title DLL is loaded, installs that title's hooks
// (camera, weapons, HUD, vehicles, melee) and publishes game state through the Game_* API in game.h.
// dllmain.cpp calls Game_Init(); vr.cpp, input.cpp, menu.cpp, d3d11_hook.cpp and the titles/ adapters call the rest.
// It must not call MinHook's create/remove directly (hooks go through MCCVR_CreateHookChecked / MCCVR_RemoveHookChecked),
// and it must not call VR_UpdateWeaponAlignment(): the held gun's profile is applied on the render thread in vr.cpp.
#include "../../common/legacy_floating_hands_logic.h"
#include "hook_install_check.h" // MinHook create/remove go through the install-time detour check
#include <atomic>
#include "../../common/runtime_types.h"
#include "guard_count.h"
// Accepted snap turns per title (log line: "<title> snap turn: mode=... accepted=N",
// the wording Halo 2 already uses). Counted at each title's accept point.
std::atomic<unsigned long long> g_snapTurnsByTitle[8]{};
void Game_NoteSnapTurn(GameTitle title) noexcept
{
    const unsigned index = static_cast<unsigned>(title);
    g_snapTurnsByTitle[index < 8 ? index : 7].fetch_add(1, std::memory_order_relaxed);
}
#include "../../common/weapon_model_observation.h"
#include "../../common/cockpit_screen_logic.h"
#include "../../common/vehicle_monitors_logic.h"
#include "../../common/weapon_frame_transfer.h"
#include "../titles/reach/reach_action_test_guard.h"
#include "../../common/titles/reach/reach_frame_cap_logic.h"
#include "../../common/dlss_logic.h"
void HookInstallCheck_Report(); // hook_install_check.cpp
#include "../../common/fp_rig_coverage_logic.h"
#include "contact_melee_queue.h"
#include "../../common/titles/reach/reach_wind_replay.h"
#include "native_reload_policy.h"
#include "native_vehicle_first_person.h"
#include "../titles/ce/haloce_native_bindings.h"
#include "../../common/weapon_model_catalog.h"
#include "../../common/reserve_weapon_logic.h"
#include "../../common/weapon_inventory_logic.h"
#include "../../common/weapon_muzzle.h"
#include "../../common/vehicle_view_logic.h"
#include "../../common/muzzle_refusal.h"
#include "../../common/lens_anchor.h"
#include "../../common/visual_hand_config.h"
#include "../../common/vr_turn_mode.h"
#include "../../common/camera_recovery_logic.h"
#include "../../common/dual_weapon_aim_logic.h"
#include "../../common/native_shot_target_lease.h"
#include "../../common/titles/h4/halo4_vehicle_input_logic.h"
#include "../../common/hud_visibility.h"
#include "../../common/exclusive_input.h"
#include "../../common/weapon_hand_logic.h"
#include "../../common/weapon_order_diagnostic_logic.h"
#include "../../common/support_grip_logic.h"
#include "../../common/titles/h4/halo4_owner_evidence.h"
#include "../../common/anatomical_palette_logic.h"
#include "../../common/titles/reach/reach_finger_pose_logic.h"
#include "../../common/titles/h4/halo4_runtime_weapon_bounds.h"
#include "../../common/legacy_runtime_weapon_bounds.h"
#include "menu.h"
#include "hook_quiescence.h"
#include "muzzle_hook_set.h"
#include "../../common/minhook_lifecycle.h"
#include "../../common/manual_vr_recovery.h"
#include "../../common/manual_vr_recovery_logic.h"
#include "../../common/vr_interaction_refinement_logic.h"
#include "../../common/contact_melee_motion.h"
#include "../../common/contact_melee_drain.h"
#include "../../common/titles/h3/halo3_melee_selection_logic.h"
#include "../../common/titles/h4/halo4_contact_melee_logic.h"
#include <windows.h>
#include <tlhelp32.h>
#include <array>
#include <atomic>
#include <cfloat>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <vector>
#include <MinHook.h>
#include "game.h"
#include "native_fault_probe.h"
#include "../titles/ce/haloce_network_input.h"
#include "../titles/ce/haloce_stereo_core.h"
#include "../titles/ce/haloce_first_person.h"
#include "../titles/ce/haloce_contact.h"
#include "../../common/vehicle_wheel_logic.h"
#include "../../common/turret_handles_logic.h"
#include "../../common/pause_debounce.h"
#include "../../common/turret_gyro_logic.h"
#include "../../common/flight_stick_logic.h"
#include "../../common/titles/h2/halo2_vehicle_roles.h"
#include "../../common/grab_pickup_logic.h"
#include "../../common/weapon_interaction_logic.h"
#include "../titles/ce/haloce_hud.h"
#include "../titles/ce/haloce_hud_layout_runtime.h"
#include "../titles/ce/haloce_controls.h"
#include "../../common/titles/ce/haloce_pause_logic.h"
#include "../titles/ce/haloce_comfort.h"
#include "../titles/ce/haloce_orientation.h"
#include "../titles/ce/haloce_unit_control.h"
#include "title_reentry_probe.h"
#include "roomscale.h"
#include "telemetry_recorder.h"
#include "../../common/support_diagnostics.h"
#include "physical_crouch_camera.h"
#include "../../common/physical_crouch_native_read.h"
#include "../../common/physical_crouch_additional_witnesses.h"
#include "../../common/physical_crouch_cached_read.h"
#include "../../common/physical_crouch_cached_witnesses.h"
#include "d3d11_hook.h"
#include "sigscan.h"
#include "vr.h"
#include "native_subtitles.h"
#include "bloom_override.h"
#include "ik.h"
#include "title_adapter.h"
#include "../../common/authored_reticle_logic.h"
#include "../../common/titles/reach/reach_chud_logic.h"
#include "../../common/titles/reach/reach_render_logic.h"
#include "../../common/titles/reach/reach_occlusion_diag.h"
#include "../../common/titles/reach/reach_vehicle_logic.h"
#include "../../common/titles/reach/reach_hand_shot_logic.h"
#include "../titles/reach/reach_render_candidate.h"
#include "../../common/titles/reach/reach_observer_logic.h"
#include "../../common/aim_servo_logic.h"
#include "../../common/log.h"
#include "../../common/hook_registry.h"
#include "../../common/gated_hook.h"
#include "../../common/config.h"
#include "../../common/cutscene_theater_logic.h"
#include "../../common/titles/h3/halo3_cinematic_facing.h"
#include "../../common/titles/h3/halo3_theater_logic.h"
#include "../../common/titles/h3/halo3_vehicle_logic.h"
#include "../../common/hud_layout_logic.h"
#include "../../common/input_logic.h"
#include "../../common/titles/h4/halo4_render_logic.h"
#include "../../common/titles/h4/halo4_body_ik_logic.h"
#include "../../common/titles/h4/halo4_fp_finger_pose_logic.h"
#include "../../common/titles/h4/halo4_cui_reticle_logic.h"
#include "../../common/titles/h4/halo4_hud_logic.h"
#include "../../common/titles/h4/halo4_restoration_logic.h"
#include "../../common/titles/h4/halo4_parity_trace_logic.h"
#include "../../common/titles/h4/halo4_world_collision_logic.h"
#include "../../common/legacy_weapon_collision_catalog.h"
#include "../../common/level_load_gate_logic.h"
#include "../titles/h2/halo2_adapter.h"
#include "../titles/h2/halo2_cold_observation.h"
#include "../../common/titles/h2/halo2_render_logic.h"
#include "../titles/h2/halo2_stereo_core.h"
#include "../titles/h2/halo2_observer_6dof.h"
#include "../titles/h2/halo2_render_mode_guard.h"
#include "../titles/h2/halo2_look_tests.h"
#include "../titles/h4/halo4_look_tests.h"
#include "../titles/h2/halo2_anniversary_stereo.h"
#include "../titles/h4/halo4_adapter.h"
#include "../titles/h4/halo4_cold_observation.h"
#include "../../common/titles/odst/odst_bringup_logic.h"
#include "../../common/titles/odst/odst_vehicle_logic.h"
#include "../../common/head_look_test.h"
#include "../../common/scope_logic.h"
#include "../../common/two_hand_ik_logic.h"
#include <mutex>
#include "../../common/binding_cache_logic.h"
#include "gesture_melee_bindings.inl"

std::atomic<bool> g_halo4NativePauseAvailable{false};

#include "game_internal.h"
using namespace game_core;
#include "halo3_family_internal.h"
#include "../titles/reach/reach_bridge.h"
using namespace reach_bridge;
#include "../titles/h4/halo4_bridge.h"
using namespace halo4_bridge;
#include "../titles/odst/odst_bridge.h"
using namespace odst_bridge;
#include "../titles/h3/halo3_bridge.h"
using namespace halo3_bridge;
#include "../titles/h2/halo2_bridge.h"
using namespace halo2_bridge;
#include "../titles/ce/haloce_bridge.h"
using namespace haloce_bridge;
// Head tracking. We hook the game's per-frame camera-update function and,
// each frame, overwrite the authoritative camera's forward/up vectors with the
// direction of the headset. Writing from inside the game's own frame (rather
// than poking memory from a thread) means our value lands at exactly the right
// moment and holds steady instead of flickering.
//
// The hooked function (RVA 0x2A628C for build 1.3528.0.0) is __fastcall(dst,
// src): it copies the camera from src (a double-buffered heap struct) into the
// static gun-camera. src+0x28 = forward (3 floats), src+0x34 = up (3 floats).
// These offsets are build-specific and must become AOB signatures before shipping.

namespace
{

    constexpr uint32_t kRuntimeCapabilitiesRequiringArm =
        TitleCapability_Stereo | TitleCapability_ControllerAim | TitleCapability_Hud | TitleCapability_ArmIk |
        TitleCapability_RoomScale | TitleCapability_Haptics | TitleCapability_CutsceneTheater;

    constexpr uintptr_t kGunProjY = 0x34;

} // namespace
namespace game_core
{
    std::atomic<uint32_t> g_halo3RuntimeGeneration{0};
} // namespace game_core
namespace game_core
{
    std::atomic<bool> g_renderHooked{false};
    std::atomic<bool> g_enabled{false}; // F2
} // namespace game_core
namespace
{
} // namespace
namespace game_core
{
    std::atomic<bool> g_autoVrUserVeto{false};
    std::atomic<bool> g_autoVrOwned{false};
} // namespace game_core
namespace
{
    // Process-lifetime fail-stop latch. Once OpenXR session ownership fails,
    // no title worker may re-arm a camera transaction behind State::Failed.
} // namespace
namespace game_core
{
    std::atomic<bool> g_vrRuntimeFailureLatched{false};
    std::atomic<bool> g_needRecenter{true};     // F3 (yaw + position)
    std::atomic<bool> g_needPosRecenter{false}; // enabling leaning: position only, no yaw snap
    std::atomic<float> g_yawSign{-1.0f};        // F4  (default matches PSVR2 mapping)
    std::atomic<float> g_pitchSign{1.0f};       // F5
    std::atomic<float> g_pitchTrim{0.0f};       // F8/F9, radians
    std::atomic<bool> g_writeUp{true};          // F7
    std::atomic<bool> g_positional{true};       // 6DOF head translation on by default; F6 toggles
    std::atomic<int> g_stereoEye{-1};           // -1 mono, 0 left, 1 right
} // namespace game_core
namespace
{

    // World scale: Halo world units per real meter (1 wu ~= 3.05 m), so ~0.33
    // gives roughly 1:1 leaning. Offset is clamped so a bad value can't fling
    // the camera through the level.
} // namespace
namespace game_core
{
    std::atomic<float> g_worldScale{0.33f};
} // namespace game_core
namespace game_core
{
    std::atomic<float> g_projectionTanX{1.091595f};
    std::atomic<float> g_projectionTanY{1.114286f};
} // namespace game_core
namespace game_core
{
    std::atomic<float> g_zoomFactor{1.0f}; // >1 while the player is zoomed (scope)
} // namespace game_core
namespace game_core
{
    std::atomic<float> g_renderHalfFovX{atanf(1.091595f)};
    std::atomic<float> g_renderHalfFovY{atanf(1.114286f)};
} // namespace game_core
namespace
{

    // Frame-pacing transition telemetry. These are monotonic relaxed counters:
    // hot hooks never format, allocate, lock, or perform file I/O for the new
    // capture. VR samples them at exact OpenXR frame boundaries and the title
    // worker writes a completed transition only after its post-roll is frozen.
    std::atomic<uint64_t> g_perfViewRenders{0};
    std::atomic<uint64_t> g_perfFpPaletteRequests[3]{};
    std::atomic<uint64_t> g_perfFpPaletteFullSolves[3]{};
    std::atomic<uint64_t> g_perfFpPaletteCacheHits[3]{};
    std::atomic<uint64_t> g_perfFpPaletteCacheStores[3]{};
    std::atomic<uint64_t> g_perfFpPaletteCacheFull[3]{};
    std::atomic<uint64_t> g_perfZoomLogWrites{0};
    std::atomic<uint64_t> g_perfViewRateLogWrites{0};
    std::atomic<uint64_t> g_perfPaletteRateLogWrites{0};
    std::atomic<uint64_t> g_perfCameraRateLogWrites{0};
    std::atomic<uint64_t> g_perfFpDriverRateLogWrites{0};

    // The exact camera state the engine consumed this tick (position from
    // kSrcPos, fwd/up = the very floats ApplyHeadLook wrote). Captured in
    // CamCopyHook; consumed by the first-person transform (head-bake
    // cancellation MUST use these, not a re-derivation) and by FpRootShim.
} // namespace
namespace game_core
{
    std::atomic<float> g_camX{0}, g_camY{0}, g_camZ{0};
    // Gameplay camera origin before ApplyHeadLook adds headset leaning. A
    // controller world position is based on this stable origin plus
    // controller-minus-recenter, never camera-plus-controller-minus-current-
    // head (which mixes samples and shortens apparent reach).
    std::atomic<float> g_baseCamX{0}, g_baseCamY{0}, g_baseCamZ{0};
} // namespace game_core
namespace
{
} // namespace
namespace game_core
{
    std::atomic<bool> g_baseCamValid{false};
} // namespace game_core
namespace
{
} // namespace
namespace game_core
{
    std::atomic<float> g_camFwd[3] = {{1}, {0}, {0}};
    std::atomic<float> g_camUp[3] = {{0}, {0}, {1}};
} // namespace game_core
namespace
{
} // namespace
namespace game_core
{
    std::atomic<bool> g_camValid{false};
} // namespace game_core
namespace
{

    // MEASURED world-up (gravity axis) for shoulder leveling. We do NOT assume it
    // - the earlier hardcoded (0,0,1) broke the arm. Instead we EMA the engine's
    // own camera-up each tick; over normal (mostly level) play that average
    // converges to true vertical regardless of the engine's axis convention.
} // namespace
namespace game_core
{
    std::atomic<float> g_worldUp[3] = {{0}, {0}, {1}};
    std::atomic<bool> g_worldUpInit{false};
} // namespace game_core
namespace
{

    // The overlay frustum is pinned to the exact world projection: the
    // first-person bones are camera-space positions in world units, so only an
    // exact match projects the weapon at the controller's true screen position.
    // Weapon size is a MESH scale (config gun_scale, Home/End), never a
    // frustum scale - 07-15 shipped both at once (2.0 frustum x 0.33 mesh) and
    // the gun shrank to ~1/6 size ("barely visible").

    // VR aim: the game's own aim-driven camera forward, captured each frame
    // BEFORE the head-look overwrite. The XInput hook steers the game's aim
    // toward the right controller by comparing this with the controller ray.
} // namespace
namespace game_core
{
    std::atomic<bool> g_vrAim{true}; // on by default; Insert toggles
    std::atomic<float> g_aimFwdX{1}, g_aimFwdY{0}, g_aimFwdZ{0};
    // Diagnostic (Reach shot drift with the head turned): the stick
    // servo's last target pitch, the game aim pitch it measured, and the hand
    // ray's yaw from the yaw reference captured at the last recentre (not the
    // live head) in radians, printed on each Reach shot line.
    std::atomic<float> g_servoDesiredPitch{0}, g_servoAimPitch{0}, g_servoHandReferenceYaw{0};
} // namespace game_core
namespace game_core
{
    // Diagnostic: the H3 scope camera forward actually built (render thread),
    // whether it came from the barrel, the receipt age and when it was built.
    std::atomic<float> g_h3ScopeFwd[3]{};
    std::atomic<bool> g_h3ScopeFwdBarrel{false};
    std::atomic<uint64_t> g_h3ScopeFwdAgeMs{0}, g_h3ScopeFwdBuiltMs{0};
} // namespace game_core
namespace game_core
{
    std::atomic<bool> g_aimSeen{false};
} // namespace game_core
namespace
{

    // Yaw is relative (the game's heading is arbitrary, so we recenter it to
    // the head). Pitch is absolute (head-level == game-level), which avoids
    // capturing a bad reference on recenter.
} // namespace
namespace game_core
{
    float g_headYawRef = 0;
    float g_gameYawRef = 0;
} // namespace game_core
namespace
{

    // Which path latched each title's yaw reference,
    // with the values, so per-run picture shifts can be checked against them.
    // H3, ODST and Reach share g_gameYawRef/g_headYawRef; a title that reaches its
    // first gameplay frame without latching says which stale value it inherited.
    std::atomic<uint32_t> g_aimServoCommandFrames{0};   // non-zero aim-stick commands since the last latch
    GameTitle g_sharedYawRefOwner = GameTitle::Unknown; // title that last latched the shared pair
    uint32_t g_sharedYawLatchGeneration[8]{};
    uint32_t g_sharedYawCheckedGeneration[8]{};
} // namespace
namespace game_core
{
    void NoteYawReferenceLatched(
        GameTitle title, uint32_t generation, const char* path, float gameYaw, float headYaw, bool shared)
    {
        const unsigned t = static_cast<unsigned>(title);
        if (shared && t < 8)
        {
            g_sharedYawLatchGeneration[t] = generation;
            g_sharedYawRefOwner = title;
        }
        LOG("yaw reference latched (%s): title %u game yaw %.3f deg, head yaw %.3f deg, %u servo-commanded frames before it",
            path, t, gameYaw * 57.2957795f, headYaw * 57.2957795f,
            g_aimServoCommandFrames.exchange(0, std::memory_order_relaxed));
    }
    // Call on every camera frame of a shared-pair title, after its latch decision.
    void NoteSharedYawFrame(GameTitle title, uint32_t generation)
    {
        const unsigned t = static_cast<unsigned>(title);
        if (t >= 8 || !generation || g_sharedYawCheckedGeneration[t] == generation)
            return;
        g_sharedYawCheckedGeneration[t] = generation;
        if (g_sharedYawLatchGeneration[t] == generation)
            return;
        LOG("no yaw reference latched (title %u): using game yaw %.3f deg, head yaw %.3f deg from %s", t,
            g_gameYawRef * 57.2957795f, g_headYawRef * 57.2957795f,
            g_sharedYawRefOwner == GameTitle::Unknown ? "process start"
            : g_sharedYawRefOwner == title            ? "this title's previous mission"
                                                      : "a previous title");
    }
    float g_headPosRef[3] = {0, 0, 0}; // headset position (m) captured at recenter
} // namespace game_core
namespace
{

} // namespace
namespace game_core
{
    void Halo3PublishRollStableFollowFor(
        uint32_t generation, VehicleFollowOwner owner, float hullYaw, float hullPitch, float hullRoll)
    {
        if (!generation || !std::isfinite(hullYaw) || !std::isfinite(hullPitch))
            return;
        auto& published = g_halo3RollStableFollow;
        published.sequence.fetch_add(1, std::memory_order_acq_rel);
        published.generation.store(generation, std::memory_order_relaxed);
        published.sampleMs.store(GetTickCount64(), std::memory_order_relaxed);
        published.hullYaw.store(hullYaw, std::memory_order_relaxed);
        published.hullPitch.store(hullPitch, std::memory_order_relaxed);
        published.hullRoll.store(std::isfinite(hullRoll) ? hullRoll : 0.0f, std::memory_order_relaxed);
        published.owner.store(static_cast<uint32_t>(owner), std::memory_order_relaxed);
        published.sequence.fetch_add(1, std::memory_order_release);
        published.writerHasValue = true;
    }
} // namespace game_core
namespace
{

} // namespace
namespace game_core
{
    void Halo3ClearRollStableFollow()
    {
        auto& published = g_halo3RollStableFollow;
        // OFF is the default and this runs in a camera hot hook. Once cleared,
        // repeated OFF frames stay a plain camera-thread branch with no RMW.
        if (!published.writerHasValue)
            return;
        published.sequence.fetch_add(1, std::memory_order_acq_rel);
        published.writerHasValue = false;
        published.generation.store(0, std::memory_order_relaxed);
        published.sampleMs.store(0, std::memory_order_relaxed);
        published.hullYaw.store(0.0f, std::memory_order_relaxed);
        published.hullPitch.store(0.0f, std::memory_order_relaxed);
        published.sequence.fetch_add(1, std::memory_order_release);
    }
} // namespace game_core
namespace
{

} // namespace
namespace game_core
{
    VehicleSteerContext g_vehicleSteer;
} // namespace game_core
namespace
{
    // Comfort: hull yaw folded into the view by whichever follow pass
    // runs, and when it last ran (a following seat this frame).
    std::atomic<float> g_vehicleFollowYawTotal{0.0f};
    std::atomic<uint64_t> g_vehicleFollowingMs{0};
} // namespace
namespace game_core
{
    void NoteVehicleFollowYaw(float delta)
    {
        if (delta != 0.0f && std::isfinite(delta))
            g_vehicleFollowYawTotal.store(
                g_vehicleFollowYawTotal.load(std::memory_order_relaxed) + delta, std::memory_order_relaxed);
        g_vehicleFollowingMs.store(GetTickCount64(), std::memory_order_release);
    }
    void PublishVehicleSteerContext(
        GameTitle title, bool driver, vehicle_wheel::SteerClass cls, bool armed, bool aircraft)
    {
        g_vehicleSteer.aircraft.store(driver && aircraft, std::memory_order_relaxed);
        const int mode = driver ? vehicle_wheel::ModeFor(cls, g_config.vehicle_steer_car, g_config.vehicle_steer_bike)
                                : vehicle_wheel::Point;
        g_vehicleSteer.mode.store(mode, std::memory_order_relaxed);
        g_vehicleSteer.driver.store(driver && cls != vehicle_wheel::SteerClass::None, std::memory_order_relaxed);
        g_vehicleSteer.armed.store(armed, std::memory_order_relaxed);
        g_vehicleSteer.modeFollow.store(
            vehicle_wheel::EffectiveFollow(false, driver, cls, mode), std::memory_order_relaxed);
        g_vehicleSteer.title.store(static_cast<uint8_t>(title), std::memory_order_relaxed);
        g_vehicleSteer.sampleMs.store(GetTickCount64(), std::memory_order_release);
    }
} // namespace game_core
namespace game_core
{
    // Halo 2 (observer thread): a look-steered driver whose view follows the
    // hull owns its steering; stamped so a stopped observer releases it.
    std::atomic<uint64_t> g_halo2SteeringAuthorMs{0};
} // namespace game_core
namespace game_core
{
    std::atomic<bool> g_halo2SteeringAuthor{false}, g_halo2SteeringWheel{false};
} // namespace game_core
namespace game_core
{
    bool Halo2SeatAuthorsSteeringNow()
    {
        const uint64_t at = g_halo2SteeringAuthorMs.load(std::memory_order_acquire);
        const uint64_t now = GetTickCount64();
        return TitleAdapter_GetActiveTitle() == GameTitle::Halo2 &&
               g_halo2SteeringAuthor.load(std::memory_order_relaxed) && at && now >= at && now - at <= 200 &&
               Halo2Observer6Dof_VehicleControlActive();
    }
    bool VehicleSteerContextFresh()
    {
        const uint64_t at = g_vehicleSteer.sampleMs.load(std::memory_order_acquire);
        const uint64_t now = GetTickCount64();
        return at && now >= at && now - at <= 200 &&
               g_vehicleSteer.title.load(std::memory_order_relaxed) ==
                   static_cast<uint8_t>(TitleAdapter_GetActiveTitle());
    }
} // namespace game_core
namespace
{
} // namespace
namespace game_core
{
    bool Halo3ReadRollStableFollow(float& hullYaw, float& hullPitch)
    {
        // The existing checkbox remains the master switch. OFF reaches none of
        // the new transforms, preserving the shipped world-locked path.
        if (!g_config.vehicle_first_person ||
            (!g_config.vehicle_view_follow &&
                !(VehicleSteerContextFresh() && g_vehicleSteer.modeFollow.load(std::memory_order_relaxed))))
            return false;
        // Whichever title is actually driving supplies both the gate and the
        // generation the sample must match.
        uint32_t generation = 0;
        uint32_t expectedOwner = 0;
        if (Halo3VehicleFpActive())
        {
            generation = g_halo3RuntimeGeneration.load(std::memory_order_acquire);
        }
        else if (OdstFollowContextValid(generation))
        {
            expectedOwner = static_cast<uint32_t>(VehicleFollowOwner::Odst);
        }
        else if (ReachFollowContextValid(generation))
        {
            expectedOwner = static_cast<uint32_t>(VehicleFollowOwner::Reach);
        }
        if (!generation)
            return false;
        const uint64_t nowMs = GetTickCount64();
        auto& published = g_halo3RollStableFollow;
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const uint32_t before = published.sequence.load(std::memory_order_acquire);
            if (!before || (before & 1u))
                continue;
            const uint32_t sampleGeneration = published.generation.load(std::memory_order_relaxed);
            const uint64_t sampleMs = published.sampleMs.load(std::memory_order_relaxed);
            const float sampleYaw = published.hullYaw.load(std::memory_order_relaxed);
            const float samplePitch = published.hullPitch.load(std::memory_order_relaxed);
            const uint32_t sampleOwner = published.owner.load(std::memory_order_relaxed);
            if (published.sequence.load(std::memory_order_acquire) != before)
                continue;
            if (sampleOwner != expectedOwner || sampleGeneration != generation || !sampleMs || nowMs < sampleMs ||
                nowMs - sampleMs > kHalo3RollStableFollowFreshMs || !std::isfinite(sampleYaw) ||
                !std::isfinite(samplePitch))
                return false;
            hullYaw = sampleYaw;
            hullPitch = samplePitch;
            return true;
        }
        return false;
    }
    PrepareViewFn g_prepareView = nullptr;
    BuildViewportFn g_buildViewport = nullptr;
    BuildMatricesFn g_buildMatrices = nullptr;
} // namespace game_core
namespace
{

    // Halo 3: stride 0x2820, array +0x2D2F680. ODST: stride 0x2810, array
    // +0x2D73590 (log-confirmed). Reach: stride 0xA40, array +0x29F2B90 - and
    // 0xA40 is the same four-player-view stride already documented in the
    // Reach signature evidence. Each module also contains one unrelated
    // four-slot constructor of identical shape, so the stride bytes are part of
    // the pattern; with them pinned every title matches exactly once.
    const PlayerViewArrayEvidence kHalo3PlayerViewArray = {
        "48 89 5C 24 08 57 48 83 EC 20 48 8D 1D ?? ?? ?? ?? "
        "BF 04 00 00 00 48 8B CB E8 ?? ?? ?? ?? 48 81 C3 20 28 00 00 "
        "48 83 EF 01 75 ?? 48 8B 5C 24 30",
        0x2820};
    const PlayerViewArrayEvidence kOdstPlayerViewArray = {"48 89 5C 24 08 57 48 83 EC 20 48 8D 1D ?? ?? ?? ?? "
                                                          "BF 04 00 00 00 48 8B CB E8 ?? ?? ?? ?? 48 81 C3 10 28 00 00 "
                                                          "48 83 EF 01 75 ?? 48 8B 5C 24 30",
        0x2810};
    const PlayerViewArrayEvidence kReachPlayerViewArray = {
        "48 89 5C 24 08 57 48 83 EC 20 48 8D 1D ?? ?? ?? ?? "
        "BF 04 00 00 00 48 8B CB E8 ?? ?? ?? ?? 48 81 C3 40 0A 00 00 "
        "48 83 EF 01 75 ?? 48 8B 5C 24 30",
        0x0A40};
    // Halo 4: stride 0xAD0, array +0x30AD1C0 (byte-proven against
    // the pinned retail module; the constructor is the same four-slot shape
    // as the other three titles, and the adjacent six-slot constructor of
    // identical shape at +0x22A88 is why the stride bytes stay pinned).
    // The pattern is the SAME string Halo 4's cold observation verifies, on
    // purpose: a future byte fix updates the one anchor table and both the
    // gate and the observation follow, instead of drifting apart.
    const PlayerViewArrayEvidence kHalo4PlayerViewArray = {
        kHalo4RetailAnchors[kHalo4AnchorCtor].pattern, kHalo4PlayerViewStride};

} // namespace
namespace game_core
{
    uintptr_t ResolvePlayerViewArray(uintptr_t base, size_t size, const PlayerViewArrayEvidence& evidence)
    {
        if (!base || !size)
            return 0;
        const uintptr_t hit = sig::Find(base, size, evidence.constructorPattern);
        if (!hit || sig::Find(hit + 1, base + size - hit - 1, evidence.constructorPattern))
        {
            return 0;
        }
        // `lea rbx, [rip+disp32]` is the fifth instruction, 7 bytes at +0x0A.
        const int32_t displacement = *reinterpret_cast<const int32_t*>(hit + 0x0D);
        const uintptr_t array = hit + 0x11 + displacement;
        const size_t span = 4 * evidence.expectedStride;
        if (array < base || array >= base + size || span > base + size - array)
            return 0;
        return array;
    }

    // Fingerprint the WHOLE of player view 0, not a prefix. A prefix would
    // cover only the compact camera (position/forward/fov/clips), which a
    // perfectly still player does not change; the per-frame counters that
    // always move live near the end of the view - ODST's own readiness log
    // watches a boolean at +0x2810-ish. Hashing the full stride cannot miss
    // them. ~10 KB of FNV-1a on a 50 ms worker poll is a rounding error.
    bool ReadPlayerViewFingerprint(uintptr_t array, size_t stride, uint64_t& fingerprint)
    {
        if (!array || !stride)
            return false;
        uint64_t hash = 1469598103934665603ull;
        __try
        {
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(array);
            for (size_t i = 0; i < stride; ++i)
            {
                hash ^= bytes[i];
                hash *= 1099511628211ull;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("hashing the player view array faulted");
            return false;
        }
        fingerprint = hash;
        return true;
    }
} // namespace game_core
namespace
{

} // namespace
namespace game_core
{
    PlayerViewLivenessGate g_halo3LevelLoadGate;
} // namespace game_core
namespace
{
} // namespace
namespace game_core
{
    PlayerViewLivenessGate g_odstLevelLoadGate;
} // namespace game_core
namespace
{
} // namespace
namespace game_core
{
    PlayerViewLivenessGate g_reachLevelLoadGate;
} // namespace game_core
namespace
{
} // namespace
namespace game_core
{
    PlayerViewLivenessGate g_halo4LevelLoadGate;
} // namespace game_core
namespace
{

    // TOUCH NOTHING WHILE A LEVEL LOADS (2026-08-06). The gate stopped us
    // INSTALLING into a loading screen, and a load still had to be retried.
    // The decisive capture (08:54:16-18) shows why: the bounced
    // load ran with zero hooks installed, and at the bounce `haloreach.dll` is
    // ABSENT from the module list - the module was pulled, then all six
    // reloaded 50 ms later. In that 2 s window the mod had already run a whole
    // 74 MB preflight signature scan that takes a REFCOUNT PIN on the module
    // (GetModuleHandleExW without UNCHANGED_REFCOUNT, reach_render_preflight
    // .cpp), plus a pause-flag scan, plus the draw-distance debug-var scan and
    // write - all against the module MCC was loading. A pin held across MCC's
    // own unload defers that unload to OUR FreeLibrary instead of MCC's, which
    // is a real race with the loader and fits the whole history of reports
    // exactly: the FIRST load into a module we previously hooked bounces, the
    // reload that the bounce provokes then works.
    //
    // So the gate's invariant is widened from "do not install" to "do not
    // touch": while a title's level is loading we take no pin, run no module
    // scan and write nothing into it. Nothing deferred is usable before the
    // level runs anyway, so this costs no capability.
    std::atomic<bool> g_activeTitleLevelRunning{false};

} // namespace
namespace game_core
{
    uint32_t* g_engineTlsIndex = nullptr;
} // namespace game_core
namespace
{
} // namespace
namespace game_core
{
    uint32_t* g_cinematicTlsIndex = nullptr;
} // namespace game_core
namespace
{
    // The cinematic shot-state pointer lives at this TLS byte offset. Halo 3
    // uses 0x90; ODST recompiled the same function with 0xA0. LocateCinematicState
    // reads the exact value from the setter's `mov edx, imm32`, so the offset is
    // evidenced per title rather than hardcoded.
    uint32_t g_cinematicShotStateOffset = 0x90;
} // namespace
namespace game_core
{
    std::atomic<int32_t> g_cinematicRebaseScene{-1};
    std::atomic<int32_t> g_cinematicRebaseShot{-1};
    std::atomic<uint32_t> g_cinematicRebaseSerial{0};
} // namespace game_core
namespace
{
    static_assert(sizeof(BoneMatrix) == 0x34);

} // namespace
namespace game_core
{
    std::atomic<int> g_fpBoneCount[2] = {{0}, {0}};
    std::atomic<int> g_fpWristIndex[2] = {{-1}, {-1}};
    // Bones at or below the wrist (hand, fingers, held weapon). They ride the
    // wrist rigidly so the gun stays gripped while the arm articulates.
    std::atomic<uint64_t> g_fpWristDescendants[2] = {{0}, {0}};
    // LEFT arm chain (l_hand global string id 0xA1, derived offline from the
    // engine string list, anchored on r_hand=0xA6). Same parent-walk capture.
    std::atomic<int> g_fpLWristIndex[2] = {{-1}, {-1}};
} // namespace game_core
namespace
{

    // Reach/Halo 4 first-person rigs the hand re-root does not know.
} // namespace
namespace game_core
{
    fp_rig_coverage::UnmappedRigLog g_unmappedFpRigs;
} // namespace game_core
namespace
{

    // One context per held-weapon slot: slot 0 is the primary (right-hand)
    // weapon, slot 1 is the dual-wield secondary (left-hand) weapon. The
    // palette hook matches its `source` pointer against both, so any
    // interpolate/palette call ordering pairs correctly.
} // namespace
namespace game_core
{
    thread_local FpInterpolationContext g_fpInterpolationContexts[2];
} // namespace game_core
namespace
{
    weapon_muzzle::Store g_halo3Muzzles;
    std::atomic<bool> g_halo3MuzzleBindingsReady{false};
} // namespace
namespace game_core
{
    void LegacyApplyVisualHandOffsets(GameTitle title, uint16_t tag, const int32_t* boneMap,
        const FpInterpolationContext& context, BoneMatrix* destination);
} // namespace game_core
namespace
{
} // namespace
namespace game_core
{
    thread_local BoneMatrix g_fpUnmodifiedInterpolations[2][64];
} // namespace game_core
namespace
{
} // namespace
namespace game_core
{
    thread_local BoneMatrix g_fpPaletteScratch[kReachFpMaxSourceNodeCount];
} // namespace game_core
namespace
{
} // namespace
namespace game_core
{
    thread_local BoneMatrix g_scopeHiddenPalette[64];
} // namespace game_core
namespace
{

    // Halo 3, ODST and Reach each keep an independent optional world-contact
    // transaction.  Their visible-palette code is shared, but installation is
    // title-native and a failed signature disables only this feature.
    using LegacyCollisionResolveFn = bool(__fastcall*)(
        const float* start, const float* desired, float* accepted, int32_t ignoredObjectA, int32_t ignoredObjectB);
    inline constexpr int kLegacyCollisionHandSamples = 7;
} // namespace
namespace game_core
{
    LegacyWorldCollisionFeature g_halo3WorldCollision;
} // namespace game_core
namespace game_core
{
    LegacyWorldCollisionFeature g_reachWorldCollision;
} // namespace game_core
namespace
{
    // Our bounded sweeps call the title's unhooked resolver wrapper. H3/ODST's
    // wrapper re-enters the central scheduler hook, while Reach re-enters its
    // resolver hook directly. Suppress only that owned nested callback so a
    // native game query remains the sole scheduling point.
} // namespace
namespace game_core
{
    thread_local bool g_legacyCollisionOwnedQuery = false;
} // namespace game_core
namespace
{

} // namespace
namespace game_core
{
    LegacyWorldCollisionFeature* LegacyCollisionForTitle(GameTitle title);
} // namespace game_core
namespace
{
} // namespace
namespace game_core
{
    void LegacyPublishReloadTarget(GameTitle title, uint16_t tag, uint32_t generation,
        const FpInterpolationContext& context, const BoneMatrix& root, const BoneMatrix* solved, const int32_t* boneMap,
        const FpExplicitPoseTargets* targets);
    bool LegacyApplyWorldCollision(GameTitle title, int hand, BoneMatrix& target, float* appliedCorrection);
    void LegacyPublishWorldCollisionVolumes(GameTitle title, uint16_t renderModelTag,
        const FpInterpolationContext& context, const BoneMatrix& root, const BoneMatrix* solved, int32_t ignoredObject,
        uint32_t generation, const FpExplicitPoseTargets* targets);
} // namespace game_core
namespace
{
} // namespace
namespace game_core
{
    bool LegacyObserveWeaponRenderModel(GameTitle title, uint16_t renderModelTag, uint32_t generation,
        const BoneMatrix* source, const int32_t* boneMap);
    bool LegacyBuildMappedWeaponBounds(GameTitle title, const FpInterpolationContext& context, const BoneMatrix& root,
        const BoneMatrix* solved, float output[][3], uint64_t& shapeId);
    int32_t LegacyCollisionIgnoredObject(GameTitle title);
} // namespace game_core
namespace
{
} // namespace
namespace game_core
{
    void ReportHalo3DualAim();
} // namespace game_core
namespace
{
} // namespace
namespace game_core
{
    bool InstallLegacyWorldCollision(LegacyWorldCollisionFeature& feature, GameTitle title, uintptr_t base, size_t size,
        uint32_t generation, void* detour, uint32_t expectedRva, const char* signature, bool fourArgumentResolver,
        uint32_t schedulerRva, const char* schedulerSignature);
} // namespace game_core
namespace
{
    // The render-thread IK path publishes only pointer-sized diagnostics.
    // Present consumes them and owns all logging, keeping file I/O and log
    // locks out of the palette hot path.
    thread_local const char* g_armFailWhy = nullptr;
} // namespace
namespace game_core
{
    std::atomic<const char*> g_armFailurePublished{nullptr};
    // 0 = both arms applied, 1 = right-arm/fallback failure, 2 = left-arm failure.
    std::atomic<int> g_armFailureSide{-1};
} // namespace game_core
namespace
{
    std::atomic<uint64_t> g_fpSkeletonKey{0};
    struct FpBoneMapSnapshot
    {
        std::atomic<uint32_t> sequence{0};
        std::atomic<uint64_t> skeletonKey{0};
        std::atomic<uint32_t> tag{0};
        std::atomic<int> count{0};
        std::atomic<int> reconstructed{0};
        std::atomic<int32_t> map[64]{};
    };
    FpBoneMapSnapshot g_fpBoneMapSnapshots[16];
    std::atomic<int> g_fpBoneMapSnapshotCount{0};

} // namespace
namespace game_core
{
    thread_local FpStereoSolveScope g_fpStereoSolveScope;
} // namespace game_core
namespace
{
} // namespace
namespace game_core
{
    bool LegacyRouteLeftHandedPalette(GameTitle title, uint16_t tag, const int32_t* boneMap, const BoneMatrix& root,
        const FpInterpolationContext& context, FpInterpolationContext& contactContext);
} // namespace game_core
namespace
{

} // namespace
namespace game_core
{
    thread_local float g_fpPaletteCollisionCorrection[2][3]{};
    HudCrosshairVisibleFn g_realHudCrosshairVisible = nullptr;
    GameIsPlaybackFn g_gameIsPlayback = nullptr;
    HudDrawWidgetFn g_realHudDrawWidget = nullptr;
} // namespace game_core
namespace
{
    thread_local bool g_insideHudDrawWidget = false;
    // Identity of the crosshair art most recently captured, for ANY title.
    // The compositor re-uploads the authored reticle only when this changes, so
    // a static crosshair costs no swapchain work at all.
} // namespace
namespace game_core
{
    std::atomic<uint64_t> g_authoredCrosshairKey{0};
    thread_local uint64_t g_authoredCrosshairKeyAccum = 0;
    // How many class-2 widget pieces reached the capture surface this frame.
    // Populated by Reach only; 0 means "no count available" and leaves the
    // completeness guard inert, which is how Halo 3 and ODST keep their
    // accepted publish behavior exactly.
    std::atomic<uint32_t> g_authoredCrosshairDraws{0};
} // namespace game_core
namespace game_core
{
    std::atomic<uint32_t> g_authoredCrosshairColorState{0};
    thread_local uint32_t g_authoredCrosshairColorStateAccum = 0;

} // namespace game_core
namespace game_core
{
    thread_local bool g_authoredReticleCaptureStarted = false;
} // namespace game_core
namespace
{

    // Shared because Halo can execute first-person work on a render worker.
} // namespace
namespace game_core
{
    std::atomic<bool> g_scopeRenderActive{false};
    void __fastcall HudDrawWidgetHook(
        int userIndex, void* descriptor, unsigned short widgetIndex, unsigned char useAlternatePath, void* drawState)
    {
        // The scope has its own centered crosshair in the upload shader. Keep
        // every native CHUD widget out of the magnified world-only picture.
        if (g_scopeRenderActive.load(std::memory_order_acquire))
            return;
        const bool previousInside = g_insideHudDrawWidget;
        const bool previousCapture = g_authoredReticleCaptureStarted;
        g_insideHudDrawWidget = true;
        g_authoredReticleCaptureStarted = false;
        const bool hideHud = VR_HudHidden() && g_enabled.load() && VR_IsStereoEnabled() && Game_MoveStickIsLocomotion();
        if (hideHud)
            ++hud_visibility::depth;
        __try
        {
            g_realHudDrawWidget(userIndex, descriptor, widgetIndex, useAlternatePath, drawState);
        }
        __finally
        {
            if (hideHud)
                --hud_visibility::depth;
        }
        if (g_authoredReticleCaptureStarted)
        {
            // This widget's art was redirected into the authored texture, so
            // fold its identity in and publish it. Same weapon and state ->
            // same key -> the compositor skips the blocking swapchain upload.
            const bool halo3Title = TitleAdapter_GetActiveTitle() == GameTitle::Halo3;
            // Halo 3's alternate-path byte changes with CHUD presentation
            // state, so it is not widget identity. Its exact colour output is
            // tracked separately below. ODST retains its proven key unchanged.
            const unsigned int identityVariant = halo3Title ? 0u : useAlternatePath;
            g_authoredCrosshairKeyAccum =
                FoldAuthoredCrosshairKey(g_authoredCrosshairKeyAccum, widgetIndex, identityVariant);
            g_authoredCrosshairKey.store(g_authoredCrosshairKeyAccum, std::memory_order_release);
            VR_EndAuthoredReticleCapture();
        }
        g_insideHudDrawWidget = previousInside;
        g_authoredReticleCaptureStarted = previousCapture;
    }

    bool __fastcall HudCrosshairVisibleHook(int userIndex)
    {
        // Halo has already proved this widget is scripting class 2 before this
        // predicate runs. During VR, redirect that one widget into the hand-ray
        // texture. Never leave the player with no aiming reference: if capture
        // is unavailable, fail open to Halo's native center reticle. Setting
        // kill_reticle=0 deliberately selects that native fallback as well.
        if (g_enabled.load(std::memory_order_relaxed) && VR_IsStereoEnabled())
        {
            if (!g_config.crosshair)
                return false;
            if (!g_config.kill_reticle)
                return true;

            const int eye = g_stereoEye.load(std::memory_order_relaxed);
            const int captureEye = g_config.right_eye_first ? 1 : 0;
            if (eye < 0 || eye == captureEye)
            {
                if (!VR_ShouldCaptureAuthoredReticleThisFrame())
                    return false;
                if (g_insideHudDrawWidget && VR_BeginAuthoredReticleCapture())
                {
                    g_authoredReticleCaptureStarted = true;
                    return true;
                }
                static std::atomic<bool> loggedFallback{false};
                if (!loggedFallback.exchange(true))
                    LOG("authored crosshair redirect unavailable; keeping native crosshair visible");
                return true;
            }
            return false;
        }

        // Flat/non-VR rendering retains Halo's stock crosshair behavior.
        if (!g_gameIsPlayback)
            return true;
        if (!g_gameIsPlayback())
            return true;
        return g_realHudCrosshairVisible(userIndex);
    }
} // namespace game_core
namespace
{

    // The 0x2EEFC8 placement output was measured to hold colour/alpha/animation
    // state, not screen coordinates. HudPlaceHook now observes that state only;
    // HUD positioning remains in the capture transform path in vr.cpp.

} // namespace
namespace game_core
{
    std::atomic<int> g_motionBlurVarCount{-1}; // -1 = not yet resolved
    float* FindDebugVarFloat(uintptr_t base, size_t size, const char* name)
    {
        const size_t nameLen = strlen(name);
        const uint8_t* module = reinterpret_cast<const uint8_t*>(base);
        uintptr_t nameVa = 0;
        for (size_t i = 1; i + nameLen + 1 < size; ++i)
        {
            if (module[i] == static_cast<uint8_t>(name[0]) && module[i - 1] == 0 && module[i + nameLen] == 0 &&
                !memcmp(module + i, name, nameLen))
            {
                nameVa = base + i;
                break;
            }
        }
        if (!nameVa)
            return nullptr;
        for (size_t i = 0; i + 24 <= size; i += 8)
        {
            if (*reinterpret_cast<const uintptr_t*>(module + i) != nameVa)
                continue;
            const uintptr_t value = *reinterpret_cast<const uintptr_t*>(module + i + 16);
            // A real value slot points back into the module's data, never at
            // code below the data sections and never outside the module.
            if (value > base && value < base + size)
                return reinterpret_cast<float*>(value);
        }
        return nullptr;
    }

    // The same debug-var table as FindDebugVarFloat, for a caller that knows
    // which TYPE it wants. An entry is {name_ptr, type, value_ptr, help_ptr}:
    // globals carry type 5 (boolean) or 6 (float), while a debug COMMAND
    // carries type 0 and stores its HANDLER where a global stores its value.
    //
    // FindDebugVarFloat ignores the type word, which is safe only because every
    // name it is asked for is a real global. It is not safe in general - Reach's
    // `render_atmosphere_fog` is a command whose entry "value" is 0x00198F2C,
    // inside .text - so resolving a command name that way would hand back a
    // pointer to code. Requiring the exact type and proving the slot is
    // committed, writable, non-executable memory turns that class of
    // mis-resolution into a null result instead of a write into code.
    void* FindDebugVarSlot(uintptr_t base, size_t size, const char* name, uint64_t expectedType)
    {
        const size_t nameLen = strlen(name);
        const uint8_t* module = reinterpret_cast<const uint8_t*>(base);
        uintptr_t nameVa = 0;
        for (size_t i = 1; i + nameLen + 1 < size; ++i)
        {
            if (module[i] == static_cast<uint8_t>(name[0]) && module[i - 1] == 0 && module[i + nameLen] == 0 &&
                !memcmp(module + i, name, nameLen))
            {
                nameVa = base + i;
                break;
            }
        }
        if (!nameVa)
            return nullptr;
        for (size_t i = 0; i + 24 <= size; i += 8)
        {
            if (*reinterpret_cast<const uintptr_t*>(module + i) != nameVa)
                continue;
            if (*reinterpret_cast<const uint64_t*>(module + i + 8) != expectedType)
            {
                continue;
            }
            const uintptr_t value = *reinterpret_cast<const uintptr_t*>(module + i + 16);
            if (value <= base || value >= base + size)
                continue;
            MEMORY_BASIC_INFORMATION info{};
            if (VirtualQuery(reinterpret_cast<void*>(value), &info, sizeof(info)) != sizeof(info) ||
                info.State != MEM_COMMIT)
            {
                continue;
            }
            // PAGE_* protections are distinct bits in the low byte; the
            // modifiers (GUARD/NOCACHE/WRITECOMBINE) sit above it.
            const DWORD protection = info.Protect & 0xFFu;
            constexpr DWORD kExecutable =
                PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
            constexpr DWORD kWritable = PAGE_READWRITE | PAGE_WRITECOPY;
            if ((info.Protect & PAGE_GUARD) || (protection & kExecutable) || !(protection & kWritable))
            {
                continue;
            }
            return reinterpret_cast<void*>(value);
        }
        return nullptr;
    }
} // namespace game_core
namespace
{

    // Halo applies an extra 25% widescreen FOV reduction only while
    // cinematic_in_progress() is true. That flat-screen composition choice
    // also narrows the visibility projection, so geometry is rejected inside
    // the OpenXR eye frustum during cutscenes. This engine boolean is exposed
    // through the debug-var table; resolve it by name and disable only the
    // cinematic reduction while immersive stereo VR is active. Theatre restores
    // the captured stock value so its image retains the authored composition.
    // Halo's ordinary gameplay projection remains untouched.
} // namespace
namespace game_core
{
    std::atomic<uint8_t*> g_reduceCinematicFov{nullptr};
} // namespace game_core
namespace
{
    std::atomic<uint8_t> g_reduceCinematicFovOriginal{1};
} // namespace
namespace game_core
{
    std::atomic<bool> g_reduceCinematicFovApplied{false};
} // namespace game_core
namespace
{

} // namespace
namespace game_core
{
    int SafeReadByte(const uint8_t* slot, uint8_t* value)
    {
        __try
        {
            *value = *reinterpret_cast<const volatile uint8_t*>(slot);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading a game byte faulted");
            return 0;
        }
        return 1;
    }

    int SafeWriteByte(uint8_t* slot, uint8_t value)
    {
        __try
        {
            *reinterpret_cast<volatile uint8_t*>(slot) = value;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("writing a game byte faulted");
            return 0;
        }
        return 1;
    }
} // namespace game_core
namespace game_core
{
    int SafeReadFloat(const float* slot, float* value)
    {
        __try
        {
            *value = *reinterpret_cast<const volatile float*>(slot);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading a game float faulted");
            return 0;
        }
        return 1;
    }

    int SafeWriteFloat(float* slot, float value)
    {
        __try
        {
            *reinterpret_cast<volatile float*>(slot) = value;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("writing a game float faulted");
            return 0;
        }
        return 1;
    }
} // namespace game_core
namespace
{

    void InvalidateCinematicFovVar(uint8_t* staleSlot)
    {
        if (g_reduceCinematicFov.compare_exchange_strong(staleSlot, nullptr, std::memory_order_acq_rel))
            g_reduceCinematicFovApplied.store(false, std::memory_order_release);
    }

    // Shared draw-distance trim for all three titles. The engine's render
    // far-clip plane is the debug var "render_far_clip_distance" (stock 10240
    // world units), the same name in Halo 3, ODST, and Reach. Trimming it makes
    // the engine cull distant geometry earlier -> fewer draw calls -> CPU
    // headroom in sequential stereo, where the render thread (not the GPU) is
    // the limit. Resolved BY NAME (no RVAs), cached per module so the whole-
    // module name scan runs only when the title/instance changes, and re-
    // asserted from the 20 ms worker so it survives level and tag loads.
    // draw_distance 1.00 writes the captured stock value back, so the default
    // trims nothing. Fail-open: unresolved or an insane stock value is a no-op.
    struct DrawDistanceControl
    {
        float* slot = nullptr;
        float stock = 0.0f;
        uintptr_t base = 0;
        uint32_t generation = 0;
    };
    DrawDistanceControl g_drawDistance;

    void Game_ApplyDrawDistance(uintptr_t base, size_t size, uint32_t generation)
    {
        if (!base || !size)
            return;
        if (!g_drawDistance.slot || g_drawDistance.base != base || g_drawDistance.generation != generation)
        {
            // Module changed: re-resolve exactly once. This whole-module name
            // scan does NOT run every tick -- only on a title/instance change.
            g_drawDistance = {};
            float* slot = FindDebugVarFloat(base, size, "render_far_clip_distance");
            float stock = 0.0f;
            if (!slot || !SafeReadFloat(slot, &stock) || !(stock > 1.0f) || !isfinite(stock))
                return; // leave unresolved; retry on a later tick
            g_drawDistance.slot = slot;
            g_drawDistance.stock = stock;
            g_drawDistance.base = base;
            g_drawDistance.generation = generation;
        }
        float scale = g_config.draw_distance;
        if (scale < kDrawDistanceMin)
            scale = kDrawDistanceMin;
        if (scale > kDrawDistanceMax)
            scale = kDrawDistanceMax;
        const float target = g_drawDistance.stock * scale;
        float current = 0.0f;
        if (SafeReadFloat(g_drawDistance.slot, &current) && current != target)
            SafeWriteFloat(g_drawDistance.slot, target);
    }

} // namespace
namespace game_core
{
    void ResolveCinematicFovVar(uintptr_t base, size_t size)
    {
        auto* slot =
            reinterpret_cast<uint8_t*>(FindDebugVarFloat(base, size, "reduce_widescreen_fov_during_cinematics"));
        uint8_t initial = 0;
        const bool valid = slot && SafeReadByte(slot, &initial) && initial <= 1;
        g_reduceCinematicFov.store(nullptr, std::memory_order_release);
        g_reduceCinematicFovApplied.store(false, std::memory_order_release);
        if (!valid)
        {
            LOG("cutscene culling: cinematic FOV policy unavailable; stock behavior retained");
            return;
        }
        g_reduceCinematicFovOriginal.store(initial, std::memory_order_relaxed);
        g_reduceCinematicFov.store(slot, std::memory_order_release);
        LOG("cutscene culling: cinematic FOV policy resolved (stock value %u)", static_cast<unsigned>(initial));
    }

    void UpdateCinematicFovPolicy()
    {
        uint8_t* slot = g_reduceCinematicFov.load(std::memory_order_acquire);
        if (!slot)
            return;

        const bool immersiveVrActive = ShouldDisableWidescreenCinematicFov(
            g_enabled.load(std::memory_order_relaxed), VR_IsStereoEnabled(), VR_IsCutsceneTheaterActive());
        if (immersiveVrActive)
        {
            if (!g_reduceCinematicFovApplied.exchange(true))
            {
                LOG("cutscene culling: disabled Halo's widescreen cinematic FOV reduction");
            }
            // Halo normally leaves this global alone, but reassert it from the
            // Present thread in case a map transition restores debug globals.
            if (!SafeWriteByte(slot, 0))
                InvalidateCinematicFovVar(slot);
        }
        else if (g_reduceCinematicFovApplied.exchange(false))
        {
            if (!SafeWriteByte(slot, g_reduceCinematicFovOriginal.load(std::memory_order_relaxed)))
            {
                InvalidateCinematicFovVar(slot);
                return;
            }
            LOG("cutscene culling: restored Halo's cinematic FOV policy");
        }
    }
    std::atomic<uintptr_t> g_safeFrameSlots[kMaxSafeFrameHits]{};
} // namespace game_core
namespace
{
    std::atomic<uint32_t> g_safeFrameBaseCurvatureBits[kMaxSafeFrameHits]{};
    std::atomic<int> g_safeFrameHitCount{-1}; // -1 none, -2 scanning, >0 verified
    std::atomic<uint32_t> g_hudAppliedBits{0};
    std::atomic<uint32_t> g_hudAppliedAspectBits{0};
    std::atomic<uint32_t> g_hudAppliedCurvatureBits{0};
} // namespace
namespace game_core
{
    std::atomic<HudLayoutProfile> g_hudLayoutOwner{HudLayoutProfile::None};
} // namespace game_core
namespace
{
    std::atomic<uint32_t> g_hudLayoutGeneration{1};
    std::atomic<HudLayoutProfile> g_safeFramePublishedOwner{HudLayoutProfile::None};
    std::atomic<uint32_t> g_safeFramePublishedGeneration{0};
    std::atomic<bool> g_safeFrameScanInFlight{false};
    // Reach's six headset-proven HUD records have repeatedly lived in the
    // high 0x00007FF4... private tag allocation. The first scan of each Reach
    // title generation starts there; if it misses, the normal auto-rescan for
    // that same generation falls back to the full application range.
    std::atomic<uint32_t> g_reachHudFastScanGeneration{UINT32_MAX};
    SRWLOCK g_hudLayoutWriteLock = SRWLOCK_INIT;

} // namespace
namespace game_core
{
    std::atomic<bool> g_enginePauseValidated{false};
} // namespace game_core
namespace
{

    const TitleRuntimeHeartbeatPolicy& RuntimeHeartbeatPolicy()
    {
        static const TitleRuntimeHeartbeatPolicy policy = MakeTitleRuntimeHeartbeatPolicy();
        return policy;
    }

    GameTitle RetainedRuntimeTitle()
    {
        std::array<uint32_t, kTitleRuntimeSlotCount> generations{};
        generations[TitleRuntimeSlotIndex(GameTitle::Halo3)] = g_halo3RuntimeGeneration.load(std::memory_order_acquire);
        generations[TitleRuntimeSlotIndex(GameTitle::Halo3ODST)] =
            g_odstRuntimeGeneration.load(std::memory_order_acquire);
        generations[TitleRuntimeSlotIndex(GameTitle::HaloReach)] = ReachRetainedRuntimeGeneration();
        generations[TitleRuntimeSlotIndex(GameTitle::Halo4)] = Halo4RetainedRuntimeGeneration();
        generations[TitleRuntimeSlotIndex(GameTitle::Halo2)] = Halo2Stereo_Generation();
        return RetainedRuntimeTitleFromGenerations(generations);
    }

} // namespace
namespace game_core
{
    TitleAdapterRuntimeSnapshot RuntimeSnapshot(uint64_t nowMs)
    {
        return TitleAdapter_GetRuntimeSnapshot(nowMs, RuntimeHeartbeatPolicy(), RetainedRuntimeTitle());
    }
} // namespace game_core
namespace
{

    HudLayoutRememberedCache g_reachHudRemembered{};

    static HudLayoutRememberedCache* HudLayoutRememberedFor(HudLayoutProfile profile)
    {
        switch (profile)
        {
        case HudLayoutProfile::Halo3:
            return &g_halo3HudRemembered;
        case HudLayoutProfile::Halo3ODST:
            return &g_odstHudRemembered;
        case HudLayoutProfile::HaloReach:
            return &g_reachHudRemembered;
        default:
            return nullptr;
        }
    }

    // These timers used to be function statics shared by every title. Keeping
    // them in the generation-owned runtime prevents a Halo 3 attempt from
    // delaying ODST's first locate by up to fifteen seconds.
    std::atomic<uint64_t> g_hudLayoutLastVerifyMs{0};
    std::atomic<uint64_t> g_hudLayoutLastReacquireMs{0};
    std::atomic<uint64_t> g_hudLayoutLastAttemptMs{0};
    // When the current title took HUD-layout ownership. A level's tag data is
    // not always resident the first time a scan is eligible, so the first
    // attempts have to retry quickly; the long cooldown only makes sense once
    // the title has been settled for a while.
    std::atomic<uint64_t> g_hudLayoutOwnerSinceMs{0};

    static void ClearHudLayoutSlots()
    {
        for (int i = 0; i < kMaxSafeFrameHits; ++i)
        {
            g_safeFrameSlots[i].store(0, std::memory_order_relaxed);
            g_safeFrameBaseCurvatureBits[i].store(0, std::memory_order_relaxed);
        }
        g_safeFramePublishedOwner.store(HudLayoutProfile::None, std::memory_order_relaxed);
        g_safeFramePublishedGeneration.store(0, std::memory_order_relaxed);
        g_safeFrameHitCount.store(-1, std::memory_order_release);
        g_hudAppliedBits.store(0, std::memory_order_relaxed);
        g_hudAppliedAspectBits.store(0, std::memory_order_relaxed);
        g_hudAppliedCurvatureBits.store(0, std::memory_order_relaxed);
    }

    static bool HudLayoutContextMatches(HudLayoutProfile profile, uint32_t generation)
    {
        return HudLayoutPublicationMatches(g_hudLayoutOwner.load(std::memory_order_acquire),
            g_hudLayoutGeneration.load(std::memory_order_acquire), profile, generation);
    }

    static bool HudLayoutResultsMatch(HudLayoutProfile profile, uint32_t generation)
    {
        return HudLayoutPublicationMatches(profile, generation,
            g_safeFramePublishedOwner.load(std::memory_order_acquire),
            g_safeFramePublishedGeneration.load(std::memory_order_acquire));
    }

    static uint32_t EnsureHudLayoutProfile(HudLayoutProfile profile)
    {
        const HudLayoutAdapter* adapter = HudLayoutAdapterFor(profile);
        if (!adapter)
            return 0;
        if (g_hudLayoutOwner.load(std::memory_order_acquire) == profile)
            return g_hudLayoutGeneration.load(std::memory_order_acquire);

        AcquireSRWLockExclusive(&g_hudLayoutWriteLock);
        if (g_hudLayoutOwner.load(std::memory_order_acquire) == profile)
        {
            const uint32_t generation = g_hudLayoutGeneration.load(std::memory_order_acquire);
            ReleaseSRWLockExclusive(&g_hudLayoutWriteLock);
            return generation;
        }
        const uint32_t generation = g_hudLayoutGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
        // Publishing the new owner after the generation bump invalidates any
        // old scan. The exclusive lock also drains the only foreign-memory
        // writer before active slots can change title ownership.
        g_hudLayoutOwner.store(profile, std::memory_order_release);
        ClearHudLayoutSlots();
        g_hudLayoutLastVerifyMs.store(0, std::memory_order_relaxed);
        g_hudLayoutLastReacquireMs.store(0, std::memory_order_relaxed);
        g_hudLayoutLastAttemptMs.store(0, std::memory_order_relaxed);
        g_hudLayoutOwnerSinceMs.store(GetTickCount64(), std::memory_order_relaxed);
        ReleaseSRWLockExclusive(&g_hudLayoutWriteLock);
        LOG("SAFEFRAME: layout owner is %s (generation %u); active "
            "publication and timers reset",
            adapter->name, generation);
        return generation;
    }

} // namespace
namespace game_core
{
    void InvalidateHudLayoutProfile(HudLayoutProfile profile)
    {
        if (g_hudLayoutOwner.load(std::memory_order_acquire) != profile)
            return;
        AcquireSRWLockExclusive(&g_hudLayoutWriteLock);
        if (g_hudLayoutOwner.load(std::memory_order_acquire) == profile)
        {
            g_hudLayoutGeneration.fetch_add(1, std::memory_order_acq_rel);
            g_hudLayoutOwner.store(HudLayoutProfile::None, std::memory_order_release);
            ClearHudLayoutSlots();
            g_hudLayoutLastVerifyMs.store(0, std::memory_order_relaxed);
            g_hudLayoutLastReacquireMs.store(0, std::memory_order_relaxed);
            g_hudLayoutLastAttemptMs.store(0, std::memory_order_relaxed);
        }
        ReleaseSRWLockExclusive(&g_hudLayoutWriteLock);
    }
} // namespace game_core
namespace
{

    // Plain helpers: SEH frames must stay free of C++ unwinding (C2712), and a
    // region can decommit between VirtualQuery and the read, so every touch of
    // foreign memory is guarded.
    // Plain byte compare honouring the adapter's wildcard mask. No SEH here:
    // every caller has already established that the bytes are readable.
    static bool SafeFrameAnchorEquals(const unsigned char* candidate, const HudLayoutAnchorView& view)
    {
        return HudLayoutAnchorEquals(candidate, view);
    }

    // Matches every resolution-class anchor the adapter carries, not only the
    // primary one. Reach authors one curvature record per screen shape and the
    // engine picks by aspect, so the widescreen record alone is not enough.
    static int SafeFrameScanRegion(
        uintptr_t regionBase, size_t len, const HudLayoutAdapter& adapter, uintptr_t* out, int maxOut)
    {
        int found = 0;
        const unsigned char* p = reinterpret_cast<const unsigned char*>(regionBase);
        const int anchorCount = HudLayoutAnchorCount(adapter);
        __try
        {
            uint64_t prefixes[1 + kHudLayoutMaxAltAnchors]{};
            size_t spans[1 + kHudLayoutMaxAltAnchors]{};
            for (int a = 0; a < anchorCount; ++a)
            {
                const HudLayoutAnchorView view = HudLayoutAnchorAt(adapter, a);
                memcpy(&prefixes[a], view.anchor, sizeof(prefixes[a]));
                spans[a] = static_cast<size_t>(HudLayoutScanSpan(view));
            }
            for (size_t i = 0; i + 8 <= len && found < maxOut; ++i)
            {
                const uint64_t here = *reinterpret_cast<const uint64_t*>(p + i);
                for (int a = 0; a < anchorCount; ++a)
                {
                    if (here != prefixes[a] || i + spans[a] > len)
                        continue;
                    const HudLayoutAnchorView view = HudLayoutAnchorAt(adapter, a);
                    if (!SafeFrameAnchorEquals(p + i, view))
                        continue;
                    out[found++] = regionBase + i + static_cast<size_t>(view.safeFrameOffset);
                    // Skip the matched identity only: two curvature records
                    // are far further apart than one anchor.
                    i += static_cast<size_t>(view.anchorLength) - 1;
                    break;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("scanning for the HUD safe-frame anchor faulted");
            return found;
        }
        return found;
    }

    // A title whose record has no depth field reports 0.0f, which every
    // plausibility and equality check below treats as "nothing to move".
    static int SafeFrameReadLayout(
        uintptr_t slot, const HudLayoutAdapter& adapter, uint32_t* destinationZ, uint32_t* h, uint32_t* v)
    {
        __try
        {
            *destinationZ = HudLayoutHasDepthField(adapter)
                                ? *reinterpret_cast<const volatile uint32_t*>(slot + adapter.depthFromSlot)
                                : 0u;
            *h = *reinterpret_cast<const volatile uint32_t*>(slot);
            *v = *reinterpret_cast<const volatile uint32_t*>(slot + 4);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading the HUD safe-frame layout faulted");
            return 0;
        }
        return 1;
    }

    static int SafeFrameStoreLayout(
        uintptr_t slot, const HudLayoutAdapter& adapter, float destinationZ, float horizontal, float vertical)
    {
        __try
        {
            if (HudLayoutHasDepthField(adapter))
                *reinterpret_cast<volatile float*>(slot + adapter.depthFromSlot) = destinationZ;
            *reinterpret_cast<volatile float*>(slot) = horizontal;
            *reinterpret_cast<volatile float*>(slot + 4) = vertical;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("writing the HUD safe-frame layout faulted");
            return 0;
        }
        return 1;
    }

    // A record the engine only reads can live on a read-only page. Make it
    // writable for exactly this store and put the protection back. Mapped
    // views become copy-on-write, so the page privatises to this process and
    // the game's files on disk are never modified.
    static int SafeFrameWriteLayout(
        uintptr_t slot, const HudLayoutAdapter& adapter, float destinationZ, float horizontal, float vertical)
    {
        if (SafeFrameStoreLayout(slot, adapter, destinationZ, horizontal, vertical))
            return 1;
        if (!adapter.scanMappedRegions)
            return 0;

        const uintptr_t low = HudLayoutHasDepthField(adapter) ? slot + adapter.depthFromSlot : slot;
        const size_t span = (slot + 8) - low;
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<void*>(low), &mbi, sizeof(mbi)) != sizeof(mbi))
            return 0;
        const DWORD want = mbi.Type == MEM_MAPPED ? PAGE_WRITECOPY : PAGE_READWRITE;
        DWORD previous = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(low), span, want, &previous))
            return 0;
        const int wrote = SafeFrameStoreLayout(slot, adapter, destinationZ, horizontal, vertical);
        DWORD restored = 0;
        VirtualProtect(reinterpret_cast<void*>(low), span, previous, &restored);
        return wrote;
    }

    // A slot is valid if ANY of the adapter's resolution-class anchors still
    // matches at its own offset behind the slot. Distinct virtual width/height
    // bytes keep the classes from aliasing each other.
    static int SafeFrameVerifySlot(
        uintptr_t slot, const HudLayoutAdapter& adapter, uint32_t* destinationZ, uint32_t* h, uint32_t* v)
    {
        const int anchorCount = HudLayoutAnchorCount(adapter);
        __try
        {
            bool matched = false;
            for (int a = 0; a < anchorCount && !matched; ++a)
            {
                const HudLayoutAnchorView view = HudLayoutAnchorAt(adapter, a);
                const uintptr_t anchorAddress = slot - static_cast<uintptr_t>(view.safeFrameOffset);
                if (memcmp(reinterpret_cast<const void*>(anchorAddress), view.anchor, 8) != 0)
                    continue;
                uint8_t authored[8];
                const bool shifted =
                    adapter.profile == HudLayoutProfile::HaloReach && ReachAuthoredSensorOrigin(slot, authored);
                if (!HudLayoutAnchorEquals(
                        reinterpret_cast<const unsigned char*>(anchorAddress), view, shifted ? authored : nullptr))
                    continue;
                matched = true;
            }
            if (!matched)
                return 0;
            *destinationZ = HudLayoutHasDepthField(adapter)
                                ? *reinterpret_cast<const volatile uint32_t*>(slot + adapter.depthFromSlot)
                                : 0u;
            *h = *reinterpret_cast<const volatile uint32_t*>(slot);
            *v = *reinterpret_cast<const volatile uint32_t*>(slot + 4);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("verifying the HUD safe-frame slot faulted");
            return 0;
        }
    }

    static bool SafeFramePairPlausible(uint32_t h, uint32_t v)
    {
        float hf = 0.0f, vf = 0.0f;
        memcpy(&hf, &h, sizeof(hf));
        memcpy(&vf, &v, sizeof(vf));
        return hf >= 0.15f && hf <= 1.05f && vf >= 0.15f && vf <= 1.05f;
    }

    static bool HudDestinationPlausible(uint32_t bits)
    {
        float value = 0.0f;
        memcpy(&value, &bits, sizeof(value));
        return isfinite(value) && value >= -10.0f && value <= 10.0f;
    }

    static uint32_t RememberedHudCurvatureBaseline(HudLayoutProfile profile, uintptr_t slot, uint32_t fallback)
    {
        HudLayoutRememberedCache* remembered = HudLayoutRememberedFor(profile);
        if (!remembered)
            return fallback;
        const int count = remembered->count.load(std::memory_order_acquire);
        for (int i = 0; i < count && i < kMaxSafeFrameHits; ++i)
        {
            if (remembered->slots[i].load(std::memory_order_relaxed) == slot)
                return remembered->baseCurvatureBits[i].load(std::memory_order_relaxed);
        }
        return fallback;
    }

    // Halo builds its native HUD for the game render surface's pixel aspect.
    // Match Halo 3's headset-confirmed correction and user-facing aspect trim
    // identically for every title adapter.
    static void ComputeHudSafeFramePair(float size, float aspect, float& horizontal, float& vertical)
    {
        horizontal = vertical = size;
        float gameAspect = 0.0f;
        float eyeFov[4]{};
        if (VR_GetGameRenderAspect(gameAspect) && VR_GetEyeFov(0, eyeFov))
        {
            const float halfX = fmaxf(-eyeFov[0], eyeFov[1]);
            const float halfY = fmaxf(eyeFov[2], -eyeFov[3]);
            const float tanX = tanf(halfX), tanY = tanf(halfY);
            if (isfinite(tanX) && isfinite(tanY) && tanX > 0.01f && tanY > 0.01f)
            {
                const float correction = gameAspect / (tanX / tanY);
                if (isfinite(correction) && correction >= 0.25f && correction <= 4.0f)
                {
                    if (correction > 1.0f)
                        vertical = size / correction;
                    else
                        horizontal = size * correction;
                }
            }
        }
        horizontal *= aspect;
        horizontal = fmaxf(0.15f, fminf(horizontal, 1.0f));
        vertical = fmaxf(0.15f, fminf(vertical, 1.0f));
    }

    static uintptr_t EncodeHudLayoutScanToken(HudLayoutProfile profile, uint32_t generation)
    {
        static_assert(sizeof(uintptr_t) >= sizeof(uint64_t));
        return (static_cast<uintptr_t>(generation) << 32) | static_cast<uint32_t>(profile);
    }

    DWORD WINAPI SafeFrameScanThread(LPVOID parameter)
    {
        const uintptr_t token = reinterpret_cast<uintptr_t>(parameter);
        const auto profile = static_cast<HudLayoutProfile>(static_cast<uint32_t>(token));
        const uint32_t generation = static_cast<uint32_t>(token >> 32);
        const HudLayoutAdapter* adapter = HudLayoutAdapterFor(profile);
        if (!adapter)
        {
            g_safeFrameScanInFlight.store(false, std::memory_order_release);
            return 0;
        }

        const uint64_t t0 = GetTickCount64();
        uintptr_t selfBase = 0;
        size_t selfSize = 0;
        // By address, not by file name: the DLL was renamed to HaloMCCVR.dll
        // and a name lookup would silently return no range, letting the scan
        // accept hits inside our own image.
        if (!sig::SelfModuleRange(selfBase, selfSize))
            LOG("HUD safe-frame scan: own module range unavailable; "
                "self-exclusion is disabled for this scan");

        uintptr_t acceptedSlots[kMaxSafeFrameHits]{};
        uint32_t acceptedBaselines[kMaxSafeFrameHits]{};
        int accepted = 0;
        int rawHits = 0;
        bool cancelled = false;
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        const uintptr_t addrMin = reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
        const uintptr_t addrMax = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);
        uintptr_t addr = addrMin;
        if (profile == HudLayoutProfile::HaloReach)
        {
            // The prior full scan proved all six Reach records as private RW
            // near 0x00007FF4..., after spending ~45 s byte-scanning lower
            // allocations. Start the first scan of a Reach generation in that
            // proven high allocation band. If ASLR ever puts the records lower,
            // a failed scan leaves no slots, so the existing auto-rescan runs
            // again; the generation marker below then selects addrMin and gives
            // us the old complete scan as a correctness fallback.
            constexpr uintptr_t kReachHudFastStart = static_cast<uintptr_t>(0x00007FF400000000ull);
            const uint32_t priorGeneration =
                g_reachHudFastScanGeneration.exchange(generation, std::memory_order_acq_rel);
            if (priorGeneration != generation && kReachHudFastStart > addr && kReachHudFastStart < addrMax)
                addr = kReachHudFastStart;
        }
        MEMORY_BASIC_INFORMATION mbi{};
        while (addr < addrMax && accepted < kMaxSafeFrameHits &&
               VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi)) == sizeof(mbi))
        {
            if (!HudLayoutContextMatches(profile, generation))
            {
                cancelled = true;
                break;
            }
            const uintptr_t regionBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            const uintptr_t next = regionBase + mbi.RegionSize;
            // Halo 3's tag data is private read-write, and that exact filter is
            // headset-proven for Halo 3 and ODST. Reach's map data need not be:
            // its adapter also inspects mapped and copy-on-write regions, so a
            // record the engine actually reads cannot be missed just because it
            // was not allocated the way Halo 3's was. Writing a copy-on-write
            // page privatises it in this process only; no file is modified.
            // Halo 3's tag data is private read-write and that exact filter is
            // headset-proven for Halo 3 and ODST. Reach's is not: the curvature
            // record is const to the engine, so the copy it actually renders
            // from can be a read-only mapping. Reach therefore accepts any
            // committed readable private or mapped region, and the writer makes
            // the page writable copy-on-write for the store, which privatises
            // it in this process only and never modifies a file on disk.
            const bool readableProtect =
                mbi.Protect == PAGE_READWRITE ||
                (adapter->scanMappedRegions &&
                    (mbi.Protect == PAGE_READONLY || mbi.Protect == PAGE_WRITECOPY ||
                        mbi.Protect == PAGE_EXECUTE_READ || mbi.Protect == PAGE_EXECUTE_READWRITE ||
                        mbi.Protect == PAGE_EXECUTE_WRITECOPY));
            const bool writableProtect = readableProtect;
            // The headset-proven Reach anchors are MEM_PRIVATE/PAGE_READWRITE,
            // while other tag-backed copies can be mapped. Reach therefore
            // accepts both storage classes; H3/ODST retain their established
            // private-only rule. Narrowing this to mapped
            // only makes every Reach scan return zero raw anchor hits.
            const bool allowedType = adapter->scanMappedRegions ? (mbi.Type == MEM_PRIVATE || mbi.Type == MEM_MAPPED)
                                                                : mbi.Type == MEM_PRIVATE;
            const bool candidate =
                mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD) && writableProtect && allowedType;
            const bool self = selfBase && regionBase >= selfBase && regionBase < selfBase + selfSize;
            if (candidate && !self)
            {
                uintptr_t hits[kMaxSafeFrameHits]{};
                const int n = SafeFrameScanRegion(regionBase, mbi.RegionSize, *adapter, hits, kMaxSafeFrameHits);
                for (int k = 0; k < n; ++k)
                {
                    ++rawHits;
                    uint32_t destinationZ = 0, h = 0, v = 0;
                    if (!SafeFrameReadLayout(hits[k], *adapter, &destinationZ, &h, &v))
                        continue;
                    const bool payloadOk = SafeFramePairPlausible(h, v) && HudDestinationPlausible(destinationZ);
                    LOG("SAFEFRAME [%s]: anchor at %p (type 0x%X protect "
                        "0x%X) destination-Z %08X, safe frame %08X/%08X -> %s",
                        adapter->name, reinterpret_cast<void*>(hits[k]), static_cast<unsigned>(mbi.Type),
                        static_cast<unsigned>(mbi.Protect), destinationZ, h, v,
                        payloadOk ? "VERIFIED" : "payload implausible, REJECTED");
                    if (payloadOk && accepted < kMaxSafeFrameHits)
                    {
                        acceptedSlots[accepted] = hits[k];
                        acceptedBaselines[accepted] = RememberedHudCurvatureBaseline(profile, hits[k], destinationZ);
                        ++accepted;
                    }
                }
            }
            // The accepted Steam proof contains six Reach records clustered
            // in the same high tag allocation. Once all six are collected, do
            // not waste tens of seconds scanning unrelated resident modules.
            // If a future build exposes only the adapter minimum (three), this
            // condition never fires and the scan naturally retains the complete
            // fallback behavior.
            if (profile == HudLayoutProfile::HaloReach && accepted >= 6)
                break;
            addr = next;
        }

        if (cancelled)
        {
            LOG("SAFEFRAME [%s]: title generation changed during scan; "
                "cancelling before the next memory region",
                adapter->name);
            g_safeFrameScanInFlight.store(false, std::memory_order_release);
            return 0;
        }

        const int observedAccepted = accepted;
        if (!HudLayoutAcceptedCountOk(*adapter, accepted))
        {
            LOG("SAFEFRAME [%s]: expected %d to %d title-proven layout "
                "block(s), observed %d; all candidates rejected",
                adapter->name, adapter->expectedBlocks, adapter->maxBlocks, accepted);
            accepted = 0;
        }
        LOG("SAFEFRAME [%s]: scan done in %llu ms (%s) - "
            "%d raw anchor hit(s), %d plausible pair(s), %d accepted",
            adapter->name, static_cast<unsigned long long>(GetTickCount64() - t0),
            adapter->scanMappedRegions ? "private + mapped, any readable protection" : "private-RW only", rawHits,
            observedAccepted, accepted);

        if (HudLayoutContextMatches(profile, generation))
        {
            HudLayoutRememberedCache* remembered = HudLayoutRememberedFor(profile);
            for (int i = 0; i < kMaxSafeFrameHits; ++i)
            {
                const uintptr_t slot = i < accepted ? acceptedSlots[i] : 0;
                const uint32_t baseline = i < accepted ? acceptedBaselines[i] : 0;
                g_safeFrameSlots[i].store(slot, std::memory_order_relaxed);
                g_safeFrameBaseCurvatureBits[i].store(baseline, std::memory_order_relaxed);
            }
            // A failed or ambiguous rescan must not erase the only retained
            // authored baseline: the resident tag may still contain our prior
            // curvature adjustment. Replace remembered data only with a full,
            // exact-cardinality title proof.
            if (HudLayoutAcceptedCountOk(*adapter, accepted))
            {
                for (int i = 0; i < kMaxSafeFrameHits; ++i)
                {
                    remembered->slots[i].store(i < accepted ? acceptedSlots[i] : 0, std::memory_order_relaxed);
                    remembered->baseCurvatureBits[i].store(
                        i < accepted ? acceptedBaselines[i] : 0, std::memory_order_relaxed);
                }
                remembered->count.store(accepted, std::memory_order_release);
            }
            g_safeFramePublishedOwner.store(profile, std::memory_order_relaxed);
            g_safeFramePublishedGeneration.store(generation, std::memory_order_release);
            g_safeFrameHitCount.store(accepted, std::memory_order_release);
            if (accepted)
                LOG("SAFEFRAME [%s]: title-owned layout ready; hud_size and "
                    "hud_aspect apply from the next frame%s",
                    adapter->name,
                    HudLayoutHasDepthField(*adapter) ? " (hud_curvature too)"
                                                     : " - hud_curvature has NO live control in this "
                                                       "engine: its curvature is folded into a derived "
                                                       "basis when the tag block loads, so the slider is "
                                                       "deliberately not written");
        }
        else
        {
            LOG("SAFEFRAME [%s]: title generation changed during scan; "
                "discarding every result",
                adapter->name);
        }
        g_safeFrameScanInFlight.store(false, std::memory_order_release);
        return 0;
    }

    void LaunchSafeFrameScan(HudLayoutProfile profile, const char* why)
    {
        const HudLayoutAdapter* adapter = HudLayoutAdapterFor(profile);
        const uint32_t generation = EnsureHudLayoutProfile(profile);
        if (!adapter || !generation)
            return;
        if (g_safeFrameScanInFlight.exchange(true, std::memory_order_acq_rel))
            return;

        AcquireSRWLockExclusive(&g_hudLayoutWriteLock);
        if (!HudLayoutContextMatches(profile, generation))
        {
            ReleaseSRWLockExclusive(&g_hudLayoutWriteLock);
            g_safeFrameScanInFlight.store(false, std::memory_order_release);
            return;
        }
        ClearHudLayoutSlots();
        g_safeFrameHitCount.store(-2, std::memory_order_release);
        ReleaseSRWLockExclusive(&g_hudLayoutWriteLock);
        const uintptr_t token = EncodeHudLayoutScanToken(profile, generation);
        HANDLE thread = CreateThread(nullptr, 0, SafeFrameScanThread, reinterpret_cast<LPVOID>(token), 0, nullptr);
        if (thread)
        {
            CloseHandle(thread);
            LOG("SAFEFRAME [%s]: scan started (%s)", adapter->name, why);
        }
        else
        {
            if (HudLayoutContextMatches(profile, generation))
                g_safeFrameHitCount.store(-1, std::memory_order_release);
            g_safeFrameScanInFlight.store(false, std::memory_order_release);
            LOG("SAFEFRAME [%s]: scan thread create FAILED", adapter->name);
        }
    }

    // Present-thread, change/validation only. Slider changes apply on the next
    // Present and verified slots are rechecked once per second for map reloads.
    void ApplyHudLayoutOnce(HudLayoutProfile profile)
    {
        const HudLayoutAdapter* adapter = HudLayoutAdapterFor(profile);
        const uint32_t generation = g_hudLayoutGeneration.load(std::memory_order_acquire);
        if (!adapter || !HudLayoutContextMatches(profile, generation) || !HudLayoutResultsMatch(profile, generation))
            return;
        const int n = g_safeFrameHitCount.load(std::memory_order_acquire);
        if (n <= 0)
            return;

        const float wantSize = g_config.hud_size;
        const float wantAspect = g_config.hud_aspect;
        const float wantCurvature = g_config.hud_curvature;
        if (!(wantSize >= 0.30f && wantSize <= 1.00f) ||
            !(wantAspect >= kHudAspectMin && wantAspect <= kHudAspectMax) ||
            !(wantCurvature >= kHudCurvatureMin && wantCurvature <= kHudCurvatureMax))
            return;

        float wantH = wantSize, wantV = wantSize;
        ComputeHudSafeFramePair(wantSize, wantAspect, wantH, wantV);
        uint32_t wantBits = 0, wantAspectBits = 0;
        uint32_t wantCurvatureBits = 0;
        uint32_t wantHBits = 0, wantVBits = 0;
        memcpy(&wantBits, &wantSize, sizeof(wantBits));
        memcpy(&wantAspectBits, &wantAspect, sizeof(wantAspectBits));
        memcpy(&wantCurvatureBits, &wantCurvature, sizeof(wantCurvatureBits));
        memcpy(&wantHBits, &wantH, sizeof(wantHBits));
        memcpy(&wantVBits, &wantV, sizeof(wantVBits));

        // Read-back reporting. The value observed here is whatever survived
        // since the previous pass, so it distinguishes "our write never stuck"
        // from "our write stuck and the title ignores this copy" without
        // another test session. Present thread, at most once every five
        // seconds, and never from a render or palette hot hook.
        static uint64_t lastReadbackLogMs = 0;
        const uint64_t readbackNow = GetTickCount64();
        const bool reportReadback = readbackNow - lastReadbackLogMs >= 5000;
        if (reportReadback)
            lastReadbackLogMs = readbackNow;

        int live = 0;
        for (int i = 0; i < n && i < kMaxSafeFrameHits; ++i)
        {
            if (!HudLayoutContextMatches(profile, generation))
                return;
            const uintptr_t slot = g_safeFrameSlots[i].load(std::memory_order_relaxed);
            if (!slot)
                continue;
            uint32_t destinationZ = 0, h = 0, v = 0;
            if (!SafeFrameVerifySlot(slot, *adapter, &destinationZ, &h, &v))
                continue;
            if (!SafeFramePairPlausible(h, v) || !HudDestinationPlausible(destinationZ))
                continue;
            if (reportReadback)
            {
                float readH = 0.0f, readV = 0.0f;
                memcpy(&readH, &h, sizeof(readH));
                memcpy(&readV, &v, sizeof(readV));
                LOG("SAFEFRAME [%s]: slot %d at %p reads %.4f/%.4f, "
                    "writing %.4f/%.4f",
                    adapter->name, i, reinterpret_cast<void*>(slot), readH, readV, wantH, wantV);
            }
            const uint32_t baseBits = g_safeFrameBaseCurvatureBits[i].load(std::memory_order_relaxed);
            if (!HudDestinationPlausible(baseBits))
                continue;
            float authoredDestinationZ = 0.0f;
            memcpy(&authoredDestinationZ, &baseBits, sizeof(authoredDestinationZ));
            // Identical Halo 3 user semantics: 0 is flat, 1 is fully curved,
            // and 0.5 restores this title's retained authored baseline.
            const bool hasDepth = HudLayoutHasDepthField(*adapter);
            const float curvatureDelta = 0.30f - 0.60f * wantCurvature;
            const float targetDestinationZ = hasDepth ? authoredDestinationZ + curvatureDelta : 0.0f;
            uint32_t targetDestinationBits = 0;
            memcpy(&targetDestinationBits, &targetDestinationZ, sizeof(targetDestinationBits));
            if (!HudDestinationPlausible(targetDestinationBits))
                continue;
            if (!adapter->forceWriteEveryPass && (!hasDepth || destinationZ == targetDestinationBits) &&
                h == wantHBits && v == wantVBits)
            {
                ++live;
                continue;
            }
            // Serialize the only foreign writes against title invalidation.
            // A writer already inside this shared section finishes before ODST
            // teardown can release ownership; a writer arriving later observes
            // the bumped generation and performs no store.
            AcquireSRWLockShared(&g_hudLayoutWriteLock);
            const bool stillOwned =
                HudLayoutContextMatches(profile, generation) && HudLayoutResultsMatch(profile, generation);
            const bool wrote = stillOwned && SafeFrameWriteLayout(slot, *adapter, targetDestinationZ, wantH, wantV);
            ReleaseSRWLockShared(&g_hudLayoutWriteLock);
            if (!stillOwned)
                return;
            if (wrote)
                ++live;
        }

        if (live == n)
        {
            g_hudAppliedBits.store(wantBits, std::memory_order_relaxed);
            g_hudAppliedAspectBits.store(wantAspectBits, std::memory_order_relaxed);
            g_hudAppliedCurvatureBits.store(wantCurvatureBits, std::memory_order_relaxed);
        }
        else
        {
            g_hudAppliedBits.store(0, std::memory_order_relaxed);
            g_hudAppliedAspectBits.store(0, std::memory_order_relaxed);
            g_hudAppliedCurvatureBits.store(0, std::memory_order_relaxed);
            g_safeFramePublishedOwner.store(HudLayoutProfile::None, std::memory_order_relaxed);
            g_safeFramePublishedGeneration.store(0, std::memory_order_relaxed);
            g_safeFrameHitCount.store(-1, std::memory_order_release);
        }
    }

    bool TryReacquireSafeFrames(HudLayoutProfile profile)
    {
        const HudLayoutAdapter* adapter = HudLayoutAdapterFor(profile);
        HudLayoutRememberedCache* remembered = HudLayoutRememberedFor(profile);
        const uint32_t generation = g_hudLayoutGeneration.load(std::memory_order_acquire);
        if (!adapter || !remembered || !HudLayoutContextMatches(profile, generation))
            return false;

        const int count = remembered->count.load(std::memory_order_acquire);
        if (!HudLayoutCanReacquireFromRemembered(*adapter, count))
            return false;
        uintptr_t slots[kMaxSafeFrameHits]{};
        uint32_t baselines[kMaxSafeFrameHits]{};
        int accepted = 0;
        for (int i = 0; i < count && i < kMaxSafeFrameHits; ++i)
        {
            const uintptr_t slot = remembered->slots[i].load(std::memory_order_relaxed);
            const uint32_t baseline = remembered->baseCurvatureBits[i].load(std::memory_order_relaxed);
            uint32_t destinationZ = 0, h = 0, v = 0;
            if (slot && SafeFrameVerifySlot(slot, *adapter, &destinationZ, &h, &v) && SafeFramePairPlausible(h, v) &&
                HudDestinationPlausible(destinationZ) && HudDestinationPlausible(baseline))
            {
                slots[accepted] = slot;
                baselines[accepted] = baseline;
                ++accepted;
            }
        }
        if (accepted != count || !HudLayoutAcceptedCountOk(*adapter, accepted) ||
            !HudLayoutContextMatches(profile, generation))
            return false;

        for (int i = 0; i < kMaxSafeFrameHits; ++i)
        {
            g_safeFrameSlots[i].store(i < accepted ? slots[i] : 0, std::memory_order_relaxed);
            g_safeFrameBaseCurvatureBits[i].store(i < accepted ? baselines[i] : 0, std::memory_order_relaxed);
        }
        g_hudAppliedBits.store(0, std::memory_order_relaxed);
        g_hudAppliedAspectBits.store(0, std::memory_order_relaxed);
        g_hudAppliedCurvatureBits.store(0, std::memory_order_relaxed);
        g_safeFramePublishedOwner.store(profile, std::memory_order_relaxed);
        g_safeFramePublishedGeneration.store(generation, std::memory_order_release);
        g_safeFrameHitCount.store(accepted, std::memory_order_release);
        LOG("SAFEFRAME [%s]: reacquired %d title-owned layout block(s); "
            "scan skipped",
            adapter->name, accepted);
        return true;
    }

} // namespace
namespace game_core
{
    void HudLayoutAutoTick(HudLayoutProfile profile)
    {
        const HudLayoutAdapter* adapter = HudLayoutAdapterFor(profile);
        const uint32_t generation = EnsureHudLayoutProfile(profile);
        if (!adapter || !generation)
            return;

        const float wantSize = g_config.hud_size;
        const float wantAspect = g_config.hud_aspect;
        const float wantCurvature = g_config.hud_curvature;
        uint32_t wantBits = 0, wantAspectBits = 0;
        uint32_t wantCurvatureBits = 0;
        memcpy(&wantBits, &wantSize, sizeof(wantBits));
        memcpy(&wantAspectBits, &wantAspect, sizeof(wantAspectBits));
        memcpy(&wantCurvatureBits, &wantCurvature, sizeof(wantCurvatureBits));

        int count = g_safeFrameHitCount.load(std::memory_order_acquire);
        if (count > 0 && !HudLayoutResultsMatch(profile, generation))
        {
            g_safeFrameHitCount.store(-1, std::memory_order_release);
            count = -1;
        }

        const uint64_t now = GetTickCount64();
        if (count > 0)
        {
            const uint64_t lastVerify = g_hudLayoutLastVerifyMs.load(std::memory_order_relaxed);
            // Reach recomputes and overwrites this record itself (read-back
            // proved a fresh authored value can appear with no config change
            // and no title reload): a once-per-second reassert loses that race
            // for most of every second. Reasserting every Present call is the
            // only way ours is reliably the last write before the next draw.
            if (adapter->forceWriteEveryPass || g_hudAppliedBits.load(std::memory_order_relaxed) != wantBits ||
                g_hudAppliedAspectBits.load(std::memory_order_relaxed) != wantAspectBits ||
                g_hudAppliedCurvatureBits.load(std::memory_order_relaxed) != wantCurvatureBits ||
                now - lastVerify >= 1000)
            {
                g_hudLayoutLastVerifyMs.store(now, std::memory_order_relaxed);
                ApplyHudLayoutOnce(profile);
            }
            return;
        }
        if (count == -2 || g_safeFrameScanInFlight.load(std::memory_order_acquire))
            return;

        uint64_t lastCam = 0;
        if (profile == HudLayoutProfile::Halo3)
        {
            lastCam = g_halo3LastCamCopyMs.load(std::memory_order_relaxed);
        }
        else if (profile == HudLayoutProfile::Halo3ODST)
        {
            lastCam = g_odstLastCamCopyMs.load(std::memory_order_relaxed);
        }
        else if (profile == HudLayoutProfile::HaloReach)
        {
            lastCam = g_reachLastCamCopyMs.load(std::memory_order_relaxed);
        }
        if (!lastCam || now < lastCam || now - lastCam > 1000)
            return;

        const bool stockSize = wantSize >= 0.8695f && wantSize <= 0.8705f;
        const bool stockAspect = wantAspect >= 0.9995f && wantAspect <= 1.0005f;
        const bool stockCurvature = wantCurvature >= 0.4995f && wantCurvature <= 0.5005f;
        const bool settingsAreStock = stockSize && stockAspect && stockCurvature;
        if (settingsAreStock)
        {
            HudLayoutRememberedCache* remembered = HudLayoutRememberedFor(profile);
            const int rememberedCount = remembered ? remembered->count.load(std::memory_order_acquire) : 0;
            if (!HudLayoutCanReacquireFromRemembered(*adapter, rememberedCount))
                return;

            const uint64_t lastReacquire = g_hudLayoutLastReacquireMs.load(std::memory_order_relaxed);
            if (now - lastReacquire >= 1000)
            {
                g_hudLayoutLastReacquireMs.store(now, std::memory_order_relaxed);
                if (TryReacquireSafeFrames(profile))
                    ApplyHudLayoutOnce(profile);
            }
            // A stock config never triggers a process-wide scan. Remembered
            // title-owned slots are enough to restore a resident adjusted tag.
            return;
        }

        const uint64_t lastReacquire = g_hudLayoutLastReacquireMs.load(std::memory_order_relaxed);
        if (now - lastReacquire >= 1000)
        {
            g_hudLayoutLastReacquireMs.store(now, std::memory_order_relaxed);
            if (TryReacquireSafeFrames(profile))
                return;
        }

        // A level's tag data is often not resident yet the first time a scan
        // is eligible, so an early attempt finds nothing. A flat 15s cooldown
        // then meant waiting 15s plus a full scan before the HUD moved. Retry
        // quickly while the title is still settling, and only fall back to the
        // long cooldown once it has been owned long enough that a miss means
        // the record genuinely is not there.
        const uint64_t ownerSince = g_hudLayoutOwnerSinceMs.load(std::memory_order_relaxed);
        const bool settling = ownerSince && now >= ownerSince && now - ownerSince < 30000;
        const uint64_t attemptCooldownMs = settling ? 2000 : 15000;
        const uint64_t lastAttempt = g_hudLayoutLastAttemptMs.load(std::memory_order_relaxed);
        if (now - lastAttempt < attemptCooldownMs)
            return;
        // A process-wide scan of every readable private+mapped page (measured
        // 11.3 s in the 2026-08-06 capture) is the largest thing we do to a
        // loading title, and it WRITES the safe-frame slots it accepts. Hold
        // it for the same gate as everything else; the level's tag data is not
        // resident during a loading screen anyway, so a scan launched there
        // was already the one most likely to find nothing.
        if (!g_activeTitleLevelRunning.load(std::memory_order_acquire))
            return;
        g_hudLayoutLastAttemptMs.store(now, std::memory_order_relaxed);
        LaunchSafeFrameScan(profile, "auto: HUD layout is customized and no title-owned slots are located");
    }
} // namespace game_core
namespace
{
} // namespace
namespace game_core
{
#include "body_visibility_switch.inl"
} // namespace game_core
namespace
{

    // (Removed 2026-07-19: the old ResolveChudScale/ApplyChudScale patched the
    // 1.0f immediates in 0x278EE0 - the headset proved those are the CHUD ALPHA,
    // not size. That function turned out to drive game BRIGHTNESS; it's now the
    // brightness hook, HudXformHook. HUD layout is instead controlled through
    // the verified chud_globals curvature fields above.)

} // namespace
namespace game_core
{
    // Locate the first-person weapon slot that owns a composed bone array.
    // Pointer compares only, so it is safe to call before composition too.
    bool FindFirstPersonWeapon(BoneMatrix* bones, int& outSlot, unsigned char*& outWeapon)
    {
        if (!bones || !g_engineTlsIndex)
            return false;
        auto** slots = reinterpret_cast<void**>(__readgsqword(0x58));
        if (!slots)
            return false;
        auto* tls = reinterpret_cast<unsigned char*>(slots[*g_engineTlsIndex]);
        if (!tls)
            return false;
        auto* weapons = *reinterpret_cast<unsigned char**>(tls + 0x568);
        if (!weapons)
            return false;
        for (int candidate = 0; candidate < 2; ++candidate)
        {
            auto* w = weapons + candidate * 0x11BC;
            if (reinterpret_cast<BoneMatrix*>(w + 0x4A4) == bones)
            {
                outSlot = candidate;
                outWeapon = w;
                return true;
            }
        }
        return false;
    }

} // namespace game_core
namespace game_core
{
    void RotateByQuat(const float q[4], const float in[3], float out[3])
    {
        const float x = q[0], y = q[1], z = q[2], w = q[3];
        const float tx = 2 * (y * in[2] - z * in[1]), ty = 2 * (z * in[0] - x * in[2]),
                    tz = 2 * (x * in[1] - y * in[0]);
        out[0] = in[0] + w * tx + (y * tz - z * ty);
        out[1] = in[1] + w * ty + (z * tx - x * tz);
        out[2] = in[2] + w * tz + (x * ty - y * tx);
    }

    float Clamp(float v, float lo, float hi);
    float WrapPi(float a);
} // namespace game_core
namespace
{
    bool ControllerWorldPose(float basis[9], float pos[3], float& scale);
} // namespace
namespace game_core
{
    bool ControllerWorldPoseEx(bool left, float basis[9], float pos[3], float& scale);
} // namespace game_core
namespace
{

} // namespace
namespace game_core
{
    void BuildTrackedGameBasisFromFrame(
        const float q[4], bool head, bool followValid, float hullYaw, float hullPitch, float basis[9])
    {
        const float xrForward[3] = {0, 0, -1}, xrUp[3] = {0, 1, 0};
        float f[3], u[3];
        RotateByQuat(q, xrForward, f);
        RotateByQuat(q, xrUp, u);
        const float yaw = atan2f(f[0], -f[2]);
        const float pitch = asinf(Clamp(f[1], -1.0f, 1.0f));

        // Roll is measured around the tracked forward axis exactly as it is in
        // ApplyHeadLook, so head and controller pass through the same mapping.
        float rx = -f[2], rz = f[0];
        float rl = sqrtf(rx * rx + rz * rz);
        if (rl < 1e-4f)
            rl = 1e-4f;
        rx /= rl;
        rz /= rl;
        const float nux = -f[1] * rz, nuy = rl, nuz = f[1] * rx;
        const float roll = atan2f(u[0] * rx + u[2] * rz, u[0] * nux + u[1] * nuy + u[2] * nuz);

        if (followValid)
        {
            const float trackedYawDelta = g_yawSign.load() * WrapPi(yaw - g_headYawRef);
            const float trackedPitch =
                Clamp(g_pitchSign.load() * pitch + (head ? g_pitchTrim.load() : 0.0f), -1.5f, 1.5f);
            if (Halo3ComposeFollowBasisWithRoll(
                    hullYaw, hullPitch, g_gameYawRef, trackedYawDelta, trackedPitch, roll, basis))
                return;
        }

        // Exact established mapping when optional follow is off, stale, or
        // invalid. That feature can fail without affecting controller poses.
        const float gy = g_gameYawRef + g_yawSign.load() * WrapPi(yaw - g_headYawRef);
        const float gp = Clamp(g_pitchSign.load() * pitch + (head ? g_pitchTrim.load() : 0.0f), -1.5f, 1.5f);
        const float cp = cosf(gp), sp = sinf(gp), cy = cosf(gy), sy = sinf(gy);
        const float cr = cosf(roll), sr = sinf(roll);
        const float forward[3] = {cp * cy, cp * sy, sp};
        const float up[3] = {(-sp * cy) * cr + sy * sr, (-sp * sy) * cr - cy * sr, cp * cr};
        const float left[3] = {up[1] * forward[2] - up[2] * forward[1], up[2] * forward[0] - up[0] * forward[2],
            up[0] * forward[1] - up[1] * forward[0]};
        memcpy(basis, forward, sizeof(forward));
        memcpy(basis + 3, left, sizeof(left));
        memcpy(basis + 6, up, sizeof(up));
    }
} // namespace game_core
namespace
{

    void BuildTrackedGameBasis(const float q[4], bool head, float basis[9])
    {
        float hullYaw = 0.0f, hullPitch = 0.0f;
        const bool followValid = Halo3ReadRollStableFollow(hullYaw, hullPitch);
        BuildTrackedGameBasisFromFrame(q, head, followValid, hullYaw, hullPitch, basis);
    }

    // Basis columns (forward,left,up) from game-frame yaw/pitch/roll.
} // namespace
namespace game_core
{
    void BasisFromAngles(float yaw, float pitch, float roll, float basis[9])
    {
        const float cp = cosf(pitch), sp = sinf(pitch), cy = cosf(yaw), sy = sinf(yaw);
        const float cr = cosf(roll), sr = sinf(roll);
        const float forward[3] = {cp * cy, cp * sy, sp};
        const float up[3] = {(-sp * cy) * cr + sy * sr, (-sp * sy) * cr - cy * sr, cp * cr};
        const float left[3] = {up[1] * forward[2] - up[2] * forward[1], up[2] * forward[0] - up[0] * forward[2],
            up[0] * forward[1] - up[1] * forward[0]};
        memcpy(basis, forward, sizeof(forward));
        memcpy(basis + 3, left, sizeof(left));
        memcpy(basis + 6, up, sizeof(up));
    }

    // Column-basis product out = left * right. Element (row,column) is stored
    // at [column*3+row]. Keeping this in one helper makes the otherwise subtle
    // H^-1*C order explicit at the weapon-bank call site.
    void MultiplyBases(const float left[9], const float right[9], float out[9])
    {
        for (int c = 0; c < 3; ++c)
            for (int r = 0; r < 3; ++r)
            {
                float v = 0.0f;
                for (int k = 0; k < 3; ++k)
                    v += left[k * 3 + r] * right[c * 3 + k];
                out[c * 3 + r] = v;
            }
    }

    // Shortest-arc rotation (column-major 3x3) taking unit vector `from` onto
    // unit vector `to`. Used to swing a bone's rest orientation onto its new
    // IK-solved child direction while carrying the authored twist.
    void ShortestArcRotation(const float from[3], const float to[3], float out[9])
    {
        const float c = from[0] * to[0] + from[1] * to[1] + from[2] * to[2];
        float v[3] = {
            from[1] * to[2] - from[2] * to[1], from[2] * to[0] - from[0] * to[2], from[0] * to[1] - from[1] * to[0]};
        if (c > 0.99999f)
        {
            out[0] = out[4] = out[8] = 1;
            out[1] = out[2] = out[3] = out[5] = out[6] = out[7] = 0;
            return;
        }
        if (c < -0.99999f)
        {
            // 180 degrees: rotate about any axis perpendicular to `from`.
            float ax[3] = {1, 0, 0};
            if (fabsf(from[0]) > 0.9f)
            {
                ax[0] = 0;
                ax[1] = 1;
            }
            float p[3] = {from[1] * ax[2] - from[2] * ax[1], from[2] * ax[0] - from[0] * ax[2],
                from[0] * ax[1] - from[1] * ax[0]};
            const float pl = sqrtf(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
            if (pl < 1e-5f)
            {
                out[0] = out[4] = out[8] = 1;
                out[1] = out[2] = out[3] = out[5] = out[6] = out[7] = 0;
                return;
            }
            p[0] /= pl;
            p[1] /= pl;
            p[2] /= pl;
            // R = 2*p*p^T - I (180 deg about p), stored column-major.
            for (int col = 0; col < 3; ++col)
                for (int row = 0; row < 3; ++row)
                    out[col * 3 + row] = 2.0f * p[row] * p[col] - (row == col ? 1.0f : 0.0f);
            return;
        }
        // Rodrigues: R = I + [v]x + [v]x^2 / (1+c). Column-major m[col*3+row].
        const float k = 1.0f / (1.0f + c);
        const float vx = v[0], vy = v[1], vz = v[2];
        // rows of R:
        const float r00 = 1 - (vy * vy + vz * vz) * k, r01 = -vz + vx * vy * k, r02 = vy + vx * vz * k;
        const float r10 = vz + vx * vy * k, r11 = 1 - (vx * vx + vz * vz) * k, r12 = -vx + vy * vz * k;
        const float r20 = -vy + vx * vz * k, r21 = vx + vy * vz * k, r22 = 1 - (vx * vx + vy * vy) * k;
        out[0] = r00;
        out[1] = r10;
        out[2] = r20; // column 0 (rows 0,1,2)
        out[3] = r01;
        out[4] = r11;
        out[5] = r21; // column 1
        out[6] = r02;
        out[7] = r12;
        out[8] = r22; // column 2
    }
} // namespace game_core
namespace
{

    // Column-basis product out = orthonormalLeft^-1 * right = left^T * right.
    void MultiplyInverseBasis(const float orthonormalLeft[9], const float right[9], float out[9])
    {
        for (int c = 0; c < 3; ++c)
            for (int r = 0; r < 3; ++r)
            {
                float v = 0.0f;
                for (int k = 0; k < 3; ++k)
                    v += orthonormalLeft[r * 3 + k] * right[c * 3 + k];
                out[c * 3 + r] = v;
            }
    }

} // namespace
namespace game_core
{
    bool NormalizedBasis(const BoneMatrix& matrix, float out[9])
    {
        for (int c = 0; c < 3; ++c)
        {
            float lengthSquared = 0.0f;
            for (int r = 0; r < 3; ++r)
            {
                out[c * 3 + r] = matrix.rotation[c * 3 + r];
                lengthSquared += out[c * 3 + r] * out[c * 3 + r];
            }
            const float length = sqrtf(lengthSquared);
            if (!isfinite(length) || length < 0.001f)
                return false;
            for (int r = 0; r < 3; ++r)
                out[c * 3 + r] /= length;
        }
        // A corrupt/non-orthogonal pose is much more dangerous than dropping
        // one weapon palette. Normal animation matrices are orthonormal to
        // floating-point noise.
        for (int a = 0; a < 3; ++a)
            for (int b = a + 1; b < 3; ++b)
            {
                float dot = 0.0f;
                for (int r = 0; r < 3; ++r)
                    dot += out[a * 3 + r] * out[b * 3 + r];
                if (!isfinite(dot) || fabsf(dot) > 0.02f)
                    return false;
            }
        return true;
    }
} // namespace game_core
namespace
{

} // namespace
namespace game_core
{
    bool ComposeBoneMatrices(const BoneMatrix& left, const BoneMatrix& right, BoneMatrix& output)
    {
        if (!isfinite(left.scale) || !isfinite(right.scale) || fabsf(left.scale) < 0.001f ||
            fabsf(right.scale) < 0.001f)
            return false;
        float leftBasis[9], rightBasis[9];
        if (!NormalizedBasis(left, leftBasis) || !NormalizedBasis(right, rightBasis))
            return false;
        BoneMatrix result{};
        result.scale = left.scale * right.scale;
        MultiplyBases(leftBasis, rightBasis, result.rotation);
        for (int r = 0; r < 3; ++r)
        {
            float rotated = 0.0f;
            for (int c = 0; c < 3; ++c)
                rotated += leftBasis[c * 3 + r] * right.translation[c];
            result.translation[r] = left.translation[r] + left.scale * rotated;
            if (!isfinite(result.translation[r]))
                return false;
        }
        output = result;
        return true;
    }

    bool InvertBoneMatrix(const BoneMatrix& input, BoneMatrix& output)
    {
        if (!isfinite(input.scale) || fabsf(input.scale) < 0.001f)
            return false;
        float basis[9];
        if (!NormalizedBasis(input, basis))
            return false;
        BoneMatrix result{};
        result.scale = 1.0f / input.scale;
        for (int c = 0; c < 3; ++c)
            for (int r = 0; r < 3; ++r)
                result.rotation[c * 3 + r] = basis[r * 3 + c];
        for (int r = 0; r < 3; ++r)
        {
            float v = 0.0f;
            for (int c = 0; c < 3; ++c)
                v += result.rotation[c * 3 + r] * input.translation[c];
            result.translation[r] = -v / input.scale;
            if (!isfinite(result.translation[r]))
                return false;
        }
        output = result;
        return true;
    }
    bool LoadCameraBasis(float basis[9])
    {
        if (!g_camValid.load())
            return false;
        for (int j = 0; j < 3; ++j)
        {
            basis[j] = g_camFwd[j].load();
            basis[6 + j] = g_camUp[j].load();
        }
        basis[3] = basis[7] * basis[2] - basis[8] * basis[1];
        basis[4] = basis[8] * basis[0] - basis[6] * basis[2];
        basis[5] = basis[6] * basis[1] - basis[7] * basis[0];
        return true;
    }
} // namespace game_core
namespace game_core
{

    bool GetControllerFirstPersonTransform(int slot, float target[3], float desired[9])
    {
        // Slot 0 is Halo's right-hand first-person weapon. Do not move a
        // second/left-hand slot as part of the right-controller override.
        if (slot != 0 || !g_vrAim.load() || !g_enabled.load())
            return false;
        if (!g_aimSeen.load())
            return false;
        float hq[4], hp[3], cq[4], cp[3];
        if (!VR_GetHeadPose(hq, hp) || !VR_GetAimPose(cq, cp))
            return false;

        // The visible renderer supplies the head camera as the skeleton root:
        //     World = Head * record
        // Therefore the replacement record (not a delta composed onto the
        // engine's already head-baked record) must be:
        //     record = Head^-1 * Controller
        // so World = Head * Head^-1 * Controller = Controller.
        const float ih[4] = {-hq[0], -hq[1], -hq[2], hq[3]};
        const float dp[3] = {cp[0] - hp[0], cp[1] - hp[1], cp[2] - hp[2]};
        float rp[3];
        RotateByQuat(ih, dp, rp);
        const float s = g_worldScale.load();
        // Head basis = the EXACT vectors the engine's camera consumed this
        // tick (captured in CamCopyHook), not a re-derivation. left = up x fwd.
        float headBasis[9], controllerBasis[9];
        if (!LoadCameraBasis(headBasis))
            return false;
        BuildTrackedGameBasis(cq, false, controllerBasis);
        // The renderer draws World = Root * record, Root = (Head, camPos).
        // We want World = Controller, so record must be Head^-1 * Controller,
        // and the offset must be expressed IN the head frame (Head^-1 * world
        // offset == the head-frame components themselves).
        target[0] = -rp[2] * s;
        target[1] = -rp[0] * s;
        target[2] = rp[1] * s; // (fwd,left,up)
        // Head-relative with the engine's EXACT camera floats. The two
        // bracketing experiments (2026-07-15 ~05:1x): zero head terms -> the
        // gun FOLLOWS the head (proves the render root carries the camera
        // orientation); head-cancellation -> counter-wobble DURING head
        // motion only (the renderer samples the camera on its own interpolated
        // 120Hz clock; our 60Hz sim-side write is stale by up to a tick).
        // This form is exactly right whenever the head is not mid-motion; the
        // residual is a velocity-proportional wobble + a one-tick flick on
        // snap turns. Fixing THAT requires cancelling on the renderer's clock
        // = intercepting the render-side FP root build (not yet located; see
        // CONTINUATION). Do not retry frame-algebra variants: both directions
        // are already falsified.
        // rel = Head^-1 * Controller. ORDER IS THE WHOLE BUG (2026-07-15 05:2x):
        //   rel = Controller * Head^T  -> World = H*C*H^T = CONJUGATION: the
        //     head rotation is applied TWICE -> "gun tracks inverted".
        //   rel = 0 head terms         -> World = H*C -> "gun follows head".
        //   rel = Head^T * Controller  -> World = H*H^T*C = C. Correct.
        // Storage is column-major (m[c*3+j] = component j of column c), so
        // (H^T C)(r,c) = sum_k H(k,r)C(k,c) => rel[c*3+r] = sum_k H[r*3+k]*C[c*3+k].
        float rel[9];
        MultiplyInverseBasis(headBasis, controllerBasis, rel);

        // Verify the storage/order invariant in the live build. If an engine
        // update ever changes the basis convention, do not write a plausible
        // but wrong quaternion into the animation bank.
        float reconstructedController[9];
        MultiplyBases(headBasis, rel, reconstructedController);
        float maxError = 0.0f;
        for (int i = 0; i < 9; ++i)
            maxError = fmaxf(maxError, fabsf(reconstructedController[i] - controllerBasis[i]));
        if (!isfinite(maxError) || maxError > 0.002f)
            return false;
        memcpy(desired, rel, sizeof(rel));

        return true;
    }
} // namespace game_core
namespace
{

    // AUTO BARREL ALIGNMENT source data. In the authored first-person pose the
    // barrel lies on the camera-forward axis (Halo aims the viewmodel at the
    // center reticle), and the render root IS the camera, so in record space
    // the authored barrel direction expressed in the wrist frame is
    // invRot(wrist) * (1,0,0). That is a rig constant per weapon (the gun is
    // glued to the hand); a slow EMA rides out idle sway and reload swings.
    // Written by the game thread (FpInterpolateHook), read by DesiredWristWorld.
    // Authored barrel-in-wrist direction per held-weapon slot: the primary
    // (slot 0) aligns to the right controller ray, the dual-wield secondary
    // (slot 1) to the left. Each skeleton's own animated wrist is measured -
    // the secondary's authored pose differs from the primary's.
} // namespace
namespace game_core
{
    std::atomic<float> g_barrelInWrist[2][3];
    std::atomic<bool> g_barrelInWristValid[2] = {{false}, {false}};
} // namespace game_core
namespace
{

    // Marker/effect packet builders also consume 0x184B08 directly. Preserve
    // their already headset-verified controller registration on the live
    // interpolation buffer. The visible palette never consumes this mutation:
    // it receives the private unmodified copy reconstructed below.
    // Same single same-frame rigid transform as the visible palette (wrist ->
    // controller), applied to the live interpolation buffer that feeds
    // markers/muzzle effects, so the flash and the gun cannot diverge.
} // namespace
namespace game_core
{
    bool ApplyControllerToMarkerBonesWithTarget(const BoneMatrix& root, const BoneMatrix& desiredWristWorld,
        float meshScale, BoneMatrix* bones, int count, int transformAnchor)
    {
        if (!bones || count <= 0 || count > 120 || transformAnchor < 0 || transformAnchor >= count ||
            !isfinite(meshScale) || meshScale <= 0.0f)
        {
            return false;
        }
        BoneMatrix candidate[120]{};
        memcpy(candidate, bones, static_cast<size_t>(count) * sizeof(BoneMatrix));
        BoneMatrix wristWorld{}, inverseWristWorld{}, t{}, inverseRoot{}, tRoot{}, m{};
        if (!ComposeBoneMatrices(root, candidate[transformAnchor], wristWorld) ||
            !InvertBoneMatrix(wristWorld, inverseWristWorld) ||
            !ComposeBoneMatrices(desiredWristWorld, inverseWristWorld, t) || !InvertBoneMatrix(root, inverseRoot) ||
            !ComposeBoneMatrices(t, root, tRoot) || !ComposeBoneMatrices(inverseRoot, tRoot, m))
            return false;
        for (int i = 0; i < count; ++i)
        {
            BoneMatrix transformed{};
            if (!ComposeBoneMatrices(m, candidate[i], transformed))
                return false;
            candidate[i] = transformed;
        }
        if (meshScale != 1.0f)
        {
            const float anchor[3] = {candidate[transformAnchor].translation[0],
                candidate[transformAnchor].translation[1], candidate[transformAnchor].translation[2]};
            for (int i = 0; i < count; ++i)
            {
                for (int r = 0; r < 3; ++r)
                    candidate[i].translation[r] = anchor[r] + (candidate[i].translation[r] - anchor[r]) * meshScale;
                candidate[i].scale *= meshScale;
                if (!isfinite(candidate[i].scale) || !isfinite(candidate[i].translation[0]) ||
                    !isfinite(candidate[i].translation[1]) || !isfinite(candidate[i].translation[2]))
                {
                    return false;
                }
            }
        }
        const std::span<const float> packed{
            reinterpret_cast<const float*>(candidate), static_cast<size_t>(count) * kReachFpBoneMatrixFloatCount};
        return ReachFpCommitGraphIfFinite(packed, static_cast<size_t>(count),
            [&]()
            {
                memcpy(bones, candidate, static_cast<size_t>(count) * sizeof(BoneMatrix));
                return true;
            });
    }
} // namespace game_core
namespace
{

} // namespace
namespace game_core
{
    bool ApplyControllerToMarkerBonesFromRoot(
        BoneMatrix root, BoneMatrix* bones, int count, int wrist, int leftWrist, bool dual)
    {
        if (!bones || count <= 0 || count > 64 || wrist < 0 || wrist >= count)
            return false;
        // Root POSE from the LIVE center camera, not the TLS cache: the cache
        // can hold a stale or per-eye root, and the transform baked against it
        // put an IPD-sized static offset on the flash and made it follow head
        // motion (2026-07-19 report: "flash follows my head and hand, offset
        // from the gun; bullets spawn further out"). The old headset-proven
        // flash lever used these same camera atomics. TLS root keeps only its
        // scale.
        {
            float camBasis[9];
            if (g_camValid.load() && LoadCameraBasis(camBasis))
            {
                memcpy(root.rotation, camBasis, sizeof(camBasis));
                root.translation[0] = g_camX.load();
                root.translation[1] = g_camY.load();
                root.translation[2] = g_camZ.load();
            }
        }
        // IDENTICAL hand target as the visible gun (one shared definition) so
        // muzzle flash and weapon can never diverge. A dual-wield secondary is
        // carried by the slot's LEFT hand; the hand follows the controller and
        // the weapon/marker assembly inherits that same rigid hand delta.
        const int transformAnchor = dual ? leftWrist : wrist;
        if (transformAnchor < 0 || transformAnchor >= count)
            return false;
        float meshScale = 1.0f;
        BoneMatrix desiredWristWorld{};
        if (!DesiredWristWorld(dual, desiredWristWorld, meshScale))
            return false;
        if (!ApplyControllerToMarkerBonesWithTarget(root, desiredWristWorld, meshScale, bones, count, transformAnchor))
            return false;
        static std::atomic<bool> logged{false};
        if (!logged.exchange(true))
            LOG("marker/muzzle bones rigid-parented with the same transform as the gun");
        return true;
    }
    LegacyWorldCollisionFeature* LegacyCollisionForTitle(GameTitle title)
    {
        switch (title)
        {
        case GameTitle::Halo3:
            return &g_halo3WorldCollision;
        case GameTitle::Halo3ODST:
            return &g_odstWorldCollision;
        case GameTitle::HaloReach:
            return &g_reachWorldCollision;
        default:
            return nullptr;
        }
    }
} // namespace game_core
namespace
{

    void LegacyPublishCorrection(LegacyWorldCollisionFeature& feature, int hand, uint32_t generation,
        const float source[3], const float correction[3], bool contact)
    {
        auto& publication = feature.correction[hand];
        publication.version.fetch_add(1, std::memory_order_acq_rel);
        publication.generation = generation;
        publication.publishedAtMs = GetTickCount64();
        publication.contact = contact;
        memcpy(publication.source, source, sizeof(publication.source));
        memcpy(publication.correction, correction, sizeof(publication.correction));
        publication.version.fetch_add(1, std::memory_order_release);
    }

    bool LegacyReadCorrection(LegacyWorldCollisionFeature& feature, int hand, uint32_t& generation,
        uint64_t& publishedAtMs, bool& contact, float source[3], float correction[3])
    {
        const auto& publication = feature.correction[hand];
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const uint32_t before = publication.version.load(std::memory_order_acquire);
            if (before & 1u)
                continue;
            generation = publication.generation;
            publishedAtMs = publication.publishedAtMs;
            contact = publication.contact;
            memcpy(source, publication.source, sizeof(publication.source));
            memcpy(correction, publication.correction, sizeof(publication.correction));
            std::atomic_thread_fence(std::memory_order_acquire);
            if (publication.version.load(std::memory_order_acquire) == before)
                return Halo4WorldCollisionFiniteVector(source) && Halo4WorldCollisionFiniteVector(correction);
        }
        return false;
    }

} // namespace
namespace game_core
{
    bool LegacyApplyWorldCollision(GameTitle title, int hand, BoneMatrix& target, float* appliedCorrection)
    {
        if (appliedCorrection)
            memset(appliedCorrection, 0, sizeof(float) * 3);
        LegacyWorldCollisionFeature* feature = LegacyCollisionForTitle(title);
        if (!feature || hand < 0 || hand > 1 || !g_config.world_collision ||
            !feature->installed.load(std::memory_order_acquire) || TitleAdapter_GetActiveTitle() != title ||
            !Halo4WorldCollisionFiniteVector(target.translation))
            return false;
        uint32_t generation = 0;
        uint64_t publishedAtMs = 0;
        bool contact = false;
        float source[3]{}, correction[3]{};
        if (!LegacyReadCorrection(*feature, hand, generation, publishedAtMs, contact, source, correction) || !contact ||
            generation != feature->generation)
            return false;
        const uint64_t now = GetTickCount64();
        const float worldScale = g_worldScale.load(std::memory_order_relaxed);
        const float drift = Halo4WorldCollisionDistanceSquared(source, target.translation);
        const float limit = 0.15f * worldScale;
        float correctionSquared = 0.0f;
        for (float value : correction)
            correctionSquared += value * value;
        if (publishedAtMs > now || now - publishedAtMs > 150 || drift < 0.0f || drift > limit * limit ||
            !std::isfinite(correctionSquared) || correctionSquared > 0.75f * worldScale * 0.75f * worldScale)
            return false;
        for (int axis = 0; axis < 3; ++axis)
            target.translation[axis] += correction[axis];
        if (appliedCorrection)
            memcpy(appliedCorrection, correction, sizeof(correction));
        feature->applied[hand].fetch_add(1, std::memory_order_relaxed);
        return true;
    }
} // namespace game_core
namespace
{

    void LegacyPublishTarget(LegacyWorldCollisionFeature& feature, int hand, uint32_t generation,
        const float samples[][3], uint32_t sampleCount, uint32_t handSampleCount, int32_t ignoredObject,
        uint64_t shapeId = 0)
    {
        if (hand < 0 || hand > 1 || !samples || !generation || !sampleCount ||
            sampleCount > kLegacyCollisionMaxSamples || handSampleCount > sampleCount)
            return;
        for (uint32_t sample = 0; sample < sampleCount; ++sample)
            if (!Halo4WorldCollisionFiniteVector(samples[sample]))
                return;
        auto& publication = feature.targetPublication[hand];
        publication.version.fetch_add(1, std::memory_order_acq_rel);
        publication.generation = generation;
        publication.publishedAtMs = GetTickCount64();
        publication.sampleCount = sampleCount;
        publication.handSampleCount = handSampleCount;
        publication.shapeId = shapeId;
        publication.ignoredObject = ignoredObject;
        memcpy(publication.samples, samples, sizeof(float) * sampleCount * 3);
        publication.version.fetch_add(1, std::memory_order_release);
    }

    bool LegacyReadTarget(LegacyWorldCollisionFeature& feature, int hand, uint32_t& generation, uint64_t& publishedAtMs,
        uint32_t& sampleCount, uint32_t& handSampleCount, int32_t& ignoredObject, float samples[][3], uint64_t& shapeId)
    {
        const auto& publication = feature.targetPublication[hand];
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const uint32_t before = publication.version.load(std::memory_order_acquire);
            if (before & 1u)
                continue;
            generation = publication.generation;
            publishedAtMs = publication.publishedAtMs;
            sampleCount = publication.sampleCount;
            handSampleCount = publication.handSampleCount;
            shapeId = publication.shapeId;
            ignoredObject = publication.ignoredObject;
            if (!generation || !sampleCount || sampleCount > kLegacyCollisionMaxSamples ||
                handSampleCount > sampleCount)
                return false;
            memcpy(samples, publication.samples, sizeof(float) * sampleCount * 3);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (publication.version.load(std::memory_order_acquire) != before)
                continue;
            for (uint32_t sample = 0; sample < sampleCount; ++sample)
                if (!Halo4WorldCollisionFiniteVector(samples[sample]))
                    return false;
            return true;
        }
        return false;
    }

    void LegacyWorldCollisionTickBody(LegacyWorldCollisionFeature& feature)
    {
        if (!feature.installed.load(std::memory_order_acquire) || !feature.resolver || !g_config.world_collision ||
            TitleAdapter_GetActiveTitle() != feature.title || !g_enabled.load(std::memory_order_acquire) ||
            !VR_IsStereoEnabled())
            return;
        const uint64_t now = GetTickCount64();
        uint64_t due = feature.nextQueryAtMs.load(std::memory_order_relaxed);
        if (now < due || !feature.nextQueryAtMs.compare_exchange_strong(
                             due, now + 33, std::memory_order_acq_rel, std::memory_order_relaxed))
            return;
        feature.queryTicks.fetch_add(1, std::memory_order_relaxed);
        const float worldScale = g_worldScale.load(std::memory_order_relaxed);
        if (!std::isfinite(worldScale) || worldScale <= 0.0f)
            return;
        for (int hand = 0; hand < 2; ++hand)
        {
            uint32_t generation = 0, sampleCount = 0, handSampleCount = 0;
            uint64_t targetAtMs = 0;
            uint64_t shapeId = 0;
            int32_t ignoredObject = -1;
            float desired[kLegacyCollisionMaxSamples][3]{};
            auto& worker = feature.worker[hand];
            if (!LegacyReadTarget(feature, hand, generation, targetAtMs, sampleCount, handSampleCount, ignoredObject,
                    desired, shapeId) ||
                generation != feature.generation || targetAtMs > now || now - targetAtMs > 150)
            {
                feature.targetMisses.fetch_add(1, std::memory_order_relaxed);
                worker.seeded = false;
                const float zero[3]{};
                LegacyPublishCorrection(feature, hand, feature.generation, zero, zero, false);
                continue;
            }
            if (!worker.seeded || worker.generation != generation || worker.sampleCount != sampleCount ||
                worker.shapeId != shapeId ||
                Halo4WorldCollisionMovementIsTeleport(worker.accepted[0], desired[0], worldScale))
            {
                feature.seeds.fetch_add(1, std::memory_order_relaxed);
                worker.smoothing.Reset();
                worker.seeded = true;
                worker.generation = generation;
                worker.publishedAtMs = targetAtMs;
                worker.sampleCount = sampleCount;
                worker.shapeId = shapeId;
                memcpy(worker.accepted, desired, sizeof(float) * sampleCount * 3);
                const float zero[3]{};
                LegacyPublishCorrection(feature, hand, generation, desired[0], zero, false);
                continue;
            }
            bool contact = false;
            uint32_t contactSample = 0;
            float strongest[3]{};
            float strongestSquared = 0.0f;
            for (uint32_t sample = 0; sample < sampleCount; ++sample)
            {
                if (Halo4WorldCollisionDistanceSquared(worker.accepted[sample], desired[sample]) < 1.0e-10f)
                    continue;
                float accepted[3]{desired[sample][0], desired[sample][1], desired[sample][2]};
                bool resolved = false;
                g_legacyCollisionOwnedQuery = true;
                __try
                {
                    if (feature.fourArgumentResolver)
                        resolved = reinterpret_cast<ReachCollisionResolveFn>(feature.resolver)(
                                       worker.accepted[sample], desired[sample], accepted, ignoredObject) != 0;
                    else
                        resolved = reinterpret_cast<LegacyCollisionResolveFn>(feature.resolver)(
                            worker.accepted[sample], desired[sample], accepted, ignoredObject, -1);
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    GUARD_COUNT("the game's collision resolver faulted");
                    g_legacyCollisionOwnedQuery = false;
                    feature.failures.fetch_add(1, std::memory_order_relaxed);
                    feature.installed.store(false, std::memory_order_release);
                    return;
                }
                g_legacyCollisionOwnedQuery = false;
                feature.queries.fetch_add(1, std::memory_order_relaxed);
                if (!Halo4WorldCollisionFiniteVector(accepted))
                {
                    feature.failures.fetch_add(1, std::memory_order_relaxed);
                    feature.installed.store(false, std::memory_order_release);
                    return;
                }
                (void)resolved;
                float correction[3]{};
                float correctionSquared = 0.0f;
                for (int axis = 0; axis < 3; ++axis)
                {
                    correction[axis] = accepted[axis] - desired[sample][axis];
                    correctionSquared += correction[axis] * correction[axis];
                }
                if (correctionSquared > 1.0e-10f && (!contact || correctionSquared > strongestSquared))
                {
                    contact = true;
                    contactSample = sample;
                    strongestSquared = correctionSquared;
                    memcpy(strongest, correction, sizeof(strongest));
                }
            }
            for (uint32_t sample = 0; sample < sampleCount; ++sample)
                for (int axis = 0; axis < 3; ++axis)
                    worker.accepted[sample][axis] = desired[sample][axis] + (contact ? strongest[axis] : 0.0f);
            worker.publishedAtMs = targetAtMs;
            const bool visibleCorrection = worker.smoothing.Apply(now, contact, worldScale, strongest);
            LegacyPublishCorrection(feature, hand, generation, desired[0], strongest, visibleCorrection);
            if (contact)
            {
                if (contactSample < handSampleCount)
                    feature.contacts[hand].fetch_add(1, std::memory_order_relaxed);
                else
                    feature.weaponContacts.fetch_add(1, std::memory_order_relaxed);
                VR_PulseContactHaptics(hand == 0, feedback::Strength(feedback::Cue::WorldContact));
            }
        }
    }

    void LegacyWorldCollisionTick(LegacyWorldCollisionFeature& feature)
    {
        // The vector entry may be used by several engine threads. Serialize
        // our own worker with a nonblocking lease; stock calls never wait.
        bool idle = false;
        if (!feature.queryActive.compare_exchange_strong(idle, true, std::memory_order_acq_rel))
            return;
        __try
        {
            LegacyWorldCollisionTickBody(feature);
        }
        __finally
        {
            feature.queryActive.store(false, std::memory_order_release);
        }
    }

} // namespace
namespace game_core
{
#include "../titles/h3/halo3_collision_dispatch.inl"
} // namespace game_core
namespace
{

    // Mirror of vr.cpp's TelemetryDirectionUsable, which lives in that
    // translation unit's anonymous namespace and is not reachable here; the
    // predicate and threshold are identical (finite components, squared length
    // > 1e-12). The sparse shot channel must apply the same usability rule as
    // the frame's engine_aim_* capture, so a read that succeeds but carries no
    // direction can never publish a real source ordinal next to a zero or
    // non-finite vector.
    bool TelemetryShotDirectionUsable(const float value[3]) noexcept
    {
        if (!std::isfinite(value[0]) || !std::isfinite(value[1]) || !std::isfinite(value[2]))
            return false;
        const double lengthSquared = static_cast<double>(value[0]) * value[0] +
                                     static_cast<double>(value[1]) * value[1] +
                                     static_cast<double>(value[2]) * value[2];
        return lengthSquared > 1e-12;
    }

    // ---- T-2 sparse shot events (read-only evidence) ----
    // One fixed `shot` event per actual firing-path invocation, published from
    // the title's own firing detour after the final origin/direction are
    // decided (post-substitution, post-native call) and inside that detour's
    // SEH scope. Title wrappers pass Telemetry_WeaponEventsAccepting() FIRST:
    // with recording off the only work is one atomic load, and no other field
    // below is read. The event channel's admission/sequence/gap semantics are
    // unchanged (Telemetry_PublishShotEvent).
    //
    //   origin/direction  the FINAL ray the engine consumes, engine-world
    //                     units; a non-finite ray suppresses the event rather
    //                     than publishing a fabricated one.
    //   engineAim         title-qualified engine aim feedback snapshot (zeros
    //                     when that publication is unavailable or carries no
    //                     usable direction, the frame capture's rule) plus its
    //                     engineAimSource ordinal (0 when no usable snapshot).
    //   reticleDirection  consumer-visible presented reticle forward in
    //                     OpenXR LOCAL (zeros when unavailable). Capture lags
    //                     the reticle block by construction, so this is
    //                     normally the previous prepared serial's pose; no
    //                     serial equality is implied or asserted.
    //   preparedSerial    the latest published prepared serial at publish (0
    //                     before the first publish). The firing path consumes
    //                     no serial inside the weapon transaction, so this is a
    //                     bounded snapshot, never an equality claim.
    //   slot/barrel       kTelemetryShotIndexUnknown when the firing context
    //                     does not carry them (serialized as -1).
    //   flags             bit0 predicted, bit1 substituted (the mod's
    //                     independent path produced the final ray), bit2
    //                     vrAimActive, bit3 twoHandActive, bit4 leftHanded,
    //                     bit5 dualActive, bit6 firesFromCamera, bit7 unitAim
    //                     (unknown = 0; the title passes what its own call
    //                     carries).
} // namespace
namespace game_core
{
    void PublishShotDiagnostic(GameTitle title, uint32_t titleGeneration, uint32_t unit, uint32_t weapon, uint8_t slot,
        uint8_t barrel, bool predicted, bool substituted, bool firesFromCamera, bool unitAim, const float origin[3],
        const float direction[3]) noexcept
    {
        if (!Telemetry_WeaponEventsAccepting())
            return; // one atomic load; no diagnostic read happens when off
        if (!origin || !direction)
            return;
        for (int axis = 0; axis < 3; ++axis)
            if (!std::isfinite(origin[axis]) || !std::isfinite(direction[axis]))
                return; // never publish a non-finite final ray
        TelemetryShotPayload shot{};
        shot.slot = slot;
        shot.barrel = barrel;
        if (predicted)
            shot.flags |= 0x0001u;
        if (substituted)
            shot.flags |= 0x0002u;
        if (g_vrAim.load(std::memory_order_acquire))
            shot.flags |= 0x0004u;
        // Bounded snapshot of already-published presentation state: three
        // lock-free reads, never the aim getter, no lock/allocation/sampling.
        const VRShotFlagSnapshot presentation = VR_GetShotFlagSnapshot();
        if (presentation.twoHandActive)
            shot.flags |= 0x0008u;
        if (presentation.leftHanded)
            shot.flags |= 0x0010u;
        if (presentation.dualActive)
            shot.flags |= 0x0020u;
        if (firesFromCamera)
            shot.flags |= 0x0040u;
        if (unitAim)
            shot.flags |= 0x0080u;
        // Engine aim feedback, title-qualified, with the same source vocabulary
        // as the frame's engine_aim_source. Every read below is bounded and
        // lock-free; an unavailable or unusable publication (read failure, or
        // a direction that is non-finite/zero-length per
        // TelemetryShotDirectionUsable, the frame capture's own rule) leaves
        // the zero vector and source 0 (never a guessed ray). Halo 3/ODST read
        // the shared g_aimFwd publication (ODST's camera copy is
        // title-qualified); Reach prefers a coherent seated sample and labels
        // the shared compact fallback; Halo 4 reads its stereo-transaction
        // observer engine aim.
        if (title == GameTitle::Halo3)
        {
            float aim[3]{};
            if (Game_TelemetryReadSharedAim(aim) && TelemetryShotDirectionUsable(aim))
            {
                std::memcpy(shot.engineAim, aim, sizeof(shot.engineAim));
                shot.engineAimSource = static_cast<uint8_t>(TelemetryEngineAimSource::H3Shared);
            }
        }
        else if (title == GameTitle::Halo3ODST)
        {
            float aim[3]{};
            if (Game_TelemetryReadSharedAim(aim) && TelemetryShotDirectionUsable(aim))
            {
                std::memcpy(shot.engineAim, aim, sizeof(shot.engineAim));
                shot.engineAimSource = static_cast<uint8_t>(TelemetryEngineAimSource::OdstShared);
            }
        }
        else if (title == GameTitle::HaloReach)
        {
            // The seated reader admits only seated sources, so a shared
            // compact sample can never masquerade as seated truth; the
            // fallback path below is the on-foot/shared label.
            float seated[3]{};
            uint8_t seatedSource = 0;
            uint32_t seatedGeneration = 0;
            uint64_t seatedSampleMs = 0;
            if (Game_TelemetryReadReachSeatedAim(seated, seatedSource, seatedGeneration, seatedSampleMs) &&
                TelemetryShotDirectionUsable(seated))
            {
                std::memcpy(shot.engineAim, seated, sizeof(shot.engineAim));
                shot.engineAimSource =
                    static_cast<uint8_t>(seatedSource == static_cast<uint8_t>(ReachAimFeedbackSource::SeatedUnitAim)
                                             ? TelemetryEngineAimSource::ReachSeatedUnitAim
                                             : TelemetryEngineAimSource::ReachSeatedCompactFallback);
            }
            else
            {
                float compact[3]{};
                if (Game_TelemetryReadSharedAim(compact) && TelemetryShotDirectionUsable(compact))
                {
                    std::memcpy(shot.engineAim, compact, sizeof(shot.engineAim));
                    shot.engineAimSource = static_cast<uint8_t>(TelemetryEngineAimSource::ReachOnFootCompactFallback);
                }
            }
        }
        else if (title == GameTitle::Halo4)
        {
            float aim[3]{};
            bool pitchValid = false;
            float pitchRadians = 0.0f;
            uint64_t pitchSerial = 0;
            if (Game_TelemetryReadHalo4EngineAim(aim, pitchValid, pitchRadians, pitchSerial) &&
                TelemetryShotDirectionUsable(aim))
            {
                std::memcpy(shot.engineAim, aim, sizeof(shot.engineAim));
                shot.engineAimSource = static_cast<uint8_t>(TelemetryEngineAimSource::H4Observer);
            }
        }
        else if (title == GameTitle::Halo2)
        {
            // Halo 2 has no g_aimFwd writer, so its engine aim feedback is the
            // observer publication's STOCK forward (the engine's own camera,
            // never the mod's tracked pose), exactly as the per-frame Halo 2
            // branch reads it. The publication must belong to the live Halo 2
            // generation and carry a usable direction; an unreadable or stale
            // publication stays the explicit absent label with an exactly-zero
            // vector (never a guessed ray).
            const uint32_t generation = TitleAdapter_GetGeneration(GameTitle::Halo2);
            Halo2ObserverPosePublication publication{};
            bool usable = false;
            if (generation != 0 && Halo2Observer6Dof_ReadPublishedPose(publication) &&
                publication.generation == generation && publication.serial != 0)
            {
                const float* forward = publication.stock.forward;
                if (std::isfinite(forward[0]) && std::isfinite(forward[1]) && std::isfinite(forward[2]))
                {
                    const double lengthSquared = static_cast<double>(forward[0]) * forward[0] +
                                                 static_cast<double>(forward[1]) * forward[1] +
                                                 static_cast<double>(forward[2]) * forward[2];
                    usable = lengthSquared > 1e-12;
                }
            }
            if (usable)
            {
                shot.engineAimSource = static_cast<uint8_t>(TelemetryEngineAimSource::H2Observer);
                std::memcpy(shot.engineAim, publication.stock.forward, sizeof(shot.engineAim));
            }
            else
                shot.engineAimSource = static_cast<uint8_t>(TelemetryEngineAimSource::H2Absent);
        }
        // Presented-reticle publication, rotated from its pose to a forward
        // exactly as the Reach firing path consumes it. The read is a bounded
        // lock-free 2-attempt seqlock; unavailable/unstable stays zero.
        float reticleQuat[4]{};
        float reticlePosition[3]{};
        uint64_t reticleSampleMs = 0;
        uint64_t reticleSupportEpoch = 0;
        uint64_t reticleSolveSerial = 0;
        bool reticleSupportTrusted = false;
        if (VR_GetPresentedReticleAimPoseWithSupportProvenance(reticleQuat, reticlePosition, reticleSampleMs,
                reticleSupportEpoch, reticleSupportTrusted, reticleSolveSerial))
        {
            float lengthSquared = 0.0f;
            for (float component : reticleQuat)
                lengthSquared += component * component;
            if (std::isfinite(lengthSquared) && lengthSquared > 1.0e-8f)
            {
                const float inverse = 1.0f / sqrtf(lengthSquared);
                for (float& component : reticleQuat)
                    component *= inverse;
                const float localForward[3] = {0.0f, 0.0f, -1.0f};
                float reticleForward[3]{};
                RotateByQuat(reticleQuat, localForward, reticleForward);
                if (std::isfinite(reticleForward[0]) && std::isfinite(reticleForward[1]) &&
                    std::isfinite(reticleForward[2]))
                    std::memcpy(shot.reticleDirection, reticleForward, sizeof(shot.reticleDirection));
            }
        }
        std::memcpy(shot.origin, origin, sizeof(shot.origin));
        std::memcpy(shot.direction, direction, sizeof(shot.direction));
        Telemetry_PublishShotEvent(static_cast<uint8_t>(WeaponOrderEventStatus::Success), static_cast<uint8_t>(title),
            titleGeneration, VR_CurrentPreparedSerial(), unit, weapon, shot);
    }
    // TRUE RIGID PARENTING (2026-07-19, replacing the cached-relation
    // reconstruction). Everything below is computed from THIS frame only:
    // the untouched interpolated bones, the actual render root passed to the
    // palette consumer, and a live controller read. One rigid transform T
    // snaps the untouched wrist bone's world pose onto the controller
    // (rotation AND position, all axes), and the same T is applied to every
    // bone, so the assembly stays exactly as authored/animated. No cached
    // wrist->camera_control relation, no synthesized bones, no sim-clock
    // state - the layer that acted as a "mask" is gone. Barrel
    // mounting is a CONSTANT user trim (gun_pitch/yaw/roll + gun_forward_m),
    // not a per-frame estimate.
    // ONE shared definition of "where a wrist glued to its controller belongs
    // in the world" - used by the visible palette, the arm IK, AND the
    // muzzle/marker path so the gun, flash, and hands can never diverge. The
    // right hand carries the weapon mount trim + forward standoff; the left
    // hand mirrors the yaw/roll trim and has no standoff.
    bool DesiredWristWorld(bool left, BoneMatrix& out, float& meshScale, float* collisionCorrection)
    {
        float basisC[9], posC[3];
        if (!ControllerWorldPoseEx(left, basisC, posC, meshScale))
            return false;
        float mounted[9];
        // Begin with the controller basis. Automatic authored-barrel alignment
        // establishes the zero-slider pose first; user trim is applied later.
        memcpy(mounted, basisC, sizeof(mounted));
        // AUTO BARREL ALIGNMENT (weapon hands only): swing the default hand by
        // the minimal world rotation that puts the measured authored barrel
        // axis exactly on the controller ray (basis column 0) - the SAME ray
        // the cursor and bullet steering use. The barrel therefore sits on the
        // cursor line by construction at zero trim. User calibration comes
        // afterward so pitch, yaw, and roll remain independent controls.
        // Weapon hand = the right hand only: the dual-wield secondary is
        // seated by its weapon NODE directly at the palm point, so the
        // wrist-row-0 swing heuristic must not fight it (23:04 headset
        // result: the heuristic mis-seated the left gun).
        const int barrelSlot = left ? -1 : 0;
        if (barrelSlot >= 0 && g_barrelInWristValid[barrelSlot].load(std::memory_order_acquire))
        {
            const float b[3] = {g_barrelInWrist[barrelSlot][0].load(std::memory_order_relaxed),
                g_barrelInWrist[barrelSlot][1].load(std::memory_order_relaxed),
                g_barrelInWrist[barrelSlot][2].load(std::memory_order_relaxed)};
            float worldBarrel[3] = {0, 0, 0};
            for (int c = 0; c < 3; ++c)
                for (int r = 0; r < 3; ++r)
                    worldBarrel[r] += mounted[c * 3 + r] * b[c];
            const float ray[3] = {basisC[0], basisC[1], basisC[2]};
            float swing[9], aligned[9];
            ShortestArcRotation(worldBarrel, ray, swing);
            MultiplyBases(swing, mounted, aligned);
            bool ok = true;
            for (int j = 0; j < 9; ++j)
                if (!isfinite(aligned[j]))
                {
                    ok = false;
                    break;
                }
            if (ok)
            {
                memcpy(mounted, aligned, sizeof(aligned));
                static std::atomic<bool> loggedSwing{false};
                if (!loggedSwing.exchange(true))
                {
                    const float d =
                        Clamp(worldBarrel[0] * ray[0] + worldBarrel[1] * ray[1] + worldBarrel[2] * ray[2], -1.0f, 1.0f);
                    LOG("arm IK: auto barrel alignment ACTIVE, first swing %.1f deg", acosf(d) * 57.2958f);
                }
            }
        }
        // Right-hand calibration is already part of VR_GetAimPose, so applying
        // it here again would rotate only the mesh a second time. Preserve the
        // existing mirrored presentation trim for the independent left hand.
        if (left)
        {
            float mount[9], trimmed[9];
            BasisFromAngles(-g_config.gun_yaw_deg * 0.0174533f, g_config.gun_pitch_deg * 0.0174533f,
                -g_config.gun_roll_deg * 0.0174533f, mount);
            MultiplyBases(mounted, mount, trimmed);
            memcpy(mounted, trimmed, sizeof(trimmed));
        }
        else if (g_config.barrel_pitch_deg != 0.0f || g_config.barrel_yaw_deg != 0.0f ||
                 g_config.barrel_roll_deg != 0.0f)
        {
            // Mesh-only barrel trim (per title profile): turns the drawn
            // gun about the controller after the automatic barrel
            // alignment; the aim ray and crosshair are untouched.
            float mount[9], trimmed[9];
            BasisFromAngles(-g_config.barrel_yaw_deg * 0.0174533f, g_config.barrel_pitch_deg * 0.0174533f,
                -g_config.barrel_roll_deg * 0.0174533f, mount);
            MultiplyBases(mounted, mount, trimmed);
            bool finite = true;
            for (int j = 0; j < 9; ++j)
                if (!isfinite(trimmed[j]))
                {
                    finite = false;
                    break;
                }
            if (finite)
                memcpy(mounted, trimmed, sizeof(trimmed));
        }
        out = BoneMatrix{};
        out.scale = 1.0f;
        memcpy(out.rotation, mounted, sizeof(mounted));
        memcpy(out.translation, posC, sizeof(posC));
        (void)LegacyApplyWorldCollision(TitleAdapter_GetActiveTitle(), left ? 0 : 1, out, collisionCorrection);
        return true;
    }

} // namespace game_core
namespace game_core
{
    bool ReconstructVisiblePaletteSource(uint16_t tag, const FpInterpolationContext& context, const BoneMatrix& root,
        const BoneMatrix* source, const BoneMatrix*& replacement, const FpExplicitPoseTargets* explicitTargets,
        const BoneMatrix* unmodifiedOverride, bool* controllerArmsSolved)
    {
        if (controllerArmsSolved)
            *controllerArmsSolved = false;
        memset(g_fpPaletteCollisionCorrection, 0, sizeof(g_fpPaletteCollisionCorrection));
        if (explicitTargets)
            memcpy(g_fpPaletteCollisionCorrection, explicitTargets->collisionCorrection,
                sizeof(g_fpPaletteCollisionCorrection));
        if (!context.valid || context.slot < 0 || context.slot > 1 || source != context.source || context.count <= 0 ||
            context.count > static_cast<int>(kReachFpMaxSourceNodeCount) || context.wrist < 0 ||
            context.wrist >= context.count)
            return false;

        // Dual-wield secondary (slot 1): its LEFT hand follows the left
        // controller. The separate weapon subtree then inherits that solved
        // hand's rigid delta; it is never independently seated on a controller.
        // Slot-1 failures never touch the primary arm diagnostics.
        const bool dual = (context.slot == 1);
        const BoneMatrix* const unmodified =
            unmodifiedOverride ? unmodifiedOverride : g_fpUnmodifiedInterpolations[context.slot];
        const size_t paletteBytes = static_cast<size_t>(context.count) * sizeof(BoneMatrix);
        const bool twoHandAimActive =
            explicitTargets
                ? explicitTargets->twoHandAimActive
                : (g_fpStereoSolveScope.armed ? g_fpStereoSolveScope.twoHandAimActive : VR_IsTwoHandAiming());
        // Persistent support grip (H3 primary weapon only): the visible support
        // attachment follows the owner trust frozen for this FP invocation, NOT
        // the aim-authority flag. A trusted same-owner invocation
        // keeps the support hand attached even when aim authority is off, and
        // an untrusted invocation never inherits support presentation. The
        // frozen decision exists only for a title that resolves it (H3): PG
        // off, the dual-wield secondary and the explicit-target Reach path all
        // keep the existing twoHandAimActive semantics exactly.
        const bool supportGripAttached = g_config.persistent_support_grip && !explicitTargets && context.slot == 0 &&
                                                 g_fpStereoSolveScope.armed && g_fpStereoSolveScope.supportGripResolved
                                             ? g_fpStereoSolveScope.supportGripAttached
                                             : twoHandAimActive;
        // Reach supplies an explicit, pair-frozen source palette and a proven
        // right-hand target. When its persistent support grip is attached, the
        // second endpoint is derived from that same right-hand rigid delta
        // below; this is the exact authored gun-seat point, not a competing
        // controller aim target. Other titles retain the existing ownership
        // gate.
        const bool reachSupportArmTransaction = explicitTargets && supportGripAttached;
        const bool armIkActive =
            reachSupportArmTransaction ? g_config.arm_ik : ShouldApplyArmIk(g_config.arm_ik, supportGripAttached);
        bool controllerArmsWereSolved = false;

        auto cacheMatches = [&](const FpStereoPaletteCache& cache)
        {
            return cache.valid && cache.tag == tag && cache.player == context.player && cache.slot == context.slot &&
                   cache.count == context.count && cache.wrist == context.wrist && cache.elbow == context.elbow &&
                   cache.shoulder == context.shoulder && cache.wristDescendants == context.wristDescendants &&
                   cache.heldObjectStart == context.heldObjectStart && cache.lWrist == context.lWrist &&
                   cache.lElbow == context.lElbow && cache.lShoulder == context.lShoulder &&
                   cache.lWristDescendants == context.lWristDescendants && cache.armIk == armIkActive &&
                   cache.supportGripAttached == supportGripAttached &&
                   memcmp(cache.original, unmodified, paletteBytes) == 0;
        };
        if (g_fpStereoSolveScope.armed)
        {
            for (const auto& cache : g_fpStereoSolveScope.palettes)
            {
                if (!cacheMatches(cache))
                    continue;
                bool reused = false;
                if (memcmp(&cache.root, &root, sizeof(BoneMatrix)) == 0)
                {
                    memcpy(g_fpPaletteScratch, cache.solved, paletteBytes);
                    reused = true;
                }
                else
                {
                    BoneMatrix inverseRoot{}, cachedToCurrent{};
                    reused = InvertBoneMatrix(root, inverseRoot) &&
                             ComposeBoneMatrices(inverseRoot, cache.root, cachedToCurrent);
                    for (int i = 0; reused && i < context.count; ++i)
                        reused = ComposeBoneMatrices(cachedToCurrent, cache.solved[i], g_fpPaletteScratch[i]);
                }
                if (reused)
                {
                    memcpy(g_fpPaletteCollisionCorrection, cache.collisionCorrection,
                        sizeof(g_fpPaletteCollisionCorrection));
                    if (controllerArmsSolved)
                        *controllerArmsSolved = cache.armsSolved;
                    replacement = g_fpPaletteScratch;
                    return true;
                }
            }
        }
        auto cacheSolvedPalette = [&](const BoneMatrix* solved)
        {
            if (!g_fpStereoSolveScope.armed || !solved)
                return;
            for (auto& cache : g_fpStereoSolveScope.palettes)
            {
                if (cache.valid)
                    continue;
                cache.valid = true;
                cache.tag = tag;
                cache.player = context.player;
                cache.slot = context.slot;
                cache.count = context.count;
                cache.wrist = context.wrist;
                cache.elbow = context.elbow;
                cache.shoulder = context.shoulder;
                cache.wristDescendants = context.wristDescendants;
                cache.heldObjectStart = context.heldObjectStart;
                cache.lWrist = context.lWrist;
                cache.lElbow = context.lElbow;
                cache.lShoulder = context.lShoulder;
                cache.lWristDescendants = context.lWristDescendants;
                cache.armIk = armIkActive;
                cache.armsSolved = controllerArmsWereSolved;
                cache.supportGripAttached = supportGripAttached;
                memcpy(cache.collisionCorrection, g_fpPaletteCollisionCorrection, sizeof(cache.collisionCorrection));
                cache.root = root;
                memcpy(cache.original, unmodified, paletteBytes);
                memcpy(cache.solved, solved, paletteBytes);
                return;
            }
        };

        float meshScale = 1.0f;
        BoneMatrix desiredWristWorld{};
        bool haveDesiredWrist = false;
        if (explicitTargets)
        {
            if (dual && explicitTargets->leftWristValid)
            {
                desiredWristWorld = explicitTargets->leftWrist;
                meshScale = explicitTargets->leftScale;
                haveDesiredWrist = true;
            }
            else if (!dual && explicitTargets->rightWristValid)
            {
                desiredWristWorld = explicitTargets->rightWrist;
                meshScale = explicitTargets->rightScale;
                haveDesiredWrist = true;
            }
        }
        else
        {
            haveDesiredWrist =
                DesiredWristWorld(dual, desiredWristWorld, meshScale, g_fpPaletteCollisionCorrection[dual ? 0 : 1]);
        }
        if (!haveDesiredWrist)
        {
            if (dual)
                return false;
            g_armFailurePublished.store("right-controller-pose", std::memory_order_relaxed);
            g_armFailureSide.store(1, std::memory_order_release);
            return false;
        }
        auto loadLeftWristTarget = [&](BoneMatrix& desired, float& scale)
        {
            if (explicitTargets)
            {
                if (!explicitTargets->leftWristValid)
                    return false;
                desired = explicitTargets->leftWrist;
                scale = explicitTargets->leftScale;
                return true;
            }
            return DesiredWristWorld(true, desired, scale, g_fpPaletteCollisionCorrection[0]);
        };
        // CENTER-ROOT WORLD SOLVE (2026-07-19): this function runs once per
        // EYE, and the palette consumer's `root` is that eye's camera. Any
        // world position built from it (the planted shoulder, the solved
        // elbow) shifts by the eye offset, so the two eyes solved DIFFERENT
        // arms - the bare left arm showed split between eyes. Build all
        // WORLD-space poses from the live CENTER camera instead; only the
        // final record conversion (invRoot) may use the eye root, which makes
        // the rendered world pose eye-independent and the stereo fuse.
        BoneMatrix centerRoot = root;
        if (explicitTargets)
        {
            if (!explicitTargets->centerRootValid)
                return false;
            centerRoot = explicitTargets->centerRoot;
        }
        else
        {
            float camBasis[9];
            if (g_camValid.load() && LoadCameraBasis(camBasis))
            {
                memcpy(centerRoot.rotation, camBasis, sizeof(camBasis));
                centerRoot.translation[0] = g_camX.load();
                centerRoot.translation[1] = g_camY.load();
                centerRoot.translation[2] = g_camZ.load();
            }
        }

        // SHOULDER LEVELING (2026-07-19): the arm rest pose is composed from the
        // camera frame, so its FULL pitch/roll rides the head - look down and the
        // shoulder swings up into your face (headset report: "the shoulders follow my head,
        // they don't stay in place"). Build a torso frame with the camera's
        // HEADING only (yaw about world-up +Z; Blam is a Z-up engine, and the
        // camera cols are fwd/left/up per below), level in pitch/roll, at the
        // camera position. Anchor the arm to THAT instead of the pitching camera.
        // The hand + gun ride the controller target and are root-invariant (the
        // root cancels in desired*invWristRest*bone), so this levels the visible
        // UPPER ARM without moving the hand/gun, which are already right. Falls
        // back to the camera frame when looking near-vertical or when disabled.
        BoneMatrix torsoRoot = centerRoot;
        if (g_config.shoulder_level)
        {
            float cb[9];
            if (NormalizedBasis(centerRoot, cb))
            {
                // MEASURED world-up (not assumed) - see g_worldUp / CamCopyHook.
                const float U[3] = {explicitTargets ? 0.0f : g_worldUp[0].load(),
                    explicitTargets ? 0.0f : g_worldUp[1].load(), explicitTargets ? 1.0f : g_worldUp[2].load()};
                float fwd[3] = {cb[0], cb[1], cb[2]}; // camera forward, world
                const float d = fwd[0] * U[0] + fwd[1] * U[1] + fwd[2] * U[2];
                float fH[3] = {fwd[0] - U[0] * d, fwd[1] - U[1] * d, fwd[2] - U[2] * d};
                const float fl = sqrtf(fH[0] * fH[0] + fH[1] * fH[1] + fH[2] * fH[2]);
                if (fl > 1e-3f) // not looking straight up/down
                {
                    fH[0] /= fl;
                    fH[1] /= fl;
                    fH[2] /= fl;
                    float L[3] = {U[1] * fH[2] - U[2] * fH[1], // left = up x fwd
                        U[2] * fH[0] - U[0] * fH[2], U[0] * fH[1] - U[1] * fH[0]};
                    const float ll = sqrtf(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
                    if (ll > 1e-3f)
                    {
                        L[0] /= ll;
                        L[1] /= ll;
                        L[2] /= ll;
                        torsoRoot.rotation[0] = fH[0];
                        torsoRoot.rotation[1] = fH[1];
                        torsoRoot.rotation[2] = fH[2];
                        torsoRoot.rotation[3] = L[0];
                        torsoRoot.rotation[4] = L[1];
                        torsoRoot.rotation[5] = L[2];
                        torsoRoot.rotation[6] = U[0];
                        torsoRoot.rotation[7] = U[1];
                        torsoRoot.rotation[8] = U[2];
                    }
                }
            }
        }
        // The arm rest pose and elbow pole are built from this frame; when
        // shoulder_level is off it equals centerRoot, so behavior is unchanged.
        const BoneMatrix& armRoot = torsoRoot;

        // VRIK ARM IK: keep the body exactly where the game posed it and bend
        // ONLY the arms so each wrist reaches its controller - shoulders
        // planted, elbows solved analytically (ik.cpp), hand + gun ride the
        // wrist rigidly. Right arm is required; the left arm applies when its
        // chain resolved and the left controller is tracked.
        const bool carrierChainValid =
            dual ? (context.lShoulder >= 0 && context.lShoulder < context.count && context.lElbow >= 0 &&
                       context.lElbow < context.count && context.lWrist >= 0 && context.lWrist < context.count)
                 : (context.shoulder >= 0 && context.shoulder < context.count && context.elbow >= 0 &&
                       context.elbow < context.count);
        if (armIkActive && carrierChainValid)
        {
            const BoneMatrix* unmod = unmodified;
            memcpy(g_fpPaletteScratch, unmod, static_cast<size_t>(context.count) * sizeof(BoneMatrix));
            BoneMatrix invRoot{};
            if (InvertBoneMatrix(root, invRoot))
            {
                // Reach supplies an explicit head-centre root.  Convert every
                // authored record through that centre before modifying either
                // arm, so the cached world pose cannot inherit whichever eye
                // happened to render first.  Halo 3/ODST retain their existing
                // per-title path when explicitTargets is null.
                if (explicitTargets)
                {
                    BoneMatrix centerToEye{};
                    if (!ComposeBoneMatrices(invRoot, centerRoot, centerToEye))
                        return false;
                    for (int i = 0; i < context.count; ++i)
                        if (!ComposeBoneMatrices(centerToEye, unmod[i], g_fpPaletteScratch[i]))
                            return false;
                }
                // IK divergence probe (log-only): capture the per-eye solve
                // inputs so a single desktop/headset session names WHICH input
                // differs between the two eye passes (root by design; anything
                // else is the left-arm-splits-between-eyes bug). Filled by
                // applyArm, compared when eye 1 lands against eye 0.
                struct ArmProbe
                {
                    float S[3];
                    float E[3];
                    float T[3];
                    float upperLen;
                    float lowerLen;
                };
                auto applyArm = [&](int shoulder, int elbow, int wrist, uint64_t mask, bool carryHeldObjects,
                                    const BoneMatrix& desired, float outSign, ArmProbe* probeOut,
                                    float shoulderDrop) -> bool
                {
                    g_armFailWhy = "compose-rest";
                    BoneMatrix shW{}, elW{}, wrW{};
                    if (!ComposeBoneMatrices(armRoot, unmod[shoulder], shW) ||
                        !ComposeBoneMatrices(armRoot, unmod[elbow], elW) ||
                        !ComposeBoneMatrices(armRoot, unmod[wrist], wrW))
                        return false;
                    // Lower the shoulder anchor along the view-DOWN axis (camera
                    // up column, negated) so Chief's arm sits lower and stops
                    // clipping the face (drop; right arm only, shoulderDrop>0),
                    // and push BOTH shoulders back along the forward axis so a
                    // title that plants them in front of you (ODST) seats them at
                    // the torso. Halo 3 keeps shoulder_back_m=0 -> unchanged.
                    const float shoulderBack = g_config.shoulder_back_m;
                    if (shoulderDrop > 0.0f || shoulderBack != 0.0f)
                    {
                        float rb[9];
                        if (NormalizedBasis(armRoot, rb))
                            for (int j = 0; j < 3; ++j)
                                shW.translation[j] -= rb[6 + j] * shoulderDrop + rb[0 + j] * shoulderBack;
                    }
                    const float* S = shW.translation;
                    const float* Er = elW.translation;
                    const float* Wr = wrW.translation;
                    auto dist = [](const float* a, const float* b)
                    {
                        const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
                        return sqrtf(dx * dx + dy * dy + dz * dz);
                    };
                    const float upperLen = dist(S, Er), lowerLen = dist(Er, Wr);
                    const float* T = desired.translation;
                    float mid[3] = {(S[0] + Wr[0]) * 0.5f, (S[1] + Wr[1]) * 0.5f, (S[2] + Wr[2]) * 0.5f};
                    float pole[3] = {Er[0] - mid[0], Er[1] - mid[1], Er[2] - mid[2]};
                    if (pole[0] * pole[0] + pole[1] * pole[1] + pole[2] * pole[2] < 1e-6f)
                    {
                        pole[0] = 0;
                        pole[1] = 0;
                        pole[2] = -1;
                    }
                    // Elbow pole bias: OUT (away from the torso, camera left/
                    // right column) and DOWN, 75/25 over the authored pole
                    // (headset report: elbows "feel inward - I want them to stick
                    // outward"). Root is the camera: cols fwd/left/up.
                    {
                        float rootB[9];
                        if (NormalizedBasis(armRoot, rootB))
                        {
                            const float outDir[3] = {rootB[3] * outSign - rootB[6] * 0.6f,
                                rootB[4] * outSign - rootB[7] * 0.6f, rootB[5] * outSign - rootB[8] * 0.6f};
                            const float ol =
                                sqrtf(outDir[0] * outDir[0] + outDir[1] * outDir[1] + outDir[2] * outDir[2]);
                            const float pl = sqrtf(pole[0] * pole[0] + pole[1] * pole[1] + pole[2] * pole[2]);
                            if (ol > 1e-4f && pl > 1e-4f)
                                for (int j = 0; j < 3; ++j)
                                    pole[j] = pole[j] / pl * 0.25f + outDir[j] / ol * 0.75f;
                        }
                    }
                    // FULL EXTENSION: when the controller is beyond the arm's
                    // natural reach, the two-bone solver clamps to a straight
                    // chain that stops SHORT of the hand, so the hand visibly
                    // detaches from the forearm. Stretch both bones so the arm
                    // always reaches the wrist (skinning stretches the mesh with
                    // it) - capped so it never looks rubbery. Right arm only
                    // (the left arm is already right and untouched here anyway).
                    float solveUpper = upperLen, solveLower = lowerLen;
                    {
                        const float reach = upperLen + lowerLen;
                        const float td = sqrtf((T[0] - S[0]) * (T[0] - S[0]) + (T[1] - S[1]) * (T[1] - S[1]) +
                                               (T[2] - S[2]) * (T[2] - S[2]));
                        if (reach > 1e-4f && td > reach)
                        {
                            const float k = fminf(td / reach, 1.8f);
                            solveUpper *= k;
                            solveLower *= k;
                        }
                    }
                    float E[3];
                    g_armFailWhy = "two-bone-solve";
                    if (!IK_SolveTwoBone(S, T, solveUpper, solveLower, pole, E))
                        return false;
                    if (probeOut)
                    {
                        for (int j = 0; j < 3; ++j)
                        {
                            probeOut->S[j] = S[j];
                            probeOut->E[j] = E[j];
                            probeOut->T[j] = T[j];
                        }
                        probeOut->upperLen = upperLen;
                        probeOut->lowerLen = lowerLen;
                    }
                    auto unit = [](const float* a, const float* b, float* o)
                    {
                        o[0] = a[0] - b[0];
                        o[1] = a[1] - b[1];
                        o[2] = a[2] - b[2];
                        const float l = sqrtf(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
                        if (l > 1e-6f)
                        {
                            o[0] /= l;
                            o[1] /= l;
                            o[2] /= l;
                        }
                    };
                    float restU[3], restF[3], newU[3], newF[3];
                    unit(Er, S, restU);
                    unit(Wr, Er, restF);
                    unit(E, S, newU);
                    unit(T, E, newF);
                    float Ru[9], Rf[9];
                    ShortestArcRotation(restU, newU, Ru);
                    ShortestArcRotation(restF, newF, Rf);
                    BoneMatrix newSh = shW;
                    MultiplyBases(Ru, shW.rotation, newSh.rotation);
                    BoneMatrix newEl = elW;
                    MultiplyBases(Rf, elW.rotation, newEl.rotation);
                    newEl.translation[0] = E[0];
                    newEl.translation[1] = E[1];
                    newEl.translation[2] = E[2];
                    BoneMatrix invWrRest{}, D{};
                    g_armFailWhy = "invert-wrist";
                    if (!InvertBoneMatrix(wrW, invWrRest))
                        return false;
                    ComposeBoneMatrices(desired, invWrRest, D);
                    ComposeBoneMatrices(invRoot, newSh, g_fpPaletteScratch[shoulder]);
                    ComposeBoneMatrices(invRoot, newEl, g_fpPaletteScratch[elbow]);
                    for (int i = 0; i < context.count; ++i)
                    {
                        const bool handDescendant = i < 64 && (mask & (uint64_t{1} << i));
                        const bool heldObject =
                            carryHeldObjects && context.heldObjectStart >= 0 && i >= context.heldObjectStart;
                        if (!handDescendant && !heldObject)
                            continue;
                        BoneMatrix boneW{}, newW{};
                        if (ComposeBoneMatrices(armRoot, unmod[i], boneW) && ComposeBoneMatrices(D, boneW, newW))
                            ComposeBoneMatrices(invRoot, newW, g_fpPaletteScratch[i]);
                    }
                    g_armFailWhy = nullptr;
                    return true;
                };
                // Primary keeps its right-hand path. A dual weapon is owned by
                // the actual LEFT hand, which follows the left controller.
                const bool handApplied =
                    dual ? applyArm(context.lShoulder, context.lElbow, context.lWrist, context.lWristDescendants, true,
                               desiredWristWorld, 1.0f, nullptr, 0.0f)
                         : applyArm(context.shoulder, context.elbow, context.wrist, context.wristDescendants, true,
                               desiredWristWorld, -1.0f, nullptr, g_config.right_shoulder_drop);
                if (handApplied)
                {
                    bool leftArmApplied = false;
                    if (dual)
                    {
                        BoneMatrix authoredLeftWorld{}, invAuthoredLeft{}, handDelta{};
                        if (!ComposeBoneMatrices(armRoot, unmod[context.lWrist], authoredLeftWorld) ||
                            !InvertBoneMatrix(authoredLeftWorld, invAuthoredLeft) ||
                            !ComposeBoneMatrices(desiredWristWorld, invAuthoredLeft, handDelta))
                            return false;
                        for (int i = 0; i < context.count && i < 64; ++i)
                        {
                            if (!(context.wristDescendants & (1ull << i)))
                                continue;
                            BoneMatrix authoredWorld{}, lockedWorld{};
                            if (!ComposeBoneMatrices(armRoot, unmod[i], authoredWorld) ||
                                !ComposeBoneMatrices(handDelta, authoredWorld, lockedWorld) ||
                                !ComposeBoneMatrices(invRoot, lockedWorld, g_fpPaletteScratch[i]))
                                return false;
                        }
                        static std::atomic<bool> loggedDualIk{false};
                        leftArmApplied = true;
                        if (!explicitTargets && !loggedDualIk.exchange(true))
                            LOG("DUAL VRIK: slot 1 arm IK active on the LEFT "
                                "controller (wrist %d, elbow %d, shoulder %d)",
                                context.lWrist, context.lElbow, context.lShoulder);
                    }
                    // Left SUPPORT arm (primary weapon only): same treatment
                    // onto the left controller. During dual wield the left
                    // hand holds slot 1's weapon instead, and slot 1's own
                    // mirror-side chain stays game-animated.
                    auto publishLeftFailure = [](const char* why)
                    {
                        g_armFailurePublished.store(why, std::memory_order_relaxed);
                        g_armFailureSide.store(2, std::memory_order_release);
                    };
                    if (dual)
                    {
                        // HEADSET RESULTS: 22:51 build - the visible arm is
                        // skinned to the lWrist/lElbow/lShoulder chain, not the
                        // gun chain. 22:59 build - carrying that arm at its
                        // AUTHORED offset from the gun put the hand ~1 m left:
                        // with the stock weapon-IK branch patched off, the
                        // secondary's animation never poses l_hand on the grip,
                        // so there is no authored grip relation to preserve.
                        // Target the visible hand at the LEFT CONTROLLER itself
                        // - the identical, user-tuned support-hand treatment
                        // (palm correction + mirrored trim, F1 slider applies).
                        if (context.lShoulder >= 0 && context.lShoulder < context.count && context.lElbow >= 0 &&
                            context.lElbow < context.count && context.lWrist >= 0 && context.lWrist < context.count)
                        {
                            BoneMatrix desiredL{};
                            float leftScale = 1.0f;
                            if (loadLeftWristTarget(desiredL, leftScale))
                            {
                                static std::atomic<bool> loggedDualArm{false};
                                leftArmApplied = applyArm(context.lShoulder, context.lElbow, context.lWrist,
                                    context.lWristDescendants, false, desiredL, 1.0f, nullptr, 0.0f);
                                if (leftArmApplied && !explicitTargets && !loggedDualArm.exchange(true))
                                    LOG("DUAL VRIK: secondary VISIBLE hand bound to "
                                        "the left controller (wrist %d, elbow %d, "
                                        "shoulder %d)",
                                        context.lWrist, context.lElbow, context.lShoulder);
                            }
                        }
                    }
                    else if (context.lShoulder >= 0 && context.lShoulder < context.count && context.lElbow >= 0 &&
                             context.lElbow < context.count && context.lWrist >= 0 && context.lWrist < context.count)
                    {
                        BoneMatrix desiredLeft{};
                        float leftScale = 1.0f;
                        bool leftTargetValid = false;
                        if (reachSupportArmTransaction)
                        {
                            leftTargetValid =
                                ReachBuildRigidCarriedSupportEndpoint(
                                    armRoot, unmod[context.wrist], desiredWristWorld, unmod[context.lWrist],
                                    desiredLeft, [](const BoneMatrix& a, const BoneMatrix& b, BoneMatrix& out)
                                    { return ComposeBoneMatrices(a, b, out); },
                                    [](const BoneMatrix& value, BoneMatrix& out)
                                    { return InvertBoneMatrix(value, out); }) &&
                                ReachBoneMatrixFinite(desiredLeft);
                            leftScale = explicitTargets->leftScale;
                        }
                        else
                            leftTargetValid = loadLeftWristTarget(desiredLeft, leftScale);
                        if (leftTargetValid)
                        {
                            static std::atomic<bool> loggedLeft{false};
                            leftArmApplied = applyArm(context.lShoulder, context.lElbow, context.lWrist,
                                context.lWristDescendants, false, desiredLeft, 1.0f, nullptr, 0.0f);
                            if (leftArmApplied)
                            {
                                g_armFailurePublished.store(nullptr, std::memory_order_relaxed);
                                g_armFailureSide.store(0, std::memory_order_release);
                                if (!explicitTargets && !loggedLeft.exchange(true))
                                    LOG("arm IK: LEFT arm on the left controller "
                                        "(wrist %d, elbow %d, shoulder %d)",
                                        context.lWrist, context.lElbow, context.lShoulder);
                            }
                            else
                                publishLeftFailure(g_armFailWhy ? g_armFailWhy : "apply-arm");
                        }
                        else if (!explicitTargets || reachSupportArmTransaction)
                            publishLeftFailure("left-controller-pose");
                    }
                    else
                        publishLeftFailure("left-chain-indices");
                    if (reachSupportArmTransaction && !leftArmApplied)
                    {
                        // Do not expose a one-sided or partially written arm
                        // solve. Skip palette commit/cache and let the existing
                        // rigid right-wrist transaction rebuild every node
                        // from untouchedLive, preserving the proven gun pose.
                        publishLeftFailure("support-arm-transaction-rollback");
                    }
                    else
                    {
                        // Compare this eye's LEFT-arm solve inputs to the other
                        // eye's. dRoot large is expected (eye offset). Any nonzero
                        // dCenterRoot / dWrist / dLens / dDesired names the leaking
                        // per-eye input behind the left-arm split. Rate-limited.
                        // Uniform size trim about the gripping hand (grip stays put).
                        // Each side scales its OWN wrist subtree. Before 2026-07-20
                        // the mask here was always context.wristDescendants - the
                        // RIGHT wrist's bones - even when the anchor switched to the
                        // left wrist, so no left-hand size value ever reached a bone
                        // ("the left arm is not scalable").
                        auto trimSubtree = [&](int anchorBone, uint64_t mask, bool carryHeldObjects, float scale)
                        {
                            if (scale == 1.0f || (!mask && !carryHeldObjects) || anchorBone < 0 ||
                                anchorBone >= context.count)
                                return;
                            const float anchor[3] = {g_fpPaletteScratch[anchorBone].translation[0],
                                g_fpPaletteScratch[anchorBone].translation[1],
                                g_fpPaletteScratch[anchorBone].translation[2]};
                            for (int i = 0; i < context.count; ++i)
                            {
                                const bool handDescendant = i < 64 && (mask & (uint64_t{1} << i));
                                const bool heldObject =
                                    carryHeldObjects && context.heldObjectStart >= 0 && i >= context.heldObjectStart;
                                if (!handDescendant && !heldObject)
                                    continue;
                                BoneMatrix& bone = g_fpPaletteScratch[i];
                                for (int r = 0; r < 3; ++r)
                                    bone.translation[r] = anchor[r] + (bone.translation[r] - anchor[r]) * scale;
                                bone.scale *= scale;
                            }
                        };
                        const float leftScale =
                            explicitTargets ? explicitTargets->leftScale : Clamp(g_config.left_hand_scale, 0.3f, 3.0f);
                        if (dual)
                        {
                            // The dual-wield carrier IS the left hand; its own gun
                            // rides that subtree, so the left size governs both.
                            trimSubtree(context.lWrist, context.lWristDescendants, true, leftScale);
                        }
                        else
                        {
                            trimSubtree(context.wrist, context.wristDescendants, true, meshScale);
                            // The support hand is game-animated onto the gun rather
                            // than IK-solved, but its bones are still ours to size.
                            // Exclude anything already trimmed above so a skeleton
                            // that nests the two subtrees cannot scale a bone twice.
                            trimSubtree(context.lWrist, context.lWristDescendants & ~context.wristDescendants, false,
                                leftScale);
                        }
                        // NOTE: a "length squash along the barrel" is NOT possible
                        // here and must not be re-attempted: the weapon mesh is
                        // rigid geometry on essentially one bone, and BoneMatrix
                        // carries a single UNIFORM scale. Squashing bone ORIGINS
                        // only translates the mesh (user-confirmed 2026-07-19:
                        // "the length just moves the gun"). Size trims are
                        // gun_scale (uniform) and gun_forward_m (seat depth).
                        controllerArmsWereSolved = !dual && leftArmApplied;
                        replacement = g_fpPaletteScratch;
                        if (controllerArmsSolved)
                            *controllerArmsSolved = controllerArmsWereSolved;
                        cacheSolvedPalette(g_fpPaletteScratch);
                        static std::atomic<bool> loggedIk{false};
                        if (!explicitTargets && !loggedIk.exchange(true))
                            LOG("arm IK active - shoulder %d planted, elbow %d solved, "
                                "wrist %d + %lld subtree bones to controller",
                                context.shoulder, context.elbow, context.wrist,
                                (long long)__popcnt64(context.wristDescendants));
                        // Full-solve cache misses. Exact duplicate palettes inside
                        // this stereo pair return above without repeating arm IK.
                        return true;
                    }
                }
                if (!dual)
                {
                    g_armFailurePublished.store(
                        g_armFailWhy ? g_armFailWhy : "right-apply-arm", std::memory_order_relaxed);
                    g_armFailureSide.store(1, std::memory_order_release);
                }
            }
            else if (!dual)
            {
                g_armFailurePublished.store("invert-root", std::memory_order_relaxed);
                g_armFailureSide.store(1, std::memory_order_release);
            }
            // IK could not solve this frame - fall through to rigid parent.
        }

        // M = rootEye^-1 * T * rootCenter applied per record: the WORLD result
        else if (armIkActive && !dual)
        {
            g_armFailurePublished.store("right-chain-indices", std::memory_order_relaxed);
            g_armFailureSide.store(1, std::memory_order_release);
        }
        // T * rootCenter * record is built from the center camera (identical
        // in both eyes), and only the record conversion uses the eye root.
        BoneMatrix wristWorld{}, inverseWristWorld{}, t{}, inverseRoot{}, m{}, tRoot{};
        if (!ComposeBoneMatrices(centerRoot, unmodified[context.wrist], wristWorld) ||
            !InvertBoneMatrix(wristWorld, inverseWristWorld) ||
            !ComposeBoneMatrices(desiredWristWorld, inverseWristWorld, t) || !InvertBoneMatrix(root, inverseRoot) ||
            !ComposeBoneMatrices(t, centerRoot, tRoot) || !ComposeBoneMatrices(inverseRoot, tRoot, m))
            return false;

        for (int i = 0; i < context.count; ++i)
            if (!ComposeBoneMatrices(m, unmodified[i], g_fpPaletteScratch[i]))
                return false;

        // Uniform user size trim, applied about the wrist so the grip stays on
        // the controller.
        if (meshScale != 1.0f)
        {
            const float* wristT = g_fpPaletteScratch[context.wrist].translation;
            const float anchor[3] = {wristT[0], wristT[1], wristT[2]};
            for (int i = 0; i < context.count; ++i)
            {
                BoneMatrix& bone = g_fpPaletteScratch[i];
                for (int r = 0; r < 3; ++r)
                    bone.translation[r] = anchor[r] + (bone.translation[r] - anchor[r]) * meshScale;
                bone.scale *= meshScale;
                if (!isfinite(bone.scale) || !isfinite(bone.translation[0]))
                    return false;
            }
        }
        // Left hand size in the rigid path. The loop above already scaled the
        // WHOLE assembly by meshScale, so only the remaining ratio is applied
        // here - the left hand ends up at left_hand_scale either way, and the
        // slider behaves the same with arm IK on or off.
        {
            const float leftScale =
                explicitTargets ? explicitTargets->leftScale : Clamp(g_config.left_hand_scale, 0.3f, 3.0f);
            const float relative = leftScale / meshScale;
            if (context.lWristDescendants && context.lWrist >= 0 && context.lWrist < context.count &&
                isfinite(relative) && relative != 1.0f)
            {
                const float* lT = g_fpPaletteScratch[context.lWrist].translation;
                const float anchor[3] = {lT[0], lT[1], lT[2]};
                for (int i = 0; i < context.count && i < 64; ++i)
                {
                    if (!(context.lWristDescendants & (1ull << i)))
                        continue;
                    BoneMatrix& bone = g_fpPaletteScratch[i];
                    for (int r = 0; r < 3; ++r)
                        bone.translation[r] = anchor[r] + (bone.translation[r] - anchor[r]) * relative;
                    bone.scale *= relative;
                    if (!isfinite(bone.scale) || !isfinite(bone.translation[0]))
                        return false;
                }
            }
        }

        replacement = g_fpPaletteScratch;
        cacheSolvedPalette(g_fpPaletteScratch);
        static std::atomic<bool> logged{false};
        if (!explicitTargets && !logged.exchange(true))
            LOG("FP palette rigid-parented to the controller "
                "(player %d, %d bones, wrist %d, single same-frame transform)",
                context.player, context.count, context.wrist);
        return true;
    }

    void LegacyPublishWorldCollisionVolumes(GameTitle title, uint16_t /*renderModelTag*/,
        const FpInterpolationContext& context, const BoneMatrix& root, const BoneMatrix* solved, int32_t ignoredObject,
        uint32_t generation, const FpExplicitPoseTargets* targets)
    {
        LegacyWorldCollisionFeature* feature = LegacyCollisionForTitle(title);
        if (!feature || !solved || !context.valid || !generation ||
            (!g_config.world_collision &&
                !((title == GameTitle::HaloReach || title == GameTitle::Halo3 || title == GameTitle::Halo3ODST) &&
                    g_config.physical_melee)) ||
            (!feature->installed.load(std::memory_order_acquire) &&
                !(title == GameTitle::HaloReach && ReachContactMeleeReady()) &&
                !(title == GameTitle::Halo3 && Halo3ContactMeleeReady()) &&
                !(title == GameTitle::Halo3ODST && OdstContactMeleeReady())) ||
            context.count <= 0 || context.count > 64)
            return;
        float rightPoints[64][3]{}, leftPoints[64][3]{};
        int rightCount = 0, leftCount = 0;
        BoneMatrix rightWristWorld{}, leftWristWorld{};
        bool rightWristValid = false, leftWristValid = false;
        for (int node = 0; node < context.count; ++node)
        {
            BoneMatrix world{};
            if (!ComposeBoneMatrices(root, solved[node], world) || !Halo4WorldCollisionFiniteVector(world.translation))
                return;
            const uint64_t bit = uint64_t{1} << node;
            if (context.wristDescendants & bit)
            {
                memcpy(rightPoints[rightCount++], world.translation, sizeof(world.translation));
            }
            if (context.lWristDescendants & bit)
            {
                memcpy(leftPoints[leftCount++], world.translation, sizeof(world.translation));
            }
            if (node == context.wrist)
            {
                rightWristWorld = world;
                rightWristValid = true;
            }
            if (node == context.lWrist)
            {
                leftWristWorld = world;
                leftWristValid = true;
            }
        }
        if (rightWristValid && !rightCount)
            memcpy(rightPoints[rightCount++], rightWristWorld.translation, sizeof(rightWristWorld.translation));
        if (leftWristValid && !leftCount)
            memcpy(leftPoints[leftCount++], leftWristWorld.translation, sizeof(leftWristWorld.translation));

        if (title == GameTitle::Halo3)
        {
            if (context.slot == 0 && rightWristValid && rightCount)
                Halo3PublishContactHand(1, context, root, solved, rightPoints, rightCount, ignoredObject, generation);
            if (leftWristValid && leftCount)
                Halo3PublishContactHand(0, context, root, solved, leftPoints, leftCount, ignoredObject, generation);
        }
        if (title == GameTitle::Halo3ODST)
        {
            if (context.slot == 0 && rightWristValid && rightCount)
                OdstPublishContactHand(1, context, root, solved, rightPoints, rightCount, ignoredObject, generation);
            if (leftWristValid && leftCount)
                OdstPublishContactHand(0, context, root, solved, leftPoints, leftCount, ignoredObject, generation);
        }
        if (title == GameTitle::HaloReach && targets)
        {
            if (rightWristValid && rightCount)
                ReachPublishContactHand(
                    1, *targets, context, root, solved, rightPoints, rightCount, ignoredObject, generation);
            if (leftWristValid && leftCount)
                ReachPublishContactHand(
                    0, *targets, context, root, solved, leftPoints, leftCount, ignoredObject, generation);
        }
        if (!g_config.world_collision || !feature->installed.load(std::memory_order_acquire))
            return;

        const bool secondary = (title == GameTitle::Halo3 || title == GameTitle::Halo3ODST) && context.slot == 1;
        if (!secondary && rightWristValid && rightCount)
        {
            float samples[kLegacyCollisionMaxSamples][3]{};
            const int handCount = Halo4SelectWorldCollisionExtrema(
                rightWristWorld.translation, &rightPoints[0][0], rightCount, samples, kLegacyCollisionMaxSamples);
            int total = handCount;
            uint64_t shapeId = 0;
            if (handCount == kLegacyCollisionHandSamples &&
                LegacyBuildMappedWeaponBounds(title, context, root, solved, samples + handCount, shapeId))
            {
                total += kLegacyCollisionWeaponSamples;
                feature->boundsPublished.fetch_add(1, std::memory_order_relaxed);
            }
            else if (handCount == kLegacyCollisionHandSamples)
            {
                feature->boundsFallbacks.fetch_add(1, std::memory_order_relaxed);
            }
            if (handCount == kLegacyCollisionHandSamples)
            {
                if (targets)
                    for (int sample = 0; sample < total; ++sample)
                        for (int axis = 0; axis < 3; ++axis)
                            samples[sample][axis] -= targets->collisionCorrection[1][axis];
                LegacyPublishTarget(*feature, 1, generation, samples, total, handCount, ignoredObject, shapeId);
            }
        }
        if (leftWristValid && leftCount)
        {
            // The secondary's actual left-hand palette owns this publication.
            // A subsequent primary/support-arm callback must not replace its
            // gun bounds with a hand-only packet. Expire ownership on a gap or
            // player change, including engines that stop submitting slot 1.
            const uint64_t now = GetTickCount64();
            const uint64_t secondaryAt = feature->secondaryPublishedAtMs.load(std::memory_order_acquire);
            if (!secondary && secondaryAt && now >= secondaryAt && now - secondaryAt <= 150 && ignoredObject != -1 &&
                feature->secondaryPublishedObject.load(std::memory_order_relaxed) == ignoredObject)
                return;
            float samples[kLegacyCollisionMaxSamples][3]{};
            const int handCount = Halo4SelectWorldCollisionExtrema(
                leftWristWorld.translation, &leftPoints[0][0], leftCount, samples, kLegacyCollisionHandSamples);
            if (handCount == kLegacyCollisionHandSamples)
            {
                int total = handCount;
                uint64_t shapeId = 0;
                if (secondary &&
                    LegacyBuildMappedWeaponBounds(title, context, root, solved, samples + handCount, shapeId))
                {
                    total += kLegacyCollisionWeaponSamples;
                    feature->boundsPublished.fetch_add(1, std::memory_order_relaxed);
                }
                else if (secondary)
                    feature->boundsFallbacks.fetch_add(1, std::memory_order_relaxed);
                if (targets)
                    for (int sample = 0; sample < total; ++sample)
                        for (int axis = 0; axis < 3; ++axis)
                            samples[sample][axis] -= targets->collisionCorrection[0][axis];
                LegacyPublishTarget(*feature, 0, generation, samples, total, handCount, ignoredObject, shapeId);
                if (secondary)
                {
                    feature->secondaryPublishedObject.store(ignoredObject, std::memory_order_relaxed);
                    feature->secondaryPublishedAtMs.store(now, std::memory_order_release);
                }
            }
        }
    }
    // CRASH LESSON (both fatal errors tonight, same root cause): halo3.dll is
    // LTCG-optimized - the sway loop keeps its counter (r9d), bone pointer
    // (r8) and count (r10d) LIVE IN VOLATILE REGISTERS across `call 0x120DF8`,
    // because the compiler knows that function never touches them. ANY
    // compiled C/C++ interposition (a MinHook detour or a C++ shim) clobbers
    // those registers and corrupts the caller -> wild writes -> fatal error on
    // level load. Interposing engine-internal calls therefore requires a
    // hand-assembled shim restricted to registers the caller provably treats
    // as dead - here, only RAX (verified: reloaded/unused after the call).
    //
    // The emitted shim (see InstallHalo3CameraHooks) compares rdx (the bone) against
    // these bounds and returns without rotating when it lies inside an
    // assembly ApplyControllerToRoot re-rooted this frame; everything else
    // tail-jumps to the real rotator. Same game thread writes and reads the
    // bounds (compose -> sway loop), so there is no race.
    alignas(8) volatile uintptr_t g_fpSkipBounds[4] = {0, 0, 0, 0}; // lo0,hi0,lo1,hi1

} // namespace game_core
namespace game_core
{
    bool ControllerWorldPoseEx(bool left, float basis[9], float pos[3], float& scale)
    {
        if (!g_vrAim.load() || !g_enabled.load() || !g_camValid.load() || !g_baseCamValid.load())
            return false;
        float cq[4], cp[3];
        // Right/weapon hand uses the shared aim pose (two-hand-adjusted); the
        // left hand uses its own controller for the support-arm IK target.
        // Persistent support grip: an owner-untrusted pair (or a pair whose FP
        // invocation never proved the relationship owner) resolves to the
        // ordinary calibrated one-hand solve for this invocation only. The
        // thread-local gate is set and cleared around the aim getter; the
        // durable relationship is never mutated here. PG off: never set.
        const bool oneHandOverride = g_config.persistent_support_grip && !left && g_fpStereoSolveScope.armed &&
                                     !g_fpStereoSolveScope.supportGripAttached;
        if (oneHandOverride)
            VR_SetSupportInvocationUntrusted(true);
        const bool poseOk = left ? VR_GetLeftControllerPose(cq, cp) : VR_GetAimPose(cq, cp);
        if (oneHandOverride)
            VR_SetSupportInvocationUntrusted(false);
        if (!poseOk)
            return false;
        float hullYaw = 0.0f, hullPitch = 0.0f;
        const bool followValid = Halo3ReadRollStableFollow(hullYaw, hullPitch);
        BuildTrackedGameBasisFromFrame(cq, false, followValid, hullYaw, hullPitch, basis);
        // Full controller displacement from the recentered room origin. The
        // old camera + (controller-currentHead) expression combined a camera
        // position containing an older head-lean sample with a fresh relative
        // hand sample. That subtraction shortened forward reach and leaked
        // head motion. The gameplay base below was captured before head lean.
        const float dx = cp[0] - g_headPosRef[0];
        const float dy = cp[1] - g_headPosRef[1];
        const float dz = cp[2] - g_headPosRef[2];
        const float sh = sinf(g_headYawRef), ch = cosf(g_headYawRef);
        const float roomForward = dx * sh - dz * ch;
        const float roomRight = dx * ch + dz * sh;
        const float cg = cosf(g_gameYawRef), sg = sinf(g_gameYawRef);
        const float s = g_worldScale.load();
        float off[3];
        float followBasis[9], followLocal[3] = {roomForward, -roomRight, dy};
        float followedOffset[3];
        const bool positionFollows =
            followValid &&
            Halo3ComposeFollowBasisWithRoll(hullYaw, hullPitch, g_gameYawRef, 0.0f, 0.0f, 0.0f, followBasis) &&
            Halo3TransformBasisVector(followBasis, followLocal, followedOffset);
        if (positionFollows)
        {
            off[0] = followedOffset[0] * s;
            off[1] = followedOffset[1] * s;
            off[2] = followedOffset[2] * s;
        }
        else
        {
            off[0] = (cg * roomForward + sg * roomRight) * s;
            off[1] = (sg * roomForward - cg * roomRight) * s;
            off[2] = dy * s;
        }
        float cam[3] = {g_baseCamX.load(), g_baseCamY.load(), g_baseCamZ.load()};
        // In a first-person vehicle seat the engine camera source is the
        // occupant's own head marker, so using it here hangs the arms and gun
        // off the seated biped's FACE - rotate the view and they are dragged
        // with it. Anchor them to the seat itself instead: rigid in the
        // vehicle's frame, so the vehicle still carries them and the head no
        // longer does. Unavailable anchor keeps the existing origin, so this
        // can only ever fall back to the behavior it replaces.
        float bodyAnchor[3] = {};
        // Whichever title owns the camera supplies the seat: only one of these
        // can be active at a time, and each returns false unless its own
        // vehicle transaction is armed and fresh.
        bool bodyAnchorValid = Halo3ComputeSeatBodyAnchor(bodyAnchor);
        if (!bodyAnchorValid)
            bodyAnchorValid = OdstComputeSeatBodyAnchor(bodyAnchor);
        Halo3SelectHandOrigin(g_config.vehicle_hands_follow_body, bodyAnchorValid, bodyAnchor, cam);
        // Forward standoff along the controller's own aim direction (basis
        // column 0 = forward). EVERY left-hand use - support hand AND the
        // dual-wield gun seat - is the same wrist-to-palm PALM point (23:17
        // headset result: seating the dual gun by the weapon depth put it on
        // the wrist, ~12 cm behind the rendered hand). Right keeps its
        // independent weapon offset.
        const float standoff =
            (left ? Clamp(g_config.left_hand_forward_m, -0.15f, 0.30f) : Clamp(g_config.gun_forward_m, -0.3f, 0.5f)) *
            s;
        const float rightOff = !left ? Clamp(g_config.gun_right_m, -0.3f, 0.3f) * s : 0.0f;
        const float upOff = !left ? Clamp(g_config.gun_up_m, -0.3f, 0.3f) * s : 0.0f;
        for (int j = 0; j < 3; ++j)
            pos[j] = cam[j] + off[j] + basis[j] * standoff + basis[3 + j] * rightOff + basis[6 + j] * upOff;
        scale = Clamp(g_config.gun_scale, 0.3f, 3.0f);
        for (int j = 0; j < 9; ++j)
            if (!isfinite(basis[j]))
                return false;
        for (int j = 0; j < 3; ++j)
            if (!isfinite(pos[j]))
                return false;
        return true;
    }
} // namespace game_core
namespace
{

    // Legacy name: the right-hand pose (existing call sites).
    bool ControllerWorldPose(float basis[9], float pos[3], float& scale)
    {
        return ControllerWorldPoseEx(false, basis, pos, scale);
    }

    // The 03:27 lever, restored: write the composers' `defaults` root. Proven
    // side-effect-free in the headset and it puts the muzzle flash/markers on
    // the controller. Does NOT move the mesh (that consumer is still unfound).
    bool ApplyControllerToRoot(BoneMatrix* output, BoneMatrix* root)
    {
        if (!root || !output)
            return false;
        int slot = -1;
        unsigned char* weapon = nullptr;
        if (!FindFirstPersonWeapon(output, slot, weapon))
            return false;
        float target[3], desired[9];
        if (!GetControllerFirstPersonTransform(slot, target, desired))
            return false;
        for (float v : target)
            if (!isfinite(v))
                return false;
        for (float v : desired)
            if (!isfinite(v))
                return false;
        const float meshScale = Clamp(g_config.gun_scale, 0.3f, 3.0f);
        if (!isfinite(meshScale) || meshScale <= 0.0f)
            return false;
        root->scale = meshScale;
        memcpy(root->rotation, desired, sizeof(desired));
        memcpy(root->translation, target, sizeof(target));
        // This assembly now carries the controller pose; exempt exactly these
        // bones from the engine's camera pitch/turn rotation (emitted shim).
        const int count = *reinterpret_cast<int*>(weapon + 0x49C);
        if (count > 0 && count <= 64)
        {
            g_fpSkipBounds[slot * 2 + 0] = reinterpret_cast<uintptr_t>(output);
            g_fpSkipBounds[slot * 2 + 1] =
                g_fpSkipBounds[slot * 2 + 0] + static_cast<size_t>(count) * sizeof(BoneMatrix);
        }
        static std::atomic<unsigned> logged{0};
        const unsigned bit = 1u << slot;
        if (!(logged.fetch_or(bit) & bit))
            LOG("first-person slot %d rooted to the %s controller (markers/flash lever, scale %.2f)", slot,
                slot == 0 ? "right" : "left", meshScale);
        return true;
    }

    // CENSUS RESULT (2026-07-19, retired): the two composer hooks we install
    // process ONLY first-person weapon/arm skeletons (42-45 bones, camera-space
    // at origin) - never world bipeds. The biped skeleton lives in the render
    // pool at RVA ~0x468xxxx (found live via camscan).
    // The census + biped probe that proved this are removed.

    // BANK WRITES ARE BANNED (2026-07-15, 03:4x headset result): writing the
    // controller pose into bank record 0 did NOT move the mesh, but it DID
    // bleed the wrist into the body/camera - record 0 propagates into
    // camera_control, which the game reads back to drive the camera. The
    // ApplyControllerToBankRoot helper is intentionally no longer called;
    // kept only as documentation of the falsified lever.
    // BULLET-SPAWN FIX (2026-07-19): the projectile spawn / effect origins
    // read the COMPOSED output, which stayed AUTHORED (head-glued) once the
    // bank write went dormant - bullets emerged at the authored center-screen
    // muzzle, "slightly left and ahead of the gun". Composed output is WORLD
    // space (output[0] = defaultsRoot * record0), so snap ONLY the right-wrist
    // subtree onto the shared controller wrist target. Everything else -
    // especially record 0 and camera_control - is left untouched: rewriting
    // those is the falsified "wrist moves the world" camera feedback.
    bool ApplyControllerToComposedWristSubtree(BoneMatrix* output)
    {
        int slot = -1;
        unsigned char* weapon = nullptr;
        if (!FindFirstPersonWeapon(output, slot, weapon))
            return false;
        const int count = g_fpBoneCount[slot].load(std::memory_order_acquire);
        const int wrist = g_fpWristIndex[slot].load(std::memory_order_acquire);
        const int lWrist = g_fpLWristIndex[slot].load(std::memory_order_acquire);
        const uint64_t mask = g_fpWristDescendants[slot].load(std::memory_order_acquire);
        if (count <= 0 || count > 64 || wrist < 0 || wrist >= count || *reinterpret_cast<int*>(weapon + 0x49C) != count)
            return false;
        float meshScale = 1.0f;
        BoneMatrix desired{}, invWrist{}, t{};
        // Slot 1 (dual-wield secondary): effect origins belong on the LEFT
        // controller, matching its visible weapon - anchored on the carrier
        // hand bone exactly like the visible seat.
        const bool dual = (slot == 1);
        const int transformAnchor = dual ? lWrist : wrist;
        if (transformAnchor < 0 || transformAnchor >= count)
            return false;
        if (!DesiredWristWorld(dual, desired, meshScale))
            return false;
        if (!InvertBoneMatrix(output[transformAnchor], invWrist) || !ComposeBoneMatrices(desired, invWrist, t))
            return false;
        for (int i = 0; i < count; ++i)
        {
            if (!(mask & (1ull << i)))
                continue;
            BoneMatrix moved{};
            if (!ComposeBoneMatrices(t, output[i], moved))
                return false;
            output[i] = moved;
        }
        if (meshScale != 1.0f)
        {
            const float* a = desired.translation;
            for (int i = 0; i < count; ++i)
            {
                if (!(mask & (1ull << i)))
                    continue;
                for (int r = 0; r < 3; ++r)
                    output[i].translation[r] = a[r] + (output[i].translation[r] - a[r]) * meshScale;
                output[i].scale *= meshScale;
            }
        }
        static std::atomic<bool> logged{false};
        if (!logged.exchange(true))
            LOG("composed wrist subtree snapped to the controller "
                "(bullet spawn / effect origins now on the gun)");
        return true;
    }

} // namespace
namespace game_core
{
    float Clamp(float v, float lo, float hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }
    float WrapPi(float a)
    {
        while (a > 3.14159265f)
            a -= 6.2831853f;
        while (a < -3.14159265f)
            a += 6.2831853f;
        return a;
    }

    // Rodrigues rotation of v (in place) about the unit axis by the angle
    // whose cosine/sine are given.
    void RotateAboutAxis(float* v, const float* axis, float cosA, float sinA)
    {
        const float d = axis[0] * v[0] + axis[1] * v[1] + axis[2] * v[2];
        const float c[3] = {
            axis[1] * v[2] - axis[2] * v[1], axis[2] * v[0] - axis[0] * v[2], axis[0] * v[1] - axis[1] * v[0]};
        for (int i = 0; i < 3; ++i)
            v[i] = v[i] * cosA + c[i] * sinA + axis[i] * d * (1.0f - cosA);
    }
} // namespace game_core
namespace
{

    // The authored section (cinematic scene index) each title last published;
    // -1 when the title has none. Read by the theatre section gate.
    std::atomic<int32_t> g_cinematicSection[8]{-1, -1, -1, -1, -1, -1, -1, -1};
} // namespace
namespace game_core
{
    void PublishCinematicSection(GameTitle title, int32_t scene)
    {
        const size_t i = static_cast<size_t>(title);
        if (i < 8)
            g_cinematicSection[i].store(scene, std::memory_order_relaxed);
    }
    CinematicControlState ReadCinematicControl(int32_t& scene, int32_t& shot)
    {
        if (!g_cinematicTlsIndex)
            return CinematicControlState::Unknown;
        __try
        {
            auto** slots = reinterpret_cast<void**>(__readgsqword(0x58));
            if (!slots)
                return CinematicControlState::Unknown;
            auto* tls = reinterpret_cast<unsigned char*>(slots[*g_cinematicTlsIndex]);
            if (!tls)
                return CinematicControlState::Unknown;
            auto* globals = *reinterpret_cast<unsigned char**>(tls + 0xA8);
            if (!globals)
                return ClassifyHalo3FamilyCinematicControl(false, false, false);
            if (globals[5] == 0)
                return ClassifyHalo3FamilyCinematicControl(true, false, false);
            auto* shotState = *reinterpret_cast<unsigned char**>(tls + g_cinematicShotStateOffset);
            if (!shotState)
                return ClassifyHalo3FamilyCinematicControl(true, true, false);
            scene = *reinterpret_cast<const int32_t*>(shotState + 4);
            shot = *reinterpret_cast<const int32_t*>(shotState + 8);
            return ClassifyHalo3FamilyCinematicControl(true, true, true);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading the cinematic control state faulted");
            return CinematicControlState::Unknown;
        }
    }
} // namespace game_core
namespace
{

} // namespace
namespace game_core
{
    bool CinematicFovPolicyStockReady()
    {
        return g_reduceCinematicFov.load(std::memory_order_acquire) != nullptr &&
               !g_reduceCinematicFovApplied.load(std::memory_order_acquire);
    }
} // namespace game_core
namespace
{

    bool SharedVrTurnSeated();

} // namespace
namespace game_core
{
    void PrepareCachedPhysicalCrouch(GameTitle title, uintptr_t base, size_t size, uint32_t generation);
    void ApplyCachedPhysicalCrouch(GameTitle title, float* position, float physicalDown, bool playerControlled);
// Snap/smooth turning from the right Sense stick. Rotating the yaw
// reference turns the head-locked view instantly, and the hand-steered aim
// follows because its target is expressed relative to the same reference.
#include "shared_vr_turn.inl"

    void ApplyVrTurn()
    {
        VrPadState pad;
        VR_GetPadState(pad);
        ApplyVrTurn(pad);
    }
} // namespace game_core
namespace
{

} // namespace
namespace game_core
{
    Halo3PlayerUnitGetterFn g_halo3PlayerUnitGetter = nullptr;
    Halo3UnitInVehicleFn g_halo3UnitInVehicle = nullptr;
} // namespace game_core
namespace
{
    // Identity probe: the tag-instance-table and tag-data-base globals,
    // decoded from rip-relative displacements inside the MATCHED type
    // accessor body (its proven instruction layout) - never from absolute RVAs.
} // namespace
namespace game_core
{
    void** g_halo3TagInstanceTable = nullptr;
    void** g_halo3TagDataBase = nullptr;
} // namespace game_core
namespace
{

} // namespace
namespace game_core
{
    unsigned char* Halo3LoadedTagDefinition(uint32_t datum)
    {
        void** instSlot = g_halo3TagInstanceTable;
        void** baseSlot = g_halo3TagDataBase;
        auto* instances = instSlot ? static_cast<unsigned char*>(*instSlot) : nullptr;
        auto* tagBase = baseSlot ? static_cast<unsigned char*>(*baseSlot) : nullptr;
        const uint32_t index = datum & 0xFFFFu;
        if (!instances || !tagBase || index == 0xFFFFu)
            return nullptr;
        const uint32_t address = *reinterpret_cast<const uint32_t*>(instances + static_cast<size_t>(index) * 8 + 4);
        return address ? tagBase + static_cast<size_t>(address) * 4 : nullptr;
    }
} // namespace game_core
namespace
{

    // The engine's real aim, in VR space, published only while a seat's own
    // angular limit is holding the gun short of where the hand points. The
    // reticle consumes it so the crosshair never promises an impossible shot.
    std::atomic<uint32_t> g_aimClampedValid{0};
    std::atomic<float> g_aimClampedVrYaw{0.0f};
    std::atomic<float> g_aimClampedVrPitch{0.0f};
    // Every publication is stamped: an aim loop that stops running (seat left,
    // early return, title change) can never leave the reticle bent. A turret
    // publication also needs the turret seat to still be occupied.
    std::atomic<uint64_t> g_aimClampedAtMs{0};
    std::atomic<uint32_t> g_aimClampedTurret{0};
} // namespace
namespace game_core
{
    void PublishAimClamped(float vrYaw, float vrPitch, bool turret)
    {
        g_aimClampedVrYaw.store(vrYaw, std::memory_order_relaxed);
        g_aimClampedVrPitch.store(vrPitch, std::memory_order_relaxed);
        g_aimClampedTurret.store(turret ? 1u : 0u, std::memory_order_relaxed);
        g_aimClampedAtMs.store(GetTickCount64(), std::memory_order_relaxed);
        g_aimClampedValid.store(1, std::memory_order_release);
    }
    void ClearAimClamped()
    {
        g_aimClampedValid.store(0, std::memory_order_release);
    }
} // namespace game_core
namespace game_core
{
    std::atomic<uint32_t> g_vehicleExitButtons{0};
} // namespace game_core
namespace
{
    // Turret handles: the handle aim the turret servo uses while held.
    std::atomic<bool> g_turretHandleAiming{false};
} // namespace
namespace game_core
{
    std::atomic<uint32_t> g_turretSwallowsGrips{0};
} // namespace game_core
namespace
{
    std::atomic<float> g_turretHandleQ[4]{{0.0f}, {0.0f}, {0.0f}, {1.0f}};
    std::atomic<float> g_turretHandleP[3]{{0.0f}, {0.0f}, {0.0f}};
    // Flight stick: stick rates the aircraft driver flies by while held.
    std::atomic<bool> g_flightStickFlying{false};
    std::atomic<uint32_t> g_flightStickSwallowsGrip{0};
    std::atomic<float> g_flightStickRx{0.0f}, g_flightStickRy{0.0f};

} // namespace
namespace game_core
{
    bool Halo2SeatAuthorsSteeringNow();
    bool SeatUsesWheel()
    {
        if (Halo2SeatAuthorsSteeringNow())
            return g_halo2SteeringWheel.load(std::memory_order_relaxed);
        // The authored flag can outlive Halo 3's module during an in-process
        // title switch.  It owns the shared wheel only while Halo 3's vehicle
        // path is still live; otherwise let ODST or Reach answer for its seat.
        if (Halo3SeatAuthorsSteering())
        {
            return Halo3SeatUsesWheelNow();
        }
        // The wheel is title-agnostic once a seat authors steering.
        if (OdstSeatUsesWheel())
            return true;
        return ReachSeatUsesWheel();
    }

    // ApplyVrTurn and the aim author are shared with ODST and Reach, so this
    // re-checks the live Halo 3 gate rather than trusting the latched flag: a
    // title change that stopped the camera hook mid-drive could otherwise
    // leave a stale "authored" flag suppressing another title's snap turn.
    bool SeatAuthorsSteeringNow()
    {
        if (Halo3SeatAuthorsSteering())
            return true;
        if (Halo2SeatAuthorsSteeringNow())
            return true;
        if (OdstSeatAuthorsSteeringNow())
            return true;
        return ReachSeatAuthorsSteeringNow();
    }
} // namespace game_core
namespace
{

    // ================= ODST first-person vehicle seat ======================
    // A deliberate mirror of the headset-accepted Halo 3 package rather than a
    // retarget of it: the pure math, seat-point convention, settle latch and
    // debounce are the shared ones, but every engine offset, function pointer
    // and piece of published state is ODST's own. ODST shifted its object and
    // unit layout by +0x20 and moved its object-table TLS slot, so a shared
    // sampler could only ever read one title correctly.

    // Turret gyro (turret_gyro_logic.h): yaw/pitch of the hull node a MOUNTED
    // gun rides (the carrier's attachment node, which turns with the hull and
    // never with the gun), plus the sample time and a seat key. Walk-up guns
    // are skipped: their seat node can turn with the gun itself.
    template <class Transform>
    bool LegacyReadGyroHull(Transform& xf, turret_gyro::Hull& out, int& vehicleId)
    {
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const uint32_t before = xf.seq.load(std::memory_order_acquire);
            if (before & 1u)
                continue;
            if (!xf.valid.load(std::memory_order_relaxed))
                return false;
            const uint64_t sampleMs = xf.sampleMs.load(std::memory_order_relaxed);
            float f[3];
            memcpy(f, xf.rawFwd, sizeof(f));
            const int id = static_cast<int>(xf.vehicleId);
            const bool mounted = xf.mountedTurret != 0;
            const uint32_t key = static_cast<uint32_t>(xf.parentHandle) ^ (static_cast<uint32_t>(xf.seatIndex) << 24);
            if (xf.seq.load(std::memory_order_acquire) != before)
                continue;
            const float h = f[0] * f[0] + f[1] * f[1];
            if (!mounted || !sampleMs || !(h > 1e-8f) || !std::isfinite(f[2]))
                return false;
            out.valid = true;
            out.sampleMs = sampleMs;
            out.generation = key;
            out.yaw = atan2f(f[1], f[0]);
            out.pitch = atan2f(f[2], sqrtf(h));
            vehicleId = id;
            return true;
        }
        return false;
    }

#include "vr_turn_vehicle_state.inl"

    // observer_apply_camera_effect (halo3.dll+0x17DF44 in build 1.3528).
    // This is the dedicated post-observer camera-impulse stage: it reads the
    // computed position/forward/up, composes the active effect transform, and
    // writes those three fields back. Bypassing this one function leaves real
    // observer locomotion and headset leaning untouched while preventing the
    // game from moving the HMD view for recoil or other screen shake.
} // namespace
namespace game_core
{
    const char* kObserverCameraEffectSig = "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 4C 89 60 20 "
                                           "55 41 56 41 57 48 8D 68 A1 48 81 EC D0 00 00 00 "
                                           "0F 28 05 ?? ?? ?? ?? 8B 15 ?? ?? ?? ?? 0F 28 0D ?? ?? ?? ?? "
                                           "44 0F 29 40 D8";
} // namespace game_core
namespace
{

    // The leaf cinematic_in_progress getter reads TLS+0xA8, then byte +5. The
    // cinematic_set_shot evaluator reads the same TLS index and writes the
    // current scene/shot pair through a TLS shot-state pointer at +4/+8. Halo 3
    // holds that pointer at TLS+0x90; ODST recompiled the identical function
    // with TLS+0xA0. The `mov edx, imm32` that carries that offset is wildcarded
    // here so the one signature matches both titles (verified unique in each),
    // and LocateCinematicState reads the exact per-title offset from it. The
    // getter is byte-identical between Halo 3 and ODST. Requiring both unique
    // signatures to resolve the same TLS-index global proves every offset used
    // by ReadCinematicControl; otherwise cinematic qualification stays Unknown.
    const char* kCinematicInProgressSig = "8B 0D ?? ?? ?? ?? 33 D2 65 48 8B 04 25 58 00 00 00 "
                                          "41 B8 A8 00 00 00 48 8B 04 C8 49 8B 0C 00 48 85 C9 "
                                          "74 03 8A 51 05 8A C2 C3";
    const char* kCinematicSetShotSig = "40 53 48 83 EC 20 8B DA E8 ?? ?? ?? ?? 48 85 C0 74 33 "
                                       "44 8B 48 04 8B 00 44 8B 05 ?? ?? ?? ?? "
                                       "65 48 8B 0C 25 58 00 00 00 BA ?? 00 00 00 "
                                       "4A 8B 0C C1 48 8B 14 0A 8B CB 89 42 04 44 89 4A 08";

} // namespace
namespace game_core
{
    void LocateCinematicState(uintptr_t base, size_t size)
    {
        // Never retain a pointer from a prior title generation when a new
        // signature proof fails.
        g_cinematicTlsIndex = nullptr;
        const uintptr_t getter = sig::Find(base, size, kCinematicInProgressSig);
        const uintptr_t setter = sig::Find(base, size, kCinematicSetShotSig);
        const bool uniqueGetter = getter && !sig::Find(getter + 1, base + size - getter - 1, kCinematicInProgressSig);
        const bool uniqueSetter = setter && !sig::Find(setter + 1, base + size - setter - 1, kCinematicSetShotSig);
        if (!uniqueGetter || !uniqueSetter)
        {
            LOG("cutscene facing: cinematic state signatures missing/ambiguous; "
                "automatic shot yaw disabled");
            return;
        }

        const int32_t getterDisp = *reinterpret_cast<const int32_t*>(getter + 2);
        const int32_t setterDisp = *reinterpret_cast<const int32_t*>(setter + 0x1B);
        auto* getterIndex = reinterpret_cast<uint32_t*>(getter + 6 + getterDisp);
        auto* setterIndex = reinterpret_cast<uint32_t*>(setter + 0x1F + setterDisp);
        const uintptr_t getterAddress = reinterpret_cast<uintptr_t>(getterIndex);
        const uintptr_t setterAddress = reinterpret_cast<uintptr_t>(setterIndex);
        if (getterIndex != setterIndex || getterAddress < base || getterAddress + sizeof(uint32_t) > base + size ||
            setterAddress < base || setterAddress + sizeof(uint32_t) > base + size || *getterIndex >= 256)
        {
            LOG("cutscene facing: cinematic TLS proof failed; "
                "automatic shot yaw disabled");
            return;
        }

        // Read the per-title shot-state TLS offset directly from the setter's
        // `mov edx, imm32` (0x90 Halo 3 / 0xA0 ODST). Reject an out-of-range
        // value rather than trusting a mismatched build.
        const uint32_t shotStateOffset = *reinterpret_cast<const uint32_t*>(setter + 0x29);
        if (shotStateOffset < 0x40 || shotStateOffset > 0x400)
        {
            LOG("cutscene facing: shot-state offset 0x%X out of range; "
                "automatic shot yaw disabled",
                shotStateOffset);
            return;
        }

        g_cinematicShotStateOffset = shotStateOffset;
        g_cinematicTlsIndex = getterIndex;
        LOG("cutscene facing: exact scene/shot state resolved (shot-state "
            "TLS offset 0x%X); yaw will rebase at cinematic cuts",
            shotStateOffset);
    }
} // namespace game_core
namespace
{

    // H3 callbacks retain their trampolines through native calls as well as
    // mod work. Until the complete install publishes ready, use stock only.
    std::atomic<bool> g_manualVrRecoveryRequested{false};
    std::atomic<uint64_t> g_manualVrRecoveryToken{0};
    std::atomic<bool> g_manualVrRecoveryComplete{false};
    std::atomic<const char*> g_manualVrRecoveryStatus{
        "All games: retries VR after a flat-screen lock. Wait for the level to finish loading."};
} // namespace
namespace game_core
{
    bool ManualVrRecoveryFor(GameTitle title)
    {
        return ManualVrRecoveryMatches(
            g_manualVrRecoveryToken.load(std::memory_order_acquire), title, TitleAdapter_GetGeneration(title));
    }
} // namespace game_core
namespace
{

    // common/gated_hook.h's install policy on the live MinHook calls (which carry
    // the test-only fault injection). Used by Reach and Halo 4.
} // namespace
namespace game_core
{
    GatedHook InstallGatedHook(void*& target, void* address, void* detour, void** original)
    {
        return InstallGatedHookWith([](void* t, void* d, void** o) { return MCCVR_CreateHookChecked(t, d, o); },
            [](void* t) { return MH_EnableHook(t); }, [](void* t) { return MH_DisableHook(t); },
            [](void* t) { return MCCVR_RemoveHookChecked(t); }, target, address, detour, original);
    }
} // namespace game_core
namespace
{

    // Reach production first-person path. Every interpolation transaction keeps
    // a bounded untouched graph; the live copy serves marker/attachment users,
    // while every matching final palette reconstructs from private scratch.
    // Hot hooks perform bounded reads and atomic status publication only.
} // namespace
namespace game_core
{
    int SafeReadBytes(const void* src, void* dst, size_t n)
    {
        __try
        {
            memcpy(dst, src, n);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("copying from game memory faulted");
            return 0;
        }
        return 1;
    }

    int SafeWriteBytes(void* dst, const void* src, size_t n)
    {
        __try
        {
            memcpy(dst, src, n);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("copying into game memory faulted");
            return 0;
        }
        return 1;
    }
} // namespace game_core
namespace
{

} // namespace
namespace game_core
{
    int32_t LegacyCollisionIgnoredObject(GameTitle title)
    {
        int32_t object = -1;
        __try
        {
            if (title == GameTitle::Halo3 && g_halo3PlayerUnitGetter)
                object = g_halo3PlayerUnitGetter(0);
            else if (title == GameTitle::Halo3ODST && g_odstPlayerUnitGetter)
                object = g_odstPlayerUnitGetter(0);
            else if (title == GameTitle::HaloReach && g_reachCamera.playerUnitByOutputUser)
                object = g_reachCamera.playerUnitByOutputUser(0);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading the player unit for collision faulted");
            object = -1;
        }
        return object;
    }
} // namespace game_core
namespace
{

} // namespace
namespace game_core
{
    const LegacyWeaponCollisionBounds* LegacyWeaponBoundsForTitle(GameTitle title, uint32_t checksum)
    {
        if (title == GameTitle::HaloReach)
            return LegacyFindWeaponCollisionBounds(kReachWeaponCollisionBounds, checksum);
        if (title == GameTitle::Halo3 || title == GameTitle::Halo3ODST)
            return LegacyFindWeaponCollisionBounds(kH3OdstWeaponCollisionBounds, checksum);
        return nullptr;
    }

    bool LegacyReadWeaponRenderModelChecksum(GameTitle title, uint16_t renderModelTag, uint32_t& checksum)
    {
        checksum = 0;
        if (renderModelTag == 0xFFFFu)
            return false;
        if (title == GameTitle::HaloReach)
        {
            int nodeCount = 0;
            return ReachReadRenderModelIdentity(renderModelTag, checksum, nodeCount);
        }
        __try
        {
            const uint8_t* definition = title == GameTitle::Halo3       ? Halo3LoadedTagDefinition(renderModelTag)
                                        : title == GameTitle::Halo3ODST ? OdstLoadedTagDefinition(renderModelTag)
                                                                        : nullptr;
            return definition && SafeReadBytes(definition + 0x08, &checksum, sizeof(checksum));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading the weapon render-model checksum faulted");
            return false;
        }
    }
} // namespace game_core
namespace
{

} // namespace
namespace game_core
{
    void LegacyPublishReloadTarget(GameTitle title, uint16_t tag, uint32_t generation,
        const FpInterpolationContext& context, const BoneMatrix& root, const BoneMatrix* solved, const int32_t* boneMap,
        const FpExplicitPoseTargets* targets)
    {
        if (!g_config.manual_reload || !context.valid || context.slot != 0 || !solved || !boneMap ||
            context.count <= 0 || context.count > 64)
            return;
        uint32_t identity = 0;
        int32_t mapped = -1;
        if (!LegacyReadWeaponRenderModelChecksum(title, tag, identity) ||
            !SafeReadBytes(boneMap, &mapped, sizeof(mapped)) || mapped < 0 || mapped >= context.count)
            return;
        const auto* model = weapon_model::Find(title, identity);
        BoneMatrix held{};
        float basis[9]{}, point[3]{};
        if (!model || !model->vertexCount || !ComposeBoneMatrices(root, solved[mapped], held) ||
            !NormalizedBasis(held, basis) ||
            !weapon_model::ReceiverPoint(model, held.scale, basis, held.translation, point))
            return;
        contact_melee::TrackingToWorld transform{};
        uint64_t serial = 0;
        if (title == GameTitle::HaloReach)
        {
            if (!targets)
                return;
            transform = targets->contactSpace[1];
            serial = targets->contactSerial;
        }
        else
        {
            if (!g_fpStereoSolveScope.armed || !g_baseCamValid.load())
                return;
            serial = g_fpStereoSolveScope.anatomicalTracking.serial;
            const float sh = sinf(g_headYawRef), ch = cosf(g_headYawRef);
            const float cg = cosf(g_gameYawRef), sg = sinf(g_gameYawRef);
            transform.unitsPerMetre = g_worldScale.load();
            transform.axis[0] = {cg * sh + sg * ch, sg * sh - cg * ch, 0};
            transform.axis[1] = {0, 0, 1};
            transform.axis[2] = {-cg * ch + sg * sh, -sg * ch - cg * sh, 0};
            const auto reference = transform.World({g_headPosRef[0], g_headPosRef[1], g_headPosRef[2]});
            transform.origin = {
                g_baseCamX.load() - reference.x, g_baseCamY.load() - reference.y, g_baseCamZ.load() - reference.z};
        }
        VR_PublishReloadTarget(title, generation, identity, serial, transform, point);
    }
} // namespace game_core
namespace
{

} // namespace
namespace game_core
{
#include "left_hand_presentation.inl"
} // namespace game_core
namespace
{
} // namespace
namespace game_core
{
#include "visual_hand_palette.inl"
} // namespace game_core
namespace
{
#include "legacy_runtime_weapon_bounds.inl"

    void LegacyObserveReserveWeapon(GameTitle title, uint32_t generation, uint64_t heldIdentity);

    void LegacyObserveReloadModel(
        GameTitle title, uint16_t renderModelTag, uint32_t generation, const BoneMatrix* source, const int32_t* boneMap)
    {
        // Optional reload identity is independent of collision settings. Read
        // through each title's existing verified tag reader; never change a
        // palette, inventory, native animation or camera to select an accessory.
        // Holsters also read it (catalog identities only) to teach the reserve
        // tracker; the held-model observer below keeps its own switches.
        const bool observeHeld = g_config.manual_reload || g_config.per_gun_alignment;
        if (source && boneMap && generation)
        {
            const bool primary =
                title == GameTitle::HaloReach || (g_fpInterpolationContexts[0].source == source &&
                                                     g_fpInterpolationContexts[0].generation == generation);
            if (!primary)
                return;
            uint32_t identity = 0;
            // Observe the native body mapping too: it establishes the appended
            // held range for unfamiliar modded models without name guessing.
            const bool held =
                observeHeld && LegacyClassifyRuntimeWeapon(title, renderModelTag, generation, source, boneMap);
            const bool read = LegacyReadWeaponRenderModelChecksum(title, renderModelTag, identity);
            {
                // One line per checksum that finds no catalog row (and is not a
                // classified held model): the identity never reaches the lens rows.
                static uint32_t loggedMiss[8]{};
                static unsigned missNext = 0;
                if (read && !weapon_model::Find(title, identity) && !held &&
                    std::find(std::begin(loggedMiss), std::end(loggedMiss), identity) == std::end(loggedMiss))
                {
                    loggedMiss[missNext++ & 7u] = identity;
                    LOG("weapon identity: title %u model tag %04X checksum %08X has no catalog row",
                        static_cast<unsigned>(title), renderModelTag, identity);
                }
            }
            if (::weapon_model::PublishLegacyIdentity(read, read && weapon_model::Find(title, identity), held))
                VR_ObserveWeaponModel(title, generation, weapon_model::LiveIdentity(identity, renderModelTag));
            if (g_config.weapon_holsters)
                LegacyObserveReserveWeapon(
                    title, generation, read && weapon_model::Find(title, identity) ? identity : 0);
        }
    }

} // namespace
namespace game_core
{
    bool LegacyObserveWeaponRenderModel(
        GameTitle title, uint16_t renderModelTag, uint32_t generation, const BoneMatrix* source, const int32_t* boneMap)
    {
        LegacyObserveReloadModel(title, renderModelTag, generation, source, boneMap);
        LegacyWorldCollisionFeature* feature = LegacyCollisionForTitle(title);
        if (!feature || !generation || feature->generation != generation)
            return false;
        if (!g_config.world_collision && !g_config.physical_melee)
            return false;
        // Observe the body even when its checksum is unfamiliar. Only the
        // native appended held graph can authorize an unknown weapon model.
        const bool mappedWeapon = feature->runtimeBoundsProven.load(std::memory_order_acquire) &&
                                  LegacyClassifyRuntimeWeapon(title, renderModelTag, generation, source, boneMap);
        uint32_t checksum = 0;
        if (!LegacyReadWeaponRenderModelChecksum(title, renderModelTag, checksum))
            return false;
        const bool catalogued = LegacyWeaponBoundsForTitle(title, checksum) != nullptr;
        if (!catalogued && !mappedWeapon)
            return false;
        int32_t mappedRoot = -1;
        if (!source || !boneMap || !SafeReadBytes(boneMap, &mappedRoot, sizeof(mappedRoot)) || mappedRoot < 0 ||
            mappedRoot >= 64)
            return false;
        // H3 and ODST's proven native interpolation slots own their source graphs.
        // Do not let a secondary submission evict the primary weapon identity.
        const int slot = (title == GameTitle::Halo3 || title == GameTitle::Halo3ODST) &&
                                 g_fpInterpolationContexts[1].source == source &&
                                 g_fpInterpolationContexts[1].generation == generation &&
                                 g_fpInterpolationContexts[1].slot == 1
                             ? 1
                             : 0;
        auto& observation = feature->weaponObservation[slot];
        uint32_t sequence = observation.sequence.load(std::memory_order_acquire);
        if ((sequence & 1u) ||
            !observation.sequence.compare_exchange_strong(sequence, sequence + 1, std::memory_order_acq_rel))
            return false;
        feature->weaponObservation[slot].root.store(mappedRoot, std::memory_order_relaxed);
        feature->weaponObservation[slot].source.store(source, std::memory_order_relaxed);
        feature->weaponObservation[slot].tag.store(renderModelTag, std::memory_order_relaxed);
        feature->weaponObservation[slot].checksum.store(checksum, std::memory_order_relaxed);
        feature->weaponObservation[slot].generation.store(generation, std::memory_order_relaxed);
        feature->weaponObservation[slot].atMs.store(GetTickCount64(), std::memory_order_relaxed);
        observation.sequence.store(sequence + 2, std::memory_order_release);
        if (!catalogued)
            feature->unknownWeaponModels.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    bool LegacyBuildMappedWeaponBounds(GameTitle title, const FpInterpolationContext& context, const BoneMatrix& root,
        const BoneMatrix* solved, float output[][3], uint64_t& shapeId)
    {
        shapeId = 0;
        auto* feature = LegacyCollisionForTitle(title);
        if (!feature || !solved || !context.valid || context.slot < 0 || context.slot > 1 ||
            (context.slot == 1 && title != GameTitle::Halo3 && title != GameTitle::Halo3ODST))
            return false;
        const auto& observation = feature->weaponObservation[context.slot];
        const uint32_t sequence = observation.sequence.load(std::memory_order_acquire);
        if (sequence & 1u)
            return false;
        const uint64_t observedAt = feature->weaponObservation[context.slot].atMs.load(std::memory_order_acquire);
        if (!LegacyWeaponCollisionCacheCanSupply(GetTickCount64(), observedAt, feature->generation,
                feature->weaponObservation[context.slot].generation.load(std::memory_order_relaxed)))
            return false;
        const int32_t mappedRoot = feature->weaponObservation[context.slot].root.load(std::memory_order_relaxed);
        if (!LegacyMappedWeaponRootIsUsable(mappedRoot, context.count,
                reinterpret_cast<uintptr_t>(
                    feature->weaponObservation[context.slot].source.load(std::memory_order_relaxed)),
                reinterpret_cast<uintptr_t>(context.source)))
            return false;
        const uint32_t tag = feature->weaponObservation[context.slot].tag.load(std::memory_order_relaxed);
        const uint32_t checksum = feature->weaponObservation[context.slot].checksum.load(std::memory_order_relaxed);
        if (observation.sequence.load(std::memory_order_acquire) != sequence)
            return false;
        uint32_t liveChecksum = 0;
        if (tag > 0xFFFEu || !LegacyReadWeaponRenderModelChecksum(title, static_cast<uint16_t>(tag), liveChecksum) ||
            liveChecksum != checksum)
            return false;
        Halo4WeaponCollisionBounds bounds{};
        if (LegacyReadRuntimeWeaponBounds(title, static_cast<uint16_t>(tag), checksum, bounds))
            feature->runtimeBoundsReads.fetch_add(1, std::memory_order_relaxed);
        else
        {
            feature->runtimeBoundsRefusals.fetch_add(1, std::memory_order_relaxed);
            const auto* authored = LegacyWeaponBoundsForTitle(title, checksum);
            if (!authored)
                return false;
            bounds.runtimeImportChecksum = checksum;
            memcpy(bounds.minimum, authored->minimum, sizeof(bounds.minimum));
            memcpy(bounds.maximum, authored->maximum, sizeof(bounds.maximum));
        }
        BoneMatrix modelRoot{};
        float basis[9]{};
        if (!ComposeBoneMatrices(root, solved[mappedRoot], modelRoot) || !NormalizedBasis(modelRoot, basis))
            return false;
        // Match the renderer: destination[0] = root * source[boneMap[0]].
        // The combined graph's source[0] is a body/camera root, not the gun.
        const bool valid = Halo4BuildWeaponCollisionBoundsSamples(bounds, modelRoot.scale, basis, modelRoot.translation,
                               modelRoot.translation, modelRoot.translation, output,
                               kLegacyCollisionWeaponSamples) == kLegacyCollisionWeaponSamples;
        if (valid)
            shapeId = LegacyRuntimeWeaponShapeId(static_cast<uint16_t>(tag), mappedRoot, bounds);
        return valid;
    }
} // namespace game_core
namespace
{

} // namespace
namespace game_core
{
    bool InstallLegacyWorldCollision(LegacyWorldCollisionFeature& feature, GameTitle title, uintptr_t base, size_t size,
        uint32_t generation, void* detour, uint32_t expectedRva, const char* signature, bool fourArgumentResolver,
        uint32_t schedulerRva, const char* schedulerSignature)
    {
        const char* const titleName = title == GameTitle::Halo3       ? "Halo 3"
                                      : title == GameTitle::Halo3ODST ? "ODST"
                                      : title == GameTitle::HaloReach ? "Reach"
                                                                      : "Unknown title";
        static constexpr char kFiveArgumentResolveSignature[] =
            "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 41 56 41 57 "
            "48 8D 68 A9 48 81 EC C0 00 00 00 48 8B 05 ?? ?? ?? ?? 41 83 CF "
            "FF 48 89 45 BF 49 8B F8 0F BA 6D BF 09";
        const char* const resolveSignature = signature ? signature : kFiveArgumentResolveSignature;
        feature.title = title;
        feature.runtimeBoundsProven.store(false, std::memory_order_release);
        feature.generation = generation;
        const bool runtimeBounds = LegacyProveRuntimeWeaponBounds(title, base, size);
        feature.runtimeBoundsProven.store(runtimeBounds, std::memory_order_release);
        feature.runtimeBoundsReads.store(0);
        feature.runtimeBoundsRefusals.store(0);
        feature.unknownWeaponModels.store(0);
        LOG("%s runtime weapon bounds: %s; native held-model remap; shared world contact "
            "and physical melee samples; catalog/hand fallback is feature-local",
            titleName, runtimeBounds ? "proven" : "StockFallback (missing/moved/ambiguous native proof)");
        feature.fourArgumentResolver = fourArgumentResolver;
        feature.installed.store(false, std::memory_order_release);
        feature.target = nullptr;
        feature.original = nullptr;
        feature.resolver = nullptr;
        feature.nextQueryAtMs.store(0, std::memory_order_release);
        feature.queryActive.store(false, std::memory_order_release);
        feature.engineCalls.store(0, std::memory_order_release);
        feature.queryTicks.store(0, std::memory_order_release);
        feature.targetMisses.store(0, std::memory_order_release);
        feature.seeds.store(0, std::memory_order_release);
        feature.worker[0] = LegacyCollisionWorkerHand{};
        feature.worker[1] = LegacyCollisionWorkerHand{};
        feature.callbacks.store(0, std::memory_order_release);
        feature.queries.store(0, std::memory_order_release);
        feature.contacts[0].store(0, std::memory_order_release);
        feature.contacts[1].store(0, std::memory_order_release);
        feature.weaponContacts.store(0, std::memory_order_release);
        feature.boundsPublished.store(0, std::memory_order_release);
        feature.boundsFallbacks.store(0, std::memory_order_release);
        feature.secondaryPublishedAtMs.store(0, std::memory_order_release);
        feature.secondaryPublishedObject.store(-1, std::memory_order_release);
        for (auto& observation : feature.weaponObservation)
        {
            observation.sequence.store(0, std::memory_order_release);
            observation.tag.store(0xFFFFu, std::memory_order_release);
            observation.checksum.store(0, std::memory_order_release);
            observation.generation.store(0, std::memory_order_release);
            observation.atMs.store(0, std::memory_order_release);
            observation.root.store(-1, std::memory_order_release);
            observation.source.store(nullptr, std::memory_order_release);
        }
        feature.applied[0].store(0, std::memory_order_release);
        feature.applied[1].store(0, std::memory_order_release);
        feature.failures.store(0, std::memory_order_release);
        const uintptr_t hit = sig::Find(base, size, resolveSignature);
        const bool unique = hit && !sig::Find(hit + 1, base + size - hit - 1, resolveSignature);
        if (!unique || hit != base + expectedRva)
        {
            LOG("%s world collision StockFallback: editing-kit-mapped "
                "collision resolver was %s%s; camera, hands, input and "
                "OpenXR remain live",
                titleName, unique ? "unique" : "missing/ambiguous",
                unique && hit != base + expectedRva ? " but moved" : "");
            return false;
        }
        uintptr_t hookHit = hit;
        if (schedulerRva)
        {
            if (!schedulerSignature)
                return false;
            hookHit = sig::Find(base, size, schedulerSignature);
            const bool schedulerUnique =
                hookHit && !sig::Find(hookHit + 1, base + size - hookHit - 1, schedulerSignature);
            if (!schedulerUnique || hookHit != base + schedulerRva)
            {
                LOG("%s world collision StockFallback: editing-kit-mapped "
                    "active collision scheduler was %s%s; camera, hands, "
                    "input and OpenXR remain live",
                    titleName, schedulerUnique ? "unique" : "missing/ambiguous",
                    schedulerUnique && hookHit != base + schedulerRva ? " but moved" : "");
                return false;
            }
        }
        void* trampoline = nullptr;
        void* const target = reinterpret_cast<void*>(hookHit);
        if (MCCVR_CreateHookChecked(target, detour, &trampoline) != MH_OK || !trampoline)
        {
            LOG("%s world collision StockFallback: optional resolver hook "
                "creation failed",
                titleName);
            return false;
        }
        feature.target = target;
        feature.original = trampoline;
        feature.resolver = schedulerRva ? reinterpret_cast<void*>(hit) : trampoline;
        if (MH_EnableHook(target) != MH_OK)
        {
            MCCVR_RemoveHookChecked(target);
            feature.target = nullptr;
            feature.original = nullptr;
            feature.resolver = nullptr;
            LOG("%s world collision StockFallback: optional resolver hook "
                "enable failed",
                titleName);
            return false;
        }
        feature.installed.store(true, std::memory_order_release);
        if (schedulerRva)
            LOG("%s world collision Installed: native resolve wrapper "
                "+0x%X, active central scheduler +0x%X; final visible hand "
                "extrema and checksum-selected authored weapon bounds; "
                "33 ms bounded sweeps, contact haptics, fail-open isolation",
                titleName, expectedRva, schedulerRva);
        else
            LOG("%s world collision Installed: native resolve wrapper +0x%X; "
                "final visible hand extrema and checksum-selected authored "
                "weapon bounds; 33 ms bounded sweeps, contact haptics, "
                "fail-open isolation",
                titleName, expectedRva);
        return true;
    }
} // namespace game_core
namespace
{

    struct SharedMeleeTelemetry
    {
        std::atomic<uint32_t> polls{0}, unavailable{0}, invalidVelocity{0};
        std::atomic<uint32_t> crossings{0}, pulses{0}, cooldownBlocks{0}, peakCmPerSecond{0};
    };
    SharedMeleeTelemetry g_sharedMeleeTelemetry[5];

    SharedMeleeTelemetry* SharedMeleeForTitle(GameTitle title)
    {
        switch (title)
        {
        case GameTitle::Halo2:
            return &g_sharedMeleeTelemetry[0];
        case GameTitle::Halo3:
            return &g_sharedMeleeTelemetry[1];
        case GameTitle::Halo3ODST:
            return &g_sharedMeleeTelemetry[2];
        case GameTitle::HaloReach:
            return &g_sharedMeleeTelemetry[3];
        case GameTitle::Halo4:
            return &g_sharedMeleeTelemetry[4];
        default:
            return nullptr;
        }
    }

    void LegacyWorldCollisionLogTick()
    {
        const GameTitle title = TitleAdapter_GetActiveTitle();
        LegacyWorldCollisionFeature* feature = LegacyCollisionForTitle(title);
        static uint64_t lastLogMs = 0;
        const uint64_t now = GetTickCount64();
        if (now - lastLogMs < 2000)
            return;
        lastLogMs = now;
        if (title == GameTitle::HaloReach)
        {
            ReportReachContactMelee();
            ReportReachMuzzle();
            ReportReachWeaponHaptics();
            ReportReachGrabPickup();
        }
        if (title == GameTitle::Halo3)
        {
            ReportHalo3ContactMelee();
            ReportHalo3WeaponHaptics();
            ReportHalo3Avatar();
        }
        if (title == GameTitle::Halo3ODST)
        {
            ReportOdstContactMelee();
            ReportOdstMuzzle();
            ReportOdstWeaponHaptics();
            ReportOdstControllerFingers();
        }
        if (auto* melee = SharedMeleeForTitle(title))
            LOG("Physical melee title=%d: requested=%d world-contact=%d threshold=%.2f m/s; "
                "%u polls / %u unavailable, %u missing velocity, peak %.2f m/s, "
                "%u crossings / %u native-input pulses / %u cooldown blocks; native range/target/damage",
                static_cast<int>(title), g_config.physical_melee ? 1 : 0, g_config.world_collision ? 1 : 0,
                g_config.physical_melee_swing_speed, melee->polls.exchange(0), melee->unavailable.exchange(0),
                melee->invalidVelocity.exchange(0), melee->peakCmPerSecond.exchange(0) / 100.0f,
                melee->crossings.exchange(0), melee->pulses.exchange(0), melee->cooldownBlocks.exchange(0));
        if (!feature)
            return;
        const char* const name = title == GameTitle::Halo3       ? "Halo 3"
                                 : title == GameTitle::Halo3ODST ? "ODST"
                                                                 : "Reach";
        LOG("%s world collision: installed=%d requested=%d, %llu native "
            "queries, left/right contacts %llu/%llu, weapon contacts %llu, "
            "authored bounds %llu published / %llu hand-only fallback, "
            "visible corrections %llu/%llu, callbacks %u, isolated "
            "failures %llu; scheduling %llu engine calls / %llu ticks / "
            "%llu target misses / %llu seeds, mapped gun root %d",
            name, feature->installed.load(std::memory_order_acquire) ? 1 : 0, g_config.world_collision ? 1 : 0,
            static_cast<unsigned long long>(feature->queries.exchange(0, std::memory_order_relaxed)),
            static_cast<unsigned long long>(feature->contacts[0].exchange(0, std::memory_order_relaxed)),
            static_cast<unsigned long long>(feature->contacts[1].exchange(0, std::memory_order_relaxed)),
            static_cast<unsigned long long>(feature->weaponContacts.exchange(0, std::memory_order_relaxed)),
            static_cast<unsigned long long>(feature->boundsPublished.exchange(0, std::memory_order_relaxed)),
            static_cast<unsigned long long>(feature->boundsFallbacks.exchange(0, std::memory_order_relaxed)),
            static_cast<unsigned long long>(feature->applied[0].exchange(0, std::memory_order_relaxed)),
            static_cast<unsigned long long>(feature->applied[1].exchange(0, std::memory_order_relaxed)),
            feature->callbacks.load(std::memory_order_acquire),
            static_cast<unsigned long long>(feature->failures.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(feature->engineCalls.exchange(0)),
            static_cast<unsigned long long>(feature->queryTicks.exchange(0)),
            static_cast<unsigned long long>(feature->targetMisses.exchange(0)),
            static_cast<unsigned long long>(feature->seeds.exchange(0)),
            feature->weaponObservation[0].root.load(std::memory_order_relaxed));
        LOG("%s runtime weapon bounds: proven=%d reads=%llu refusals=%llu "
            "unknown-model observations=%llu (contact and melee)",
            name, feature->runtimeBoundsProven.load() ? 1 : 0, feature->runtimeBoundsReads.exchange(0),
            feature->runtimeBoundsRefusals.exchange(0), feature->unknownWeaponModels.exchange(0));
        if (title == GameTitle::HaloReach)
            LOG("Reach on-foot hand shot ray: %u applied / %u local stock fallback; "
                "exact completed-pair controller origin/direction, native world clipping",
                g_reachHandShotMoves.exchange(0, std::memory_order_relaxed),
                g_reachHandShotMisses.exchange(0, std::memory_order_relaxed));
    }

} // namespace
namespace game_core
{
#include "../titles/h3/halo3_contact_melee_runtime.inl"
} // namespace game_core
namespace
{
    // Diagnostic (headset feedback: H3 shots inaccurate while scoped): per player shot
    // (hip and scoped), the barrel-trajectory refusal reason and the angle between the
    // view and the direction the shot leaves (marker +0x3C after the resolver), split
    // into yaw/pitch. Scoped: the scope camera forward actually built (<=250 ms old),
    // and whether it came from the barrel. Hip: the engine aim. At most 4 lines a
    // second. Game thread.
    void Halo3ScopedShotDiagnostic(bool applied, const char* reason, const void* markers) noexcept
    {
        static ULONGLONG shotSecond = 0;
        static unsigned shotLines = 0;
        const ULONGLONG shotNow = GetTickCount64();
        if (shotNow - shotSecond >= 1000)
        {
            shotSecond = shotNow;
            shotLines = 0;
        }
        const bool scoped = VR_IsScopeActive();
        if (!markers || ++shotLines > 4)
            return;
        float shotDir[3]{};
        std::memcpy(shotDir, static_cast<const uint8_t*>(markers) + 0x3C, 12);
        const uint64_t builtMs = g_h3ScopeFwdBuiltMs.load(std::memory_order_acquire);
        const bool camera = scoped && builtMs && shotNow >= builtMs && shotNow - builtMs <= 250;
        const float view[3]{camera ? g_h3ScopeFwd[0].load() : g_aimFwdX.load(),
            camera ? g_h3ScopeFwd[1].load() : g_aimFwdY.load(), camera ? g_h3ScopeFwd[2].load() : g_aimFwdZ.load()};
        float yaw = 0, pitch = 0, total = 0;
        DirectionDeltaDegrees(view, shotDir, yaw, pitch, total);
        char source[64];
        if (camera)
            std::snprintf(source, sizeof source, "camera=%s (receipt age %llu ms)",
                g_h3ScopeFwdBarrel.load() ? "barrel" : "aim-fallback",
                static_cast<unsigned long long>(g_h3ScopeFwdAgeMs.load()));
        else
            std::snprintf(source, sizeof source, "%s", scoped ? "camera=not built" : "view=engine aim");
        float shotOrigin[3]{};
        std::memcpy(shotOrigin, static_cast<const uint8_t*>(markers) + 0x60, 12);
        char frame[256];
        Game_FormatShotFrame(GameTitle::Halo3, frame, sizeof frame);
        LOG("Halo 3 shot: barrel %s%s%s; scoped=%d; %s; view vs shot direction %.2f deg (yaw %+.2f pitch %+.2f); "
            "origin %.3f %.3f %.3f, direction %.3f %.3f %.3f%s",
            applied ? "applied" : "refused", applied ? "" : " reason=", reason ? reason : "", scoped ? 1 : 0, source,
            total, yaw, pitch, shotOrigin[0], shotOrigin[1], shotOrigin[2], shotDir[0], shotDir[1], shotDir[2], frame);
    }
} // namespace
namespace game_core
{
#include "../titles/h3/halo3_dual_wield_runtime.inl"
} // namespace game_core
namespace game_core
{
#include "../titles/h3/halo3_weapon_haptics.inl"
#include "../titles/h3/halo3_avatar.inl"

} // namespace game_core
namespace game_core
{
    ReserveWeaponState g_reserveWeapon[6]; // Reach, Halo 3, ODST, Halo 4, Halo 2, CE
} // namespace game_core
namespace
{

    int ReserveWeaponTitleIndex(GameTitle title)
    {
        return title == GameTitle::HaloReach   ? 0
               : title == GameTitle::Halo3     ? 1
               : title == GameTitle::Halo3ODST ? 2
               : title == GameTitle::Halo4     ? 3
               : title == GameTitle::Halo2     ? 4
               : title == GameTitle::HaloCE    ? 5
                                               : -1;
    }

    // Caller holds state.busy. read=false (or kNoHandle primary) breaks the
    // learning window; the reserve's learned identity is published.
} // namespace
namespace game_core
{
    void ReserveWeaponObserveLocked(ReserveWeaponState& state, uint32_t generation, bool read, uint32_t primary,
        uint32_t reserve, uint64_t heldIdentity)
    {
        state.tracker.Observe(read ? primary : reserve_weapon::kNoHandle, heldIdentity, GetTickCount64());
        state.publishedIdentity.store(read ? state.tracker.IdentityOf(reserve) : 0, std::memory_order_relaxed);
        state.diagCalls.fetch_add(1, std::memory_order_relaxed);
        state.diagRead.store(read, std::memory_order_relaxed);
        state.diagPrimary.store(primary, std::memory_order_relaxed);
        state.diagReserve.store(reserve, std::memory_order_relaxed);
        state.diagHeld.store(heldIdentity, std::memory_order_relaxed);
        state.diagCandidateHandle.store(state.tracker.CandidateHandle(), std::memory_order_relaxed);
        state.diagCandidateIdentity.store(state.tracker.CandidateIdentity(), std::memory_order_relaxed);
        state.diagCandidateSince.store(state.tracker.CandidateSince(), std::memory_order_relaxed);
        state.diagPrevious.store(state.previous.Previous(), std::memory_order_relaxed);
        state.publishedGeneration.store(generation, std::memory_order_release);
    }
    bool ReserveWeaponEnter(ReserveWeaponState& state, uint32_t generation)
    {
        if (state.busy.test_and_set(std::memory_order_acquire))
            return false;
        if (state.trackerGeneration != generation)
        {
            state.tracker.Reset();
            state.previous.Reset();
            state.trackerGeneration = generation;
        }
        return true;
    }
} // namespace game_core
namespace
{

    bool ReadReserveWeaponHandles(GameTitle title, uint32_t& primary, uint32_t& reserve)
    {
        primary = reserve = reserve_weapon::kNoHandle;
        __try
        {
            if (title == GameTitle::HaloReach)
            {
                if (!g_reachCamera.playerUnitByOutputUser)
                    return false;
                const uint32_t unit = uint32_t(g_reachCamera.playerUnitByOutputUser(0));
                return unit != UINT32_MAX && ReachReadReserveWeapon(unit, primary, reserve);
            }
            if (title == GameTitle::Halo3)
            {
                if (!g_halo3PlayerUnitGetter)
                    return false;
                const uint32_t unit = uint32_t(g_halo3PlayerUnitGetter(0));
                return unit != UINT32_MAX && Halo3ReadReserveWeapon(unit, primary, reserve);
            }
            if (title == GameTitle::Halo3ODST)
            {
                if (!g_odstPlayerUnitGetter)
                    return false;
                const uint32_t unit = uint32_t(g_odstPlayerUnitGetter(0));
                return unit != UINT32_MAX && OdstReadReserveWeapon(unit, primary, reserve);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading the reserve weapon handles faulted");
        }
        primary = reserve = reserve_weapon::kNoHandle;
        return false;
    }

    void LegacyObserveReserveWeapon(GameTitle title, uint32_t generation, uint64_t heldIdentity)
    {
        const int index = ReserveWeaponTitleIndex(title);
        if (index < 0 || index > 2 || !generation)
            return;
        auto& state = g_reserveWeapon[index];
        if (!ReserveWeaponEnter(state, generation))
            return;
        uint32_t primary = reserve_weapon::kNoHandle, reserve = reserve_weapon::kNoHandle;
        const bool read = ReadReserveWeaponHandles(title, primary, reserve);
        ReserveWeaponObserveLocked(state, generation, read, primary, reserve, heldIdentity);
        state.busy.clear(std::memory_order_release);
    }

} // namespace
namespace game_core
{
    ReachGyroHullPublication g_reachGyroHull;
} // namespace game_core
namespace game_core
{
    void GyroPublishHull(ReachGyroHullPublication& p, uint32_t generation, uint32_t identity, const float forward[3])
    {
        const float h = forward[0] * forward[0] + forward[1] * forward[1];
        if (!generation || !(h > 1e-8f) || !std::isfinite(forward[2]))
            return;
        p.sequence.fetch_add(1, std::memory_order_acq_rel);
        p.generation.store(generation, std::memory_order_relaxed);
        p.identity.store(identity, std::memory_order_relaxed);
        p.yaw.store(atan2f(forward[1], forward[0]), std::memory_order_relaxed);
        p.pitch.store(atan2f(forward[2], sqrtf(h)), std::memory_order_relaxed);
        p.sampleMs.store(GetTickCount64(), std::memory_order_relaxed);
        p.sequence.fetch_add(1, std::memory_order_release);
    }
} // namespace game_core
namespace
{

} // namespace
namespace game_core
{
    void GyroClearHull(ReachGyroHullPublication& p)
    {
        if (!p.sampleMs.load(std::memory_order_relaxed))
            return;
        p.sequence.fetch_add(1, std::memory_order_acq_rel);
        p.sampleMs.store(0, std::memory_order_relaxed);
        p.sequence.fetch_add(1, std::memory_order_release);
    }
} // namespace game_core
namespace
{

    // Reader (input thread): a consistent sample or none.
} // namespace
namespace game_core
{
    bool GyroReadHull(ReachGyroHullPublication& p, turret_gyro::Hull& out, uint32_t& identityOut)
    {
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const uint32_t before = p.sequence.load(std::memory_order_acquire);
            if (before & 1u)
                continue;
            out.sampleMs = p.sampleMs.load(std::memory_order_relaxed);
            out.generation = p.generation.load(std::memory_order_relaxed);
            out.yaw = p.yaw.load(std::memory_order_relaxed);
            out.pitch = p.pitch.load(std::memory_order_relaxed);
            identityOut = p.identity.load(std::memory_order_relaxed);
            if (p.sequence.load(std::memory_order_acquire) == before)
            {
                out.valid = out.sampleMs != 0;
                return out.valid;
            }
        }
        return false;
    }
} // namespace game_core
namespace
{

} // namespace
namespace game_core
{
    bool ResolveReachDetourCodeRange(const void* function, ReachDetourCodeRange& range)
    {
        DWORD64 imageBase = 0;
        const PRUNTIME_FUNCTION runtimeFunction =
            RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(function), &imageBase, nullptr);
        if (!runtimeFunction || runtimeFunction->EndAddress <= runtimeFunction->BeginAddress)
        {
            return false;
        }
        range.begin = imageBase + runtimeFunction->BeginAddress;
        range.end = imageBase + runtimeFunction->EndAddress;
        return true;
    }
} // namespace game_core
namespace game_core
{

    bool DisableAndRemoveHalo3ContactMelee()
    {
        g_halo3Contact.enabled.store(false, std::memory_order_release);
        if (!RemoveHalo3MeleeSelection())
            return false;
        return LegacyContactRetire<Halo3ContactTraits>(); // muzzle_hook_set.h retirement
    }

} // namespace game_core
namespace game_core
{
    bool ReachColdExactSignatureAt(uintptr_t base, size_t size, uintptr_t expectedRva, const char* pattern)
    {
        // The shared exact-code proof over the fault-safe scan.
        return base && pattern &&
               ExactCodeSignatureAt(base, size, expectedRva, pattern, MCCVR_SIG_FIND, MCCVR_SIG_IS_CODE);
    }
} // namespace game_core
namespace
{

    std::atomic<uint8_t> g_frameCapStatus[5]{};
} // namespace
namespace game_core
{
    ReachFrameCapBinding g_reachFrameCap{"Reach", "reach_uncap_framerate", &g_frameCapStatus[0]},
        g_halo4FrameCap{"Halo 4", "titles_uncap_framerate", &g_frameCapStatus[1]};
} // namespace game_core
namespace
{

} // namespace
namespace game_core
{
    bool ResolveReachFrameCap(uintptr_t base, size_t size, volatile int32_t*& cap, volatile float*& target)
    {
        constexpr uintptr_t kSetterRva = 0xD5D5, kTableRva = 0xD600;
        constexpr uintptr_t kGateRva = 0x328CC, kLimiterRva = 0x322F5;
        if (size != kReachRetailImageSize || // pinned like every other Reach resolver
            !ReachColdExactSignatureAt(base, size, kSetterRva,
                "83 25 ?? ?? ?? ?? 00 0F 57 C0 F3 0F 11 05 ?? ?? ?? ?? 48 8B 8C 24 90 01 00 00") ||
            !ReachColdExactSignatureAt(base, size, kTableRva,
                "B8 F0 00 00 00 89 05 ?? ?? ?? ?? EB 1D B8 B4 00 00 00 EB F1 B8 78 00 00 00 EB EA "
                "B8 3C 00 00 00 89 05 ?? ?? ?? ?? 85 C0 74 B2 F3 0F 10 0D ?? ?? ?? ?? 66 0F 6E C0 "
                "0F 5B C0 F3 0F 5E C8 F3 0F 11 0D ?? ?? ?? ??") ||
            !ReachColdExactSignatureAt(base, size, kGateRva,
                "48 83 EC 28 45 33 D2 41 BB 01 00 00 00 44 39 15 ?? ?? ?? ?? 41 0F 94 C1 E8 ?? ?? ?? ?? "
                "84 C0 41 0F B6 C9 41 0F 44 CB 84 C9 75 12") ||
            !ReachColdExactSignatureAt(
                base, size, kLimiterRva, "F3 0F 10 3D ?? ?? ?? ?? EB 08 F3 0F 10 3D ?? ?? ?? ?? E8"))
            return false;
        // and dword [cap],0 (imm8 follows the disp) / movss [target],xmm0
        const uintptr_t setterCap = sig::RipTarget(base + kSetterRva + 2, base + kSetterRva + 7);
        const uintptr_t setterTarget = sig::RipTarget(base + kSetterRva + 14, base + kSetterRva + 18);
        const uintptr_t tableCap60 = sig::RipTarget(base + kTableRva + 0x22, base + kTableRva + 0x26);
        const uintptr_t tableTarget = sig::RipTarget(base + kTableRva + 0x41, base + kTableRva + 0x45);
        const uintptr_t gateCap = sig::RipTarget(base + kGateRva + 16, base + kGateRva + 20);
        const uintptr_t limiterTarget = sig::RipTarget(base + kLimiterRva + 4, base + kLimiterRva + 8);
        if (!setterCap || setterCap != gateCap || setterCap != tableCap60 || setterTarget != limiterTarget ||
            setterTarget != tableTarget || setterTarget + 4 != setterCap || setterCap < base ||
            setterCap + 4 > base + size)
            return false;
        cap = reinterpret_cast<volatile int32_t*>(setterCap);
        target = reinterpret_cast<volatile float*>(setterTarget);
        return true;
    }
} // namespace game_core
namespace
{

    // Halo 4 (halo4.dll, hash-pinned by image size): Reach's exact option
    // setter (+0x3DAC5), 60/120/180/240 table (+0x3DAF0) and limiter gate (+0x5F3C4),
    // and the limiter's target load (+0x5EF10, xmm9). Setter, table and gate agree on
    // the cap; setter, table and limiter on the target (here not adjacent to the cap).
} // namespace
namespace game_core
{
    bool ResolveHalo4FrameCap(uintptr_t base, size_t size, volatile int32_t*& cap, volatile float*& target)
    {
        constexpr uintptr_t kSetterRva = 0x3DAC5, kTableRva = 0x3DAF0;
        constexpr uintptr_t kGateRva = 0x5F3C4, kLimiterRva = 0x5EF10;
        if (size != kHalo4RetailImageSize ||
            !ReachColdExactSignatureAt(base, size, kSetterRva,
                "83 25 ?? ?? ?? ?? 00 0F 57 C0 F3 0F 11 05 ?? ?? ?? ?? 48 8B 8C 24 90 01 00 00") ||
            !ReachColdExactSignatureAt(base, size, kTableRva,
                "B8 F0 00 00 00 89 05 ?? ?? ?? ?? EB 1D B8 B4 00 00 00 EB F1 B8 78 00 00 00 EB EA "
                "B8 3C 00 00 00 89 05 ?? ?? ?? ?? 85 C0 74 B2 F3 0F 10 0D ?? ?? ?? ?? 66 0F 6E C0 "
                "0F 5B C0 F3 0F 5E C8 F3 0F 11 0D ?? ?? ?? ??") ||
            !ReachColdExactSignatureAt(base, size, kGateRva,
                "48 83 EC 28 45 33 D2 41 BB 01 00 00 00 44 39 15 ?? ?? ?? ?? 41 0F 94 C1 E8 ?? ?? ?? ?? "
                "84 C0 41 0F B6 C9 41 0F 44 CB 84 C9 75 12") ||
            !ReachColdExactSignatureAt(
                base, size, kLimiterRva, "84 C0 74 0B F3 44 0F 10 0D ?? ?? ?? ?? EB 09 F3 44 0F 10 0D ?? ?? ?? ?? E8"))
            return false;
        const uintptr_t setterCap = sig::RipTarget(base + kSetterRva + 2, base + kSetterRva + 7);
        const uintptr_t setterTarget = sig::RipTarget(base + kSetterRva + 14, base + kSetterRva + 18);
        const uintptr_t tableCap60 = sig::RipTarget(base + kTableRva + 0x22, base + kTableRva + 0x26);
        const uintptr_t tableTarget = sig::RipTarget(base + kTableRva + 0x41, base + kTableRva + 0x45);
        const uintptr_t gateCap = sig::RipTarget(base + kGateRva + 16, base + kGateRva + 20);
        const uintptr_t limiterTarget = sig::RipTarget(base + kLimiterRva + 9, base + kLimiterRva + 13);
        if (!setterCap || setterCap != gateCap || setterCap != tableCap60 || !setterTarget ||
            setterTarget != limiterTarget || setterTarget != tableTarget || setterCap < base ||
            setterCap + 4 > base + size || setterTarget < base || setterTarget + 4 > base + size)
            return false;
        cap = reinterpret_cast<volatile int32_t*>(setterCap);
        target = reinterpret_cast<volatile float*>(setterTarget);
        return true;
    }
} // namespace game_core
namespace
{

    // Halo 3 and ODST (halo3.dll / halo3odst.dll, hash-pinned by image size)
    // carry Reach's cap/target pair (cap int fps, target = 1/cap, at cap + 4) in their
    // own layout: the option setter's "unlimited" branch and 60/120/180/240 table, the
    // in-game settings packet's same pair, and the limiter (target load, then "cap == 0"
    // skips it). All five cap and three target references must agree, and both setters
    // divide the same 1.0. Halo 3 runs at the headset rate only when MCC's stored choice
    // for it is "unlimited"; ODST was pinned at 60 by its own (seen in an automated test run).
    struct Halo3EngineCapSites
    {
        const char* title;
        size_t imageSize;
        uintptr_t setterRva, packetRva, limiterRva;
    };
    constexpr Halo3EngineCapSites kHalo3CapSites{"Halo 3", 0x04768000, 0x9C23, 0xB70B, 0xB0F5D};
    constexpr Halo3EngineCapSites kOdstCapSites{"ODST", 0x04797000, 0x9F43, 0xBD31, 0xBE5A1};

    bool ResolveHalo3EngineFrameCap(
        uintptr_t base, size_t size, const Halo3EngineCapSites& s, volatile int32_t*& cap, volatile float*& target)
    {
        const uintptr_t S = s.setterRva, B = s.packetRva, L = s.limiterRva;
        if (size != s.imageSize ||
            !ReachColdExactSignatureAt(base, size, S,
                "83 25 ?? ?? ?? ?? 00 EB 4C B8 F0 00 00 00 89 05 ?? ?? ?? ?? F3 0F 10 0D ?? ?? ?? ?? "
                "66 0F 6E C0 0F 5B C0 F3 0F 5E C8 F3 0F 11 0D ?? ?? ?? ??") ||
            !ReachColdExactSignatureAt(base, size, B,
                "44 89 25 ?? ?? ?? ?? EB 48 B8 F0 00 00 00 89 05 ?? ?? ?? ?? 66 0F 6E C0 0F 5B C0 "
                "F3 0F 10 0D ?? ?? ?? ?? F3 0F 5E C8 F3 0F 11 0D ?? ?? ?? ??") ||
            !ReachColdExactSignatureAt(base, size, L,
                "84 C0 74 0A F3 0F 10 3D ?? ?? ?? ?? EB 08 F3 0F 10 3D ?? ?? ?? ?? 83 3D ?? ?? ?? ?? 00 "
                "41 0F 94 C1 E8"))
            return false;
        // and [cap],0 / mov [cap],eax (setter); mov [cap],r12d / mov [cap],eax (packet); cmp [cap],0 (limiter)
        const uintptr_t caps[5]{sig::RipTarget(base + S + 2, base + S + 7),
            sig::RipTarget(base + S + 16, base + S + 20), sig::RipTarget(base + B + 3, base + B + 7),
            sig::RipTarget(base + B + 16, base + B + 20), sig::RipTarget(base + L + 24, base + L + 29)};
        // movss [target],xmm1 (setter, packet); movss xmm7,[target] (limiter)
        const uintptr_t targets[3]{sig::RipTarget(base + S + 43, base + S + 47),
            sig::RipTarget(base + B + 43, base + B + 47), sig::RipTarget(base + L + 8, base + L + 12)};
        const uintptr_t ones[2]{
            sig::RipTarget(base + S + 24, base + S + 28), sig::RipTarget(base + B + 31, base + B + 35)};
        for (const uintptr_t c : caps)
            if (!c || c != caps[0])
                return false;
        for (const uintptr_t t : targets)
            if (t != caps[0] + 4)
                return false;
        if (caps[0] < base || caps[0] + 8 > base + size)
            return false;
        for (const uintptr_t o : ones)
        {
            if (o < base || o + 4 > base + size)
                return false;
            float one = 0.0f;
            __try
            {
                one = *reinterpret_cast<const float*>(o);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                GUARD_COUNT("reading Halo 3's frame-cap constant faulted");
                return false;
            }
            if (one != 1.0f)
                return false;
        }
        cap = reinterpret_cast<volatile int32_t*>(caps[0]);
        target = reinterpret_cast<volatile float*>(targets[0]);
        return true;
    }
    bool ResolveHalo3FrameCap(uintptr_t base, size_t size, volatile int32_t*& cap, volatile float*& target)
    {
        return ResolveHalo3EngineFrameCap(base, size, kHalo3CapSites, cap, target);
    }
    bool ResolveOdstFrameCap(uintptr_t base, size_t size, volatile int32_t*& cap, volatile float*& target)
    {
        return ResolveHalo3EngineFrameCap(base, size, kOdstCapSites, cap, target);
    }
    ReachFrameCapBinding g_halo3FrameCap{"Halo 3", "titles_uncap_framerate", &g_frameCapStatus[2]},
        g_odstFrameCap{"ODST", "titles_uncap_framerate", &g_frameCapStatus[3]};

} // namespace
namespace game_core
{
    void ReachFrameCapTick(uintptr_t base, size_t size, uint32_t generation, bool coldWorkAllowed,
        ReachFrameCapBinding& b, FrameCapResolver resolve, bool want);
} // namespace game_core
namespace
{
    // The guarded reads and writes of the proven cap/target pair (the target is
    // written before the cap, as before the lift).
    struct FrameCapOps
    {
        template <class... A>
        static void Log(const char* format, A... args)
        {
            LOG(format, args...);
        }
        static bool Read(const ReachFrameCapBinding& b, int32_t& cap, float& target)
        {
            __try
            {
                cap = *b.cap;
                target = *b.target;
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                GUARD_COUNT("reading the frame cap faulted");
                return false;
            }
        }
        static bool Write(ReachFrameCapBinding& b, int32_t cap, float target)
        {
            __try
            {
                *b.target = target;
                *b.cap = cap;
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                GUARD_COUNT("writing the frame cap faulted");
                return false;
            }
        }
    };
} // namespace
namespace game_core
{
    void ReachFrameCapTick(uintptr_t base, size_t size, uint32_t generation, bool coldWorkAllowed,
        ReachFrameCapBinding& b, FrameCapResolver resolve, bool want)
    {
        reach_frame_cap::Tick<FrameCapOps>(b, base, size, generation, coldWorkAllowed, resolve, want);
    }
} // namespace game_core
namespace
{

    // Halo 2 frame cap (halo2.dll, hash-pinned by image size). MCC's
    // frame-rate option is unpacked by +0x53090 into a settings block: its table at
    // +0x53238 maps the option index to a cap (0=60, 1=120, 2=180, 3=240, else 0 =
    // unlimited) stored at +0x54. The poller +0x53910 (every few seconds) calls it
    // (+0x53BAA) and copies a changed block over the live one at [G]+0xB0, so a
    // plain write would be copied back. The limiter +0x2B634D reads [G]+0x104 (=
    // +0xB0+0x54): cap > 0 -> waits to 1/cap; cap <= 0 -> 1/1000 s (unlimited).
    // Uncap = clear the four immediates' low bytes (every option maps to MCC's own
    // "unlimited"), then write 0 into the live block once; the poller then finds
    // the same value and leaves it. Switching titles_uncap_framerate off writes the
    // stock bytes back, and the poller copies MCC's real cap back in.
    struct Halo2FrameCapBinding
    {
        uint32_t generation = 0;
        bool resolved = false, proven = false, patched = false;
        uint8_t* immediates[4]{};
        volatile uintptr_t* settings = nullptr; // G: the live block's owner
        bool refusedLogged = false;
        int32_t loggedCap = -1;
        uint64_t writes = 0;
        // The second limiter's frame time and "limited" flag, and what we
        // replaced (restored when titles_uncap_framerate goes off).
        bool secondProven = false, secondSaved = false, secondRefusedLogged = false;
        volatile float* frameTime = nullptr;
        volatile uint8_t* limitedFlag = nullptr;
        float savedFrameTime = 0.0f;
        uint8_t savedFlag = 0;
        uint64_t secondWrites = 0;
    } g_halo2FrameCap;

    bool ResolveHalo2FrameCap(uintptr_t base, size_t size, Halo2FrameCapBinding& b)
    {
        constexpr uintptr_t kUnpackRva = 0x53090, kTableRva = 0x53238;
        constexpr uintptr_t kPollCallRva = 0x53BAA, kLimiterRva = 0x2B634D;
        if (size != kHalo2RetailImageSize ||
            !ReachColdExactSignatureAt(base, size, kUnpackRva,
                "40 55 53 48 8D AC 24 F8 FE FF FF 48 81 EC 08 02 00 00 48 8B D9 48 8D 4C 24 30 E8") ||
            !ReachColdExactSignatureAt(base, size, kTableRva,
                "85 C9 74 2B 83 E9 01 74 1F 83 E9 01 74 13 83 E9 01 74 07 83 E9 01 33 C0 EB 1A "
                "B8 F0 00 00 00 EB 13 B8 B4 00 00 00 EB 0C B8 78 00 00 00 EB 05 B8 3C 00 00 00 "
                "0F B6 4C 24 46 84 C9 89 43 54") ||
            !ReachColdExactSignatureAt(base, size, kPollCallRva,
                "E8 ?? ?? ?? ?? 4C 8B 05 ?? ?? ?? ?? 41 0F 10 80 C0 00 00 00 41 0F 10 88 D0 00 00 00 "
                "41 0F 10 90 B0 00 00 00") ||
            !ReachColdExactSignatureAt(base, size, kLimiterRva,
                "48 8B 05 ?? ?? ?? ?? F3 44 0F 10 15 ?? ?? ?? ?? F2 41 0F 5E C0 8B 88 04 01 00 00 "
                "41 0F 28 D2 66 0F 5A C8 85 C9 7E 07 66 0F 6E D1 0F 5B D2"))
            return false;
        const uintptr_t called = sig::RipTarget(base + kPollCallRva + 1, base + kPollCallRva + 5);
        const uintptr_t pollG = sig::RipTarget(base + kPollCallRva + 8, base + kPollCallRva + 12);
        const uintptr_t limiterG = sig::RipTarget(base + kLimiterRva + 3, base + kLimiterRva + 7);
        const uintptr_t unlimited = sig::RipTarget(base + kLimiterRva + 12, base + kLimiterRva + 16);
        float unlimitedSeconds = 0.0f;
        __try
        {
            unlimitedSeconds = *reinterpret_cast<const float*>(unlimited);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading Halo 2's unlimited-frame constant faulted");
            return false;
        }
        if (called != base + kUnpackRva || !pollG || pollG != limiterG || pollG < base || pollG + 8 > base + size ||
            unlimitedSeconds != 1000.0f)
            return false;
        // B8 imm32 at +26, +33, +40, +47: their low bytes. Already zero = our own
        // patch on this same mapped module (a new generation); anything else refuses.
        constexpr uintptr_t kImm[4]{27, 34, 41, 48};
        uint8_t low[4]{};
        for (int i = 0; i < 4; ++i)
        {
            b.immediates[i] = reinterpret_cast<uint8_t*>(base + kTableRva + kImm[i]);
            low[i] = *b.immediates[i];
        }
        const auto table = reach_frame_cap::Halo2CapImmediates(low);
        if (table == reach_frame_cap::Halo2Table::Unknown)
            return false;
        b.patched = table == reach_frame_cap::Halo2Table::Patched;
        b.settings = reinterpret_cast<volatile uintptr_t*>(pollG);
        return true;
    }

    // Halo 2's second limiter. +0x3CD05 reads MCC's frame-rate choice again
    // (through MCC's settings interface) into its own pair: a frame time (choice 0..3 =
    // 1/60, 1/120, 1/180, 1/240 s; 4 = 0) and a "limited" flag (choice != 4). The loop at
    // +0x6BFA53 tests the flag, then Sleeps and spins until the frame time has passed;
    // clearing only the first limiter left Halo 2 at 60 (renderWindow 16.5 ms,
    // xrWaitFrame wait 0). Setter and limiter must agree on both globals, and the four
    // frame-time constants must be the four choices.
    bool ResolveHalo2SecondLimiter(uintptr_t base, size_t size, Halo2FrameCapBinding& b)
    {
        constexpr uintptr_t S = 0x3CD05, L = 0x6BFA53;
        if (size != kHalo2RetailImageSize ||
            !ReachColdExactSignatureAt(base, size, S,
                "0F B6 44 24 45 3C 04 8B C8 0F 95 05 ?? ?? ?? ?? 84 C0 74 32 83 E9 01 74 23 83 E9 01 "
                "74 14 83 E9 01 74 05 0F 57 C0 EB 26 F3 0F 10 05 ?? ?? ?? ?? EB 1C F3 0F 10 05 ?? ?? ?? ?? "
                "EB 12 F3 0F 10 05 ?? ?? ?? ?? EB 08 F3 0F 10 05 ?? ?? ?? ?? 80 7C 24 39 02 "
                "F3 0F 11 05 ?? ?? ?? ??") ||
            !ReachColdExactSignatureAt(base, size, L,
                "80 3D ?? ?? ?? ?? 00 0F 84 ?? ?? ?? ?? 80 3D ?? ?? ?? ?? 00 0F 85 ?? ?? ?? ?? "
                "E8 ?? ?? ?? ?? 84 C0 0F 84 ?? ?? ?? ?? F3 0F 10 05 ?? ?? ?? ?? 0F 2F C6 0F 86"))
            return false;
        // setne [flag] / movss [time],xmm0 (setter); cmp [flag],0 / movss xmm0,[time] (limiter)
        const uintptr_t flag = sig::RipTarget(base + S + 12, base + S + 16);
        const uintptr_t time = sig::RipTarget(base + S + 87, base + S + 91);
        if (!flag || flag != sig::RipTarget(base + L + 2, base + L + 7) || !time ||
            time != sig::RipTarget(base + L + 43, base + L + 47) || flag < base || flag + 1 > base + size ||
            time < base || time + 4 > base + size)
            return false;
        // movss xmm0,[1/240], [1/180], [1/120], [1/60]
        constexpr uintptr_t kConstDisp[4]{44, 54, 64, 74};
        constexpr int kFps[4]{240, 180, 120, 60};
        for (int i = 0; i < 4; ++i)
        {
            const uintptr_t c = sig::RipTarget(base + S + kConstDisp[i], base + S + kConstDisp[i] + 4);
            if (c < base || c + 4 > base + size)
                return false;
            float v = 0.0f;
            __try
            {
                v = *reinterpret_cast<const float*>(c);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                GUARD_COUNT("reading Halo 2's second limiter constant faulted");
                return false;
            }
            if (v != 1.0f / float(kFps[i]))
                return false;
        }
        b.frameTime = reinterpret_cast<volatile float*>(time);
        b.limitedFlag = reinterpret_cast<volatile uint8_t*>(flag);
        return true;
    }

    // Uncap = MCC's own "unlimited" pair (0, 0); switched off = the saved pair back.
    void Halo2SecondLimiterTick(Halo2FrameCapBinding& b)
    {
        if (!b.secondProven)
            return;
        float time = 0.0f;
        uint8_t flag = 0;
        __try
        {
            time = *b.frameTime;
            flag = *b.limitedFlag;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading Halo 2's second frame limiter faulted");
            b.secondProven = false;
            LOG("Halo 2 second frame limiter: FAULTED reading; stock");
            return;
        }
        const auto pair = reach_frame_cap::ClassifyFrameTimeFlag(time, flag);
        if (pair == reach_frame_cap::FramePair::Unknown)
        {
            if (!b.secondRefusedLogged)
            {
                b.secondRefusedLogged = true;
                LOG("Halo 2 second frame limiter: unexpected values (frame time %.6f, flag %u); left stock", time,
                    flag);
            }
            return;
        }
        if (!g_config.titles_uncap_framerate)
        {
            if (b.secondSaved && pair == reach_frame_cap::FramePair::Unlimited)
            {
                __try
                {
                    *b.frameTime = b.savedFrameTime;
                    *b.limitedFlag = b.savedFlag;
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    GUARD_COUNT("restoring Halo 2's second frame limiter faulted");
                    b.secondProven = false;
                    LOG("Halo 2 second frame limiter: FAULTED restoring");
                    return;
                }
                LOG("Halo 2 second frame limiter: titles_uncap_framerate off; restored %.0f fps",
                    1.0f / b.savedFrameTime);
            }
            b.secondSaved = false;
            return;
        }
        if (pair != reach_frame_cap::FramePair::Limited)
            return;
        b.savedFrameTime = time;
        b.savedFlag = flag;
        b.secondSaved = true;
        __try
        {
            *b.frameTime = 0.0f;
            *b.limitedFlag = 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("writing Halo 2's second frame limiter faulted");
            b.secondProven = false;
            LOG("Halo 2 second frame limiter: FAULTED writing; stock");
            return;
        }
        if (b.secondWrites++ < 8)
            LOG("Halo 2 second frame limiter: MCC limited it to %.0f fps; set to unlimited (frame time 0, flag 0)",
                1.0f / time);
    }

    // The option table's four immediate low bytes: 0 (every choice = unlimited) or the
    // stock 240/180/120/60 back, single bytes under VirtualProtect + FlushInstructionCache.
    bool Halo2WriteCapTable(Halo2FrameCapBinding& b, bool restore)
    {
        uint8_t* first = b.immediates[0];
        const size_t span = static_cast<size_t>(b.immediates[3] - first) + 1;
        DWORD oldProtect = 0, ignored = 0;
        if (!VirtualProtect(first, span, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            b.proven = false;
            LOG("Halo 2 frame cap: could not unprotect the option table; %s", restore ? "left patched" : "stock");
            return false;
        }
        for (int i = 0; i < 4; ++i)
            *b.immediates[i] = restore ? reach_frame_cap::kHalo2CapLowBytes[i] : 0;
        VirtualProtect(first, span, oldProtect, &ignored);
        FlushInstructionCache(GetCurrentProcess(), first, span);
        b.patched = !restore;
        b.loggedCap = -1;
        LOG(restore
                ? "Halo 2 frame cap: titles_uncap_framerate off; option table restored (MCC's limit returns at its next settings poll)"
                : "Halo 2 frame cap: option table now maps every frame-rate choice to MCC's \"unlimited\"");
        return true;
    }

} // namespace
namespace game_core
{
    void Halo2FrameCapTick(uintptr_t base, size_t size, uint32_t generation, bool coldWorkAllowed)
    {
        auto& b = g_halo2FrameCap;
        if (b.generation != generation)
        {
            b = {};
            b.generation = generation;
        }
        if (!coldWorkAllowed || !generation)
            return;
        if (!b.resolved)
        {
            b.resolved = true;
            b.proven = ResolveHalo2FrameCap(base, size, b);
            LOG("Halo 2 frame cap: %s", b.proven ? "option table, settings poller and limiter proven; it runs uncapped "
                                                   "and the headset paces it"
                                                 : "binding NOT proven; it keeps MCC's frame-rate limit (stock)");
            b.secondProven = ResolveHalo2SecondLimiter(base, size, b);
            LOG("Halo 2 second frame limiter (+0x6BFA53): %s",
                b.secondProven ? "setter, limiter, flag and frame time proven"
                               : "NOT proven; it keeps MCC's frame-rate limit (stock)");
        }
        Halo2SecondLimiterTick(b);
        g_frameCapStatus[4].store(static_cast<uint8_t>(reach_frame_cap::Halo2Status(
                                      b.proven, b.secondProven, g_config.titles_uncap_framerate)),
            std::memory_order_release);
        if (!b.proven)
            return;
        if (!g_config.titles_uncap_framerate && b.patched)
        {
            (void)Halo2WriteCapTable(b, true);
            return;
        }
        int32_t cap = 0;
        volatile int32_t* live = nullptr;
        __try
        {
            const uintptr_t block = *b.settings;
            if (!block)
                return;
            live = reinterpret_cast<volatile int32_t*>(block + 0x104);
            cap = *live;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading Halo 2's frame cap faulted");
            b.proven = false;
            LOG("Halo 2 frame cap: FAULTED reading the cap; stock");
            return;
        }
        if (cap != b.loggedCap)
        {
            b.loggedCap = cap;
            LOG("Halo 2 frame cap: the limiter's cap is %d (%s)", cap, cap ? "fps" : "unlimited");
        }
        switch (reach_frame_cap::DecideCapOnly(cap, g_config.titles_uncap_framerate))
        {
        case reach_frame_cap::Action::Refuse:
            if (!b.refusedLogged)
            {
                b.refusedLogged = true;
                LOG("Halo 2 frame cap: unexpected cap %d; left stock", cap);
            }
            return;
        case reach_frame_cap::Action::None:
            return;
        case reach_frame_cap::Action::Uncap:
            break;
        }
        // Single-byte writes (the immediates' other bytes are already zero).
        if (!b.patched && !Halo2WriteCapTable(b, false))
            return;
        __try
        {
            *live = 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("writing Halo 2's frame cap faulted");
            b.proven = false;
            LOG("Halo 2 frame cap: FAULTED writing; stock");
            return;
        }
        if (b.writes++ < 8)
            LOG("Halo 2 frame cap: MCC limited Halo 2 to %d fps; set to unlimited (the headset paces each frame)", cap);
    }
} // namespace game_core
namespace
{

    // =======================================================================
    // Halo 4 stock-projection stereo geometry on the sustained wrapper scope.
    //
    // Halo 4 derives EVERY camera artifact - rasterizer camera, projection,
    // render pair and constant bank - inside one straight-line setup call per
    // window. Writing the published element after setup returns is
    // stale by construction, so this core substitutes setup's INPUT instead:
    // the observer result. Per eye it rewrites that camera, re-runs the
    // engine's own unmodified setup, and then runs the engine's own unmodified
    // render transaction. Nothing derives the projection, the bank or the
    // render pair on our side - Halo 4 does, twice, from two cameras. This core
    // changes only position/forward/up for the runtime's two eye offsets and
    // reads back the camera and finished stock projection before rendering.
    // HMD head pose, 6DOF, and HUD/CUI are handled elsewhere, not here.
    //
    // Both hooked functions have exactly ONE caller each and it is the same
    // proven loop, so each detour additionally requires its exact retail
    // return address before it will claim a transaction.
    // =======================================================================

    // =======================================================================
    // Halo 4 experimental authored-volume world contact.
    //
    // Halo 3 has no existing world-contact implementation to copy. This is a
    // deliberately Halo-4-native experiment based on H4EK PhysicsRayCast and
    // the exact authored Storm/held-model node positions. The render thread
    // publishes immutable volume samples and consumes a bounded translation.
    // It hooks the live raycast itself, always preserves the original
    // call, and borrows only an engine context which just completed a dynamic-
    // inclusive ray. Optional object motion is independently fail-open.
    // =======================================================================
#pragma pack(push, 1)
#pragma pack(pop)
} // namespace
namespace game_core
{
#include "cached_physical_crouch.inl"
} // namespace game_core
namespace game_core
{
    TitleHookSupervisorState g_supervisorTitles;
} // namespace game_core
namespace
{

    // Per-title runtime operations used by TitleHookSupervisorThread instead of switching on the title. One row per title;
    // a nullptr entry means the title has nothing to do at that stage. This is the first slice of the title
    // adapter: the shared loop asks "what does the active title do here" and the row answers.
    struct TitleRuntimeOps
    {
        GameTitle title;
        // Manual VR recovery ("Retry VR" in the launcher, F1 menu): accept a retry, report when the recovered
        // camera is live, reset a cancelled request.
        void (*recoveryRequest)(uint32_t generation);
        bool (*recoveryCameraReady)();
        void (*recoveryCancel)();
        // Level-load gate, sampled exactly once per tick on the active title before anything else may touch its
        // module. Returns whether the level is running (hooks/scans allowed).
        bool (*sampleLevelGate)(uintptr_t base, size_t size, uint32_t generation, uint64_t now);
        // Weapon/hand/HUD tunables profile index for Config_ApplyTitleProfile (-1 = shared defaults).
        int (*tunableProfile)();
    };
    // not constinit: MSVC rejects it (C2127); read only after DLL init, so dynamic init order is irrelevant.
    const TitleRuntimeOps kTitleRuntimeOps[] = {
        {GameTitle::Halo3,
            [](uint32_t)
            {
                g_supervisorTitles.manualHaloRecovery = true;
                g_supervisorTitles.haloAttemptedGeneration = 0;
                if (!g_supervisorTitles.gameHooked)
                    g_halo3LevelLoadGate.Rearm();
            },
            [] { return g_halo3InstallReady.load(std::memory_order_acquire); }, nullptr,
            [](uintptr_t base, size_t size, uint32_t generation, uint64_t)
            { return g_halo3LevelLoadGate.AllowsInstall(generation, base, size, kHalo3PlayerViewArray, "Halo 3"); },
            [] { return 0; }},
        {GameTitle::Halo3ODST,
            [](uint32_t)
            {
                g_supervisorTitles.odstManualRecoveryPending = true;
                if (g_supervisorTitles.odstHooked)
                    OdstRequestFallback(OdstFallbackReason::ManualRecovery);
            },
            []
            {
                return !g_supervisorTitles.odstManualRecoveryPending &&
                       g_odstCamera.armed.load(std::memory_order_acquire) &&
                       !g_odstCamera.teardownRequested.load(std::memory_order_acquire);
            },
            nullptr, [](uintptr_t base, size_t size, uint32_t generation, uint64_t)
            { return g_odstLevelLoadGate.AllowsInstall(generation, base, size, kOdstPlayerViewArray, "ODST"); },
            [] { return 1; }},
        {GameTitle::HaloReach,
            [](uint32_t generation)
            {
                g_supervisorTitles.reachManualRecoveryPending = generation;
                if (g_reachCamera.installed.load(std::memory_order_acquire))
                {
                    LOG("Reach camera retirement requested by manual VR recovery");
                    g_reachCamera.teardownRequested.store(true, std::memory_order_release);
                }
            },
            []
            {
                return !g_supervisorTitles.reachManualRecoveryPending &&
                       g_reachCamera.armed.load(std::memory_order_acquire) &&
                       !g_reachCamera.teardownRequested.load(std::memory_order_acquire);
            },
            [] { g_supervisorTitles.reachManualRecoveryPending = 0; },
            // The gate covers the pinned retail image size, not the queried module size.
            [](uintptr_t base, size_t, uint32_t generation, uint64_t)
            {
                return g_reachLevelLoadGate.AllowsInstall(
                    generation, base, kReachRetailImageSize, kReachPlayerViewArray, "Reach");
            },
            [] { return 2; }},
        {GameTitle::Halo4,
            [](uint32_t generation)
            {
                g_supervisorTitles.halo4ManualRecoveryPending = generation;
                if (g_halo4Camera.installed.load(std::memory_order_acquire))
                {
                    LOG("Halo 4 camera retirement requested by manual VR recovery");
                    g_halo4Camera.teardownRequested.store(true, std::memory_order_release);
                }
            },
            []
            {
                return !g_supervisorTitles.halo4ManualRecoveryPending &&
                       g_halo4Camera.armed.load(std::memory_order_acquire) &&
                       !g_halo4Camera.teardownRequested.load(std::memory_order_acquire) &&
                       !g_halo4Camera.sceneTargetMissing.load(std::memory_order_acquire);
            },
            [] { g_supervisorTitles.halo4ManualRecoveryPending = 0; },
            // Halo 4 joins the level-load gate (without it, the open default let the draw-distance
            // debug-var scan touch halo4.dll during loading screens). The range is kept for the cold observation.
            [](uintptr_t base, size_t size, uint32_t generation, uint64_t)
            {
                const bool running =
                    g_halo4LevelLoadGate.AllowsInstall(generation, base, size, kHalo4PlayerViewArray, "Halo 4");
                g_supervisorTitles.halo4Gate = {base, size, true};
                return running;
            },
            [] { return 3; }},
        {GameTitle::Halo2,
            [](uint32_t generation)
            {
                g_supervisorTitles.halo2ManualColdRecoveryPending =
                    Halo2ColdObservation_Passed(generation) ? 0 : generation;
                Halo2Stereo_RequestRecovery(generation);
                Halo2AnniversaryStereo_RequestRecovery(generation);
                Halo2Observer6Dof_RequestRecovery(generation);
                VR_ResetHalo2CaptureSource("manual VR recovery"); // F1: let Retry VR clear a bad source lock
            },
            []
            {
                return !g_supervisorTitles.halo2ManualColdRecoveryPending && !Halo2Observer6Dof_RecoveryPending() &&
                       !Halo2Stereo_RecoveryPending() && !Halo2AnniversaryStereo_RecoveryPending() &&
                       Halo2Observer6Dof_Armed() && (Halo2Stereo_Armed() || Halo2AnniversaryStereo_Armed());
            },
            [] { g_supervisorTitles.halo2ManualColdRecoveryPending = 0; },
            // The gate reads only two bounded code anchors and the official game-time singleton until a coherent
            // active update opens the gate. The observer itself performs the full scan only after that proof.
            [](uintptr_t base, size_t size, uint32_t generation, uint64_t)
            {
                if (g_supervisorTitles.halo2ManualColdRecoveryPending == generation && !Halo2Observer6Dof_Installed() &&
                    !Halo2Stereo_Installed() && !Halo2AnniversaryStereo_Installed())
                {
                    (void)Halo2ColdObservation_RetryFailed(base, generation);
                    g_supervisorTitles.halo2ManualColdRecoveryPending = 0;
                }
                const bool running = Halo2ColdObservation_Poll(base, size, generation);
                g_supervisorTitles.halo2Gate = {base, size, true};
                return running;
            },
            // Halo 2 splits into Classic (5) and Anniversary (4) on the live render-tree gate.
            []
            {
                return Halo2ColdObservation_ClassicRenderTreeRuns(TitleAdapter_GetGeneration(GameTitle::Halo2)) ? 5 : 4;
            }},
        {GameTitle::HaloCE,
            [](uint32_t generation)
            {
                HaloCE_RequestRecovery(generation);
                HaloCE_Recenter();
            },
            [] { return HaloCE_Armed(); }, nullptr, [](uintptr_t base, size_t, uint32_t generation, uint64_t now)
            { return TitleReentryProbe_CeLevelAllowsInstall(base, generation, now); }, [] { return 6; }},
    };
    const TitleRuntimeOps* FindTitleRuntimeOps(GameTitle title)
    {
        for (const auto& ops : kTitleRuntimeOps)
            if (ops.title == title)
                return &ops;
        return nullptr;
    }

} // namespace
namespace
{

    // The weapon/hand/HUD tunables follow the ACTIVE title's own profile; Halo 2 splits into
    // Anniversary and Classic on the live gate byte. No active supported title = the shared defaults.
    // Config_ApplyTitleProfile no-ops when the profile is unchanged, so this is one atomic compare per tick.
    // The held gun's profile is applied on the render thread (VR_UpdateWeaponAlignment,
    // before each frame), so the scope glass never waits for this ~50 ms tick.
    void ApplyActiveTitleTunables()
    {
        {
            const GameTitle tunableTitle = TitleAdapter_GetActiveTitle();
            const TitleRuntimeOps* tunableOps = FindTitleRuntimeOps(tunableTitle);
            const int tunableProfile = tunableOps && tunableOps->tunableProfile ? tunableOps->tunableProfile() : -1;
            Config_ApplyTitleProfile(tunableProfile);
        }
    }

} // namespace
namespace
{

    DWORD WINAPI TitleHookSupervisorThread(LPVOID)
    {
        // The XInput hook is wanted as soon as MCC loads an xinput DLL (so the
        // Sense controllers drive the frontend menus too), the game hooks only
        // once halo3.dll appears (entering a level). MCC loads xinput DLLs
        // lazily and can add MORE of them later (it hooked only xinput1_3 in
        // one session and read the pad through xinput1_4), so this thread
        // keeps polling forever instead of stopping at the first success.
        bool& gameHooked = g_supervisorTitles.gameHooked;
        bool& hookRefreshPending = g_supervisorTitles.hookRefreshPending;
        uint64_t nextInputRefreshMs = 0;
        bool& odstHooked = g_supervisorTitles.odstHooked;
        wchar_t recoveryEventName[96]{};
        ManualVrRecoveryEventName(recoveryEventName, GetCurrentProcessId());
        HANDLE recoveryEvent = CreateEventW(nullptr, FALSE, FALSE, recoveryEventName);
        if (!recoveryEvent)
            LOG("Manual VR recovery: launcher event unavailable (%lu); F1 remains available", GetLastError());
        uint64_t lastManualRecoveryMs = 0;
        // Automated tests only: with MJOLNIRVR_QA_RIG_EVENTS=1 in MCC's environment, the test rig
        // toggles titles_uncap_framerate (live, not saved) by signalling
        // Local\HaloMCCVR.ToggleUncap.<pid>, as the F1 checkbox does. No event otherwise.
        HANDLE uncapToggleEvent = nullptr;
        {
            char value[4]{};
            const DWORD length = GetEnvironmentVariableA("MJOLNIRVR_QA_RIG_EVENTS", value, sizeof value);
            if (length == 1 && value[0] == '1')
            {
                wchar_t name[96]{};
                swprintf_s(name, L"Local\\HaloMCCVR.ToggleUncap.%lu", GetCurrentProcessId());
                uncapToggleEvent = CreateEventW(nullptr, FALSE, FALSE, name);
                LOG("QA rig events: %ls %s", name, uncapToggleEvent ? "ready" : "unavailable");
            }
        }
        uint64_t nextHaloCleanupMs = 0;
        uint64_t nextAnatomicalReportMs = 0;
        LOG("Optional left-hand alignment: title-authored palm frames preserve solved grips and guns; "
            "H3 Dervish and Reach Elite missing right palms use their own bilateral local-Y landmarks; "
            "unknown H3/ODST/Reach rigs retain stock presentation");
        for (;;)
        {
            const uint64_t pollNow = GetTickCount64();
            NativeFaultProbe_Poll();
            {
                // Same wording as Halo 2's own line, every other title, every 5 s.
                static uint64_t lastSnapReportMs = 0;
                const uint64_t snapNow = GetTickCount64();
                if (snapNow - lastSnapReportMs >= 5000)
                {
                    lastSnapReportMs = snapNow;
                    static const char* const kSnapNames[8]{
                        "Shell", "Halo 3", "Halo 3 ODST", "Halo Reach", "Halo 4", "Halo CE", "Halo 2", "Unknown"};
                    for (unsigned t = 1; t < 6; ++t)
                        if (const unsigned long long n = g_snapTurnsByTitle[t].exchange(0, std::memory_order_relaxed))
                            LOG("%s snap turn: mode=%s accepted=%llu", kSnapNames[t],
                                g_config.turn_smooth ? "smooth" : "snap", n);
                }
            }
            Roomscale_Report();
            const GameTitle bloomTitle = TitleAdapter_GetActiveTitle();
            Bloom_ColdTick(bloomTitle, TitleAdapter_GetGeneration(bloomTitle));
            if (pollNow >= nextAnatomicalReportMs)
            {
                nextAnatomicalReportMs = pollNow + 2000;
                for (int title = 0; title < 2; ++title)
                {
                    const auto done = g_legacyAnatomicalApplications[title].exchange(0);
                    const auto rejected = g_legacyAnatomicalRefusals[title].exchange(0);
                    if (done || rejected)
                        LOG("%s anatomical handedness: applied=%llu stock-presentation fallback=%llu; camera ownership unchanged",
                            title == 0 ? "Halo 3" : "ODST", static_cast<unsigned long long>(done),
                            static_cast<unsigned long long>(rejected));
                }
                const auto applied = g_reachAnatomicalApplications.exchange(0);
                const auto refused = g_reachAnatomicalRefusals.exchange(0);
                if (applied || refused)
                    LOG("Reach anatomical handedness: applied=%llu stock-presentation fallback=%llu; camera ownership unchanged",
                        static_cast<unsigned long long>(applied), static_cast<unsigned long long>(refused));
            }
            const TitleDescriptor* activeTitle = TitleAdapter_PollLoaded(pollNow);
            if (uncapToggleEvent && WaitForSingleObject(uncapToggleEvent, 0) == WAIT_OBJECT_0)
            {
                g_config.titles_uncap_framerate = !g_config.titles_uncap_framerate;
                LOG("QA rig event: titles_uncap_framerate is now %d (live, not saved)",
                    g_config.titles_uncap_framerate ? 1 : 0);
            }
            if (recoveryEvent && WaitForSingleObject(recoveryEvent, 0) == WAIT_OBJECT_0)
                g_manualVrRecoveryRequested.store(true, std::memory_order_release);
            bool& manualHaloRecovery = g_supervisorTitles.manualHaloRecovery;
            manualHaloRecovery = false; // per tick; a recovery request below may set it
            bool manualRequestAccepted = false;
            if (g_manualVrRecoveryRequested.exchange(false, std::memory_order_acq_rel))
            {
                if (lastManualRecoveryMs && pollNow - lastManualRecoveryMs < 2000)
                    LOG("Manual VR recovery: repeated request coalesced");
                else if (!activeTitle || !ManualVrRecoverySupported(activeTitle->title) ||
                         TitleRegistry_HookPlan(activeTitle->title) == TitleHookPlan::None ||
                         !TitleAdapter_GetGeneration(activeTitle->title))
                {
                    g_manualVrRecoveryStatus.store(
                        "Enter a game and let its level finish loading, then retry VR recovery.");
                    LOG("Manual VR recovery: no supported active title with a current generation; no hooks changed");
                }
                else if (g_vrRuntimeFailureLatched.load(std::memory_order_acquire))
                {
                    g_manualVrRecoveryStatus.store("OpenXR session failed. Restart MCC through the VR launcher.");
                    LOG("Manual VR recovery: runtime failure requires a new OpenXR session");
                }
                else
                {
                    lastManualRecoveryMs = pollNow;
                    manualRequestAccepted = true;
                    const GameTitle recoveryTitle = activeTitle->title;
                    const uint32_t recoveryGeneration = TitleAdapter_GetGeneration(recoveryTitle);
                    g_manualVrRecoveryComplete.store(false, std::memory_order_release);
                    g_manualVrRecoveryToken.store(
                        ManualVrRecoveryToken(recoveryTitle, recoveryGeneration), std::memory_order_release);
                    g_autoVrUserVeto.store(false, std::memory_order_release);
                    g_manualVrRecoveryStatus.store(
                        "Recovering VR: waiting for safe cleanup, a ready level and fresh camera tracking.");
                    if (const TitleRuntimeOps* ops = FindTitleRuntimeOps(recoveryTitle); ops && ops->recoveryRequest)
                        ops->recoveryRequest(recoveryGeneration);
                    LOG("Manual VR recovery: %s generation %u retry requested; normal binding, load and tracking proofs retained",
                        activeTitle->displayName, recoveryGeneration);
                }
            }
            const TitleAdapterRuntimeSnapshot runtime = RuntimeSnapshot(pollNow);
            // Draw-distance trim for whatever title currently owns the frame.
            // Game_ApplyDrawDistance caches by module, so this is a cheap
            // per-tick reassert of the configured scaled render far-clip (survives
            // level/tag loads), not a per-tick whole-module scan.
            // Sample the ACTIVE title's level-load gate exactly once per tick,
            // before anything else may touch its module. Every other consumer
            // this tick reads IsOpen()/g_activeTitleLevelRunning instead, so no
            // path double-consumes a sample. See the gate declaration for why
            // the invariant is "touch nothing", not merely "install nothing".
            const uint64_t requestedToken = g_manualVrRecoveryToken.load(std::memory_order_acquire);
            const GameTitle requestedTitle = ManualVrRecoveryTitle(requestedToken);
            const uint32_t requestedGeneration = ManualVrRecoveryGeneration(requestedToken);
            const TitleRuntimeOps* requestedOps = FindTitleRuntimeOps(requestedTitle);
            const bool requestedCameraReady =
                requestedOps && requestedOps->recoveryCameraReady && requestedOps->recoveryCameraReady();
            if (requestedToken && (requestedGeneration != TitleAdapter_GetGeneration(requestedTitle) ||
                                      (activeTitle && activeTitle->title != requestedTitle) ||
                                      (!activeTitle && !runtime.ownershipPending)))
            {
                g_manualVrRecoveryToken.store(0, std::memory_order_release);
                g_manualVrRecoveryComplete.store(false, std::memory_order_release);
                for (const auto& ops : kTitleRuntimeOps)
                    if (ops.recoveryCancel)
                        ops.recoveryCancel();
                g_manualVrRecoveryStatus.store(
                    "The active game changed. VR recovery is available again in the next level.");
                LOG("Manual VR recovery: title %u generation %u retired; request cancelled",
                    static_cast<unsigned>(requestedTitle), requestedGeneration);
            }
            else if (requestedToken && !manualRequestAccepted && requestedCameraReady &&
                     !g_manualVrRecoveryComplete.load(std::memory_order_acquire) &&
                     !g_vrRuntimeFailureLatched.load(std::memory_order_acquire) &&
                     !g_autoVrUserVeto.load(std::memory_order_acquire) && runtime.runtime.owner == requestedTitle &&
                     runtime.runtime.generation == requestedGeneration && runtime.runtime.installed &&
                     runtime.runtime.armed && !runtime.runtime.teardownRequested &&
                     runtime.runtime.qualifyingOwnerCount == 1 && !runtime.ownershipPending &&
                     g_enabled.load(std::memory_order_acquire) && VR_IsStereoEnabled() && !VR_IsPausePresentation() &&
                     !VR_IsPausePresentationTarget())
            {
                // Keep the epoch-scoped manual override after completion so a
                // saved auto_vr=false setting does not immediately undo it.
                g_manualVrRecoveryComplete.store(true, std::memory_order_release);
                g_manualVrRecoveryStatus.store("VR recovery complete: the active game has a live stereo camera.");
                LOG("Manual VR recovery: completed for title %u generation %u", static_cast<unsigned>(requestedTitle),
                    requestedGeneration);
            }
            // Cleanup is independent of the next title's load gate. In
            // particular an unloaded old module must not pin an optional
            // feature's targets until Halo 3 happens to be selected again.
            if (hookRefreshPending && pollNow >= nextHaloCleanupMs)
            {
                hookRefreshPending = !RemoveHalo3CameraHooks();
                nextHaloCleanupMs = GetTickCount64() + 500;
                if (!hookRefreshPending)
                    LOG("Halo 3 pending hook epoch cleanup completed");
            }
            const bool halo2Active = activeTitle && activeTitle->title == GameTitle::Halo2;
            // H2 is the first title whose initial bring-up stage is explicitly
            // no-write. A missing module range or disabled cold-observation
            // build must therefore publish HOLD, never inherit the historical
            // fail-open true used by titles with established runtime paths.
            bool activeLevelRunning = !halo2Active && !(activeTitle && activeTitle->title == GameTitle::HaloCE);
            if (!activeTitle || activeTitle->title != GameTitle::HaloCE)
                (void)TitleReentryProbe_CeLevelAllowsInstall(0, 0, pollNow);
            // Per-tick gate samples (see TitleHookSupervisorState::GateSample); the title rows fill them in.
            g_supervisorTitles.halo4Gate = {};
            g_supervisorTitles.halo2Gate = {};
            if (activeTitle)
            {
                uintptr_t gateBase = 0;
                size_t gateSize = 0;
                if (sig::ModuleRange(activeTitle->moduleName, gateBase, gateSize))
                {
                    const uint32_t gateGeneration = TitleAdapter_GetGeneration(activeTitle->title);
                    // Titles without a gate keep the fail-open default above.
                    if (const TitleRuntimeOps* ops = FindTitleRuntimeOps(activeTitle->title);
                        ops && ops->sampleLevelGate)
                        activeLevelRunning = ops->sampleLevelGate(gateBase, gateSize, gateGeneration, pollNow);
                    // The draw-distance reassert resolves a debug var by a
                    // whole-module name scan and WRITES it. Both are module
                    // touches, so they wait for the level exactly like hooks.
                    if (TitleRegistry_AllowsGenericDrawDistance(activeTitle->title, activeLevelRunning))
                        Game_ApplyDrawDistance(gateBase, gateSize, gateGeneration);
                    // Halo 3 and ODST frame cap (Reach's binding, their sites),
                    // under the same level gate.
                    if (activeTitle->title == GameTitle::Halo3)
                        ReachFrameCapTick(gateBase, gateSize, gateGeneration, activeLevelRunning, g_halo3FrameCap,
                            ResolveHalo3FrameCap, g_config.titles_uncap_framerate);
                    else if (activeTitle->title == GameTitle::Halo3ODST)
                        ReachFrameCapTick(gateBase, gateSize, gateGeneration, activeLevelRunning, g_odstFrameCap,
                            ResolveOdstFrameCap, g_config.titles_uncap_framerate);
                }
            }
            RefreshGestureMeleeBinding(activeTitle, activeLevelRunning, pollNow);
            if (const unsigned notice = g_bodyNotice.exchange(0, std::memory_order_acq_rel))
                LOG("Legacy H3 body switch: %s; independent body/hand visibility remains WIP",
                    notice == 1   ? "enabled"
                    : notice == 2 ? "original boolean values restored"
                                  : "guarded access failed; optional switch disabled, VR retained");
            VR_ReportWeaponInteractions(pollNow);
            NativeReloadPolicy_Poll();
            NativeSubtitles_Poll(activeLevelRunning);
            NativeVehicleFirstPerson_Poll();
            for (uint64_t rig = 0; g_unmappedFpRigs.TakeNew(rig);)
                LOG("first-person rig not mapped: %s nodes=%d id=0x%08X -- arms stay stock; "
                    "send this line so the rig can be pinned",
                    fp_rig_coverage::RigTitle(rig) == GameTitle::Halo4 ? "Halo 4" : "Reach",
                    fp_rig_coverage::RigNodeCount(rig), fp_rig_coverage::RigId(rig));
            const TitleTickContext tick{activeTitle, activeLevelRunning, pollNow};
            TickHaloCE(tick);
            TickHalo2Observation(tick);
            ApplyActiveTitleTunables();
            TickHalo2Stereo(tick);
            g_activeTitleLevelRunning.store(activeLevelRunning, std::memory_order_release);
            TickReach(tick);
            LegacyWorldCollisionLogTick();
            TickHalo4(tick);
            // Snapshot resolution is deliberately side-effect free so render
            // and input callers cannot log. The worker owns fallback-mode
            // publication when no title render path is publishing a mode.
            TitleAdapter_SetRuntimeMode(runtime.runtime.mode);
            if (pollNow >= nextInputRefreshMs)
            {
                Input_InstallXInputHook();
                Input_ClaimXInputIat();    // re-assert if Steam replaces MCC's import slot
                HookInstallCheck_Report(); // "detour unwind check: <module> N ok, M missing"
                Input_KeepMccInputActive(pollNow);
                Input_PadFirstMenus();
                nextInputRefreshMs = pollNow + 2000;
            }
            const TitleHookPlan hookPlan = TitleRegistry_HookPlan(activeTitle ? activeTitle->title : GameTitle::None);
            const TitleTickContext installTick{activeTitle, activeLevelRunning, pollNow, &runtime, hookPlan};
            const bool haloAvailableForInstall = TickHalo3Lifecycle(installTick);
            const bool odstAvailableForInstall = TickOdstLifecycle(installTick);

            if (TryInstallHalo3(installTick, haloAvailableForInstall) == InstallStep::RestartTick)
            {
                Sleep(25);
                continue;
            }

            TryInstallOdst(installTick, odstAvailableForInstall);
            g_hooked.store(gameHooked || odstHooked, std::memory_order_release);
            LogOdstNonFpCameraIfNew(); // emit any non-FP (death/vehicle) cam
            LogOdstRenderSkipIfNew();  // emit why a frame stayed flat 2D
            ReportOdstScope();
            LogOdstFpLayoutSelfCheckIfNew(); // emit FP weapon-layout self-check
            LogOdstNativeHudRouteOnce();     // bounded in-place CHUD route result
            LogHalo3VehicleProbeIfNew();     // live H3 feature status/fallback
            support_diagnostics::Drain(
                [](const char* message, uint32_t count) { LOG("%s (events since last report: %u)", message, count); });
            VR_FramePacingWorkerPoll();
            Sleep(50);
        }
    }
}

// ---- T-2 sparse shot events: cross-translation-unit entry point ----
// The shared shot publisher lives in this file's first internal-linkage
// region, but Halo 2's firing detour runs in the observer translation unit
// (halo2_observer_6dof.cpp). This door owns no state and no policy: the
// disabled gate, the non-finite ray suppression, the frame/flag snapshot reads
// and the title-qualified engine aim branch all stay in the one implementation
// above, so no title can drift into a private copy of the shot contract.
void Game_PublishShotDiagnostic(GameTitle title, uint32_t titleGeneration, uint32_t unit, uint32_t weapon, uint8_t slot,
    uint8_t barrel, bool predicted, bool substituted, bool firesFromCamera, bool unitAim, const float origin[3],
    const float direction[3]) noexcept
{
    PublishShotDiagnostic(title, titleGeneration, unit, weapon, slot, barrel, predicted, substituted, firesFromCamera,
        unitAim, origin, direction);
}

void Game_RequestManualVrRecovery()
{
    g_manualVrRecoveryStatus.store("VR recovery requested.", std::memory_order_release);
    g_manualVrRecoveryRequested.store(true, std::memory_order_release);
}

const char* Game_ManualVrRecoveryStatus()
{
    return g_manualVrRecoveryStatus.load(std::memory_order_acquire);
}

void Game_ReadFramePerfCounters(GameFramePerfCounters& out)
{
    out = {};
    out.viewRenders = g_perfViewRenders.load(std::memory_order_relaxed);
    for (int eye = 0; eye < 3; ++eye)
    {
        out.fpPaletteRequests[eye] = g_perfFpPaletteRequests[eye].load(std::memory_order_relaxed);
        out.fpPaletteFullSolves[eye] = g_perfFpPaletteFullSolves[eye].load(std::memory_order_relaxed);
        out.fpPaletteCacheHits[eye] = g_perfFpPaletteCacheHits[eye].load(std::memory_order_relaxed);
        out.fpPaletteCacheStores[eye] = g_perfFpPaletteCacheStores[eye].load(std::memory_order_relaxed);
        out.fpPaletteCacheFull[eye] = g_perfFpPaletteCacheFull[eye].load(std::memory_order_relaxed);
    }
    out.zoomLogWrites = g_perfZoomLogWrites.load(std::memory_order_relaxed);
    out.viewRateLogWrites = g_perfViewRateLogWrites.load(std::memory_order_relaxed);
    out.paletteRateLogWrites = g_perfPaletteRateLogWrites.load(std::memory_order_relaxed);
    out.cameraRateLogWrites = g_perfCameraRateLogWrites.load(std::memory_order_relaxed);
    out.fpDriverRateLogWrites = g_perfFpDriverRateLogWrites.load(std::memory_order_relaxed);
}

void Game_Init()
{
    // Establish the atomic title policy before globally shared input detours can
    // receive their first call. The worker refreshes it throughout transitions.
    TitleAdapter_PollLoaded(GetTickCount64());
    // Claim MCC's controller path synchronously, before OpenXR startup blocks
    // on SteamVR. The worker keeps re-asserting it if Steam replaces the IAT.
    Input_InstallXInputHook();
    Input_ClaimXInputIat();
    CreateThread(nullptr, 0, TitleHookSupervisorThread, nullptr, 0, nullptr);
}

bool Game_IsHooked()
{
    return g_hooked;
}
bool Game_IsHeadTracking()
{
    return g_enabled.load();
}
bool Game_IsStereoGeometryOnlyBringup()
{
    return false;
}
bool Game_UsesTitleOwnedHeadTracking()
{
    if (TitleAdapter_GetActiveTitle() == GameTitle::HaloCE)
        return HaloCE_Armed();
    return TitleAdapter_GetActiveTitle() == GameTitle::Halo2 && (Halo2Stereo_Armed() || Halo2AnniversaryStereo_Armed());
}
bool Game_IsHeadTrackingApplied()
{
    return g_enabled.load(std::memory_order_acquire) &&
           (Game_UsesTitleOwnedHeadTracking() || !Game_IsStereoGeometryOnlyBringup());
}
uint64_t Game_GetReserveWeaponIdentity(GameTitle title, uint32_t generation)
{
    const int index = ReserveWeaponTitleIndex(title);
    if (index < 0 || !generation)
        return 0;
    const auto& state = g_reserveWeapon[index];
    if (state.publishedGeneration.load(std::memory_order_acquire) != generation)
        return 0;
    const uint64_t identity = state.publishedIdentity.load(std::memory_order_relaxed);
    return state.publishedGeneration.load(std::memory_order_acquire) == generation ? identity : 0;
}

bool Game_GetReserveWeaponDiag(GameTitle title, Game_ReserveWeaponDiag& out)
{
    out = {};
    const int index = ReserveWeaponTitleIndex(title);
    if (index < 0)
        return false;
    const auto& state = g_reserveWeapon[index];
    out.publishedGeneration = state.publishedGeneration.load(std::memory_order_acquire);
    out.calls = state.diagCalls.load(std::memory_order_relaxed);
    out.read = state.diagRead.load(std::memory_order_relaxed);
    out.primary = state.diagPrimary.load(std::memory_order_relaxed);
    out.reserve = state.diagReserve.load(std::memory_order_relaxed);
    out.previous = state.diagPrevious.load(std::memory_order_relaxed);
    out.held = state.diagHeld.load(std::memory_order_relaxed);
    out.candidateHandle = state.diagCandidateHandle.load(std::memory_order_relaxed);
    out.candidateIdentity = state.diagCandidateIdentity.load(std::memory_order_relaxed);
    out.candidateSinceMs = state.diagCandidateSince.load(std::memory_order_relaxed);
    out.publishedIdentity = state.publishedIdentity.load(std::memory_order_relaxed);
    return true;
}

void Game_ObserveHeldWeapons(GameTitle title, uint32_t generation, uint32_t held0, uint32_t held1,
    uint64_t heldIdentity, Game_ReserveOwnedFn owned, void* context) noexcept
{
    const int index = ReserveWeaponTitleIndex(title);
    if ((title != GameTitle::Halo2 && title != GameTitle::HaloCE) || index < 0 || !generation || !owned)
        return;
    auto& state = g_reserveWeapon[index];
    if (!ReserveWeaponEnter(state, generation))
        return;
    state.previous.Observe(held0);
    const uint32_t reserve =
        state.previous.Reserve(held0, held1, [owned, context](uint32_t handle) { return owned(handle, context); });
    ReserveWeaponObserveLocked(state, generation, held0 != reserve_weapon::kNoHandle, held0, reserve, heldIdentity);
    state.busy.clear(std::memory_order_release);
}

bool Game_HasScopeRenderer()
{
    const auto title = TitleAdapter_GetActiveTitle();
    // CE's zoom view is a crop of its own eye pair (vr.cpp CeVisorCrop), shown
    // in the visor window only; no native render is involved.
    if (title == GameTitle::HaloCE)
        return HaloCE_Armed() && ((g_config.ce_visor_zoom && g_config.visor_zoom_mode != 0) ||
                                     (g_config.ce_classic_scope && HaloCE_ClassicScopeReady()) ||
                                     (g_config.ce_anniv_monitor_frames && HaloCE_AnniversaryActive() &&
                                         VR_CeAnnivGlassGun())); // gun camera on the glass only
    if (title == GameTitle::Halo3)
        return !Game_IsCameraOnlyBringup() &&
               g_halo3ScopeFaultGeneration.load() !=
                   g_halo3RuntimeGeneration.load(); // no scope fault in this runtime generation
    if (title == GameTitle::Halo2)
        return Halo2Observer6Dof_FinalPaletteArmed() &&
               (Halo2Stereo_ScopeAvailable() || Halo2AnniversaryStereo_ScopeAvailable());
    if (title == GameTitle::Halo3ODST)
        return g_odstCamera.armed.load() && !g_odstCamera.teardownRequested.load() &&
               g_odstRuntimeGeneration.load() != 0 && OdstScopeFaultGeneration() != g_odstRuntimeGeneration.load();
    if (title == GameTitle::HaloReach)
        return g_reachCamera.armed.load() && !g_reachCamera.teardownRequested.load() &&
               g_reachCamera.generation.load() != 0 &&
               g_reachScopeFaultGeneration.load() != g_reachCamera.generation.load() &&
               g_reachFirstPersonRenderGateReturn.load() != 0 && g_reachCamera.hudDrawWidgetTarget &&
               g_reachCamera.decoratorWindState && !g_reachWindReplayFaults.load();
    if (title == GameTitle::Halo4)
        return g_halo4Camera.armed.load() && !g_halo4Camera.teardownRequested.load() &&
               g_halo4Camera.generation.load() != 0 &&
               g_halo4ScopeFaultGeneration.load() != g_halo4Camera.generation.load() && g_halo4OrigCuiGameplayRender &&
               g_halo4OrigModelSkinning;
    return false;
}
bool Game_IsScopeRendering()
{
    return g_scopeRenderActive.load(std::memory_order_acquire);
}
bool Game_SetScopeRendering(bool active)
{
    return g_scopeRenderActive.exchange(active, std::memory_order_acq_rel);
}
bool Game_IsCameraOnlyBringup()
{
    return OdstCameraOnlyContext();
}
// Whether the ACTIVE title actually has its authored-crosshair capture hooks
// installed. This is a live fact, not an assumption: if the capture is not
// installed the procedural reticle is the only crosshair and must stay
// visible. Once a valid captured widget is held, the procedural one stays
// invisible so it cannot erase the art. Halo 4 deliberately keeps procedural
// pixels only during its measured first-capture bootstrap window.
//
// ODST reaches the capture through the shared Halo 3 path -
// OdstHudDrawWidgetHook calls HudDrawWidgetHook and OdstHudCrosshairVisibleHook
// calls HudCrosshairVisibleHook - so once InstallOdstCrosshairHider succeeds it
// captures exactly like Halo 3 does. It was previously excluded by
// Game_IsCameraOnlyBringup, which is why it painted the procedural reticle over
// art it had already captured.
bool Game_TitleCapturesAuthoredCrosshair()
{
    const GameTitle activeTitle = TitleAdapter_GetActiveTitle();
    if (activeTitle == GameTitle::HaloCE)
        return HaloCEHud_CapturedCrosshair();
    // Do not revoke the generic marker merely because the shader hooks exist.
    // Ownership changes only after a real native Halo 2 crosshair draw has
    // completed a capture in this exact module generation.
    if (activeTitle == GameTitle::Halo2)
        return D3D_Halo2NativeCrosshairCaptured();
    if (activeTitle == GameTitle::Halo4)
        return Halo4CuiReticleNativeSuppressionLive();
    if (activeTitle == GameTitle::HaloReach)
        return g_reachOrigHudDrawWidget != nullptr;
    if (activeTitle == GameTitle::Halo3ODST)
    {
        return g_odstCamera.installed.load(std::memory_order_acquire) && g_odstCamera.crosshairClassGatePatched &&
               g_realHudCrosshairVisible != nullptr && g_realHudDrawWidget != nullptr;
    }
    return activeTitle == GameTitle::Halo3 && g_realHudCrosshairVisible != nullptr && g_realHudDrawWidget != nullptr;
}
bool Game_TitlePositionsNativeCrosshair()
{
    // Halo 4 now follows Halo 3/ODST/Reach: native pixels are captured and
    // presented by the shared OpenXR reticle quad, never positioned in CUI.
    if (TitleAdapter_GetActiveTitle() == GameTitle::Halo4)
        return false;
    return false;
}

uint64_t Game_GetAuthoredCrosshairKey()
{
    if (TitleAdapter_GetActiveTitle() == GameTitle::HaloCE)
        return HaloCEHud_CrosshairKey();
    if (TitleAdapter_GetActiveTitle() == GameTitle::Halo2 && D3D_Halo2NativeCrosshairCaptured())
    {
        // The native art is texture/state driven behind one proven shader.
        // A stable nonzero identity selects the bounded-animation refresh path
        // in VR, which republishes weapon swaps and colour/kick animation.
        return 0x0a9b60d8f40268f6ULL;
    }
    return g_authoredCrosshairKey.load(std::memory_order_acquire);
}
uint32_t Game_GetAuthoredCrosshairDrawCount()
{
    return g_authoredCrosshairDraws.load(std::memory_order_acquire);
}
uint32_t Game_GetAuthoredCrosshairColorState()
{
    return g_authoredCrosshairColorState.load(std::memory_order_acquire);
}

void Game_ResetAuthoredCrosshairKey()
{
    g_authoredCrosshairKeyAccum = 0;
    g_authoredCrosshairKey.store(0, std::memory_order_release);
    g_authoredCrosshairDraws.store(0, std::memory_order_release);
    g_authoredCrosshairColorStateAccum = 0;
    g_authoredCrosshairColorState.store(0, std::memory_order_release);
}

bool Game_VrOwnsLookStick()
{
    if (TitleAdapter_GetActiveTitle() == GameTitle::HaloCE)
        return Game_CeControllerFeaturesRequested() && HaloCEControls_OwnsLookStick();
    if (OdstVrOwnsLookStick(OdstCameraOnlyContext(), g_enabled.load(std::memory_order_acquire)))
    {
        return true;
    }
    // Reach has no separate simulation-camera integration yet. Once its proven
    // stereo camera is armed, ApplyVrTurn owns yaw and the HMD owns pitch; raw
    // stock RX/RY would otherwise create a second, conflicting look transform.
    return TitleAdapter_GetActiveTitle() == GameTitle::HaloReach &&
           g_reachCamera.armed.load(std::memory_order_acquire) && g_enabled.load(std::memory_order_acquire) &&
           g_vrAim.load(std::memory_order_acquire) && VR_IsStereoEnabled();
}

bool Game_PhysicalMeleePulseActive(uint64_t)
{
    return false;
}

bool Game_ContactMeleeAvailable(GameTitle title)
{
    bool ready = false;
    switch (title)
    {
    case GameTitle::Halo2:
        ready = Halo2Observer6Dof_ContactMeleeReady();
        break;
    case GameTitle::Halo3:
        ready = Halo3ContactMeleeReady();
        break;
    case GameTitle::Halo3ODST:
        ready = OdstContactMeleeReady();
        break;
    case GameTitle::HaloReach:
        ready = ReachContactMeleeReady();
        break;
    case GameTitle::Halo4:
        ready = Halo4ContactMeleeReady();
        break;
    case GameTitle::HaloCE:
        ready = HaloCEContact_MeleeReady();
        break;
    default:
        break;
    }
    static std::atomic<uint64_t> loggedFor{0};
    if (g_config.physical_melee && !ready && title != GameTitle::None &&
        TitleAdapter_GetRuntimeMode() == RuntimeMode::Gameplay)
    {
        const uint64_t key = (uint64_t(TitleAdapter_GetGeneration(title)) << 8) | uint64_t(title);
        if (loggedFor.exchange(key, std::memory_order_acq_rel) != key)
            LOG("melee: swing melee is on but unavailable in this title (contact melee not installed); "
                "no button fallback - bind Melee in F1 > Mappings");
    }
    return ready;
}

uint32_t Game_GestureMeleeInput(uint64_t nowMs)
{
    (void)Game_ContactMeleeAvailable(TitleAdapter_GetActiveTitle()); // one-time unavailable log
    const GameTitle title = TitleAdapter_GetActiveTitle();
    if (title != GameTitle::Halo2 && title != GameTitle::Halo3 && title != GameTitle::Halo3ODST &&
        title != GameTitle::HaloReach && title != GameTitle::Halo4)
        return 0;
    // XInput's VR transport owns slot zero. Binding snapshots are read by the
    // cold worker and expire across title/generation changes and load gates.
    const uint32_t generation = TitleAdapter_GetGeneration(title);
    const uint32_t transport = ReadGestureMeleeTransport(title, generation, 0, nowMs);
    struct SharedPhysicalMeleeState
    {
        std::atomic<uint8_t> title{static_cast<uint8_t>(GameTitle::None)};
        std::atomic<uint32_t> generation{0};
        std::atomic<uint32_t> transport{0};
        std::atomic<bool> available{false};
        std::atomic<uint64_t> pulseUntilMs{0};
        std::atomic<uint64_t> cooldownUntilMs{0};
        std::atomic<bool> velocityLatched[2]{};
    };
    static SharedPhysicalMeleeState state;
    const uint8_t titleValue = static_cast<uint8_t>(title);
    const uint8_t previousTitle = state.title.exchange(titleValue, std::memory_order_acq_rel);
    const uint32_t previousGeneration = state.generation.exchange(generation, std::memory_order_acq_rel);
    const uint32_t previousTransport = state.transport.exchange(transport, std::memory_order_acq_rel);
    const bool changed =
        previousTitle != titleValue || previousGeneration != generation || previousTransport != transport;
    if (changed)
    {
        state.pulseUntilMs.store(0, std::memory_order_release);
        state.cooldownUntilMs.store(0, std::memory_order_release);
        state.velocityLatched[0].store(false, std::memory_order_release);
        state.velocityLatched[1].store(false, std::memory_order_release);
    }

    auto& telemetry = *SharedMeleeForTitle(title);
    telemetry.polls.fetch_add(1, std::memory_order_relaxed);
    // With both modes selected, contact owns melee. The gesture must never
    // add a second native target/animation to the same physical strike.
    if (exclusive_input::Active() || !g_config.gesture_melee || g_config.physical_melee || !transport ||
        TitleAdapter_GetRuntimeMode() != RuntimeMode::Gameplay || !g_enabled.load(std::memory_order_acquire) ||
        !VR_IsStereoEnabled())
    {
        state.available.store(false, std::memory_order_release);
        state.pulseUntilMs.store(0, std::memory_order_release);
        state.cooldownUntilMs.store(0, std::memory_order_release);
        state.velocityLatched[0].store(false, std::memory_order_release);
        state.velocityLatched[1].store(false, std::memory_order_release);
        telemetry.unavailable.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    bool titleArmed = false;
    switch (title)
    {
    case GameTitle::Halo2:
        titleArmed = Halo2Observer6Dof_Armed() && Halo2Observer6Dof_FinalPaletteArmed();
        break;
    case GameTitle::Halo3:
        titleArmed = g_renderHooked.load(std::memory_order_acquire);
        break;
    case GameTitle::Halo3ODST:
        titleArmed = g_odstCamera.armed.load(std::memory_order_acquire);
        break;
    case GameTitle::HaloReach:
        titleArmed = g_reachCamera.armed.load(std::memory_order_acquire);
        break;
    case GameTitle::Halo4:
        titleArmed = g_halo4Camera.armed.load(std::memory_order_acquire);
        break;
    default:
        break;
    }
    if (!titleArmed)
    {
        state.available.store(false, std::memory_order_release);
        state.pulseUntilMs.store(0, std::memory_order_release);
        state.cooldownUntilMs.store(0, std::memory_order_release);
        state.velocityLatched[0].store(false, std::memory_order_release);
        state.velocityLatched[1].store(false, std::memory_order_release);
        telemetry.unavailable.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    const bool newlyAvailable = !state.available.exchange(true, std::memory_order_acq_rel);
    if (changed || newlyAvailable)
    {
        // A layout switch, new title, toggle or resumed native reader is not a
        // new swing. Wait for each hand to settle before accepting its edge.
        state.velocityLatched[0].store(true, std::memory_order_release);
        state.velocityLatched[1].store(true, std::memory_order_release);
        return 0;
    }

    const float requiredSpeed =
        std::clamp(g_config.physical_melee_swing_speed, kPhysicalMeleeSpeedMin, kPhysicalMeleeSpeedMax);
    for (int hand = 0; hand < 2; ++hand)
    {
        float speed = -1.0f;
        const bool velocityValid = VR_GetPhysicalMeleeSpeed(hand == 0, speed);
        if (!velocityValid)
            telemetry.invalidVelocity.fetch_add(1, std::memory_order_relaxed);
        if (std::isfinite(speed) && speed >= 0)
        {
            const uint32_t peak = static_cast<uint32_t>(std::min(speed, 100.0f) * 100.0f);
            uint32_t previous = telemetry.peakCmPerSecond.load(std::memory_order_relaxed);
            while (previous < peak &&
                   !telemetry.peakCmPerSecond.compare_exchange_weak(previous, peak, std::memory_order_relaxed))
            {
            }
        }
        std::atomic<bool>& latch = state.velocityLatched[hand];
        const bool wasLatched = latch.load(std::memory_order_acquire);
        const Halo4PhysicalMeleeVelocityDecision decision =
            Halo4UpdatePhysicalMeleeVelocityLatch(velocityValid, speed, requiredSpeed, wasLatched);
        if (!decision.latched)
        {
            latch.store(false, std::memory_order_release);
            continue;
        }
        if (!decision.trigger)
            continue;
        bool expected = false;
        if (!latch.compare_exchange_strong(expected, true, std::memory_order_acq_rel, std::memory_order_relaxed))
            continue;
        telemetry.crossings.fetch_add(1, std::memory_order_relaxed);
        if (nowMs >= state.cooldownUntilMs.load(std::memory_order_acquire))
        {
            telemetry.pulses.fetch_add(1, std::memory_order_relaxed);
            state.pulseUntilMs.store(nowMs + kHalo4PhysicalMeleePulseMs, std::memory_order_release);
            state.cooldownUntilMs.store(nowMs + Halo4PhysicalMeleeCooldownMs(), std::memory_order_release);
        }
        else
            telemetry.cooldownBlocks.fetch_add(1, std::memory_order_relaxed);
    }
    return nowMs != 0 && nowMs < state.pulseUntilMs.load(std::memory_order_acquire) ? transport : 0;
}

bool Game_ProcessPresentationDetachRequest()
{
    if (g_odstCamera.presentationDetachInProgress.test_and_set(std::memory_order_acq_rel))
        return false;
    const uint64_t completed = g_odstCamera.presentationDetachCompleted.load(std::memory_order_acquire);
    const uint64_t requested = g_odstCamera.presentationDetachRequested.load(std::memory_order_acquire);
    if (requested == completed)
    {
        g_odstCamera.presentationDetachInProgress.clear(std::memory_order_release);
        return false;
    }
    g_odstCamera.armed.store(false, std::memory_order_release);
    PublishOdstLifecycle();
    g_enabled.store(false, std::memory_order_release);
    g_autoVrOwned.store(false, std::memory_order_release);
    g_autoVrUserVeto.store(false, std::memory_order_release);
    // Keep requested != completed throughout synchronous render-thread
    // cleanup. If another request arrives during the detach, acknowledging
    // only this snapshot leaves the newer generation owned for the next pass.
    VR_DetachGamePresentation();
    g_odstCamera.presentationDetachCompleted.store(requested, std::memory_order_release);
    g_odstCamera.presentationDetachInProgress.clear(std::memory_order_release);
    return true;
}
bool Game_AllowsSharedGameplayFeatures()
{
    const GameTitle activeTitle = TitleAdapter_GetActiveTitle();
    if (OdstCameraOnlyContext())
        return false;
    if (activeTitle == GameTitle::None || activeTitle == GameTitle::Halo3)
        return true;
    if (activeTitle != GameTitle::Unknown)
        return false;
    const TitleAdapterRuntimeSnapshot runtime = RuntimeSnapshot(GetTickCount64());
    return runtime.runtime.owner == GameTitle::Halo3 && runtime.runtime.qualifyingOwnerCount == 1 &&
           runtime.runtime.installed && !runtime.runtime.teardownRequested;
}
bool Game_AllowsSharedControllerInput()
{
    const GameTitle activeTitle = TitleAdapter_GetActiveTitle();
    const TitleDescriptor* activeDescriptor = TitleRegistry_Find(activeTitle);
    const bool explicitTitleAllowsControllerInput =
        activeDescriptor && (activeDescriptor->admissionCapabilities & TitleCapability_ControllerInput) != 0;
    const bool resolvedOwnerAllowsControllerInput =
        activeTitle == GameTitle::Unknown && Game_HasTitleCapability(TitleCapability_ControllerInput);
    const bool cameraOnlyOwned = OdstCameraOnlyContext();
    // Preserve the accepted cumulative frontend transport rule. MCC commonly
    // keeps several title DLLs resident in its shell, so raw availability is
    // Unknown and there is intentionally no camera owner/capability snapshot.
    // Ordinary virtual-pad input must remain usable there. Camera-only teardown
    // still wins unless a unique owner publishes ControllerInput, and explicit
    // unsupported titles remain fail-closed.
    const bool allowAmbiguousFrontend = activeTitle == GameTitle::Unknown;
    return TitleRegistry_AllowsSharedControllerInput(activeTitle, resolvedOwnerAllowsControllerInput, cameraOnlyOwned,
        allowAmbiguousFrontend, explicitTitleAllowsControllerInput);
}
bool Game_AllowsPauseToggleInput()
{
    bool odstCameraOnly = false;
    odstCameraOnly = OdstCameraOnlyContext();
    const bool titleSpecificPauseOwner = TitleSpecificPauseToggleOwner(TitleAdapter_GetActiveTitle(), odstCameraOnly);
    return PauseToggleInputAllowed(Game_AllowsSharedGameplayFeatures(), titleSpecificPauseOwner);
}
bool Game_HasTitleCapability(uint32_t requiredCapabilities)
{
    if (!requiredCapabilities || (requiredCapabilities & ~kTitleRuntimeKnownCapabilities))
    {
        return false;
    }
    const TitleAdapterRuntimeSnapshot runtime = RuntimeSnapshot(GetTickCount64());
    const uint32_t enabled = TitleRuntimeMaskUnarmedCapabilities(runtime.runtime, kRuntimeCapabilitiesRequiringArm);
    return (enabled & requiredCapabilities) == requiredCapabilities;
}
CinematicControlState Game_GetCinematicControlState()
{
    const uint64_t now = GetTickCount64();
    const TitleAdapterRuntimeSnapshot runtime = RuntimeSnapshot(now);
    if (runtime.ownershipPending || runtime.runtime.qualifyingOwnerCount != 1)
    {
        return CinematicControlState::Unknown;
    }
    const uint32_t enabled = TitleRuntimeMaskUnarmedCapabilities(runtime.runtime, kRuntimeCapabilitiesRequiringArm);
    const CinematicControlPublication publication = TitleAdapter_GetCinematicControlPublication(runtime.runtime.owner);
    return ResolveCinematicControl(publication, runtime.runtime.owner, runtime.runtime.generation, enabled, now);
}
bool Game_GetCutsceneTheaterPresentation(float& authoredAspect)
{
    authoredAspect = 0.0f;
    const uint64_t now = GetTickCount64();
    const TitleAdapterRuntimeSnapshot runtime = RuntimeSnapshot(now);
    if (runtime.ownershipPending || runtime.runtime.qualifyingOwnerCount != 1)
    {
        return false;
    }
    const uint32_t enabled = TitleRuntimeMaskUnarmedCapabilities(runtime.runtime, kRuntimeCapabilitiesRequiringArm);
    const CinematicControlPublication controlBefore =
        TitleAdapter_GetCinematicControlPublication(runtime.runtime.owner);
    if (!CutsceneTheaterRequested(g_config.cutscene_theater_enabled,
            ResolveCinematicControl(controlBefore, runtime.runtime.owner, runtime.runtime.generation, enabled, now)))
    {
        return false;
    }
    const CutsceneTheaterProjectionPublication projection =
        TitleAdapter_GetCutsceneTheaterProjectionPublication(runtime.runtime.owner);
    if (!ResolveCutsceneTheaterProjection(
            projection, runtime.runtime.owner, runtime.runtime.generation, enabled, now, authoredAspect))
    {
        return false;
    }
    const CinematicControlPublication controlAfter = TitleAdapter_GetCinematicControlPublication(runtime.runtime.owner);
    // cutscene_3d_sections: listed sections stay immersive (free head look).
    {
        const size_t i = static_cast<size_t>(runtime.runtime.owner);
        const int32_t scene = i < 8 ? g_cinematicSection[i].load(std::memory_order_relaxed) : -1;
        static int32_t loggedScene = INT32_MIN;
        static GameTitle loggedTitle = GameTitle::None;
        const bool stays3D = CutsceneSectionStays3D(g_config.cutscene_3d_sections, runtime.runtime.owner, scene);
        if (scene != loggedScene || runtime.runtime.owner != loggedTitle)
        {
            LOG("cutscene section: title=%u scene=%d -> %s", unsigned(runtime.runtime.owner), scene,
                stays3D ? "3D (cutscene_3d_sections)" : "theatre");
            loggedScene = scene;
            loggedTitle = runtime.runtime.owner;
        }
        if (stays3D)
            return false;
    }
    return controlBefore.title == controlAfter.title && controlBefore.generation == controlAfter.generation &&
           controlBefore.state == controlAfter.state && controlBefore.heartbeatMs == controlAfter.heartbeatMs;
}
bool Game_CurrentCinematicSection(GameTitle& title, int32_t& scene)
{
    title = TitleAdapter_GetActiveTitle();
    scene = -1;
    const size_t i = static_cast<size_t>(title);
    if (i >= 8 || !VR_IsCutsceneTheaterActive() && !CutsceneSectionStays3D(g_config.cutscene_3d_sections, title,
                                                       g_cinematicSection[i].load(std::memory_order_relaxed)))
        return false;
    scene = g_cinematicSection[i].load(std::memory_order_relaxed);
    return true;
}
void Game_OnCutsceneTheaterPresentationChanged()
{
    // Called by the compositor exactly at full black, after it publishes the
    // new theatre-active state and before the next game frame can be built.
    // This removes a one-frame lag in Halo 3/ODST's stock cinematic-FOV policy.
    UpdateCinematicFovPolicy();
    if (!VR_IsCutsceneTheaterActive() || CinematicFovPolicyStockReady())
        return;
    const TitleAdapterRuntimeSnapshot runtime = RuntimeSnapshot(GetTickCount64());
    if (runtime.runtime.owner == GameTitle::Halo3 || runtime.runtime.owner == GameTitle::Halo3ODST)
    {
        // The engine's stock FOV could not be restored. Revoke only the
        // authored-projection proof while the screen is fully black; the
        // generic compositor will keep this cinematic immersive.
        TitleAdapter_ClearCutsceneTheaterProjection(runtime.runtime.owner, runtime.runtime.generation);
    }
}
bool Game_CanToggleImmersiveView()
{
    return Game_AllowsSharedGameplayFeatures() || Game_IsCameraOnlyBringup() || Game_UsesTitleOwnedHeadTracking();
}
void Game_DetachForVrRuntimeFailure()
{
    if (TitleAdapter_GetActiveTitle() == GameTitle::HaloCE)
        HaloCE_PublishTracking({}, false);
    // This is the same render-thread ownership transition used by normal title
    // unload/pause paths, reached only after OpenXR can no longer submit. Stop
    // every title from beginning new camera/stereo transactions before the VR
    // side releases retained presentation resources.
    g_vrRuntimeFailureLatched.store(true, std::memory_order_release);
    const GameTitle activeTitle = TitleAdapter_GetActiveTitle();
    const TitleAdapterRuntimeSnapshot runtime = RuntimeSnapshot(GetTickCount64());
    const bool haloOwned = activeTitle == GameTitle::Halo3 ||
                           (activeTitle == GameTitle::Unknown && runtime.runtime.owner == GameTitle::Halo3);
    const bool odstOwned = activeTitle == GameTitle::Halo3ODST ||
                           (activeTitle == GameTitle::Unknown && runtime.runtime.owner == GameTitle::Halo3ODST);
    if (odstOwned)
    {
        g_odstCamera.armed.store(false, std::memory_order_release);
        PublishOdstLifecycle();
    }
    const bool reachOwned = activeTitle == GameTitle::HaloReach ||
                            (activeTitle == GameTitle::Unknown && runtime.runtime.owner == GameTitle::HaloReach);
    if (reachOwned)
        g_reachCamera.armed.store(false, std::memory_order_release);
    const bool halo2Owned = activeTitle == GameTitle::Halo2 ||
                            (activeTitle == GameTitle::Unknown && runtime.runtime.owner == GameTitle::Halo2);
    if (halo2Owned)
        Halo2Stereo_ShutdownForVrFailure();
    Halo2Observer6Dof_ShutdownForVrFailure();
    Halo2AnniversaryStereo_ShutdownForVrFailure();
    g_enabled.store(false, std::memory_order_release);
    g_autoVrOwned.store(false, std::memory_order_release);
    g_autoVrUserVeto.store(true, std::memory_order_release);
    if (haloOwned)
        PublishHalo3Lifecycle(true, false, false);
    VR_DetachGamePresentation();
}
bool Game_HasAuthoritativePauseState()
{
    const GameTitle activeTitle = TitleAdapter_GetActiveTitle();
    if (activeTitle == GameTitle::HaloCE)
    {
        // The simulation clock is not authoritative for a local multiplayer
        // menu. Preserve the explicit Start/menu presentation request in CE.
        return false;
    }
    const TitleAdapterRuntimeSnapshot runtime = RuntimeSnapshot(GetTickCount64());
    // g_enginePauseValidated belongs specifically to Halo 3. MCC keeps title
    // modules resident, so this proof can remain true after switching away
    // from Halo 3. Never let that stale proof suppress another title's edge
    // fallback (notably Halo 2, which intentionally owns pause presentation
    // without a proven native pause boolean).
    const bool halo3Owned = activeTitle == GameTitle::Halo3 ||
                            (runtime.runtime.owner == GameTitle::Halo3 && runtime.runtime.qualifyingOwnerCount == 1);
    if (halo3Owned && g_enginePauseValidated.load(std::memory_order_acquire))
        return true;
    if ((activeTitle == GameTitle::Halo3ODST ||
            (runtime.runtime.owner == GameTitle::Halo3ODST && runtime.runtime.qualifyingOwnerCount == 1)) &&
        g_odstNativePauseFlag.load(std::memory_order_acquire) != 0)
    {
        return true;
    }
    if ((activeTitle == GameTitle::HaloReach ||
            (runtime.runtime.owner == GameTitle::HaloReach && runtime.runtime.qualifyingOwnerCount == 1)) &&
        g_reachNativePauseFlag.load(std::memory_order_acquire) != 0)
    {
        return true;
    }
    if ((activeTitle == GameTitle::Halo4 ||
            (runtime.runtime.owner == GameTitle::Halo4 && runtime.runtime.qualifyingOwnerCount == 1)) &&
        g_halo4NativePauseAvailable.load(std::memory_order_acquire))
    {
        return true;
    }
    return false;
}

// HUD layout (F1 menu): manual rescan + status. The scan normally starts itself
// whenever size, aspect, or curvature is non-stock and no slots are located.
void Game_LocateHudSafeFrames()
{
    HudLayoutProfile profile = HudLayoutProfile::None;
    if (Game_IsCameraOnlyBringup())
        profile = HudLayoutProfile::Halo3ODST;
    // Re-enabled with the missing resolution class. The earlier
    // no-effect result was measured while only Reach's "fullscreen wide"
    // record was ever written; the per-eye VR target is 3752x3828 (aspect
    // 0.98), which is not widescreen, so the engine was reading a record this
    // mod never touched. The adapter now carries the fullscreen-standard
    // record too.
    if (profile == HudLayoutProfile::None && TitleAdapter_GetActiveTitle() == GameTitle::HaloReach)
        profile = HudLayoutProfile::HaloReach;
    if (profile == HudLayoutProfile::None && Game_AllowsSharedGameplayFeatures())
        profile = HudLayoutProfile::Halo3;
    const HudLayoutAdapter* adapter = HudLayoutAdapterFor(profile);
    if (!adapter)
    {
        LOG("SAFEFRAME: manual rescan skipped; no title-owned HUD layout adapter");
        return;
    }
    LaunchSafeFrameScan(profile, "manual rescan from the menu");
}

void Game_GetHudSafeFrameStatus(int& matches, bool& scanning)
{
    const int c = g_safeFrameHitCount.load(std::memory_order_acquire);
    scanning = (c == -2) || g_safeFrameScanInFlight.load(std::memory_order_acquire);
    matches = (c > 0) ? c : 0;
}

void Game_ToggleHeadTracking()
{
    if (!Game_CanToggleImmersiveView())
        return;
    const bool on = !g_enabled.load();
    g_enabled = on;
    if (!Game_IsCameraOnlyBringup() && !Game_UsesTitleOwnedHeadTracking())
        PublishHalo3Lifecycle(true, on, false);
    if (on)
    {
        g_needRecenter = true;
        if (TitleAdapter_GetActiveTitle() == GameTitle::HaloCE)
        {
            HaloCE_Recenter();
            g_autoVrUserVeto.store(false, std::memory_order_release);
        }
        if (TitleAdapter_GetActiveTitle() == GameTitle::Halo2 && Game_UsesTitleOwnedHeadTracking())
        {
            Halo2Stereo_RequestRecenter();
            Halo2Observer6Dof_RequestRecenter();
            Halo2AnniversaryStereo_RequestRecenter();
        }
    }
    else
        g_autoVrUserVeto = true; // user turned VR off by hand; don't auto re-arm
    LOG("head tracking %s", on ? "ON" : "OFF");
}

// Called every frame from VR_OnPresent. Turns head tracking + stereo ON shortly
// after a level starts driving the camera, and back OFF when you return to the
// menu - so the mod behaves like a normal VR game (no F2/F11). Manual F2 off
// while in a level vetoes auto-arm until the next level load; F2/F11 still work.
// ---- Per-title presentation ticks (Phase 3 step 6), moved verbatim out of Game_AutoVrTick ----------------
// Menu/Start pressed while Halo 2 is the active title (input thread); the H2
// presentation tick turns it into pause presentation only if the game-time
// clock then freezes (Halo2PauseEntryClock).
namespace game_core
{
    std::atomic<uint64_t> g_halo2PauseIntentMs{0};
} // namespace game_core
void Game_NoteHalo2PauseIntent(uint64_t nowMs)
{
    if (g_config.halo2_start_pause && TitleAdapter_GetActiveTitle() == GameTitle::Halo2)
        g_halo2PauseIntentMs.store(nowMs ? nowMs : 1, std::memory_order_release);
}

void Game_AutoVrTick()
{
    AutoVrHalo2ClaimGate();
    if (AutoVrTickHaloCE())
        return;
    if (AutoVrTickOdst())
        return;

    if (AutoVrTickReach())
        return;

    if (AutoVrTickHalo2Stereo6Dof())
        return;

    if (AutoVrTickHalo4())
        return;

    AutoVrTickHalo3();
}

void Game_Recenter()
{
    if (TitleAdapter_GetActiveTitle() == GameTitle::HaloCE)
        HaloCE_Recenter();
    // One public recenter action owns both references: Halo's camera/position
    // origin and the OpenXR head-locked screen origin. This keeps keyboard F3,
    // the F1 button, and transition-triggered recentering behavior identical.
    g_needRecenter = true;
    // All three Halo 2 pose references move together, or the observer
    // (the pose owner in both graphics modes) keeps the old origin while
    // the per-eye cores take the new one and every frame carries the
    // difference as a fixed offset.
    if (TitleAdapter_GetActiveTitle() == GameTitle::Halo2)
    {
        Halo2Stereo_RequestRecenter();
        Halo2Observer6Dof_RequestRecenter();
        Halo2AnniversaryStereo_RequestRecenter();
    }
    VR_RequestRecenter();
}
void Game_FlipYaw()
{
    g_yawSign = -g_yawSign.load();
    LOG("yaw sign %+.0f", g_yawSign.load());
}
void Game_FlipPitch()
{
    g_pitchSign = -g_pitchSign.load();
    LOG("pitch sign %+.0f", g_pitchSign.load());
}
void Game_ToggleUp()
{
    g_writeUp = !g_writeUp.load();
    LOG("write up-vector %s", g_writeUp.load() ? "on" : "off");
}
float Game_GetYawSign()
{
    return g_yawSign.load();
}
float Game_GetPitchSign()
{
    return g_pitchSign.load();
}
bool Game_GetWriteUp()
{
    return g_writeUp.load();
}

void Game_TogglePositional()
{
    if (VR_IsStereoEnabled())
    {
        g_positional = true;
        LOG("positional remains ON (required for stereo VR)");
        return;
    }
    const bool on = !g_positional.load();
    g_positional = on;
    if (on)
        g_needPosRecenter = true; // capture neutral head position, no yaw snap
    LOG("positional (leaning) %s", on ? "ON" : "OFF");
}

void Game_ForcePositional()
{
    g_positional = true;
    g_needPosRecenter = true;
    LOG("positional 6DOF forced ON for stereo VR");
}

void Game_PitchTrim(int dir)
{
    const float t = Clamp(g_pitchTrim.load() + dir * 0.035f, -0.8f, 0.8f); // ~2 deg steps
    g_pitchTrim = t;
    LOG("pitch trim %.1f deg", t * 57.2958f);
}

void Game_LeanScale(int dir)
{
    const float s = Clamp(g_worldScale.load() + dir * 0.05f, 0.05f, 2.0f);
    g_worldScale = s;
    LOG("lean scale %.2f (game units per meter)", s);
}

void Game_ToggleVrAim()
{
    const bool on = !g_vrAim.load();
    g_vrAim = on;
    LOG("VR aim (right controller steers weapon) %s", on ? "ON" : "OFF");
}

// Grip-exit press (the game's Use) while it is being held; 0 otherwise.
// Comfort: accumulated followed hull yaw (radians) and whether a seat is
// following the hull right now (published within the last 200 ms).
bool Game_VehicleFollowYaw(float& yawTotal)
{
    yawTotal = g_vehicleFollowYawTotal.load(std::memory_order_relaxed);
    const uint64_t at = g_vehicleFollowingMs.load(std::memory_order_acquire);
    const uint64_t now = GetTickCount64();
    return at && now >= at && now - at <= 200;
}

// Two-grip turret handles in gunner seats (turret_handles_logic.h).
void Game_UpdateTurretHandles(const VrPadState& pad)
{
    static turret_handles::Handles handles;
    static weapon_interaction::BodyYaw bodyYaw;
    static turret_handles::SeatAxis seatAxis;
    const uint64_t now = GetTickCount64();
    turret_handles::Input in{};
    in.now = now;
    float lq[4]{}, rq[4]{}, hq[4]{}, aq[4]{0, 0, 0, 1}, ap[3]{};
    const bool poses = VR_GetLeftControllerPose(lq, in.left.pos) && VR_GetRightControllerPose(rq, in.right.pos) &&
                       VR_GetHeadPose(hq, in.head);
    in.left.valid = in.right.valid = poses;
    in.left.grip = pad.gripL;
    in.right.grip = pad.gripR;
    const bool turretSeat =
        g_config.vehicle_turret_handles && pad.valid && g_enabled.load() && g_vrAim.load() && OccupiedTurretSeat();
    if (turretSeat && !handles.Holding() && VR_GetAimPose(aq, ap))
        for (int i = 0; i < 4; ++i)
            in.controller[i] = aq[i];
    weapon_interaction::Vec facing{};
    const weapon_interaction::Quat headQ{hq[0], hq[1], hq[2], hq[3]};
    const bool facingOk = poses && bodyYaw.Update(headQ, now, handles.Holding(), facing);
    in.bodyForward[0] = facing.x;
    in.bodyForward[1] = facing.y;
    in.bodyForward[2] = facing.z;
    // Centre the handles on the seat axis captured at entry (head yaw only decides the axis once).
    float seatFwd[3]{};
    const float headFwd[3]{facing.x, facing.y, facing.z};
    const bool axisOk = seatAxis.Update(turretSeat && facingOk, headFwd, seatFwd);
    if (g_config.turret_handle_seat_axis && axisOk)
        for (int i = 0; i < 3; ++i)
            in.bodyForward[i] = seatFwd[i];
    in.available = turretSeat && facingOk;
    turret_handles::Settings settings{};
    settings.forward = g_config.turret_handle_forward_m;
    settings.down = g_config.turret_handle_down_m;
    settings.span = g_config.turret_handle_span_m;
    settings.lever = g_config.turret_handle_lever;
    const turret_handles::Output out = handles.Update(in, settings);
    // Pad and poses are by role here ("left" = support, "right" = weapon hand),
    // as is VR_PulseContactHaptics(true = support).
    if (out.grabLeft)
        VR_PulseContactHaptics(true, feedback::Strength(feedback::Cue::WheelGrab));
    if (out.grabRight)
        VR_PulseContactHaptics(false, feedback::Strength(feedback::Cue::WheelGrab));
    if (out.released)
    {
        VR_PulseContactHaptics(true, feedback::Strength(feedback::Cue::WheelRelease));
        VR_PulseContactHaptics(false, feedback::Strength(feedback::Cue::WheelRelease));
    }
    static uint64_t lastLogMs = 0;
    if ((out.grabbed || out.released) && now - lastLogMs >= 250)
    {
        LOG("Action fired: turret %s", out.grabbed ? "grab" : "release");
        lastLogMs = now;
    }
    for (int i = 0; i < 4; ++i)
        g_turretHandleQ[i].store(out.orientation[i], std::memory_order_relaxed);
    for (int i = 0; i < 3; ++i)
        g_turretHandleP[i].store(out.origin[i], std::memory_order_relaxed);
    g_turretHandleAiming.store(out.aiming, std::memory_order_release);
    g_turretSwallowsGrips.store(out.consumeLeft || out.consumeRight ? 1u : 0u, std::memory_order_release);
}

namespace
{
    bool FlightStickSeat()
    {
        return g_config.vehicle_flight_stick && VehicleSteerContextFresh() &&
               g_vehicleSteer.aircraft.load(std::memory_order_relaxed);
    }
}

// A virtual flight stick for the weapon hand in aircraft driver seats
// (flight_stick_logic.h). Only that hand's grip is withheld; both triggers stay
// native, so the weapon-hand trigger still fires the aircraft's guns.
void Game_UpdateFlightStick(const VrPadState& pad)
{
    static flight_stick::Stick stick;
    static weapon_interaction::BodyYaw bodyYaw;
    const uint64_t now = GetTickCount64();
    flight_stick::Input in{};
    in.now = now;
    float hq[4]{};
    const bool head = VR_GetHeadPose(hq, in.head);
    // The "right" pose and gripR are the weapon hand's (already swapped for
    // left-handed players).
    const bool hand = VR_GetRightControllerPose(in.handQ, in.hand);
    in.handValid = head && hand;
    in.grip = pad.gripR;
    const bool seat = pad.valid && g_enabled.load() && g_vrAim.load() && FlightStickSeat();
    weapon_interaction::Vec facing{};
    const weapon_interaction::Quat headQ{hq[0], hq[1], hq[2], hq[3]};
    const bool facingOk = head && bodyYaw.Update(headQ, now, stick.Holding(), facing);
    in.bodyForward[0] = facing.x;
    in.bodyForward[1] = facing.y;
    in.bodyForward[2] = facing.z;
    in.available = seat && facingOk;
    flight_stick::Settings settings{};
    settings.forward = g_config.flight_stick_forward_m;
    settings.down = g_config.flight_stick_down_m;
    settings.tiltDeg = g_config.flight_stick_tilt_deg;
    settings.fullDeg = g_config.flight_stick_full_deg;
    const flight_stick::Output out = stick.Update(in, settings);
    if (out.grabbed)
        VR_PulseContactHaptics(false, feedback::Strength(feedback::Cue::WheelGrab));
    if (out.released)
        VR_PulseContactHaptics(false, feedback::Strength(feedback::Cue::WheelRelease));
    static uint64_t lastLogMs = 0;
    if ((out.grabbed || out.released) && now - lastLogMs >= 250)
    {
        LOG("Action fired: flight stick %s", out.grabbed ? "grab" : "release");
        lastLogMs = now;
    }
    g_flightStickRx.store(out.rx, std::memory_order_relaxed);
    g_flightStickRy.store(g_config.flight_stick_invert ? -out.ry : out.ry, std::memory_order_relaxed);
    g_flightStickFlying.store(out.flying, std::memory_order_release);
    g_flightStickSwallowsGrip.store(out.consumeGrip ? 1u : 0u, std::memory_order_release);
}

bool Game_FlightStickSwallowsPrimaryGrip()
{
    return g_flightStickSwallowsGrip.load(std::memory_order_acquire) != 0;
}

uint32_t Game_VehicleWheelButtons()
{
    return g_vehicleExitButtons.load(std::memory_order_relaxed);
}

// Reach grab pickup (reach_grab_pickup.inl, grab_pickup_logic.h): the lowered
// support hand grips near a weapon; the game's own Use press picks it up.
namespace
{
    std::atomic<uint32_t> g_grabPickupButtons{0};
    std::atomic<bool> g_grabSwallowsSupportGrip{false};

    // On foot in Reach: the seat state the Reach LT/X swap uses, fresh. Not
    // VehicleSteerContextFresh(), which only says a steer context was
    // published; Reach publishes "not the driver" every frame on foot too.
    bool ReachGrabOnFoot(uint64_t now)
    {
        return ReachHandOnFoot(now);
    }
}

void Game_UpdateGrabPickup(const VrPadState& pad)
{
    static grab_pickup::Grab grab;
    const uint64_t now = GetTickCount64();
    const GameTitle title = TitleAdapter_GetActiveTitle();
    grab_pickup::Input in{};
    in.nowMs = now;
    const bool onFoot = title == GameTitle::HaloReach && ReachGrabOnFoot(now);
    in.enabled = g_config.reach_grab_pickup && pad.valid && g_enabled.load() && g_vrAim.load() &&
                 title == GameTitle::HaloReach && ReachGrabPickupReady() && onFoot;
    float hq[4]{}, hp[3]{}, sq[4]{}, sp[3]{};
    // The pad and the left aim pose are already role-routed: support hand.
    const bool headValid = VR_GetHeadPose(hq, hp);
    in.handValid = headValid && VR_GetLeftControllerPose(sq, sp);
    in.handDropM = in.handValid ? hp[1] - sp[1] : 0.0f;
    // The nearest-weapon line reports its offset in the body frame (head yaw).
    if (title == GameTitle::HaloReach && headValid)
        ReachGrabPublishHead(hq);
    // The weapon in the hand: a grab that changes it was taken (A4-18).
    Game_ReserveWeaponDiag held{};
    if (in.enabled && Game_GetReserveWeaponDiag(title, held) && held.read)
        in.held = held.primary;
    in.handBusy = pad.weaponConsumeSupport || pad.supportAimActive;
    in.grip = pad.gripL;
    in.target = ReachGrabChosenWeapon(now);
    grab_pickup::Settings settings{};
    settings.minDropM = std::clamp(g_config.reach_grab_drop_m, 0.10f, 1.20f);
    const grab_pickup::Output out = grab.Update(in, settings);
    ReachGrabSetSteer(out.steerSearch);
    // Rig diagnostic (build 32 gate: the hand on the weapon never steered):
    // every input of the reach gate, on change. Rig only (probe or rig log).
    if ((g_config.reach_grab_probe || g_config.reach_grab_rig_log) && title == GameTitle::HaloReach)
    {
        const int gripLevel = in.grip >= settings.grabGrip ? 2 : in.grip >= settings.reachGrip ? 1 : 0;
        // Every enabled input is in the key (Reviewer, 3e35f728): a changed
        // reason prints even while enabled stays 0.
        const uint32_t key = (in.enabled ? 1u : 0u) | (in.handValid ? 2u : 0u) | (pad.weaponConsumeSupport ? 4u : 0u) |
                             (pad.supportAimActive ? 8u : 0u) | (in.handDropM >= settings.minDropM ? 16u : 0u) |
                             (in.target != grab_pickup::kNoTarget ? 32u : 0u) | (out.steerSearch ? 64u : 0u) |
                             (static_cast<uint32_t>(gripLevel) << 8) | (g_config.reach_grab_pickup ? 1u << 10 : 0u) |
                             (pad.valid ? 1u << 11 : 0u) | (g_enabled.load() ? 1u << 12 : 0u) |
                             (g_vrAim.load() ? 1u << 13 : 0u) | (ReachGrabPickupReady() ? 1u << 14 : 0u) |
                             (onFoot ? 1u << 15 : 0u);
        static uint32_t lastKey = ~0u;
        if (key != lastKey)
        {
            lastKey = key;
            LOG("grab pickup gate: enabled %d (option %d pad %d mod %d vrAim %d ready %d onFoot %d) hand %d supportBusy %d "
                "twoHand %d grip %.2f drop %.2f m (needs %.2f) target %08X steer %d",
                in.enabled ? 1 : 0, g_config.reach_grab_pickup ? 1 : 0, pad.valid ? 1 : 0, g_enabled.load() ? 1 : 0,
                g_vrAim.load() ? 1 : 0, ReachGrabPickupReady() ? 1 : 0, onFoot ? 1 : 0, in.handValid ? 1 : 0,
                pad.weaponConsumeSupport ? 1 : 0, pad.supportAimActive ? 1 : 0, in.grip, in.handDropM,
                settings.minDropM, in.target, out.steerSearch ? 1 : 0);
        }
    }
    // Channel "left" is the support role (as the magazine/two-hand cues).
    if (out.haptic)
        VR_PulseContactHaptics(true, feedback::Strength(feedback::Cue::GrabPickupPrompt));
    uint32_t use = 0;
    if (out.pressPickup)
    {
        use = Game_VrActionTransport(vr_mapping::Interact, now);
        static uint64_t lastMissingLogMs = 0;
        if (!use && now - lastMissingLogMs >= 2000)
        {
            LOG("grab pickup: the game's Use / pick up binding is unavailable; nothing pressed");
            lastMissingLogMs = now;
        }
    }
    // One line per edge (A4-21: no rate limit; a grab already has a 1 s cooldown).
    if (out.fired)
        LOG("Action fired: grab pickup");
    if (out.taken)
        LOG("grab pickup: taken (the weapon in the hand changed)");
    if (out.aborted)
        LOG("grab pickup: the game's chosen target changed mid-press and the hand did not; Use released");
    g_grabPickupButtons.store(use, std::memory_order_relaxed);
    // A weapon under the reaching hand owns the support grip, so a grip-bound
    // action (e.g. armor ability) does not fire with the pickup.
    g_grabSwallowsSupportGrip.store(
        out.steerSearch && (in.target != grab_pickup::kNoTarget || out.pressPickup), std::memory_order_release);
}

uint32_t Game_GrabPickupButtons()
{
    return g_grabPickupButtons.load(std::memory_order_relaxed);
}

bool Game_GrabPickupSwallowsSupportGrip()
{
    return g_grabSwallowsSupportGrip.load(std::memory_order_acquire);
}

// Reach grip-held weapons (GRAB-G1-PLAN.md 9.3-9.5): the gun hand holds its
// grip. Opening it outside both holsters drops the gun, at a free holster stows
// it, at a full holster keeps it (a pulse says the holster is full); an empty
// hand closing at a holster draws that weapon. Requests go to the Reach hand
// owner (game thread), which does them with the game's own switch and drop.
namespace
{
    std::atomic<bool> g_gripHeldSwallowsPrimary{false};

    const char* GripHeldZoneName(int8_t zone)
    {
        return zone == holster::kHip ? "hip" : zone == holster::kShoulder ? "shoulder" : "none";
    }

    // Per weapon handle: its class location (learned while it was in the hand
    // or published as the reserve) and the zone the player holstered it in.
    struct GripHeldWeapon
    {
        uint32_t handle = 0xFFFFFFFFu;
        int8_t location = holster::kNoZone, pinned = holster::kNoZone;
    };
    GripHeldWeapon* GripHeldEntry(GripHeldWeapon (&table)[8], uint32_t handle, bool create)
    {
        for (auto& entry : table)
            if (entry.handle == handle)
                return &entry;
        if (!create)
            return nullptr;
        // At most 4 weapons are carried and stale entries are cleared each update.
        for (auto& entry : table)
            if (entry.handle == 0xFFFFFFFFu)
            {
                entry = GripHeldWeapon{};
                entry.handle = handle;
                return &entry;
            }
        return nullptr;
    }
    int8_t GripHeldLocationFor(uint64_t identity)
    {
        const auto slot = weapon_interaction::SlotFor(weapon_profile::WeaponClassFor(GameTitle::HaloReach, identity));
        return slot == weapon_interaction::HolsterSlot::Hip        ? holster::kHip
               : slot == weapon_interaction::HolsterSlot::Shoulder ? holster::kShoulder
                                                                   : holster::kNoZone;
    }
}

uint32_t Input_LastPadButtons() noexcept; // input.cpp: the buttons the game last received

void Game_UpdateGripHeld(const VrPadState& pad)
{
    static holster::GripHold hold;
    static holster::GripClose close;
    static GripHeldWeapon weapons[8];
    static uint32_t lastHand = 0xFFFFFFFFu;
    const uint64_t now = GetTickCount64();
    const GameTitle title = TitleAdapter_GetActiveTitle();
    uint32_t slots[4]{};
    int role = -1, planMode = 0;
    const bool reach = g_config.reach_grip_held && title == GameTitle::HaloReach;
    const bool onFoot = reach && ReachHandOnFoot(now);
    const bool live = reach && pad.valid && g_enabled.load() && g_vrAim.load() && ReachGrabPickupReady() && onFoot &&
                      ReachHandRead(slots, role, planMode);
    // On foot the gun-hand grip holds the gun and is not a game button (7.7, Rule 2).
    g_gripHeldSwallowsPrimary.store(live, std::memory_order_release);
    // Rig only (probe or rig log; build 33 Q7): whether the grip reaches the game, on change.
    if ((g_config.reach_grab_probe || g_config.reach_grab_rig_log) && reach)
    {
        static int lastPasses = -1;
        const int seat = onFoot ? 0 : ReachHandSeatKind(now);
        const int passes = live ? 0 : seat == static_cast<int>(holster::Seat::Seated) ? 1 : !onFoot ? 2 : -1;
        if (passes >= 0 && passes != lastPasses)
        {
            lastPasses = passes;
            LOG(passes == 1   ? "grip-held: gun-hand grip passes (seat)"
                : passes == 2 ? "grip-held: gun-hand grip passes (seat state unknown)"
                              : "grip-held: gun-hand grip withheld (on foot)");
        }
    }
    // The switch button the game last received (rising edge): while the mod
    // owns the hand, the game cannot switch to a holstered weapon itself.
    {
        static bool lastSwitch = false;
        const int index = weapon_interaction::TitleIndex(GameTitle::HaloReach);
        const uint32_t mask = !reach || index < 0 ? 0u
                              : g_config.vr_action_mapping
                                  ? Game_VrActionTransport(vr_mapping::SwitchWeapon, now)
                                  : weapon_interaction::Button(g_config.weapon_switch_button[index]);
        const bool pressed = mask && (Input_LastPadButtons() & mask) == mask;
        if (pressed && !lastSwitch && live && planMode != 0)
            ReachHandPostRequest(static_cast<int>(holster::ProbeAction::Switch), -1);
        lastSwitch = pressed;
    }
    if (!live)
    {
        hold.NewWeapon();
        (void)close.Update(0.0f, false);
        lastHand = 0xFFFFFFFFu;
        return;
    }
    // Forget weapons no longer carried; learn classes from the held and reserve diag.
    for (auto& entry : weapons)
        if (entry.handle != 0xFFFFFFFFu &&
            std::find(std::begin(slots), std::end(slots), entry.handle) == std::end(slots))
            entry = GripHeldWeapon{};
    const uint32_t hand = role >= 0 && role < 4 ? slots[role] : 0xFFFFFFFFu;
    Game_ReserveWeaponDiag diag{};
    if (Game_GetReserveWeaponDiag(title, diag) && diag.read)
    {
        if (diag.held && diag.primary == hand && hand != 0xFFFFFFFFu)
            if (GripHeldWeapon* entry = GripHeldEntry(weapons, hand, true))
                entry->location = GripHeldLocationFor(diag.held);
        if (diag.publishedIdentity && diag.reserve != 0xFFFFFFFFu &&
            std::find(std::begin(slots), std::end(slots), diag.reserve) != std::end(slots))
            if (GripHeldWeapon* entry = GripHeldEntry(weapons, diag.reserve, true))
                entry->location = GripHeldLocationFor(diag.publishedIdentity);
    }
    int8_t preferred[4], zones[4];
    bool pinned[4]{};
    for (int i = 0; i < 4; ++i)
    {
        const GripHeldWeapon* entry = slots[i] != 0xFFFFFFFFu ? GripHeldEntry(weapons, slots[i], false) : nullptr;
        pinned[i] = entry && entry->pinned != holster::kNoZone && i != role;
        preferred[i] = !entry                             ? holster::kNoZone
                       : pinned[i]                        ? entry->pinned
                       : g_config.weapon_holster_by_class ? entry->location
                                                          : holster::kNoZone;
    }
    const int8_t fallback = g_config.weapon_holster_location == 1 ? holster::kHip : holster::kShoulder;
    holster::AssignZones(slots, preferred, pinned, fallback, zones);
    bool occupied[2]{};
    for (int i = 0; i < 4; ++i)
        if (i != role && zones[i] != holster::kNoZone)
            occupied[zones[i]] = true;

    holster::GripSettings settings{};
    settings.releaseMs = static_cast<uint64_t>(std::clamp(g_config.reach_grip_release_ms, 100, 400));
    holster::GripInput in{};
    in.grip = std::isfinite(pad.gunGripRaw) ? pad.gunGripRaw : 0.0f;
    in.tracked = pad.gunTracked;
    in.actionActive = pad.gunGripActive;
    in.sampleAgeMs = pad.gunSampleMs && now >= pad.gunSampleMs ? now - pad.gunSampleMs : UINT64_MAX;
    in.focused = pad.gunFocused;
    in.blocked = !pad.gunReady || Menu_IsOpen();
    in.gateOpen = true; // checked above with the seat
    in.nowMs = now;
    if (hand != lastHand)
    {
        hold.NewWeapon(); // a weapon that just came to the hand must be Held once first
        lastHand = hand;
    }
    if (hand != 0xFFFFFFFFu)
    {
        (void)close.Update(in.grip, false); // a close needs a fresh open once the hand is empty
        if (!hold.Update(in, settings))
            return;
        switch (holster::ReleaseAt(pad.gunZonesKnown, pad.gunZone, occupied))
        {
        case holster::Release::Drop:
            LOG("Action fired: grip-held drop");
            ReachHandPostRequest(static_cast<int>(holster::ProbeAction::Drop), role);
            break;
        case holster::Release::Stow:
            LOG("Action fired: holster stow (%s)", GripHeldZoneName(pad.gunZone));
            if (GripHeldWeapon* entry = GripHeldEntry(weapons, hand, true))
                entry->pinned = pad.gunZone;
            ReachHandPostRequest(static_cast<int>(holster::ProbeAction::Stow), role);
            break;
        case holster::Release::Occupied:
            // A full holster resists, as a real one would: the gun stays in the hand.
            LOG("grip-held: the %s holster is full; the gun stays in the hand", GripHeldZoneName(pad.gunZone));
            VR_PulseContactHaptics(false, g_config.weapon_holster_haptic);
            break;
        default:
            break;
        }
        return;
    }
    const bool evidence =
        in.tracked && in.actionActive && in.sampleAgeMs <= settings.staleMs && in.focused && !in.blocked;
    if (!close.Update(in.grip, evidence, settings) || !pad.gunZonesKnown || pad.gunZone == holster::kNoZone)
        return;
    for (int i = 0; i < 4; ++i)
        if (i != role && zones[i] == pad.gunZone && slots[i] != 0xFFFFFFFFu)
        {
            LOG("Action fired: holster draw (%s, slot %d)", GripHeldZoneName(pad.gunZone), i);
            if (GripHeldWeapon* entry = GripHeldEntry(weapons, slots[i], false))
                entry->pinned = holster::kNoZone;
            ReachHandPostRequest(static_cast<int>(holster::ProbeAction::Draw), i);
            return;
        }
}

bool Game_GripHeldSwallowsPrimaryGrip()
{
    return g_gripHeldSwallowsPrimary.load(std::memory_order_acquire);
}

// Trigger throttle: right trigger forward, left trigger brake/reverse, for
// an unarmed driver seat in Wheel or Stick mode. Armed seats (Ghost,
// Chopper, Revenant) keep the triggers for their guns and the stick throttle.
bool Game_VehicleTriggerThrottle(float rightTrigger, float leftTrigger, float& throttle)
{
    throttle = 0.0f;
    if (!VehicleSteerContextFresh() || !g_vehicleSteer.driver.load(std::memory_order_relaxed) ||
        g_vehicleSteer.armed.load(std::memory_order_relaxed))
        return false;
    const int mode = g_vehicleSteer.mode.load(std::memory_order_relaxed);
    if (mode != vehicle_wheel::Wheel && mode != vehicle_wheel::Stick)
        return false;
    throttle = vehicle_wheel::TriggerThrottle(rightTrigger, leftTrigger);
    return true;
}

// The per-vehicle trim table in config.h mirrors Halo3VehicleId by
// contract; if the enum ever grows or reorders, this is the tripwire.
static_assert(static_cast<int>(Halo3VehicleId::Scorpion) == 1 &&
                  static_cast<int>(Halo3VehicleId::StationaryTurret) == kVehicleTrimCount,
    "kVehicleTrimNames must mirror Halo3VehicleId order");

// The occupied seat in whichever title owns the camera. Only one title's
// vehicle transaction can be armed at a time.
int Game_VehicleSeatTrimSlotEx(VehicleTrimBank* outBank)
{
    if (outBank)
        *outBank = VehicleTrimBank::Halo3;
    const int haloSlot = Game_Halo3CurrentSeatTrimSlot();
    if (haloSlot >= 0)
        return haloSlot;
    if (OdstVehicleFpActive())
    {
        OdstSeatSnapshot seat;
        if (OdstReadSeatSnapshot(seat))
        {
            const int slot = ConfigOdstSeatTrimSlot(static_cast<int>(seat.identity), seat.seatIndex, seat.mounted);
            if (slot >= 0 && outBank)
                *outBank = VehicleTrimBank::Odst;
            return slot;
        }
    }
    if (TitleAdapter_GetActiveTitle() == GameTitle::HaloReach)
    {
        const int slot = ReachVehicleTrimSlotNow();
        if (slot >= 0 && outBank)
            *outBank = VehicleTrimBank::Reach;
        return slot;
    }
    return -1;
}

// ---- Runtime seat-view switch (first-person seat view <-> the game's own third-person camera).
// Seated detection and the vehicle class key do not depend on the first-person flag, so the button
// also works from the third-person view. Key 0 = not seated.
static uint32_t VehicleViewSeatKey() noexcept
{
    if (TitleAdapter_GetRuntimeMode() != RuntimeMode::Gameplay)
        return 0;
    const GameTitle title = TitleAdapter_GetActiveTitle();
    uint32_t vehicle = 0;
    bool seated = false;
    switch (title)
    {
    case GameTitle::Halo3:
    {
        seated = Game_Halo3VehicleState().state == Halo3VehicleState::Vehicle;
        const int slot = seated ? Game_Halo3CurrentSeatTrimSlot() : -1;
        vehicle = slot >= 0 ? static_cast<uint32_t>(slot / kVehicleSeatSlots) + 1u : 0xFFu;
        break;
    }
    case GameTitle::Halo3ODST:
    {
        OdstSeatSnapshot seat;
        seated = OdstReadSeatSnapshot(seat);
        vehicle = seated ? (static_cast<uint32_t>(seat.identity) & 0xFFFFu) : 0u;
        break;
    }
    case GameTitle::HaloReach:
    {
        seated = Game_ReachPlayerIsInVehicle();
        const int slot = seated ? ReachVehicleTrimSlotNow() : -1;
        vehicle = slot >= 0 ? static_cast<uint32_t>(slot / kReachVehicleSeatSlots) + 1u : 0xFFu;
        break;
    }
    case GameTitle::HaloCE:
    case GameTitle::Halo2:
    case GameTitle::Halo4:
    {
        NativeVehicleCameraOwner owner{};
        seated = Game_ReadVehicleCameraOwner(title, owner);
        const uint64_t id = owner.vehicleIdentity;
        vehicle = seated ? static_cast<uint32_t>((id ^ (id >> 16) ^ (id >> 32)) & 0xFFFFu) : 0u;
        break;
    }
    default:
        break;
    }
    if (!seated)
        return 0;
    return 0x80000000u | (static_cast<uint32_t>(title) << 16) | (vehicle & 0xFFFFu);
}

static vehicle_view::Memory g_vehicleViewMemory;
static vehicle_view::State g_vehicleViewState;
static std::atomic<bool> g_vehicleViewSeated{false};
static std::atomic<bool> g_vehicleViewSwallowX{false};
static vehicle_view::XGuard g_vehicleViewXGuard;

// Input thread, once per poll. X switches the view while seated (sprint does nothing in a vehicle).
void Game_UpdateVehicleViewToggle(const VrPadState& pad)
{
    const uint32_t key = (g_enabled.load() && pad.valid && !Menu_IsOpen()) ? VehicleViewSeatKey() : 0u;
    g_vehicleViewSeated.store(key != 0, std::memory_order_relaxed);
    g_vehicleViewSwallowX.store(g_vehicleViewXGuard.Update(key != 0, pad.x), std::memory_order_relaxed);
    const bool wasFirst = g_config.vehicle_first_person;
    bool live = wasFirst;
    const vehicle_view::Event e = vehicle_view::Step(
        g_vehicleViewState, g_vehicleViewMemory, key, key != 0 && pad.x, g_config.vehicle_first_person_cfg, live);
    if (live != wasFirst)
        g_config.vehicle_first_person = live;
    if (e.switched)
    {
        const GameTitle title = TitleAdapter_GetActiveTitle();
        static const char* const kTitleNames[] = {
            "none", "Halo 3", "Halo 3 ODST", "Halo Reach", "Halo 4", "Halo CE", "Halo 2"};
        const unsigned t = static_cast<unsigned>(title);
        LOG("vehicle view: %s -> %s (%s, vehicle %04X)", e.from ? "first person" : "third person",
            e.to ? "first person" : "third person", t < 7 ? kTitleNames[t] : "unknown", key & 0xFFFFu);
    }
}
// While seated, X belongs to the view switch: input.cpp withholds it (raw bit and its mapped action) from the game.
// F1's frame-cap line for one title (reach_frame_cap::Status).
uint8_t Game_FrameCapStatus(GameTitle title)
{
    int index = -1;
    switch (title)
    {
    case GameTitle::HaloReach:
        index = 0;
        break;
    case GameTitle::Halo4:
        index = 1;
        break;
    case GameTitle::Halo3:
        index = 2;
        break;
    case GameTitle::Halo3ODST:
        index = 3;
        break;
    case GameTitle::Halo2:
        index = 4;
        break;
    default:
        break;
    }
    return index < 0 ? uint8_t(reach_frame_cap::Status::NotChecked)
                     : g_frameCapStatus[index].load(std::memory_order_acquire);
}

bool Game_VehicleViewSwallowsX()
{
    return g_vehicleViewSwallowX.load(std::memory_order_relaxed);
}
// F1: seated state and the live view, for the Vehicles page.
bool Game_VehicleViewState(bool& firstPerson)
{
    firstPerson = g_config.vehicle_first_person;
    return g_vehicleViewSeated.load(std::memory_order_relaxed);
}
void Game_VehicleViewForget()
{
    g_vehicleViewMemory.Clear();
}

const char* Game_VehicleSeatTrimName(int slot, VehicleTrimBank bank)
{
    if (bank == VehicleTrimBank::Halo3)
        return Game_Halo3SeatTrimName(slot);
    if (bank == VehicleTrimBank::Reach)
    {
        static char text[64];
        if (slot < 0 || slot >= kReachVehicleTrimSlots)
            return "this seat";
        const int v = slot / kReachVehicleSeatSlots;
        const int s = slot % kReachVehicleSeatSlots;
        // Every row must carry a name. The list stopped at 20 while the bank
        // held 34, so the census rows named the F1 header through a null
        // pointer; row 35 is the unmatched row.
        static const char* const kNiceVehicle[kReachVehicleTrimCount] = {"Banshee", "Space Banshee", "Ghost",
            "Revenant", "Wraith", "Wraith gunner", "Mongoose", "Warthog", "Warthog chaingun", "Warthog gauss",
            "Warthog rockets", "Falcon", "Sabre", "Scorpion", "Forklift", "Cart", "Shade plasma", "Shade flak",
            "Plasma turret", "Machine gun", "MAC cannon", "Scorpion anti-infantry", "Seraph", "Pelican",
            "Pelican chin gun", "Corvette cannon", "Space Phantom chin gun", "Space Phantom beam turret", "Cargo truck",
            "Military truck", "ONI van", "Pickup", "Large truck cab", "Squad drop pod", "Unmatched vehicle"};
        static_assert(_countof(kNiceVehicle) == kReachVehicleTrimCount, "every Reach trim row needs a display name");
        _snprintf_s(text, sizeof(text), _TRUNCATE, "Reach %s seat %d", kNiceVehicle[v], s);
        return text;
    }
    if (bank != VehicleTrimBank::Odst)
        return "this seat";
    static char text[56];
    if (slot < 0 || slot >= kOdstVehicleTrimSlots)
        return "this seat";
    const int v = slot / kOdstVehicleSeatSlots;
    const int s = slot % kOdstVehicleSeatSlots;
    static const char* const kNiceVehicle[kOdstVehicleTrimCount] = {"Scorpion", "Warthog", "Mongoose", "Ghost",
        "Wraith", "Prowler", "Banshee", "Hornet", "Chopper", "Turret", "Shade"};
    static const char* const kNiceSeat[kOdstVehicleSeatSlots] = {
        "driver", "passenger", "passenger 2", "passenger 3", "passenger 4", "gunner"};
    // Both single-seat cases read wrong with a seat word after them.
    if (s == 0 && (v == kOdstVehicleTrimCount - 2 || v == kOdstVehicleTrimCount - 1))
        return v == kOdstVehicleTrimCount - 1 ? "ODST Shade" : "ODST Turret";
    _snprintf_s(text, sizeof(text), _TRUNCATE, "ODST %s %s", kNiceVehicle[v], kNiceSeat[s]);
    return text;
}

// True while the player occupies a seat whose weapon is a physically slewing
// gun with an authored rate cap, in either title that has one. Read once per
// aim frame; both the servo and the reticle publication key off it, so they
// can never disagree about which seat is being flown.
namespace game_core
{
    bool OccupiedTurretSeat()
    {
        if (Halo3VehicleFpActive())
        {
            Halo3SeatSnapshot seat;
            if (Halo3ReadSeatSnapshot(seat) &&
                Halo3SeatIsTurret(static_cast<Halo3VehicleId>(seat.identity), seat.mounted))
            {
                return true;
            }
        }
        if (OdstVehicleFpActive())
        {
            OdstSeatSnapshot seat;
            if (OdstReadSeatSnapshot(seat) && OdstSeatIsTurret(seat.identity, seat.mounted))
            {
                return true;
            }
        }
        // Halo 2: the observer's seat role (halo2_vehicle_roles.h).
        if (Halo2TurretSeatNow())
            return true;
        return false;
    }
} // namespace game_core

// The yaw references the aim servo compares against. Halo 4 and Reach
// keep theirs inside their per-level camera cores rather than the shared globals:
// the core is installed and removed per level, so a shared reference would
// survive a load it should not. Returns false (with the block reason the servo
// reports) when the title's snapshot is invalid.
struct AimYawReferences
{
    float game = 0.0f;
    float head = 0.0f;
    float halo4Forward[3]{};
    bool halo4 = false;
    int blockReason = 0;
    const char* blockText = "";
};

static bool ReadAimYawReferences(AimYawReferences& refs)
{
    if (TitleAdapter_GetActiveTitle() == GameTitle::Halo4)
    {
        if (!Halo4ReadAimReferences(refs.game, refs.head, refs.halo4Forward))
        {
            refs.blockReason = 8;
            refs.blockText = "Halo 4 yaw/aim reference snapshot invalid";
            return false;
        }
        refs.halo4 = true;
        return true;
    }
    if (TitleAdapter_GetActiveTitle() == GameTitle::HaloReach)
    {
        if (!ReachReadYawReferencePair(refs.game, refs.head))
        {
            refs.blockReason = 7;
            refs.blockText = "Reach yaw reference snapshot invalid";
            return false;
        }
        return true;
    }
    refs.game = g_gameYawRef;
    refs.head = g_headYawRef;
    return true;
}

// Vehicle seats whose steering is authored openly instead of by the aim servo.
// Returns true when outRx (and possibly outRy) now hold the steering command.
//  Halo 3: with the view reference following the hull, a look-steered
//   driver's yaw error no longer depends on the heading, so the wheel steers
//   while both hands hold it (and aims nothing), otherwise Halo's own turn stick.
//  Reach: a follow-off ground driver's hand names a heading and the loop
//   closes on the hull's own published forward (an earlier camera loop "slid
//   everywhere"); deflection is continuous out of the deadband so a small hand
//   tremor never becomes a steering step.
static bool TitleVehicleSteerStick(float /*desiredYaw*/, float aimYaw, float& outRx, float& outRy)
{
    if (SeatAuthorsSteeringNow())
    {
        if (Game_Halo3VehicleWheelActive())
        {
            const float steer = Clamp(g_halo3SteerAxis.load(std::memory_order_relaxed), -1.0f, 1.0f);
            outRx = steer;
            // Direct angle: hold the look `steer * ratio` off the hull's nose.
            // Without a fresh hull heading it stays on turn rate.
            float hullYaw = 0.0f, hullPitch = 0.0f;
            if (g_config.vehicle_wheel_feel == vehicle_wheel::DirectAngle &&
                Halo3ReadRollStableFollow(hullYaw, hullPitch))
                outRx = vehicle_wheel::DirectAngleStick(steer, hullYaw, aimYaw, g_config.vehicle_wheel_ratio_deg);
            outRy = 0.0f; // both hands are on the wheel, not aiming
        }
        else
        {
            VrPadState pad;
            VR_GetPadState(pad);
            outRx = (pad.valid && fabsf(pad.turnX) > 0.15f) ? Clamp(pad.turnX, -1.0f, 1.0f) : 0.0f;
        }
        return true;
    }
    return false;
}

void Game_PublishVehicleSteerContext(GameTitle title, bool driver, int steerClass, bool armed, bool aircraft)
{
    PublishVehicleSteerContext(title, driver, static_cast<vehicle_wheel::SteerClass>(steerClass), armed, aircraft);
}
void Game_NoteVehicleFollowYaw(float delta)
{
    NoteVehicleFollowYaw(delta);
}

static bool ComputeAimStick(float& outRx, float& outRy);
bool Game_ComputeAimStick(float& outRx, float& outRy)
{
    const bool steered = ComputeAimStick(outRx, outRy);
    if (steered && (outRx != 0.0f || outRy != 0.0f))
        g_aimServoCommandFrames.fetch_add(1, std::memory_order_relaxed); // reported at the next yaw latch
    return steered;
}
void Game_NoteHeadReferenceLatched(GameTitle title, uint32_t generation, const char* path, const float q[4])
{
    if (!q)
        return;
    // Yaw of the head's -Z (forward) about +Y, the convention of the aim servo.
    const float fx = -2.0f * (q[0] * q[2] + q[3] * q[1]);
    const float fz = -(1.0f - 2.0f * (q[0] * q[0] + q[1] * q[1]));
    LOG("yaw reference latched (%s): title %u head reference yaw %.3f deg (generation %u); the view follows the game's live yaw",
        path, static_cast<unsigned>(title), atan2f(fx, -fz) * 57.2957795f, generation);
}
// Game_ComputeAimStick's body (the counting wrapper is above).
static bool ComputeAimStick(float& outRx, float& outRy)
{
    // Keep ON-FOOT hand-driven turning off (rejected in headset testing). A newly verified
    // native seated-local-unit sample admits the normal controller stick loop
    // only in vehicles/turrets, matching the established H3 control behavior.
    bool halo2VehicleAim = false;
    halo2VehicleAim = TitleAdapter_GetActiveTitle() == GameTitle::Halo2 && Halo2Observer6Dof_VehicleControlActive();
    if (TitleAdapter_GetActiveTitle() == GameTitle::Halo2)
    {
        if (!halo2VehicleAim || !g_config.vehicle_motion)
        {
            ClearAimClamped();
            return false;
        }
    }
    if (!Game_HasTitleCapability(TitleCapability_ControllerAim) && !ReachControllerAimActive())
    {
        ClearAimClamped();
        return false;
    }
    // Closed-loop aim: emit a right-stick deflection proportional to the
    // angular error between the game's aim and the controller ray. The game
    // integrates it through its normal turn-rate path, so bullets, reticle
    // logic, vehicles and turrets all behave as if the player aimed manually.
    // Diagnostic: when aim steering is not running, log WHICH precondition
    // failed (once per distinct reason) so a dead aim is explainable from the
    // log alone.
    static std::atomic<int> lastAimBlock{-1};
    auto blocked = [](int reason, const char* what)
    {
        int prev = lastAimBlock.exchange(reason);
        if (prev != reason)
            LOG("diagnostic: aim steering blocked: %s", what);
        ClearAimClamped();
        return false;
    };
    if (VR_IsPausePresentation())
        return blocked(6, "Halo pause presentation active");
    if (!g_vrAim.load())
        return blocked(1, "VR aim toggled OFF (press Insert)");
    if (!g_enabled.load())
        return blocked(2, "head tracking OFF (press F2)");
    const bool halo2Aim = Game_Halo2ControllerAimActive() || halo2VehicleAim;
    if (!g_aimSeen.load() && !halo2Aim)
        return blocked(3, "camera hook not running (not in a level?)");
    float q[4], p[3];
    VrAimSupportReceipt aimSupportReceipt{};
    const bool coherentAim = g_config.two_hand_coherent_aim;
    if (!VR_GetAimPoseWithSupportProvenance(q, p, aimSupportReceipt, coherentAim))
        return blocked(4, "right controller not tracked");
    // While the turret handles are held, they aim the gun. The rate-capped
    // closed loop, rest hysteresis and barrel-riding reticle below are unchanged,
    // so the engine's traverse limits still apply and stay visible.
    if (g_turretHandleAiming.load(std::memory_order_acquire) && OccupiedTurretSeat())
    {
        // The ray starts at the handle-bar centre, not the controller.
        for (int i = 0; i < 4; ++i)
            q[i] = g_turretHandleQ[i].load(std::memory_order_relaxed);
        for (int i = 0; i < 3; ++i)
            p[i] = g_turretHandleP[i].load(std::memory_order_relaxed);
    }
    // While the flight stick is held in an aircraft driver seat, its
    // deflection is the right stick (Halo flies aircraft by look rate).
    if (g_flightStickFlying.load(std::memory_order_acquire) && FlightStickSeat())
    {
        outRx = g_flightStickRx.load(std::memory_order_relaxed);
        outRy = g_flightStickRy.load(std::memory_order_relaxed);
        lastAimBlock = 0;
        return true;
    }
    float hq[4], hp[3];
    if (coherentAim)
    {
        if (!aimSupportReceipt.headValid)
            return blocked(5, "headset not tracked");
        hp[0] = aimSupportReceipt.headPosition.x;
        hp[1] = aimSupportReceipt.headPosition.y;
        hp[2] = aimSupportReceipt.headPosition.z;
        hq[0] = aimSupportReceipt.headOrientation.x;
        hq[1] = aimSupportReceipt.headOrientation.y;
        hq[2] = aimSupportReceipt.headOrientation.z;
        hq[3] = aimSupportReceipt.headOrientation.w;
    }
    else if (!VR_GetHeadPose(hq, hp))
    {
        return blocked(5, "headset not tracked");
    }
    // Halo 2 driver authoring its steering (wheel / turn stick): the shared
    // steering owner, never the hand-aim servo below.
    if (halo2Aim && Halo2SeatAuthorsSteeringNow() && TitleVehicleSteerStick(0.0f, 0.0f, outRx, outRy))
    {
        lastAimBlock = 0;
        return true;
    }
    if (halo2Aim)
        return ComputeHalo2ControllerAimStick(q, p, hq, hp, outRx, outRy);
    lastAimBlock = 0;

    // Controller forward from the shared, mount-calibrated aim pose. The
    // visible weapon, muzzle, authored reticle and bullets all consume this
    // same corrected controller-local ray.
    const float localDir[3] = {0.0f, 0.0f, -1.0f};
    float f3[3];
    RotateByQuat(q, localDir, f3);
    // ODST drop pod: its look-at-the-lights steps test the native aim, so the
    // aim follows the HEAD there (ray from the head, along the head's view).
    // Stock controller aim everywhere else, and as soon as the seat is left.
    if (OdstDropPodHeadAimActive())
    {
        RotateByQuat(hq, localDir, f3);
        p[0] = hp[0];
        p[1] = hp[1];
        p[2] = hp[2];
    }

    // Halo spawns first-person projectiles at the ENGINE's camera - on foot,
    // the head - and no steering can move that origin. Aiming the bullet ray
    // PARALLEL to the hand ray therefore leaves a permanent head-to-hand
    // parallax miss (the 07-15 report "bullets shoot from my head"). Instead
    // steer the head-origin ray through the point the hand ray reaches at the
    // crosshair distance: every shot then passes exactly through the floating
    // reticle, and beyond it the two rays are effectively identical.
    //
    // `hp` is the ROOM head, which maps onto the engine's camera only while the
    // two are the same point. In a first-person vehicle seat they are not - see
    // the seat re-origin below, which is the correction for that case.
    const float d = Clamp(g_config.crosshair_distance_m, 2.0f, 50.0f);
    const AimServoParallaxRay parallaxRay = AimServoParallaxRayFromHead(p, f3, hp, d);
    const float tx = parallaxRay.x;
    const float ty = parallaxRay.y;
    const float tz = parallaxRay.z;
    const float tl = parallaxRay.distance;
    const float cy = atan2f(tx, -tz);
    const float cp = asinf(Clamp(ty, -1.0f, 1.0f));
    AimYawReferences aimReferences;
    if (!ReadAimYawReferences(aimReferences))
        return blocked(aimReferences.blockReason, aimReferences.blockText);
    const float gameYawReference = aimReferences.game;
    const float headYawReference = aimReferences.head;
    const float* const halo4AimForward = aimReferences.halo4Forward;
    const bool halo4Aim = aimReferences.halo4;
    float desiredYaw = gameYawReference + g_yawSign.load() * WrapPi(cy - headYawReference);
    float desiredPitch = Clamp(g_pitchSign.load() * cp, -1.45f, 1.45f);
    // Every block from here to the servo reads HALO 3 vehicle state
    // (roll-stable follow, occupied seat flags, turret seats). MCC keeps every
    // title's module loaded and reloads them all on each menu return, so that
    // state is not guaranteed to be clean just because Halo 4 is the active
    // title - and a stale seat would silently re-origin Halo 4's aim. Halo 4
    // has no vehicle work at all yet, so it takes the plain on-foot path.
    float hullYaw = 0.0f, hullPitch = 0.0f, followedAim[9];
    if (!halo4Aim && Halo3ReadRollStableFollow(hullYaw, hullPitch) &&
        Halo3ComposeFollowBasisWithRoll(hullYaw, hullPitch, gameYawReference,
            g_yawSign.load() * WrapPi(cy - headYawReference), Clamp(g_pitchSign.load() * cp, -1.45f, 1.45f), 0.0f,
            followedAim))
    {
        // Controller ray, reticle and bullets share the same pitched vehicle
        // frame as the view and visible hands. Aircraft seats never publish
        // this frame, retaining their proven independent pitch loop.
        desiredYaw = atan2f(followedAim[1], followedAim[0]);
        desiredPitch = asinf(Clamp(followedAim[2], -1.0f, 1.0f));
    }

    // ODST and Reach need no correction here: each has a shot-origin hook that
    // substitutes our rendered eye into (unit_get_camera_position, aiming
    // vector) on the firing call site alone, so their shot line and sight line
    // are the SAME line at every distance, and re-aiming here would
    // double-correct.
    //
    // HALO 3 HAS NO SUCH HOOK. It was built for ODST and Reach,
    // and never ported. The correction below is ODST's, which was written for
    // exactly this defect and deliberately gated "ODST only ... Halo 3's
    // first-person vehicles are the accepted baseline, which this must not
    // disturb. Halo 3 can be brought in later on its own evidence." This is
    // that later: headset testing found Halo 3 passenger shots not following the
    // crosshair, and ODST's own measurement predicts it exactly - the Warthog
    // passenger's shipped trims of +0.55 m up and -0.10 m lateral put the
    // engine's eye 0.56 m from the rendered one, and the loop above aims from
    // the RENDERED eye through the reticle, so the shot leaves the engine's eye
    // PARALLEL to the sight line. Parallel lines never converge: that is a
    // fixed 0.56 m low-and-right miss at every range, which no crosshair
    // distance can tune out. The driver ships 0.00 on both axes and so shows
    // nothing, which is why only the passenger was ever reported.
    //
    // Self-neutralising: when the two camera points coincide - on foot, or in a
    // seat with no trim - the correction is exactly zero. Gated to a seat whose
    // own loaded tag sets `allows weapons`, because a driver's or mounted
    // gunner's shots leave a vehicle barrel rather than the occupant's eye.
    if (!halo4Aim && Halo3SeatFiresPersonalWeapon(Halo3OccupiedSeatFlags()) && tl > 1e-3f &&
        g_camValid.load(std::memory_order_acquire) && g_baseCamValid.load(std::memory_order_acquire))
    {
        const float offsetX = g_camX.load() - g_baseCamX.load();
        const float offsetY = g_camY.load() - g_baseCamY.load();
        const float offsetZ = g_camZ.load() - g_baseCamZ.load();
        // The reticle floats `tl` metres from the rendered eye along the
        // desired direction; put that same point in world units and aim at it
        // from where the engine actually shoots.
        const float range = tl * g_worldScale.load(std::memory_order_relaxed);
        const float cosDesiredPitch = cosf(desiredPitch);
        const float toReticleX = cosDesiredPitch * cosf(desiredYaw) * range + offsetX;
        const float toReticleY = cosDesiredPitch * sinf(desiredYaw) * range + offsetY;
        const float toReticleZ = sinf(desiredPitch) * range + offsetZ;
        const float reticleLength = sqrtf(toReticleX * toReticleX + toReticleY * toReticleY + toReticleZ * toReticleZ);
        if (std::isfinite(reticleLength) && reticleLength > 1e-3f)
        {
            desiredYaw = atan2f(toReticleY, toReticleX);
            desiredPitch = asinf(Clamp(toReticleZ / reticleLength, -1.0f, 1.0f));
            g_halo3SeatAimReOrigins.fetch_add(1, std::memory_order_relaxed);
        }
    }

    float aimForward[3] = {g_aimFwdX.load(), g_aimFwdY.load(), g_aimFwdZ.load()};
    if (halo4Aim)
    {
        // Halo 4's feedback is the observer camera the stereo transaction
        // reads, published whole with the yaw reference above. That IS the ray
        // Halo 4 spawns first-person shots along, so closing the loop on it
        // puts the shots on the hand ray.
        memcpy(aimForward, halo4AimForward, sizeof(aimForward));
    }
    if (ReachControllerAimActive() && ReachVehicleFpActive())
    {
        float coherentReachAim[3]{};
        if (ReachReadAimFeedback(coherentReachAim))
        {
            memcpy(aimForward, coherentReachAim, sizeof(aimForward));
        }
        else
        {
            // The old compact-camera publication is the frame-local fallback.
            // Count it for the worker; never log or touch title state here.
            g_reachAimFeedbackReadFallbacks.fetch_add(1, std::memory_order_relaxed);
        }
    }
    const float ax = aimForward[0], ay = aimForward[1], az = aimForward[2];
    const float aimYaw = atan2f(ay, ax);
    const float aimPitch = asinf(Clamp(az, -1.0f, 1.0f));

    const float errYaw = WrapPi(desiredYaw - aimYaw);
    const float errPitch = desiredPitch - aimPitch;
    g_servoDesiredPitch.store(desiredPitch, std::memory_order_relaxed);
    g_servoAimPitch.store(aimPitch, std::memory_order_relaxed);
    g_servoHandReferenceYaw.store(WrapPi(cy - headYawReference), std::memory_order_relaxed);

    // A turret's gun is a rate-capped machine, not the occupant's weapon.
    // Both corrections below are scoped to exactly this seat. Halo 4 is
    // on-foot only at this stage, and its seat state would be another title's.
    const bool turretSeat = !halo4Aim && OccupiedTurretSeat();

    // A passenger's weapon is bounded to a cone around the VEHICLE, not the
    // world: the ODST/Halo 3 warthog rider seat authors yaw -90..+90 and pitch
    // -45..+45. Point a hand outside that cone
    // -- easy with a world-locked view once the hull turns -- and the engine
    // simply refuses to aim there. The closed loop then holds full deflection
    // forever while the reticle keeps promising a shot that lands somewhere
    // else, which is the reported "shots don't follow the crosshair".
    //
    // Nothing can make the gun exceed the seat's limit, so make the crosshair
    // honest instead: once the error has stalled well past normal tracking lag,
    // publish the game's REAL aim so the reticle sits on the gun. The reticle
    // is documented as the truth; while clamped it is the only thing that is.
    {
        static uint64_t stalledSinceMs = 0;
        static float previousErrorMagnitude = 0.0f;
        const float errorMagnitude = sqrtf(errYaw * errYaw + errPitch * errPitch);
        const uint64_t nowMs = GetTickCount64();
        const bool seated = !halo4Aim && (Halo3VehicleFpActive() || OdstVehicleFpActive() || ReachVehicleFpActive());
        // ~7 degrees is far beyond the loop's ordinary lag, and a shrinking
        // error means it is still converging, so neither counts as clamped.
        constexpr float kAimStallRadians = 0.12f;
        constexpr float kAimConvergingRadians = 0.005f;
        // A Halo 3 seat that fires the occupant's OWN weapon reports the
        // truth as soon as the error stalls, instead of after a further
        // quarter second. Headset testing found the passenger's shots stay
        // misaligned even with the shot re-origin running (the log measured
        // 55,572 corrected aim frames), and a quarter second at 120 fps is
        // thirty frames in which the reticle keeps promising an angle the seat
        // will never reach. The design decision is that limiting the
        // character's rotation to make the gun agree is acceptable; this is
        // the honest half of that - the crosshair stops leading the gun. Every
        // other seat, title and on-foot case keeps the original 250 ms.
        const uint64_t stallMs = (!halo4Aim && Halo3SeatFiresPersonalWeapon(Halo3OccupiedSeatFlags())) ? 0u : 250u;
        if (!seated || errorMagnitude < kAimStallRadians ||
            errorMagnitude < previousErrorMagnitude - kAimConvergingRadians)
            stalledSinceMs = 0;
        else if (!stalledSinceMs)
            stalledSinceMs = nowMs;
        previousErrorMagnitude = errorMagnitude;
        // A turret reports the truth ALWAYS, never on the stall timer. Its
        // barrel is capped by authored seat data - 15 deg/s on both axes for
        // the shade, so a hand flick outruns it for seconds - and a gun that
        // is slowly converging is by definition not stalled, which reset the
        // timer every single frame and left the reticle promising the hand's
        // direction the whole time. That is the reported "the shots don't
        // follow the covenant turret proper": the shots leave the BARREL, so
        // the only reticle that can be honest about them is the one riding it.
        if (turretSeat ? kAimServoTurretReticleRidesBarrel : (stalledSinceMs && nowMs - stalledSinceMs >= stallMs))
        {
            // Invert the same mapping the desired angles came through, so the
            // published direction is the engine's aim expressed in VR space.
            const float vrYaw = headYawReference + g_yawSign.load() * WrapPi(aimYaw - gameYawReference);
            const float vrPitch = g_pitchSign.load() * aimPitch;
            PublishAimClamped(vrYaw, vrPitch, turretSeat);
        }
        else
        {
            ClearAimClamped();
        }
    }

    // Full deflection at ~4.8 deg of error (was ~10; user: vertical follow too
    // slow). The ceiling is the game's own turn rate - raising in-game look
    // sensitivity raises it further.
    const float k = 12.0f;

    // A turret seat parks instead of hunting. ToRawStick floors every non-zero
    // command at 27.5% deflection so it clears MCC's inner deadzone, so the
    // engine only ever hears "stop" or "at least 27.5%" - the proportional
    // form has no small correction to give, and on the Warthog turret's
    // UNCAPPED pitch axis that minimum step walks straight past the target,
    // flips the error, and drives back. That is the "wiggle like crazy up and
    // down"; yaw stays smooth only because its authored 60 deg/s cap absorbs
    // the same step. Hysteresis is what a quantised actuator needs: once the
    // gun is within a third of a degree the loop commands exactly zero, and it
    // takes authority again only past a band that the measured step widens.
    static AimServoAxis servoPitch, servoYaw;
    static float servoLastPitchCommand = 0.0f;
    static float servoLastYawCommand = 0.0f;
    static bool servoWasTurret = false;
    static uint32_t servoFrames = 0, servoParkedFrames = 0;
    if (turretSeat)
    {
        if (!servoWasTurret)
        {
            AimServoReset(servoPitch);
            AimServoReset(servoYaw);
            servoLastPitchCommand = 0.0f;
            servoLastYawCommand = 0.0f;
            servoFrames = 0;
            servoParkedFrames = 0;
            LOG("Turret seat: aim servo armed. The gun is a rate-capped "
                "machine (H3EK: shade 15 deg/s both axes, warthog turret "
                "60 deg/s yaw and NO pitch cap), so the loop parks instead "
                "of hunting and the reticle rides the barrel");
        }
        AimServoObserve(servoPitch, aimPitch, servoLastPitchCommand);
        AimServoObserve(servoYaw, aimYaw, servoLastYawCommand);
        ++servoFrames;
    }
    else if (servoWasTurret)
    {
        LOG("Turret seat: aim servo released after %u frames, %u parked "
            "(%u%%); measured stick quantum pitch %.4f rad, yaw %.4f rad",
            servoFrames, servoParkedFrames, servoFrames ? servoParkedFrames * 100u / servoFrames : 0u,
            servoPitch.minStep, servoYaw.minStep);
        AimServoReset(servoPitch);
        AimServoReset(servoYaw);
        servoLastPitchCommand = 0.0f;
        servoLastYawCommand = 0.0f;
    }
    servoWasTurret = turretSeat;

    outRy = turretSeat ? AimServoCommand(servoPitch, errPitch, k, kAimServoRestEnterRadians, kAimServoRestExitRadians)
                       : Clamp(errPitch * k, -1.0f, 1.0f);

    // With the view reference following the hull, a look-steered driver's
    // yaw error stops depending on the vehicle's heading - the closed loop has
    // no feedback left there, and a fixed hand offset would become a constant
    // turn RATE instead of a heading. That seat's steering is authored openly
    // instead: the wheel while both hands hold it, and Halo's own turn stick
    // when they do not. A Banshee/Hornet driver publishes no vehicle pitch
    // frame, so its proven climb/dive loop still follows the aiming hand.
    if (TitleVehicleSteerStick(desiredYaw, aimYaw, outRx, outRy))
        return true;
    outRx = turretSeat ? -AimServoCommand(servoYaw, errYaw, k, kAimServoRestEnterRadians, kAimServoRestExitRadians)
                       : Clamp(-errYaw * k, -1.0f, 1.0f);
    const bool servoParked = turretSeat && outRx == 0.0f && outRy == 0.0f;
    // Turret gyro: feed the hull's own angular rate forward so a mounted gun
    // holds its world aim through bumps and turns instead of being dragged and
    // corrected afterwards. Reach mounted guns; Halo 3 / ODST mounted gunners.
    {
        static turret_gyro::Gyro gyro;
        static uint64_t lastGyroLogMs = 0;
        turret_gyro::Hull hull{};
        turret_gyro::Rates rates = turret_gyro::kDefaultRates;
        const GameTitle gyroTitle = TitleAdapter_GetActiveTitle();
        bool source = false;
        if (!halo4Aim && gyroTitle == GameTitle::HaloReach)
        {
            ReachVehicleId gunId = ReachVehicleId::Unknown;
            source = ReachReadGyroHull(hull, gunId);
            rates = turret_gyro::ReachRatesFor(gunId, g_config.turret_gyro_rate_scale);
        }
        else if (!halo4Aim && turretSeat && gyroTitle == GameTitle::Halo3 && Halo3VehicleFpActive())
        {
            int id = 0;
            source = LegacyReadGyroHull(g_halo3VehicleTransform, hull, id);
            rates = turret_gyro::LegacyRatesFor(id, g_config.turret_gyro_rate_scale);
        }
        else if (!halo4Aim && turretSeat && gyroTitle == GameTitle::Halo3ODST && OdstVehicleFpActive())
        {
            int id = 0;
            source = LegacyReadGyroHull(g_odstVehicleTransform, hull, id);
            rates = turret_gyro::LegacyRatesFor(id, g_config.turret_gyro_rate_scale);
        }
        turret_gyro::Settings gs{};
        gs.strength = g_config.turret_gyro_strength;
        gs.rateYaw = rates.yaw;
        gs.ratePitch = rates.pitch;
        const uint64_t gyroNow = GetTickCount64();
        const turret_gyro::Output g = gyro.Update(g_config.vehicle_turret_gyro && source, hull, aimYaw, gyroNow, gs);
        if (g.active)
        {
            outRx = Clamp(outRx + g.rx, -1.0f, 1.0f);
            outRy = Clamp(outRy + g.ry, -1.0f, 1.0f);
            if (gyroNow - lastGyroLogMs >= 5000)
            {
                LOG("turret gyro: title=%d hull dps=(%.1f,%.1f) comp=(%.2f,%.2f)", static_cast<int>(gyroTitle),
                    g.pitchDps, g.yawDps, g.rx, g.ry);
                lastGyroLogMs = gyroNow;
            }
        }
    }
    // What the engine is about to act on (servo plus gyro), so the next
    // frame's quantum measurement only counts motion this loop asked for.
    servoLastPitchCommand = fabsf(outRy);
    servoLastYawCommand = fabsf(outRx);
    if (OdstDropPodHeadAimActive())
    {
        // Diagnostic: which way the servo pushed the stick in the pod.
        if (outRy > 1e-3f)
            g_odstPodServoFrames[0].fetch_add(1, std::memory_order_relaxed);
        if (outRy < -1e-3f)
            g_odstPodServoFrames[1].fetch_add(1, std::memory_order_relaxed);
        if (outRx < -1e-3f)
            g_odstPodServoFrames[2].fetch_add(1, std::memory_order_relaxed);
        if (outRx > 1e-3f)
            g_odstPodServoFrames[3].fetch_add(1, std::memory_order_relaxed);
    }
    if (servoParked)
        ++servoParkedFrames;
    return true;
}

// True while a seat's angular limit is holding the weapon short of the hand's
// direction. `outDir` then carries the engine's ACTUAL aim as a VR-space unit
// vector, in the same convention the reticle uses (yaw about +Y, -Z forward).
bool Game_GetClampedAimDirection(float outDir[3])
{
    if (!outDir || !g_aimClampedValid.load(std::memory_order_acquire))
        return false;
    if (!aim_servo::ClampedAimCurrent(g_aimClampedAtMs.load(std::memory_order_relaxed), GetTickCount64(),
            g_aimClampedTurret.load(std::memory_order_relaxed) != 0, [] { return OccupiedTurretSeat(); }))
        return false;
    if (TitleAdapter_GetActiveTitle() == GameTitle::HaloReach)
    {
        ReachShotOccupationSnapshot occupation{};
        const uint32_t generation = g_reachCamera.generation.load(std::memory_order_acquire);
        const bool enteringVehicle =
            ReachReadShotOccupation(occupation) && occupation.active && occupation.generation == generation;
        if (ReachVehicleFpActive() || enteringVehicle)
        {
            // Reach makes the presented controller reticle the central shot
            // line for every local vehicle weapon, including entry debounce.
            // Never replace it with the occupant unit's unrelated clamped aim.
            return false;
        }
    }
    const float yaw = g_aimClampedVrYaw.load(std::memory_order_relaxed);
    const float pitch = g_aimClampedVrPitch.load(std::memory_order_relaxed);
    if (!std::isfinite(yaw) || !std::isfinite(pitch))
        return false;
    const float cp = cosf(pitch);
    outDir[0] = sinf(yaw) * cp;
    outDir[1] = sinf(pitch);
    outDir[2] = -cosf(yaw) * cp;
    return true;
}

void Game_MapMoveStick(float& mx, float& my)
{
    // ODST has its own generation/liveness-checked seat state. The shared
    // H3 movement branch below cannot detect it through Halo3VehicleFpActive.
    // A seated stick is throttle/steering, not head-relative walking.
    if (TitleAdapter_GetActiveTitle() == GameTitle::Halo3ODST && OdstVehicleFpActive())
        return;
    if (TitleAdapter_GetActiveTitle() == GameTitle::HaloCE)
    {
        float x{}, y{};
        if (Game_CeControllerFeaturesRequested() && HaloCEControls_MapMoveStick(mx, my, x, y))
        {
            mx = x;
            my = y;
        }
        return;
    }
    if (TitleAdapter_GetActiveTitle() == GameTitle::HaloReach)
    {
        // Reach has no controller aim: its stock movement heading is frozen (the
        // right stick is suppressed and nothing steers the sim camera), while the
        // render view follows the head + VR turn. Rotate the move vector by
        // (gaze - stock heading) so forward walks where you look. This mirrors the
        // Halo 3 rotation below with aimYaw sourced from the published stock
        // camera facing instead of g_aimFwd. Self-neutralizing: once a real Reach
        // body-heading integration lands and the stock heading tracks gaze, the
        // delta goes to zero and this becomes a no-op.
        if (ReachVehicleFpActive())
            return;
        if (!g_enabled.load() || !g_vrAim.load() || !g_reachMoveHeadingValid.load(std::memory_order_acquire))
            return;
        float q[4], p[3];
        if (!VR_GetHeadPose(q, p))
            return;
        const float x = q[0], y = q[1], z = q[2], w = q[3];
        const float fx = -2.0f * (w * y + x * z);
        const float fz = -(1.0f - 2.0f * (x * x + y * y));
        const float hy = atan2f(fx, -fz);
        float gameYawReference = 0.0f;
        float headYawReference = 0.0f;
        if (!ReachReadYawReferencePair(gameYawReference, headYawReference))
            return;
        const float gaze = gameYawReference + g_yawSign.load() * WrapPi(hy - headYawReference);
        const float stockYaw = g_reachStockHeadingYaw.load(std::memory_order_relaxed);
        const float delta = WrapPi(gaze - stockYaw);
        const float c = cosf(delta), s = sinf(delta);
        const float nx = mx * c - my * s;
        const float ny = mx * s + my * c;
        mx = nx;
        my = ny;
        return;
    }
    // Halo 4 needs its own branch before the Halo 3 rotation below,
    // because that rotation reads g_gameYawRef/g_headYawRef/g_aimFwd - Halo 3's
    // globals, which hold nothing meaningful during a Halo 4 level. Publishing
    // ControllerAim for Halo 4 without this would have rotated every move by a
    // stale angle. Same construction, Halo 4's own references.
    if (TitleAdapter_GetActiveTitle() == GameTitle::Halo4)
    {
        if (!Halo4ControllerAimActive())
            return;
        Halo4VehicleInputState seat{};
        if (Halo4ReadVehicleInput(seat) && seat.seated)
            return;
        float gameYawReference = 0.0f;
        float headYawReference = 0.0f;
        float aimForward[3]{};
        if (!Halo4ReadAimReferences(gameYawReference, headYawReference, aimForward))
            return;
        float q[4], p[3];
        if (!VR_GetHeadPose(q, p))
            return;
        const float x = q[0], y = q[1], z = q[2], w = q[3];
        const float fx = -2.0f * (w * y + x * z);
        const float fz = -(1.0f - 2.0f * (x * x + y * y));
        const float hy = atan2f(fx, -fz);
        const float gaze = gameYawReference + g_yawSign.load() * WrapPi(hy - headYawReference);
        const float aimYaw = atan2f(aimForward[1], aimForward[0]);
        const float delta = WrapPi(gaze - aimYaw);
        const float c = cosf(delta), s = sinf(delta);
        const float nx = mx * c - my * s;
        const float ny = mx * s + my * c;
        mx = nx;
        my = ny;
        return;
    }
    // Halo 2 walks relative to the body's facing (the engine yaw the
    // right stick turns) while the view adds the head's yaw on top. Rotate
    // the move vector by that head yaw so forward goes where you look. The
    // observer publishes both cameras every frame; without a publication the
    // stick passes through unrotated.
    if (TitleAdapter_GetActiveTitle() == GameTitle::Halo2)
    {
        // A seated stick controls native throttle/strafe, not head-relative
        // walking. Looking across the cockpit must not rotate that command.
        if (Halo2Observer6Dof_VehicleControlActive())
            return;
        Halo2ObserverPosePublication publication{};
        float delta = 0.0f;
        if (!Halo2Observer6Dof_Armed() || !g_enabled.load() || !Halo2Observer6Dof_ReadPublishedPose(publication) ||
            !publication.serial || !Halo2HeadYawDeltaRadians(publication.stock, publication.tracked, delta))
        {
            return;
        }
        const float c = cosf(delta), s = sinf(delta);
        const float nx = mx * c - my * s;
        const float ny = mx * s + my * c;
        mx = nx;
        my = ny;
        return;
    }
    if (!Game_HasTitleCapability(TitleCapability_ControllerAim))
        return;
    // In a first-person vehicle seat the left stick is not locomotion -
    // it is throttle, and on a Scorpion or Wraith it is the hull's own
    // steering. Rotating it by (view - aim) made "forward" diagonal the moment
    // the player looked away from the nose, and on those two the aim is the
    // turret rather than the hull. Hand it to the game unrotated.
    if (Halo3VehicleFpActive())
        return;
    // The game moves relative to its aim heading, which VR aim points at the
    // hand. Rotate the move vector by (head - aim) yaw so pushing forward
    // walks where you look instead of where the gun points.
    if (!g_enabled.load() || !g_aimSeen.load() || !g_vrAim.load())
        return;
    float q[4], p[3];
    if (!VR_GetHeadPose(q, p))
        return;
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float fx = -2.0f * (w * y + x * z);
    const float fz = -(1.0f - 2.0f * (x * x + y * y));
    const float hy = atan2f(fx, -fz);
    const float headYaw = g_gameYawRef + g_yawSign.load() * WrapPi(hy - g_headYawRef);
    const float aimYaw = atan2f(g_aimFwdY.load(), g_aimFwdX.load());
    const float delta = WrapPi(headYaw - aimYaw);
    const float c = cosf(delta), s = sinf(delta);
    const float nx = mx * c - my * s;
    const float ny = mx * s + my * c;
    mx = nx;
    my = ny;
}

bool Game_MoveStickIsLocomotion()
{
    if (TitleAdapter_GetActiveTitle() == GameTitle::HaloCE)
    {
        HaloCELocalPlayerState player{};
        return HaloCEControls_GetLocalPlayerState(player) && player.hasControlledUnit && !player.nativeInputBlocked &&
               !player.nativeLookBlocked && !player.nativePaused && !player.nativeCinematicFlag && !Menu_IsOpen() &&
               !VR_IsPausePresentation() && !VR_IsPausePresentationTarget() && !VR_IsCutsceneTheaterActive();
    }
    // Only these runtime modes drive the character with the left stick. Every
    // other mode (Paused/settings, Shell, Loading, Cutscene, Dead, Unsupported)
    // means the game is reading the stick for menu navigation, so the input
    // hook must not rotate it head-relative or floor its axes past the deadzone.
    // Halo 3 and ODST both drive RuntimeMode, so this is one shared behavior.
    const RuntimeMode mode = TitleAdapter_GetRuntimeMode();
    if (mode == RuntimeMode::Gameplay || mode == RuntimeMode::Vehicle || mode == RuntimeMode::Turret)
        return true;
    // With the Reach camera heartbeat in place, the shared RuntimeMode
    // does hold Gameplay for armed Reach, so the mode check above normally
    // answers. This armed-core fallback stays for the bounded windows where
    // ownership has not resolved yet (first heartbeats after arming, the
    // <=500 ms expiry after a transition) so locomotion does not glitch to
    // raw-stick there. Scoped to Reach; Halo 3/ODST keep the pure mode-driven
    // path unchanged.
    //
    // The fallback must honour native pause, or it reinstates the exact defect
    // the pause work removes: it would answer "locomotion" during the pause
    // menu even while the mode above correctly says Paused, and the menu stick
    // would still be treated as walking. The cache is published by the Present
    // tick (-1 = flag not located, in which case this behaves as it always
    // did).
    if (TitleAdapter_GetActiveTitle() == GameTitle::HaloReach && g_reachCamera.armed.load(std::memory_order_acquire) &&
        g_enabled.load(std::memory_order_acquire))
        return g_reachEnginePauseCache.load(std::memory_order_acquire) != 1;
    return false;
}

void Game_GunScale(int dir)
{
    // Uniform mesh scale of the hand-anchored arms+gun assembly around the
    // wrist. Home = bigger, End = smaller. Persisted next time settings save.
    const float s = Clamp(g_config.gun_scale * (dir > 0 ? 1.05f : 1.0f / 1.05f), 0.3f, 3.0f);
    g_config.gun_scale = s;
    LOG("weapon size %.2fx", s);
}

float Game_GetWorldScale()
{
    return g_worldScale.load();
}

// ---- Shots-vs-reticle frame tranche (read-only evidence) ----
// See game.h. All readers below are bounded, lock-free, finite-checked and
// side-effect-free; they never touch gameplay, tracking, or lifecycle state.
bool Game_TelemetryReadSharedAim(float forwardOut[3]) noexcept
{
    if (!forwardOut || !g_aimSeen.load(std::memory_order_acquire))
        return false;
    // Latest-only three-float publication (H3/ODST/Reach-compact writers, no
    // serial): a concurrent writer can mix components across frames, exactly
    // as the existing in-game consumers accept. Finite-checked at read.
    const float sample[3] = {g_aimFwdX.load(std::memory_order_relaxed), g_aimFwdY.load(std::memory_order_relaxed),
        g_aimFwdZ.load(std::memory_order_relaxed)};
    if (!std::isfinite(sample[0]) || !std::isfinite(sample[1]) || !std::isfinite(sample[2]))
        return false;
    memcpy(forwardOut, sample, sizeof(sample));
    return true;
}

bool Game_TelemetryReadSharedCamera(bool& baseValidOut, float baseOut[3], bool& eyeValidOut, float eyeOut[3]) noexcept
{
    baseValidOut = g_baseCamValid.load(std::memory_order_acquire);
    eyeValidOut = g_camValid.load(std::memory_order_acquire);
    if (!baseOut || !eyeOut)
        return false;
    // Always copied (forensic state travels even when invalid); the flags are
    // authoritative and the recorder zeroes invalid payloads. Shared H3/ODST
    // singletons: the capture site title-qualifies and labels the writer.
    const float base[3] = {g_baseCamX.load(std::memory_order_relaxed), g_baseCamY.load(std::memory_order_relaxed),
        g_baseCamZ.load(std::memory_order_relaxed)};
    const float eye[3] = {g_camX.load(std::memory_order_relaxed), g_camY.load(std::memory_order_relaxed),
        g_camZ.load(std::memory_order_relaxed)};
    memcpy(baseOut, base, sizeof(base));
    memcpy(eyeOut, eye, sizeof(eye));
    return baseValidOut || eyeValidOut;
}

bool Game_TelemetryReadWorldScale(float& scaleOut) noexcept
{
    const float scale = g_worldScale.load(std::memory_order_relaxed);
    // Engine world units per metre (~0.33); the haloce contact bound (>10
    // rejects) is the precedent for the sanity ceiling.
    if (!std::isfinite(scale) || scale <= 0.0f || scale > 10.0f)
        return false;
    scaleOut = scale;
    return true;
}

bool Game_IsPositionalTracking()
{
    return g_positional.load(std::memory_order_relaxed);
}
float Game_GetZoomFactor()
{
    return g_zoomFactor.load();
}

void Game_GetProjectionTangents(float& tanX, float& tanY)
{
    tanX = g_projectionTanX.load();
    tanY = g_projectionTanY.load();
}

bool Game_GetRenderHalfFovs(uint64_t preparedFrameSerial, float halfX[2], float halfY[2])
{
    if (!halfX || !halfY)
        return false;
    const float fallbackX = g_renderHalfFovX.load();
    const float fallbackY = g_renderHalfFovY.load();
    halfX[0] = fallbackX;
    halfX[1] = fallbackX;
    halfY[0] = fallbackY;
    halfY[1] = fallbackY;
    if (TitleAdapter_GetActiveTitle() == GameTitle::Halo2)
    {
        const bool classicArmed = Halo2Stereo_Armed();
        const bool anniversaryArmed = Halo2AnniversaryStereo_Armed();
        const uint32_t generation = classicArmed ? Halo2Stereo_Generation() : Halo2AnniversaryStereo_Generation();
        if (!(classicArmed || anniversaryArmed) || !generation)
            return false;
        // A serial without a pair of its own re-presents the last pair, so
        // the crop must use that pair's cover.
        return VR_Halo2GetSynchronousHalfFovs(generation, preparedFrameSerial, halfX, halfY) ||
               VR_Halo2GetRepeatableHalfFovs(generation, preparedFrameSerial, halfX, halfY);
    }
    if (TitleAdapter_GetActiveTitle() == GameTitle::Halo4)
    {
        if (!g_halo4Camera.armed.load(std::memory_order_acquire) ||
            !Halo4PreparedPairMatches(preparedFrameSerial, g_halo4RenderFovSerial[0].load(std::memory_order_acquire),
                g_halo4RenderFovSerial[1].load(std::memory_order_acquire)))
        {
            return false;
        }
        for (int eye = 0; eye < 2; ++eye)
        {
            halfX[eye] = g_halo4RenderHalfFovX[eye].load(std::memory_order_relaxed);
            halfY[eye] = g_halo4RenderHalfFovY[eye].load(std::memory_order_relaxed);
            if (!isfinite(halfX[eye]) || !isfinite(halfY[eye]) || halfX[eye] <= 0.0f || halfX[eye] >= 1.5707f ||
                halfY[eye] <= 0.0f || halfY[eye] >= 1.5707f)
            {
                return false;
            }
        }
        return true;
    }
    if (TitleAdapter_GetActiveTitle() == GameTitle::HaloReach)
    {
        if (!g_reachCamera.armed.load(std::memory_order_acquire) || !preparedFrameSerial)
        {
            return false;
        }
        const uint64_t leftSerial = g_reachRenderFovSerial[0].load(std::memory_order_acquire);
        const uint64_t rightSerial = g_reachRenderFovSerial[1].load(std::memory_order_acquire);
        if (leftSerial != preparedFrameSerial || rightSerial != preparedFrameSerial)
        {
            return false;
        }
        for (int eye = 0; eye < 2; ++eye)
        {
            halfX[eye] = g_reachRenderHalfFovX[eye].load(std::memory_order_relaxed);
            halfY[eye] = g_reachRenderHalfFovY[eye].load(std::memory_order_relaxed);
            if (!isfinite(halfX[eye]) || !isfinite(halfY[eye]) || halfX[eye] <= 0.0f || halfX[eye] >= 1.5707f ||
                halfY[eye] <= 0.0f || halfY[eye] >= 1.5707f)
            {
                return false;
            }
        }
        return true;
    }
    const uint32_t generation = g_odstRuntimeGeneration.load(std::memory_order_acquire);
    const TitleAdapterRuntimeSnapshot runtime = RuntimeSnapshot(GetTickCount64());
    if (generation && runtime.runtime.owner == GameTitle::Halo3ODST && runtime.runtime.qualifyingOwnerCount == 1 &&
        runtime.runtime.generation == generation && runtime.runtime.installed && runtime.runtime.armed &&
        !runtime.runtime.teardownRequested && (runtime.runtime.enabledCapabilities & TitleCapability_Stereo) != 0)
    {
        halfX[0] = g_odstRenderHalfFovX[0].load(std::memory_order_relaxed);
        halfX[1] = g_odstRenderHalfFovX[1].load(std::memory_order_relaxed);
        halfY[0] = g_odstRenderHalfFovY[0].load(std::memory_order_relaxed);
        halfY[1] = g_odstRenderHalfFovY[1].load(std::memory_order_relaxed);
    }
    return true;
}

void Game_SetStereoEye(int eye)
{
    g_stereoEye = (eye == 0 || eye == 1) ? eye : -1;
}

bool WaitForNativeDetourQuiescence(
    const void* const* functions, const void* const* trampolines, size_t count, const std::atomic<uint32_t>& callbacks)
{
    // This shared worker-side retirement path serves CE's nine control
    // callbacks and Reach's larger grouped hook sets. Keep the bound fixed and
    // stack-backed so teardown never allocates while validating detours.
    if (!functions || !trampolines || !count || count > kNativeDetourRangeCapacity)
        return false;
    ReachDetourCodeRange ranges[kNativeDetourRangeCapacity]{};
    for (size_t i = 0; i < count; ++i)
        if (!functions[i] || !ResolveReachDetourCodeRange(functions[i], ranges[i]))
            return false;
    for (unsigned waited = 0; waited < 2000; ++waited)
    {
        if (!callbacks.load(std::memory_order_acquire))
        {
            ReachThreadFreeze frozen;
            if (!frozen.Capture())
                return false;
            bool busy = false, scanOk = true;
            for (const ReachFrozenThread& thread : frozen.Threads())
            {
                if (!thread.suspended)
                    continue;
                CONTEXT context{};
                context.ContextFlags = CONTEXT_CONTROL;
                if (!GetThreadContext(thread.handle, &context))
                {
                    if (WaitForSingleObject(thread.handle, 0) != WAIT_OBJECT_0)
                        scanOk = false;
                    continue;
                }
                for (size_t i = 0; i < count; ++i)
                {
                    const DWORD64 trampoline = reinterpret_cast<DWORD64>(trampolines[i]);
                    busy |= (context.Rip >= ranges[i].begin && context.Rip < ranges[i].end) ||
                            (trampoline && context.Rip >= trampoline && context.Rip < trampoline + 64);
                }
            }
            busy |= callbacks.load(std::memory_order_acquire) != 0;
            const bool resumed = frozen.Release();
            if (!scanOk || !resumed)
                return false;
            if (!busy)
                return true;
        }
        Sleep(1);
    }
    return false;
}

bool Game_ReadVehicleCameraOwner(GameTitle title, NativeVehicleCameraOwner& owner) noexcept
{
    if (title != TitleAdapter_GetActiveTitle() || !Game_IsHeadTracking() || !VR_IsStereoEnabled() || Menu_IsOpen() ||
        VR_IsPausePresentation() || VR_IsPausePresentationTarget() || VR_IsCutsceneTheaterActive() ||
        TitleAdapter_GetRuntimeMode() != RuntimeMode::Gameplay)
        return false;
    if (title == GameTitle::HaloCE)
        return HaloCEControls_ReadVehicleCameraOwner(owner);
    if (title == GameTitle::Halo2)
        return Halo2Observer6Dof_ReadVehicleCameraOwner(owner);
    if (title != GameTitle::Halo4)
        return false;
    __try
    {
        Halo4VehicleInputState seat{};
        if (!Halo4ReadVehicleInput(seat) || !seat.seated)
            return false;
        const auto** slots = reinterpret_cast<const uint8_t**>(__readgsqword(0x58));
        const auto* tls = slots ? slots[*g_halo4EngineTlsIndex] : nullptr;
        if (!tls)
            return false;
        const auto* table = Halo4VehicleRead<const uint8_t*>(tls, 0x18);
        const auto* unit = Halo4VehicleObject(table, seat.unit, 1);
        const auto* parent = Halo4VehicleObject(table, seat.parent, 2);
        // H4's selector can also force chase for the unit+664 special state.
        if (!unit || !parent || Halo4VehicleRead<int32_t>(unit, 0x664) != -1 ||
            Halo4VehicleRead<uint32_t>(unit, 0x24) != seat.parent || Halo4VehicleRead<int16_t>(unit, 0x2c) != seat.seat)
            return false;
        owner = {TitleAdapter_GetGeneration(title), seat.unit, seat.parent, seat.seat,
            reinterpret_cast<uintptr_t>(unit), Halo4ReadVehicleIdentity(parent)};
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("reading the Halo 4 vehicle camera owner faulted");
        return false;
    }
}

// Called on the title's native weapon/action thread (engine TLS is valid here).
// Unknown owner/cinematic evidence disables this optional feature only.
void* Game_ReloadPolicyWeapon(GameTitle title, uint32_t weapon)
{
    if (weapon == UINT32_MAX || !(weapon >> 16) || title != TitleAdapter_GetActiveTitle() || !g_enabled.load() ||
        !g_vrAim.load() || !Game_IsHeadTracking() || !VR_IsStereoEnabled() || Menu_IsOpen() ||
        VR_IsPausePresentation() || VR_IsPausePresentationTarget() || VR_IsCutsceneTheaterActive() ||
        TitleAdapter_GetRuntimeMode() != RuntimeMode::Gameplay)
        return nullptr;
    __try
    {
        const uint8_t* data = nullptr;
        uint32_t local = UINT32_MAX, owner = UINT32_MAX;
        switch (title)
        {
        case GameTitle::HaloCE:
        {
            HaloCELocalPlayerState state{};
            if (!HaloCEControls_GetLocalPlayerState(state) || state.weapon != weapon || !state.onFoot ||
                !state.nativePreparesFirstPerson || state.nativePaused || state.nativeCinematicFlag ||
                state.nativeInputBlocked || state.nativeLookBlocked)
                return nullptr;
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"halo1.dll"));
            // Controls admission independently verifies this exact accessor.
            using Get = uintptr_t(__fastcall*)(uint32_t, uint32_t);
            return base ? reinterpret_cast<void*>(reinterpret_cast<Get>(
                              base + halo_ce::contract::player_state::state_object_try_get)(weapon, 4))
                        : nullptr;
        }
        case GameTitle::Halo2:
            return Halo2Observer6Dof_ReloadWeapon(weapon);
        case GameTitle::Halo3:
            if (!g_halo3PlayerUnitGetter)
                return nullptr;
            local = g_halo3PlayerUnitGetter(0);
            data = Halo3MeleeSelectionObject(weapon, 2);
            if (data && data[0x15d])
                owner = *reinterpret_cast<const uint32_t*>(data + 0x168);
            break;
        case GameTitle::Halo3ODST:
        {
            if (!g_odstPlayerUnitGetter || !OdstContactObject(weapon))
                return nullptr;
            local = g_odstPlayerUnitGetter(0);
            const auto table = *reinterpret_cast<const uint8_t* const*>(OdstContactTls() + kOdstTlsObjectTableOffset);
            const auto entries = *reinterpret_cast<const uint8_t* const*>(table + 0x48);
            const auto entry = entries + (weapon & 0xffff) * 0x18;
            if (entry[3] != 2)
                return nullptr;
            data = *reinterpret_cast<const uint8_t* const*>(entry + 0x10);
            if (data && data[0x155])
                owner = *reinterpret_cast<const uint32_t*>(data + 0x160);
            break;
        }
        case GameTitle::HaloReach:
        {
            if (!g_reachCamera.playerUnitByOutputUser)
                return nullptr;
            local = g_reachCamera.playerUnitByOutputUser(0);
            uint8_t kind = 0;
            data = ReachVehicleObjectData(weapon, kind);
            if (!data || kind != 2)
                return nullptr;
            owner = *reinterpret_cast<const uint32_t*>(data + 0x32c);
            if (owner == UINT32_MAX && data[0x1a9])
                owner = *reinterpret_cast<const uint32_t*>(data + 0x1b4);
            break;
        }
        case GameTitle::Halo4:
        {
            Halo4FirstPersonAccess fp{};
            (void)Halo4ResolveFirstPerson(0, fp);
            if (!fp.record)
                return nullptr;
            local = *reinterpret_cast<const uint32_t*>(fp.record + kHalo4FirstPersonRecordUnitOffset);
            data = Halo4ContactObject(weapon);
            if (!data)
                return nullptr;
            data = Halo4ContactObjectOfKinds(weapon, 4);
            if (!data)
                return nullptr;
            owner = *reinterpret_cast<const uint32_t*>(data + 0x624);
            if (owner == UINT32_MAX && data[0x471])
                owner = *reinterpret_cast<const uint32_t*>(data + 0x480);
            break;
        }
        default:
            return nullptr;
        }
        return local != UINT32_MAX && (local >> 16) && owner == local ? const_cast<uint8_t*>(data) : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("reading the weapon for the reload policy faulted");
        return nullptr;
    }
}

// ---- Weapon-order diagnostic tranche (read-only evidence) ----
// No gameplay, tracking, aiming, rendering, input, or lifecycle effect.
// detailOut bits shared by every diagnostic reader:
//   bit0  secondary slot present (where the reader naturally knows it)
//   bit1  ownership proven (H2/H4 mirror validation)
//   bit2  FP-side agreement (engine FP record already shows this handle)
//   bit30 lifecycle guard rejection (maps to GuardRejected)
//   bit31 native fault (maps to ExceptionOrFault)
// The Capture probe records the inventory/ownership view (bit2 raw), while
// the FP commit check requires bit2 so it fires only at the existing stable
// commit point where the FP record also agrees. CE is probed directly from
// vr.cpp through HaloCEControls_GetLocalPlayerState; H2 forwards to the
// observer core's guarded datum reader below.
namespace
{

    std::atomic<uint64_t> g_diagnosticFpThread[8]{};
    // Recording session that the FP-thread observation belongs to. A numeric
    // thread id reused by a later session is not authorization until that
    // session observes its own FP entry.
    std::atomic<uint64_t> g_diagnosticFpSession[8]{};
    int DiagnosticTitleSlot(GameTitle title) noexcept
    {
        const int slot = static_cast<int>(title);
        return (slot >= 0 && slot < 8) ? slot : -1;
    }
    // True when the title lifecycle moved after the diagnostic read began:
    // used to classify a native fault that coincides with a reload/teardown
    // as a guard rejection rather than an unexplained fault. Safe to call
    // from an exception handler (atomic loads only).
    bool DiagnosticLifecycleChanged(GameTitle title, uint32_t expectedGeneration) noexcept
    {
        if (title != TitleAdapter_GetActiveTitle() || TitleAdapter_GetGeneration(title) != expectedGeneration)
            return true;
        switch (title)
        {
        case GameTitle::Halo3:
            return g_halo3RuntimeGeneration.load(std::memory_order_acquire) != expectedGeneration;
        case GameTitle::Halo3ODST:
            return g_odstRuntimeGeneration.load(std::memory_order_acquire) != expectedGeneration;
        case GameTitle::HaloReach:
            return g_reachCamera.generation.load(std::memory_order_acquire) != expectedGeneration;
        case GameTitle::Halo4:
            return g_halo4Camera.generation.load(std::memory_order_acquire) != expectedGeneration;
        default:
            return false;
        }
    }
}

void Game_DiagnosticNoteFpThread(GameTitle title) noexcept
{
    const int slot = DiagnosticTitleSlot(title);
    if (slot < 0)
        return;
    const uint32_t generation = TitleAdapter_GetGeneration(title);
    if (!generation)
    {
        g_diagnosticFpThread[slot].store(0, std::memory_order_release);
        g_diagnosticFpSession[slot].store(0, std::memory_order_release);
        return;
    }
    g_diagnosticFpThread[slot].store((uint64_t(generation) << 32) | GetCurrentThreadId(), std::memory_order_release);
    g_diagnosticFpSession[slot].store(Telemetry_CurrentSessionToken(), std::memory_order_release);
}

uint8_t Game_DiagnosticCaptureProbePermission(GameTitle title) noexcept
{
    if (title != GameTitle::Halo3 && title != GameTitle::Halo3ODST && title != GameTitle::HaloReach &&
        title != GameTitle::Halo4)
        return weapon_order_diagnostic::kProbePermissionGuardRejected;
    const int slot = DiagnosticTitleSlot(title);
    if (slot < 0)
        return weapon_order_diagnostic::kProbePermissionGuardRejected;
    const uint64_t stored = g_diagnosticFpThread[slot].load(std::memory_order_acquire);
    const uint64_t session = g_diagnosticFpSession[slot].load(std::memory_order_acquire);
    const bool titleMatches = title == TitleAdapter_GetActiveTitle();
    const uint32_t generation = TitleAdapter_GetGeneration(title);
    const bool generationMatches = generation != 0 && uint32_t(stored >> 32) == generation;
    return weapon_order_diagnostic::CaptureProbePermission(
        titleMatches, generationMatches, stored, session, Telemetry_CurrentSessionToken(), GetCurrentThreadId());
}

bool Game_DiagnosticReadPrimaryWeapon(
    GameTitle title, uint32_t& unitOut, uint32_t& weaponOut, uint32_t& detailOut) noexcept
{
    unitOut = UINT32_MAX;
    weaponOut = UINT32_MAX;
    detailOut = 0;
    if (title != TitleAdapter_GetActiveTitle())
    {
        detailOut = 0x40000000u; // lifecycle guard rejection
        return false;
    }
    const uint32_t generation = TitleAdapter_GetGeneration(title);
    if (!generation)
    {
        detailOut = 0x40000000u;
        return false;
    }
    __try
    {
        switch (title)
        {
        case GameTitle::Halo3:
            return DiagnosticH3Primary(generation, unitOut, weaponOut, detailOut);
        case GameTitle::Halo3ODST:
            return DiagnosticOdstPrimary(generation, unitOut, weaponOut, detailOut);
        case GameTitle::HaloReach:
            return DiagnosticReachPrimary(generation, unitOut, weaponOut, detailOut);
        case GameTitle::Halo4:
            return DiagnosticHalo4WeaponOwner(generation, UINT32_MAX, unitOut, weaponOut, detailOut);
        case GameTitle::Halo2:
            return Halo2DiagnosticReadPrimaryWeapon(unitOut, weaponOut, detailOut);
        default:
            return false;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("the diagnostic primary-weapon read faulted");
        // A fault that coincides with a title/generation change is a
        // lifecycle guard rejection, not an unexplained native fault. The
        // generation recheck is atomic-only and safe inside the handler.
        detailOut = DiagnosticLifecycleChanged(title, generation) ? 0x40000000u : 0x80000000u;
        return false;
    }
}

// ---- Persistent support grip: production owner evidence ----
//
// The tri-state semantic-primary read a title producer publishes to the
// durable relationship input. All six gameplay titles are wired: Halo 3,
// ODST and Reach read their own guarded native state through the shared
// H3/Odst/Reach PrimaryWeaponEvidence bodies, Halo 2 through its own guarded
// direct reader (safe outside the TLS-restricted titles) and Halo 4 through
// Halo4WeaponOwnerEvidence (the body behind DiagnosticHalo4WeaponOwner).
// Each distinguishes an explicit raw primary-slot absence (KnownAbsent) from
// an unprovable read (Unknown). Halo 3's raw slot absence is source-provable
// (role byte 0xFF) while the FP interpolation hook is running; whether the
// hook runs for every unarmed presentation is recorded as an open liveness
// question in the port doc (Halo 2's absence liveness is provided by its
// level-live poll seam, Halo 4 by its per-pair skinning invocation).
bool Game_ReadPrimaryWeaponEvidence(GameTitle title, PrimaryWeaponEvidence& out) noexcept
{
    out = {};
    if (title != TitleAdapter_GetActiveTitle())
        return false;
    const uint32_t generation = TitleAdapter_GetGeneration(title);
    if (!generation)
        return false;
    out.generation = generation;
    if (title == GameTitle::Halo2)
    {
        uint32_t unit = UINT32_MAX, weapon = UINT32_MAX, detail = 0;
        bool absent = false;
        bool ok = false;
        __try
        {
            ok = Halo2DiagnosticReadPrimaryWeapon(unit, weapon, detail, &absent);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading Halo 2's primary weapon evidence faulted");
            out.detail = DiagnosticLifecycleChanged(title, generation) ? 0x40000000u : 0x80000000u;
            out.state = static_cast<uint8_t>(support_grip::OwnerEvidence::Unknown);
            return true;
        }
        out.detail = detail;
        if (ok)
        {
            out.state = static_cast<uint8_t>(support_grip::OwnerEvidence::KnownPresent);
            out.unit = unit;
            out.weapon = weapon;
            return true;
        }
        out.state = static_cast<uint8_t>(
            absent ? support_grip::OwnerEvidence::KnownAbsent : support_grip::OwnerEvidence::Unknown);
        return true;
    }
    if (title == GameTitle::Halo4)
    {
        // Halo 4's own guarded reader: the shared production body behind
        // DiagnosticHalo4WeaponOwner, including the tranche-E first-person
        // record fallback. A validated local on-foot owner is KnownPresent
        // (the shared resolver still requires the FP agreement bit for that
        // claim); an explicit raw empty primary slot is KnownAbsent without
        // it (F03); everything else is Unknown -- never absence. A native
        // fault is Unknown inside Halo4ObservePrimaryWeapon.
        out = Halo4ObservePrimaryWeapon(generation, UINT32_MAX);
        return true;
    }
    if (title != GameTitle::Halo3 && title != GameTitle::Halo3ODST && title != GameTitle::HaloReach)
    {
        out.state = static_cast<uint8_t>(support_grip::OwnerEvidence::Unknown);
        return true;
    }
    uint32_t unit = UINT32_MAX, weapon = UINT32_MAX, detail = 0;
    bool absent = false;
    bool ok = false;
    __try
    {
        // Each wired title keeps its own reader: Halo 3 role bytes +262/+263,
        // ODST +276/+277, Reach +34A/+34B, each with its own FP agreement
        // record. The absence flag comes from the raw primary role byte BEFORE
        // the slot-validation guards (F03): a proven empty slot must resolve
        // without the FP agreement bit, which those readers can never set on
        // that path.
        if (title == GameTitle::Halo3)
            ok = H3PrimaryWeaponEvidence(generation, unit, weapon, detail, &absent);
        else if (title == GameTitle::Halo3ODST)
            ok = OdstPrimaryWeaponEvidence(generation, unit, weapon, detail, &absent);
        else
            ok = ReachPrimaryWeaponEvidence(generation, unit, weapon, detail, &absent);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("reading the primary weapon evidence faulted");
        // A native fault is never proof of absence: report Unknown (the
        // lifecycle classification is diagnostic detail only).
        out.detail = DiagnosticLifecycleChanged(title, generation) ? 0x40000000u : 0x80000000u;
        out.state = static_cast<uint8_t>(support_grip::OwnerEvidence::Unknown);
        return true;
    }
    out.detail = detail;
    if (ok)
    {
        out.state = static_cast<uint8_t>(support_grip::OwnerEvidence::KnownPresent);
        out.unit = unit;
        out.weapon = weapon;
        return true;
    }
    out.state =
        static_cast<uint8_t>(absent ? support_grip::OwnerEvidence::KnownAbsent : support_grip::OwnerEvidence::Unknown);
    return true;
}

// Persistent support grip: one observation of the title's current semantic
// primary published to the durable relationship input, including an explicit
// Unknown record, so a stale KnownPresent must not survive an observation that
// proves nothing. This is the observation path a title can run at a lifecycle
// seam that does not depend on a weapon packet (Halo 2's level-live poll).
// Read-only with respect to the durable relationship beyond the publication
// itself.
void Game_PublishSupportGripOwnerEvidence(GameTitle title) noexcept
{
    PrimaryWeaponEvidence evidence{};
    if (!Game_ReadPrimaryWeaponEvidence(title, evidence))
        evidence = {};
    VR_PublishSupportGripOwnerEvidence(title, evidence.generation,
        support_grip::OwnerTuple{title, evidence.generation, evidence.unit, evidence.weapon},
        static_cast<support_grip::OwnerEvidence>(evidence.state));
}

SupportInvocationResolution Game_ResolveSupportInvocation(GameTitle title, const PrimaryWeaponEvidence& before,
    bool beforeOk, const PrimaryWeaponEvidence& after, bool afterOk, bool requireProducerAgreement) noexcept
{
    SupportInvocationResolution resolution{};
    uint32_t generation = 0;
    uint32_t unit = UINT32_MAX;
    uint32_t weapon = UINT32_MAX;
    support_grip::OwnerEvidence state = support_grip::OwnerEvidence::Unknown;
    if (beforeOk && afterOk && before.generation == after.generation && before.unit == after.unit &&
        before.weapon == after.weapon)
    {
        generation = before.generation;
        // F03: the agreement bit is a present-owner proof. The native readers
        // return before setting it when the primary slot is empty, so an
        // explicit absence must resolve without it; ResolveStableEvidence
        // requires agreement only for a KnownPresent claim.
        const support_grip::OwnerEvidence stable = support_grip::ResolveStableEvidence(requireProducerAgreement,
            before.state, (before.detail & 0x4u) != 0, after.state, (after.detail & 0x4u) != 0);
        if (stable == support_grip::OwnerEvidence::KnownPresent)
        {
            state = support_grip::OwnerEvidence::KnownPresent;
            unit = before.unit;
            weapon = before.weapon;
            resolution.evidencePresent = true;
        }
        else if (stable == support_grip::OwnerEvidence::KnownAbsent)
        {
            state = support_grip::OwnerEvidence::KnownAbsent;
            resolution.evidenceAbsent = true;
        }
    }
    if (!generation && beforeOk)
        generation = before.generation;
    if (!generation && afterOk)
        generation = after.generation;
    // Publish this invocation's evidence (including Unknown) so a stale
    // KnownPresent can never survive an invocation that could not prove
    // itself, and a proven absence reaches the durable writer.
    VR_PublishSupportGripOwnerEvidence(
        title, generation, support_grip::OwnerTuple{title, generation, unit, weapon}, state);
    // F06/F07 material: the resolved owner proof (including Unknown/absence)
    // plus the relationship reading taken at this same instant. A title that
    // publishes an invocation receipt copies these fields; every other caller
    // ignores them.
    resolution.generation = generation;
    resolution.evidence = state;
    resolution.owner = support_grip::OwnerTuple{title, before.generation, before.unit, before.weapon};
    resolution.producerAgreement = (before.detail & 0x4u) != 0 && (after.detail & 0x4u) != 0;
    if (state == support_grip::OwnerEvidence::KnownPresent)
    {
        SupportGripRelationshipSnapshot relationship{};
        if (VR_GetSupportGripRelationship(relationship))
        {
            resolution.relationshipReadable = true;
            resolution.relationshipEngaged = relationship.engaged;
            resolution.relationshipEpoch = relationship.epoch;
            if (support_grip::InvocationOwnerTrusted(relationship.engaged,
                    support_grip::OwnerTuple{
                        relationship.title, relationship.generation, relationship.unit, relationship.weapon},
                    state, support_grip::OwnerTuple{title, before.generation, before.unit, before.weapon}))
                resolution.trusted = true;
        }
    }
    return resolution;
}

bool Game_RoomscaleCameraAllowed(GameTitle title)
{
    if (title != TitleAdapter_GetActiveTitle())
        return false;
    if (!(g_config.roomscale_movement || g_config.physical_running) || !g_enabled.load() || !g_positional.load() ||
        !g_vrAim.load() || !VR_IsStereoEnabled() || Menu_IsOpen() || VR_IsPausePresentation() ||
        VR_IsPausePresentationTarget() || VR_IsCutsceneTheaterActive() ||
        TitleAdapter_GetRuntimeMode() != RuntimeMode::Gameplay)
        return false;
    const auto now = GetTickCount64();
    __try
    {
        switch (title)
        {
        case GameTitle::HaloCE:
        {
            HaloCELocalPlayerState player{};
            return HaloCEControls_GetLocalPlayerState(player) && player.hasControlledUnit && player.onFoot &&
                   player.nativePreparesFirstPerson && !player.nativeInputBlocked && !player.nativeLookBlocked &&
                   !player.nativePaused && !player.nativeCinematicFlag;
        }
        case GameTitle::Halo3:
        {
            int32_t scene = -1, shot = -1;
            if (ReadCinematicControl(scene, shot) != CinematicControlState::PlayerControlled ||
                Game_Halo3VehicleState().state != Halo3VehicleState::OnFoot || !g_halo3PlayerUnitGetter)
                return false;
            const auto* unit = Halo3MeleeSelectionObject(static_cast<uint32_t>(g_halo3PlayerUnitGetter(0)), 0);
            return unit && *reinterpret_cast<const int16_t*>(unit + kHalo3UnitSeatWordOffset) == -1;
        }
        case GameTitle::Halo3ODST:
        {
            int32_t scene = -1, shot = -1;
            const auto at = g_odstRoomscaleOnFootMs.load();
            return at && now >= at && now - at <= 100 &&
                   g_odstVehicleBinding.load() == static_cast<uint8_t>(OdstVehicleBindingState::Installed) &&
                   g_odstVehicleFpStable.load() == static_cast<uint32_t>(Halo3VehicleState::OnFoot) &&
                   ReadOdstCinematicControl(scene, shot) == CinematicControlState::PlayerControlled;
        }
        case GameTitle::HaloReach:
        {
            const auto at = g_reachVehicleSampleMs.load();
            const auto cine = TitleAdapter_GetCinematicControlPublication(title);
            return at && now >= at && now - at <= 100 &&
                   g_reachVehicleInputSnapshot.load() ==
                       ReachVehicleInputSnapshot(TitleAdapter_GetGeneration(title), ReachVehicleInputState::OnFoot) &&
                   cine.generation == TitleAdapter_GetGeneration(title) && cine.heartbeatMs &&
                   now >= cine.heartbeatMs && now - cine.heartbeatMs <= 100 &&
                   cine.state == CinematicControlState::PlayerControlled;
        }
        case GameTitle::Halo2:
            return Halo2Observer6Dof_OnFootFresh() && Halo2Observer6Dof_FinalPaletteArmed();
        case GameTitle::Halo4:
        {
            if (!Halo4ContactMeleeReady() || Halo4ContactGeneration() != g_halo4Camera.generation.load() ||
                ReadHalo4CinematicControl() != CinematicControlState::PlayerControlled)
                return false;
            Halo4FirstPersonAccess fp{};
            // record is validated even for an unarmed player with zero weapon nodes.
            (void)Halo4ResolveFirstPerson(0, fp);
            if (!fp.record)
                return false;
            const auto unit = *reinterpret_cast<const uint32_t*>(fp.record + kHalo4FirstPersonRecordUnitOffset);
            const auto* biped = Halo4ContactBiped(unit);
            // Existing H4EK-proven contact admission: unparented local biped.
            return biped && *reinterpret_cast<const int32_t*>(biped + 0x24) == -1;
        }
        default:
            return false;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("reading the player state for roomscale faulted");
        return false;
    }
}

// See game.h. Game thread.
void Game_FormatShotFrame(GameTitle title, char* out, size_t size)
{
    if (!out || !size)
        return;
    out[0] = 0;
    float p[3]{}, f[3]{}, u[3]{};
    const char* eye = "none";
    __try
    {
        if ((title == GameTitle::Halo3 || title == GameTitle::Halo3ODST) && g_camValid.load())
        {
            for (int i = 0; i < 3; ++i)
            {
                p[i] = g_camX.load();
                f[i] = g_camFwd[i].load();
                u[i] = g_camUp[i].load();
            }
            p[1] = g_camY.load();
            p[2] = g_camZ.load();
            eye = "head camera";
        }
        else if (title == GameTitle::HaloReach)
        {
            for (int attempt = 0; attempt < 4; ++attempt)
            {
                const uint32_t before = g_reachShotHead.sequence.load(std::memory_order_acquire);
                if (!before || (before & 1u))
                    continue;
                for (int i = 0; i < 3; ++i)
                {
                    p[i] = g_reachShotHead.pos[i].load();
                    f[i] = g_reachShotHead.fwd[i].load();
                    u[i] = g_reachShotHead.up[i].load();
                }
                if (g_reachShotHead.sequence.load(std::memory_order_acquire) == before)
                {
                    eye = "head camera";
                    break;
                }
            }
        }
        else if (title == GameTitle::Halo4)
        {
            const Halo4CenterRoot& root = g_halo4EyeRoot[0];
            for (int attempt = 0; attempt < 4; ++attempt)
            {
                const uint32_t before = root.sequence.load(std::memory_order_acquire);
                if (!before || (before & 1u))
                    continue;
                for (int i = 0; i < 3; ++i)
                {
                    p[i] = root.value[i].load(std::memory_order_relaxed);
                    f[i] = root.value[3 + i].load(std::memory_order_relaxed);
                    u[i] = root.value[9 + i].load(std::memory_order_relaxed);
                }
                if (root.sequence.load(std::memory_order_acquire) == before)
                {
                    eye = "left eye";
                    break;
                }
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("formatting the shot frame for the log faulted");
        eye = "none";
    }
    float ro[3]{}, rd[3]{};
    bool barrel = false, held = false;
    uint64_t age = 0;
    const bool reticle = VR_GetLastReticleRay(ro, rd, barrel, held, age);
    std::snprintf(out, size,
        "; %s pos %.3f %.3f %.3f fwd %.4f %.4f %.4f up %.4f %.4f %.4f; reticle %s pos %.3f %.3f %.3f dir %.4f %.4f %.4f",
        eye, p[0], p[1], p[2], f[0], f[1], f[2], u[0], u[1], u[2],
        !reticle ? "none"
        : barrel ? (held ? "barrel/held" : "barrel/current")
                 : "hand",
        ro[0], ro[1], ro[2], rd[0], rd[1], rd[2]);
    (void)age;
}
