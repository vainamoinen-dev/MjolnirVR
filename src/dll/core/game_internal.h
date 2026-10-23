// game_internal.h: the core types, helpers and globals that title code split out of game.cpp uses.
// Included by game.cpp and src/dll/titles/reach/*.cpp. Everything here is in namespace game_core, so a
// title file names exactly what core exports. It must not grow title logic: core state and helpers only.
#pragma once
#include "../../common/legacy_floating_hands_logic.h"
#include "hook_install_check.h" // MinHook create/remove go through the install-time detour check
#include <atomic>
#include "../../common/runtime_types.h"
#include "../../common/weapon_model_observation.h"
#include "../../common/cockpit_screen_logic.h"
#include "../../common/vehicle_monitors_logic.h"
#include "../../common/weapon_frame_transfer.h"
#include "../titles/reach/reach_action_test_guard.h"
#include "../../common/titles/reach/reach_frame_cap_logic.h"
#include "../../common/dlss_logic.h"
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
#include "../../common/holster_logic.h"
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

namespace game_core
{
    enum class VehicleFollowOwner : uint32_t;
    struct Halo3RollStableFollowPublication;
    struct PlayerViewArrayEvidence;
    class PlayerViewLivenessGate;
    struct BoneMatrix;
    struct FpInterpolationContext;
    struct FpExplicitPoseTargets;
    struct LegacyCollisionPublication;
    struct LegacyCollisionCorrection;
    struct LegacyCollisionWorkerHand;
    struct LegacyWorldCollisionFeature;
    struct FpStereoPaletteCache;
    struct FpStereoSolveScope;
    struct MotionBlurVar;
    struct HudLayoutRememberedCache;
    struct Halo3ObjectMarker;
    struct ReachDetourCodeRange;
    struct ReachFrozenThread;
    class ReachThreadFreeze;
    struct TitleHookSupervisorState;
    struct TitleTickContext;

    // ---------------------------------------------------------------------
    // LEVEL-LOAD GATE (2026-08-06). Long-standing bug: after the first level
    // of a session, loading another one sometimes bounces straight back to the
    // main menu. A preserved ODST capture with one successful load and three
    // failures settles what separates them.
    //
    // On the FIRST load of a session the engine's four-slot player-view array
    // is genuinely zeroed, so ODST's readiness check correctly reported
    // `waiting for the proven slot-0/user-0 ordinary camera mode` and held for
    // 7.6 s before installing. On EVERY later load that same array still holds
    // the PREVIOUS level's values - the failing loads read back
    // `fov=1.641310 near=0.007812 far=10240`, the exact numbers from the level
    // just exited - so the check passed 0.6 s in and hooks went into the title
    // 1.5 s after it appeared, while the loading screen was still up. All three
    // such loads died without the engine's camera ever ticking; because the
    // `ODST camera WAIT` line logs on CHANGE, its single appearance followed by
    // 1.3 s of silence proves the camera state was frozen, not merely unread.
    //
    // "Non-zero" is therefore not liveness. This gate requires the engine's own
    // camera memory to be observed CHANGING before any title installs hooks.
    // It cannot alter behavior after arming - it can only delay installation -
    // and it deliberately interprets nothing about the camera, so one
    // implementation serves all three titles.
    //
    // All three engines build their four player views with the same
    // constructor shape; only the per-title view stride differs, and the array
    // base is the constructor's own `lea rbx,[rip+disp]`. Decoding ODST's that
    // way yields +0x2D73590, which is exactly the `array=2D73590` the accepted
    // runtime log already reports - the method is validated against a
    // known-good result before being applied to the other two titles.
    // RE-ENABLED (2026-08-06, second look at the SAME log). The
    // "REFUTED - bounced with zero hooks" reading examined the wrong window:
    // 07:15:05-07:15:07 is the tail of the menu's module churn AFTER the real
    // bounce, and the load it belongs to SUCCEEDED at 07:15:17-07:15:26. The
    // real bounce was at 07:14:42 - 2.4 s after the gate's 12-sample
    // "already running" fast path accepted the outgoing level's dying ticks
    // and installed into the loading screen at 07:14:38. Every bounced load
    // in every preserved capture (8 of 8 across three builds) was
    // preceded by an install <= 1.6 s into the load; every install that
    // waited for the real frozen-then-ticking transition (5 of 5) survived.
    // The decision thresholds that close the dying-ticks hole live in
    // LevelLoadGateLogic (src/common/level_load_gate_logic.h), and the
    // unconditional 12 s timeout is gone - the preserved successful retry
    // froze for 11,766 ms, within 300 ms of that timeout re-creating the bug.
    constexpr bool kLevelLoadGateEnabled = true;
    using LegacyCollisionTestVectorFn = uint8_t(__fastcall*)(uint64_t flags, int32_t mode, const float* start,
        const float* desired, int32_t ignoredObjectA, int32_t ignoredObjectB, int32_t ignoredObjectC, void* result);
    inline constexpr int kLegacyCollisionWeaponSamples = 14;
    inline constexpr int kLegacyCollisionMaxSamples = 21;
    // GAME BRIGHTNESS (2026-07-19). 0x278EE0 was MISIDENTIFIED as the HUD-scale
    // transform. In the headset, multiplying its two float args changes the GAME
    // BRIGHTNESS/gamma, not the HUD size - a0/a1 feed a screen color/gamma
    // constant (slots 0x280000/0x2D0000), NOT a HUD geometry transform. The
    // effect is useful as its own control, so the hook stays
    // but is driven from `game_brightness` (default 1.0 = untouched). This does
    // NOT resize the HUD; real HUD scaling needs a different mechanism (a
    // captured VR panel - the deferred 2D HUD has no single geometry lever).
    // ODST and Reach publish the SAME control from the same three floats, so
    // the one slider now drives all three titles (2026-08-06). Proof, from the
    // pinned retail modules: halo3+0x278EE0, halo3odst+0x2A6308 and
    // haloreach+0x252E28 each take (a0, a1, a2) in xmm0-2, call the imported
    // `powf` with a rodata base and a2, then publish the pair
    // (a0, powf(base, a2), ...) and (a1, a0*a1, ...) as two 16-byte screen
    // colour/gamma constants. Halo 3 and ODST upload 0x280000/0x2D0000 and
    // 0x280001/0x2D0001 from a stack buffer; Reach stores into module globals,
    // divides both leading terms by the same powf result, and uploads
    // 0x4E0000/0x540000 and 0x4E0001/0x540001 from its tail call. The
    // relationship between the inputs and the published brightness is identical
    // in all three, so scaling a0 and a1 is the whole control everywhere.
    typedef void(__fastcall* HudXformFn)(float, float, float);
    // HUD CROSSHAIR CLASS HIDER.
    // H3EK's complete ui/chud tag set marks native reticles with scripting
    // class 2. Halo already resolves that class safely inside chud_draw_widget;
    // normal gameplay merely short-circuits around the check. The install code
    // below enables the existing check and hooks its visibility predicate.
    typedef bool(__fastcall* HudCrosshairVisibleFn)(int);
    typedef void(__fastcall* HudDrawWidgetFn)(int, void*, unsigned short, unsigned char, void*);
    typedef void(__fastcall* HudPlaceFn)(void*, void*, void*, void*);
    // HUD SAFE-FRAME LOCATOR. Halo 3 and ODST use the same
    // s_chud_curvature_info field order and the same shared slider math, but
    // their immutable tag payloads differ. The adapter anchors below are
    // title-proven; cached heap addresses and scan publications are tagged with
    // a title generation so one resident MCC module can never satisfy another.
    constexpr int kMaxSafeFrameHits = 16;
    // Reach frame cap (reach_frame_cap_logic.h). Three exact sites in the
    // hash-pinned module prove the cap/target pair: the option setter's
    // "unlimited" branch and 60/120/180/240 table (+0xD5D5/+0xD600), the
    // limiter gate that reads the cap (+0x328CC) and the limiter's target
    // load (+0x322F5). All rip targets must agree (target = cap - 4).
    // The same binding serves Halo 4, whose halo4.dll carries Reach's
    // exact option setter, table and limiter gate (its target sits apart from the cap).
    // The binding and its tick are reach_frame_cap_logic.h's. F1 reads one
    // status byte per title (reach_frame_cap::Status): Reach, Halo 4, Halo 3,
    // ODST, Halo 2.
    using ReachFrameCapBinding = reach_frame_cap::Binding;
    using FrameCapResolver = reach_frame_cap::Resolver;

    // Defined in game.cpp (namespace game_core).
    void NoteYawReferenceLatched(
        GameTitle title, uint32_t generation, const char* path, float gameYaw, float headYaw, bool shared);
    void NoteSharedYawFrame(GameTitle title, uint32_t generation);
    void Halo3PublishRollStableFollowFor(
        uint32_t generation, VehicleFollowOwner owner, float hullYaw, float hullPitch, float hullRoll = 0.0f);
    void Halo3ClearRollStableFollow();
    void NoteVehicleFollowYaw(float delta);
    void PublishVehicleSteerContext(
        GameTitle title, bool driver, vehicle_wheel::SteerClass cls, bool armed, bool aircraft = false);
    bool Halo3ReadRollStableFollow(float& hullYaw, float& hullPitch);
    uintptr_t ResolvePlayerViewArray(uintptr_t base, size_t size, const PlayerViewArrayEvidence& evidence);
    bool ReadPlayerViewFingerprint(uintptr_t array, size_t stride, uint64_t& fingerprint);
    void LegacyPublishReloadTarget(GameTitle title, uint16_t tag, uint32_t generation,
        const FpInterpolationContext& context, const BoneMatrix& root, const BoneMatrix* solved, const int32_t* boneMap,
        const FpExplicitPoseTargets* targets = nullptr);
    bool LegacyApplyWorldCollision(GameTitle title, int hand, BoneMatrix& target, float* appliedCorrection = nullptr);
    void LegacyPublishWorldCollisionVolumes(GameTitle title, uint16_t renderModelTag,
        const FpInterpolationContext& context, const BoneMatrix& root, const BoneMatrix* solved, int32_t ignoredObject,
        uint32_t generation, const FpExplicitPoseTargets* targets = nullptr);
    bool LegacyObserveWeaponRenderModel(GameTitle title, uint16_t renderModelTag, uint32_t generation,
        const BoneMatrix* source = nullptr, const int32_t* boneMap = nullptr);
    bool LegacyBuildMappedWeaponBounds(GameTitle title, const FpInterpolationContext& context, const BoneMatrix& root,
        const BoneMatrix* solved, float output[][3], uint64_t& shapeId);
    int32_t LegacyCollisionIgnoredObject(GameTitle title);
    bool InstallLegacyWorldCollision(LegacyWorldCollisionFeature& feature, GameTitle title, uintptr_t base, size_t size,
        uint32_t generation, void* detour, uint32_t expectedRva, const char* signature = nullptr,
        bool fourArgumentResolver = false, uint32_t schedulerRva = 0, const char* schedulerSignature = nullptr);
    float* FindDebugVarFloat(uintptr_t base, size_t size, const char* name);
    void* FindDebugVarSlot(uintptr_t base, size_t size, const char* name, uint64_t expectedType);
    int SafeReadByte(const uint8_t* slot, uint8_t* value);
    int SafeWriteByte(uint8_t* slot, uint8_t value);
    int SafeReadFloat(const float* slot, float* value);
    int SafeWriteFloat(float* slot, float value);
    void InvalidateHudLayoutProfile(HudLayoutProfile profile);
    void HudLayoutAutoTick(HudLayoutProfile profile);
    void RotateByQuat(const float q[4], const float in[3], float out[3]);
    float Clamp(float v, float lo, float hi);
    float WrapPi(float a);
    void BuildTrackedGameBasisFromFrame(
        const float q[4], bool head, bool followValid, float hullYaw, float hullPitch, float basis[9]);
    void BasisFromAngles(float yaw, float pitch, float roll, float basis[9]);
    void MultiplyBases(const float left[9], const float right[9], float out[9]);
    void ShortestArcRotation(const float from[3], const float to[3], float out[9]);
    bool ComposeBoneMatrices(const BoneMatrix& left, const BoneMatrix& right, BoneMatrix& output);
    bool InvertBoneMatrix(const BoneMatrix& input, BoneMatrix& output);
    bool ApplyControllerToMarkerBonesWithTarget(const BoneMatrix& root, const BoneMatrix& desiredWristWorld,
        float meshScale, BoneMatrix* bones, int count, int transformAnchor);
    void PublishShotDiagnostic(GameTitle title, uint32_t titleGeneration, uint32_t unit, uint32_t weapon, uint8_t slot,
        uint8_t barrel, bool predicted, bool substituted, bool firesFromCamera, bool unitAim, const float origin[3],
        const float direction[3]) noexcept;
    bool ReconstructVisiblePaletteSource(uint16_t tag, const FpInterpolationContext& context, const BoneMatrix& root,
        const BoneMatrix* source, const BoneMatrix*& replacement,
        const FpExplicitPoseTargets* explicitTargets = nullptr, const BoneMatrix* unmodifiedOverride = nullptr,
        bool* controllerArmsSolved = nullptr);
    void LegacyPublishWorldCollisionVolumes(GameTitle title, uint16_t /*renderModelTag*/,
        const FpInterpolationContext& context, const BoneMatrix& root, const BoneMatrix* solved, int32_t ignoredObject,
        uint32_t generation, const FpExplicitPoseTargets* targets);
    void RotateAboutAxis(float* v, const float* axis, float cosA, float sinA);
    void PrepareCachedPhysicalCrouch(GameTitle title, uintptr_t base, size_t size, uint32_t generation);
    void ApplyCachedPhysicalCrouch(GameTitle title, float* position, float physicalDown, bool playerControlled);
    void ApplyVrTurn();
    void ApplyVrTurn(const VrPadState& pad);
    GatedHook InstallGatedHook(void*& target, void* address, void* detour, void** original);
    int SafeReadBytes(const void* src, void* dst, size_t n);
    int SafeWriteBytes(void* dst, const void* src, size_t n);
    bool ResolveReachDetourCodeRange(const void* function, ReachDetourCodeRange& range);
    bool ReachColdExactSignatureAt(uintptr_t base, size_t size, uintptr_t expectedRva, const char* pattern);
    bool ResolveReachFrameCap(uintptr_t base, size_t size, volatile int32_t*& cap, volatile float*& target);

    extern std::atomic<bool> g_enabled;
    extern std::atomic<bool> g_vrRuntimeFailureLatched;
    extern std::atomic<bool> g_needRecenter;
    extern std::atomic<bool> g_needPosRecenter;
    extern std::atomic<float> g_yawSign;
    extern std::atomic<float> g_pitchSign;
    extern std::atomic<float> g_pitchTrim;
    extern std::atomic<bool> g_writeUp;
    extern std::atomic<bool> g_positional;
    extern std::atomic<int> g_stereoEye;
    extern std::atomic<bool> g_baseCamValid;
    extern std::atomic<bool> g_camValid;
    extern std::atomic<bool> g_vrAim;
    extern std::atomic<float> g_aimFwdX;
    extern std::atomic<float> g_aimFwdY;
    extern std::atomic<float> g_aimFwdZ;
    extern std::atomic<float> g_servoDesiredPitch;
    extern std::atomic<float> g_servoAimPitch;
    extern std::atomic<float> g_servoHandReferenceYaw;
    extern std::atomic<bool> g_aimSeen;
    extern float g_headYawRef;
    extern float g_gameYawRef;
    extern float g_headPosRef[3];
    extern Halo3RollStableFollowPublication g_halo3RollStableFollow;
    extern fp_rig_coverage::UnmappedRigLog g_unmappedFpRigs;
    extern thread_local BoneMatrix g_fpPaletteScratch[kReachFpMaxSourceNodeCount];
    extern thread_local bool g_legacyCollisionOwnedQuery;
    extern thread_local FpStereoSolveScope g_fpStereoSolveScope;
    extern std::atomic<uint64_t> g_authoredCrosshairKey;
    extern thread_local uint64_t g_authoredCrosshairKeyAccum;
    extern std::atomic<uint32_t> g_authoredCrosshairDraws;
    extern std::atomic<bool> g_scopeRenderActive;
    extern std::atomic<uintptr_t> g_safeFrameSlots[kMaxSafeFrameHits];
    extern std::atomic<HudLayoutProfile> g_hudLayoutOwner;
    extern ReachFrameCapBinding g_reachFrameCap;
    // Declared after g_reachFrameCap: its default arguments name it.
    void ReachFrameCapTick(uintptr_t base, size_t size, uint32_t generation, bool coldWorkAllowed,
        ReachFrameCapBinding& b = g_reachFrameCap, FrameCapResolver resolve = ResolveReachFrameCap, bool want = true);
    extern ReachFrameCapBinding g_halo4FrameCap;
    extern TitleHookSupervisorState g_supervisorTitles;

    enum class VehicleFollowOwner : uint32_t
    {
        Halo3 = 0,
        Odst = 1,
        Reach = 2,
    };
    struct Halo3RollStableFollowPublication
    {
        std::atomic<uint32_t> sequence{0}; // even = stable, odd = writer active
        std::atomic<uint32_t> generation{0};
        std::atomic<uint64_t> sampleMs{0};
        std::atomic<float> hullYaw{0.0f};
        std::atomic<float> hullPitch{0.0f};
        std::atomic<float> hullRoll{0.0f}; // partial-hold roll (vehicle_horizon_hold_deg), 0 = level
        // Which title's camera published this. Only one title's vehicle
        // transaction can ever be armed, so one publication serves both and
        // every shared consumer (hands, room translation, aim) keeps working
        // untouched -- but the reader must still verify the sample came from
        // the title that is actually driving, or a stale cross-title sample
        // could pitch the wrong camera.
        std::atomic<uint32_t> owner{static_cast<uint32_t>(VehicleFollowOwner::Halo3)};
        bool writerHasValue = false; // camera-thread writer only
    };
    // Every follow-basis consumer composes right after a fresh follow read;
    // the partial-hold roll rides with that publication (0 = level horizon).
    template <class... Args>
    bool Halo3ComposeFollowBasisWithRoll(Args... args)
    {
        return Halo3ComposeRollStableFollowBasis(
            args..., g_halo3RollStableFollow.hullRoll.load(std::memory_order_relaxed));
    }
    struct PlayerViewArrayEvidence
    {
        const char* constructorPattern;
        size_t expectedStride;
    };
    // One per title. Not thread-safe by design: every caller is the 50 ms title
    // worker, which is the only thread that decides to install.
    class PlayerViewLivenessGate
    {
    public:
        // The whole gate. Returns true when installing is safe: the engine's
        // camera has been seen to change, the array could not be proven (fail
        // open to the pre-gate behavior), or the timeout expired. The
        // signature scan runs once per title generation, not once per poll.
        bool AllowsInstall(uint32_t generation, uintptr_t base, size_t size, const PlayerViewArrayEvidence& evidence,
            const char* titleName)
        {
            if (!kLevelLoadGateEnabled)
                return true;
            m_titleName = titleName;
            if (generation != m_generation || base != m_base)
            {
                m_generation = generation;
                m_base = base;
                m_array = ResolvePlayerViewArray(base, size, evidence);
                m_stride = evidence.expectedStride;
                m_haveSample = false;
                m_logic.Reset();
                m_firstSampleMs = 0;
                m_lastLogMs = 0;
                m_resolveLogged = false;
            }
            if (!m_array)
            {
                if (!m_resolveLogged)
                {
                    m_resolveLogged = true;
                    LOG("%s level-load gate: the four-slot player-view array "
                        "could not be proven in this module; install proceeds "
                        "immediately, exactly as it did before this gate "
                        "existed",
                        titleName);
                }
                return true;
            }
            return Observe(titleName);
        }

        // Close the gate again after a teardown. WITHOUT THIS THE GATE IS A
        // ONE-SHOT AND THE WHOLE FEATURE IS INERT for the case that actually
        // matters. Measured in a headset run: the gate correctly held
        // ODST's first load for 7.2 s, but the title generation does NOT change
        // across a Save & Quit -> menu -> load cycle, so `m_live` stayed set
        // and the two loads that then bounced installed 1.5 s in with no gate
        // line logged at all. Every install must re-earn the proof, so this is
        // called wherever a title's hooks come out.
        void Rearm()
        {
            // Logged so a future session can prove the gate re-armed instead of
            // inferring it from absent lines, which is how the one-shot defect
            // hid: the failing loads simply had no gate output at all.
            if (m_logic.IsOpen() && m_titleName)
                LOG("%s level-load gate: re-armed after teardown; the next "
                    "install must prove the level is running again",
                    m_titleName);
            m_haveSample = false;
            m_logic.Reset();
            m_firstSampleMs = 0;
            m_lastLogMs = 0;
        }

        // Read-only view of the last sampled decision. AllowsInstall must be
        // called exactly ONCE per worker tick (each call consumes a sample);
        // every other consumer in that tick asks this instead. An unprovable
        // array reports open, matching AllowsInstall's fail-open behavior.
        bool IsOpen() const { return !kLevelLoadGateEnabled || !m_array || m_logic.IsOpen(); }

        // Whether the last AllowsInstall resolved the player-view array. A
        // consumer that must distinguish "the gate PROVED the level running"
        // from "the gate failed open on unprovable evidence" asks this: on a
        // fail-open, IsOpen() is true while the gate cannot actually see the
        // module's loading state.
        bool ArrayProven() const { return m_array != 0; }

    private:
        // FREEZE, THEN TICK. A plain "has it changed?" test is not enough, and
        // headset testing is what exposed the hole: the bug happens
        // when you load back into the SAME game, not when you switch games.
        // The preserved capture explains both halves - across every same-game
        // load/quit cycle `halo3odst.dll` stays loaded (present in 16 of 17
        // module snapshots; the one absence is the moment ODST was finally
        // left), so its camera memory is CONTINUOUS across the transition,
        // while switching games unloads the module and its data comes back
        // zeroed from the file image.
        //
        // Continuity means the outgoing level's camera can still be ticking
        // when the new title generation is detected: in the same capture, a
        // Save & Quit at 06:28:28 left the camera changing for ~3 more seconds.
        // A gate that opened on the first observed change - or on the 0.6 s
        // change run an earlier build shipped - accepts those dying ticks as the
        // NEW level's liveness and installs into the loading screen anyway,
        // which is exactly what preceded every bounced load in the preserved
        // log. The thresholds that survive the dying ticks live in
        // LevelLoadGateLogic; see src/common/level_load_gate_logic.h for the
        // measurements each one rests on.
        bool Observe(const char* titleName)
        {
            if (m_logic.IsOpen())
                return true;
            uint64_t fingerprint = 0;
            if (!ReadPlayerViewFingerprint(m_array, m_stride, fingerprint))
            {
                // An unreadable array means the module is in no state to hook;
                // keep holding, but never silently.
                const uint64_t now = GetTickCount64();
                if (now - m_lastLogMs >= 2000)
                {
                    m_lastLogMs = now;
                    LOG("%s level-load gate: player-view read FAILED; holding "
                        "install (the module is not in a hookable state)",
                        titleName);
                }
                return false;
            }
            const uint64_t now = GetTickCount64();
            if (!m_haveSample)
            {
                m_haveSample = true;
                m_fingerprint = fingerprint;
                m_firstSampleMs = now;
                return false;
            }

            const bool changed = fingerprint != m_fingerprint;
            m_fingerprint = fingerprint;
            switch (m_logic.Observe(changed))
            {
            case LevelLoadGateLogic::Decision::OpenFrozenThenTicking:
                LOG("%s level-load gate: the engine's camera was frozen and has "
                    "now started ticking (%llu ms); the level is running, so "
                    "installing here instead of during its loading screen",
                    titleName, static_cast<unsigned long long>(now - m_firstSampleMs));
                return true;
            case LevelLoadGateLogic::Decision::OpenAlreadyRunning:
                LOG("%s level-load gate: the engine's camera has changed on "
                    "%u consecutive samples (%llu ms) without ever being "
                    "still; no loading screen does that, so this title was "
                    "already running when observation began; installing",
                    titleName, m_logic.ChangeRun(), static_cast<unsigned long long>(now - m_firstSampleMs));
                return true;
            case LevelLoadGateLogic::Decision::Hold:
                break;
            }
            if (now - m_lastLogMs >= 2000)
            {
                m_lastLogMs = now;
                LOG("%s level-load gate: holding install after %llu ms "
                    "(generation %u) - frozen seen=%d, still run=%u, change "
                    "run=%u. A loading screen leaves the previous level's "
                    "camera untouched; installing into that is what bounces "
                    "the load to the menu",
                    titleName, static_cast<unsigned long long>(now - m_firstSampleMs), m_generation,
                    m_logic.SawFrozen() ? 1 : 0, m_logic.StillRun(), m_logic.ChangeRun());
            }
            return false;
        }

    private:
        const char* m_titleName = nullptr;
        uint32_t m_generation = 0;
        uintptr_t m_base = 0;
        uintptr_t m_array = 0;
        size_t m_stride = 0;
        uint64_t m_fingerprint = 0;
        uint64_t m_firstSampleMs = 0;
        uint64_t m_lastLogMs = 0;
        bool m_haveSample = false;
        bool m_resolveLogged = false;
        LevelLoadGateLogic m_logic;
    };
    // Final first-person pose records proved by offline RE: four players, two
    // held-weapon slots, up to 64 composed 0x34-byte bone matrices per slot.
    struct BoneMatrix;
    // Halo real_matrix4x3: uniform scale, then forward/left/up basis vectors,
    // then translation. The first headset build incorrectly put scale last,
    // shifting every basis read by one float and making weapon pieces diverge.
    struct BoneMatrix
    {
        float scale;
        float rotation[9];
        float translation[3];
    };
    struct FpInterpolationContext
    {
        const BoneMatrix* source = nullptr;
        uint32_t generation = 0;
        int count = 0;
        int player = -1;
        int slot = -1;
        int wrist = -1;
        int cameraControl = -1;
        int elbow = -1;
        int shoulder = -1;
        uint64_t wristDescendants = 0;
        // Reach uses this exact appended held-object boundary; H3/ODST
        // leave it disabled and retain their authored descendant masks.
        int heldObjectStart = -1;
        int lWrist = -1;
        int lElbow = -1;
        int lShoulder = -1;
        uint64_t lWristDescendants = 0;
        uint32_t muzzleUnit = UINT32_MAX, muzzleWeapon = UINT32_MAX;
        bool valid = false;
    };
    // Optional immutable inputs for title adapters whose render hooks already
    // own one exact prepared-frame controller/head snapshot. Halo 3 and ODST
    // continue through DesiredWristWorld and the accepted camera atomics when
    // this is null; Reach supplies all three absolute world poses explicitly
    // so neither eye can re-read tracking or solve from a per-eye root.
    struct FpExplicitPoseTargets
    {
        BoneMatrix centerRoot{};
        BoneMatrix rightWrist{};
        BoneMatrix leftWrist{};
        // Exact translations applied to this prepared pair, not a later
        // asynchronous correction. Collision queries must consume raw poses.
        float collisionCorrection[2][3]{};
        float rightScale = 1.0f;
        float leftScale = 1.0f;
        bool centerRootValid = false;
        bool rightWristValid = false;
        bool leftWristValid = false;
        // Reach supplies this from the same immutable prepared-frame snapshot
        // as both controller targets. H3/ODST use VR_IsTwoHandAiming() instead.
        bool twoHandAimActive = false;
        // Persistent support grip (Reach slice): the owner trust frozen for
        // this stereo pair by the FP interpolate invocation. `resolved` stays
        // false with the feature off, which keeps `twoHandAimActive` the only
        // presentation input exactly as before (PG-off parity).
        bool supportGripAttached = false;
        bool supportGripResolved = false;
        // Ordinary calibrated one-hand weapon-wrist target for this exact
        // prepared frame (same position/scale contract as rightWrist, one-hand
        // orientation only). Used solely as the stale-owner fallback for an FP
        // invocation that is not the relationship owner; never built with the
        // feature off.
        BoneMatrix rightOneHandWrist{};
        float rightOneHandScale = 1.0f;
        bool rightOneHandWristValid = false;
        bool handAlignment = false;
        // Same-frame physical tracking reference for native contact melee.
        contact_melee::TrackingToWorld contactSpace[2]{};
        contact_melee::TrackingToWorld contactController[2]{};
        uint64_t contactSerial = 0, contactReference = 0;
        // The raw tracking-space epoch (what the lens consumer compares), not the hashed contactReference.
        uint64_t contactEpoch = 0;
        int64_t contactTimeNs = 0;
    };
    struct LegacyCollisionPublication
    {
        std::atomic<uint32_t> version{0};
        uint32_t generation = 0;
        uint64_t publishedAtMs = 0;
        uint32_t sampleCount = 0;
        uint32_t handSampleCount = 0;
        uint64_t shapeId = 0;
        int32_t ignoredObject = -1;
        float samples[kLegacyCollisionMaxSamples][3]{};
    };
    struct LegacyCollisionCorrection
    {
        std::atomic<uint32_t> version{0};
        uint32_t generation = 0;
        uint64_t publishedAtMs = 0;
        bool contact = false;
        float source[3]{};
        float correction[3]{};
    };
    struct LegacyCollisionWorkerHand
    {
        ContactReleaseSmoothing smoothing{};
        bool seeded = false;
        uint32_t generation = 0;
        uint64_t publishedAtMs = 0;
        uint32_t sampleCount = 0;
        uint64_t shapeId = 0;
        float accepted[kLegacyCollisionMaxSamples][3]{};
    };
    struct LegacyWorldCollisionFeature
    {
        GameTitle title = GameTitle::None;
        void* target = nullptr;
        void* original = nullptr;
        void* resolver = nullptr;
        bool fourArgumentResolver = false;
        uint32_t generation = 0;
        std::atomic<bool> installed{false};
        std::atomic<uint32_t> callbacks{0};
        std::atomic<uint64_t> nextQueryAtMs{0};
        std::atomic<bool> queryActive{false};
        std::atomic<uint64_t> engineCalls{0};
        std::atomic<uint64_t> queryTicks{0};
        std::atomic<uint64_t> targetMisses{0};
        std::atomic<uint64_t> seeds{0};
        LegacyCollisionPublication targetPublication[2]{};
        LegacyCollisionCorrection correction[2]{};
        LegacyCollisionWorkerHand worker[2]{};
        std::atomic<uint64_t> queries{0};
        std::atomic<uint64_t> contacts[2]{};
        std::atomic<uint64_t> weaponContacts{0};
        std::atomic<uint64_t> boundsPublished{0};
        std::atomic<uint64_t> boundsFallbacks{0};
        std::atomic<bool> runtimeBoundsProven{false};
        std::atomic<uint64_t> runtimeBoundsReads{0}, runtimeBoundsRefusals{0};
        std::atomic<uint64_t> unknownWeaponModels{0};
        // Cache the native appended held-model remap or recognized catalog.
        struct WeaponObservation
        {
            std::atomic<uint32_t> sequence{0};
            std::atomic<uint32_t> tag{0xFFFFu};
            std::atomic<uint32_t> checksum{0};
            std::atomic<uint32_t> generation{0};
            std::atomic<uint64_t> atMs{0};
            std::atomic<int32_t> root{-1};
            std::atomic<const BoneMatrix*> source{nullptr};
        } weaponObservation[2];
        std::atomic<uint64_t> secondaryPublishedAtMs{0};
        std::atomic<int32_t> secondaryPublishedObject{-1};
        std::atomic<uint64_t> applied[2]{};
        std::atomic<uint64_t> failures{0};
    };
    // One visible-palette pose per stereo pair. Halo calls the interpolation
    // and palette path from each eye render, but an articulated body must be
    // posed once and merely viewed from two cameras. Re-solving independently
    // lets tiny input/timing differences put an arm in two places and wastes
    // the analytic IK work. The cache remains thread-local because the measured
    // FP prepare/palette/render sequence is synchronous on one render thread.
    struct FpStereoPaletteCache
    {
        bool valid = false;
        uint16_t tag = 0;
        int player = -1;
        int slot = -1;
        int count = 0;
        int wrist = -1;
        int elbow = -1;
        int shoulder = -1;
        uint64_t wristDescendants = 0;
        // Reach uses this exact appended held-object boundary; H3/ODST
        // leave it disabled and retain their authored descendant masks.
        int heldObjectStart = -1;
        int lWrist = -1;
        int lElbow = -1;
        int lShoulder = -1;
        uint64_t lWristDescendants = 0;
        bool armIk = false;
        bool armsSolved = false;
        // Persistent support grip: the visible support attachment this solve
        // was produced under. It already differs whenever `armIk` differs, but
        // it is stored explicitly so a configured-off arm IK cannot silently
        // collapse an attached and a detached solve into one cache entry if
        // the body ever gains support-attached-dependent geometry.
        bool supportGripAttached = false;
        float collisionCorrection[2][3]{};
        BoneMatrix root{};
        BoneMatrix original[kReachFpMaxSourceNodeCount]{};
        BoneMatrix solved[kReachFpMaxSourceNodeCount]{};
    };
    struct FpStereoSolveScope
    {
        bool armed = false;
        bool centerRootValid = false;
        // Latched once at arming: the asynchronous two-hand global can flip
        // between the two eye solves of one pair, and it participates in the
        // palette cache key, so a per-eye resample would let the eyes solve
        // different arm-IK modes for one presented frame.
        bool twoHandAimActive = false;
        // Persistent support grip (H3 slice): the owner trust resolved for
        // this stereo pair's FP invocation, frozen so both eyes use the same
        // decision. `supportGripResolved` stays false for titles that do not
        // resolve it (ODST/Reach), which keeps their existing presentation
        // semantics exactly. A resolved pair with `supportGripAttached=false`
        // is an owner-untrusted invocation: ordinary one-hand aim and no
        // support presentation migration for that pair.
        bool supportGripAttached = false;
        bool supportGripResolved = false;
        VrContactTrackingSnapshot anatomicalTracking{};
        bool anatomicalCarriersValid = false;
        Halo4FloatingTransform anatomicalPrimaryCarrier{}, anatomicalSupportCarrier{};
        BoneMatrix anatomicalTorsoRoot{};
        BoneMatrix centerRoot{};
        FpStereoPaletteCache palettes[4]{};
    };
    inline float GameBrightnessScale()
    {
        const float b = g_config.game_brightness;
        return (isfinite(b) && b > 0.05f) ? b : 1.0f;
    }
    inline uint64_t FoldAuthoredCrosshairKey(uint64_t accum, unsigned int widgetIndex, unsigned int variant)
    {
        uint64_t k = accum * 1099511628211ull;
        k ^= static_cast<uint64_t>(variant) * 0x9E3779B97F4A7C15ull;
        k ^= static_cast<uint64_t>(widgetIndex) + 0x165667B19E3779F9ull;
        return k ? k : 1; // 0 is reserved for "nothing captured"
    }
    // Motion blur (2026-07-19): Halo 3's multi-tap camera motion blur derives
    // its blur vector from a previous-frame camera. With two eye renders per
    // frame, an eye's "previous" camera is the OTHER eye's - a constant fake
    // velocity that smears bright content into discrete repeated echoes even
    // with the head still (the long-standing "left-eye ghost", reopened in
    // headset testing 2026-07-18). The engine's live tuning globals are exposed through
    // its own debug-var table ({name_ptr, type, value_ptr} entries in .data);
    // we resolve them BY NAME at runtime - no hardcoded RVAs - and force the
    // blur scales/max to zero while the player has motion blur off (default:
    // off, the VR-comfort standard). The tag loader at ~0x28D3E0 rewrites
    // these globals when effect params load, so they are re-zeroed each frame.
    struct MotionBlurVar
    {
        float* slot;
        float original;
    };
    // Previous addresses and authored baselines are retained in separate
    // per-title caches across generations. The tag allocator often reuses its
    // block across map reloads, so an exact title-anchor/payload re-check can
    // avoid a full scan without ever crossing titles or compounding curvature.
    struct HudLayoutRememberedCache
    {
        std::atomic<uintptr_t> slots[kMaxSafeFrameHits]{};
        std::atomic<uint32_t> baseCurvatureBits[kMaxSafeFrameHits]{};
        std::atomic<int> count{0};
    };
    struct Halo3ObjectMarker
    {
        int16_t nodeIndex;
        uint16_t pad;
        Halo3Matrix4x3 nodeMatrix;
        Halo3Matrix4x3 matrix;
        float radius;
    };
    struct ReachDetourCodeRange
    {
        DWORD64 begin = 0;
        DWORD64 end = 0;
    };
    struct ReachFrozenThread
    {
        HANDLE handle = nullptr;
        DWORD threadId = 0;
        bool suspended = false;
    };
    // Worker-only process freeze used after both Reach hooks are disabled. It
    // closes the small ingress window in which a thread is already inside a
    // MinHook relay or the detour prologue but has not incremented the counter.
    class ReachThreadFreeze
    {
    public:
        ~ReachThreadFreeze() { Release(); }

        bool Capture()
        {
            if (!m_threads.empty())
                return false;

            const DWORD processId = GetCurrentProcessId();
            const DWORD currentThreadId = GetCurrentThreadId();
            HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (snapshot == INVALID_HANDLE_VALUE)
                return false;

            bool enumerateOk = true;
            THREADENTRY32 entry{};
            entry.dwSize = sizeof(entry);
            if (!Thread32First(snapshot, &entry))
                enumerateOk = false;
            while (enumerateOk)
            {
                if (entry.th32OwnerProcessID == processId && entry.th32ThreadID != currentThreadId)
                {
                    HANDLE thread =
                        OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION | SYNCHRONIZE,
                            FALSE, entry.th32ThreadID);
                    if (!thread)
                    {
                        if (GetLastError() != ERROR_INVALID_PARAMETER)
                            enumerateOk = false;
                    }
                    else
                    {
                        m_threads.push_back({thread, entry.th32ThreadID, false});
                    }
                }
                if (!Thread32Next(snapshot, &entry))
                {
                    if (GetLastError() != ERROR_NO_MORE_FILES)
                        enumerateOk = false;
                    break;
                }
            }
            CloseHandle(snapshot);
            if (!enumerateOk)
            {
                Release();
                return false;
            }

            // Complete every allocation before the first suspension. All
            // captured threads then remain frozen through the RIP/counter
            // snapshot, so no observed thread can move between safe ranges.
            for (ReachFrozenThread& thread : m_threads)
            {
                const DWORD previousCount = SuspendThread(thread.handle);
                if (previousCount == static_cast<DWORD>(-1))
                {
                    if (WaitForSingleObject(thread.handle, 0) != WAIT_OBJECT_0)
                    {
                        Release();
                        return false;
                    }
                    continue;
                }
                thread.suspended = true;
            }

            if (!FrozenSnapshotIsComplete(processId, currentThreadId))
            {
                Release();
                return false;
            }
            return true;
        }

        bool Release()
        {
            bool ok = true;
            for (size_t i = m_threads.size(); i > 0; --i)
            {
                ReachFrozenThread& thread = m_threads[i - 1];
                if (thread.suspended && ResumeThread(thread.handle) == static_cast<DWORD>(-1) &&
                    WaitForSingleObject(thread.handle, 0) != WAIT_OBJECT_0)
                {
                    ok = false;
                }
                CloseHandle(thread.handle);
            }
            m_threads.clear();
            return ok;
        }

        const std::vector<ReachFrozenThread>& Threads() const { return m_threads; }

    private:
        bool FrozenSnapshotIsComplete(DWORD processId, DWORD currentThreadId) const
        {
            HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (snapshot == INVALID_HANDLE_VALUE)
                return false;

            bool complete = true;
            THREADENTRY32 entry{};
            entry.dwSize = sizeof(entry);
            if (!Thread32First(snapshot, &entry))
                complete = false;
            while (complete)
            {
                if (entry.th32OwnerProcessID == processId && entry.th32ThreadID != currentThreadId)
                {
                    bool foundFrozen = false;
                    for (const ReachFrozenThread& thread : m_threads)
                    {
                        if (thread.threadId == entry.th32ThreadID)
                        {
                            foundFrozen = thread.suspended || WaitForSingleObject(thread.handle, 0) == WAIT_OBJECT_0;
                            break;
                        }
                    }
                    if (!foundFrozen)
                        complete = false;
                }
                if (!complete || !Thread32Next(snapshot, &entry))
                {
                    if (complete && GetLastError() != ERROR_NO_MORE_FILES)
                        complete = false;
                    break;
                }
            }
            CloseHandle(snapshot);
            return complete;
        }

        std::vector<ReachFrozenThread> m_threads;
    };
    // Per-title state owned by TitleHookSupervisorThread (single thread; nothing else reads it). It lives outside the
    // function so per-title operations such as the manual-recovery table below can act on it; TitleHookSupervisorThread
    // binds its historical local names to these members.
    struct TitleHookSupervisorState
    {
        // Halo 3 install machine
        bool gameHooked = false;
        bool hookRefreshPending = false;
        uintptr_t hookedBase = 0;
        uint64_t haloInstalledAtMs = 0;
        uint32_t haloAttemptedGeneration = 0;
        bool manualHaloRecovery = false; // set for the current tick only
        // ODST install machine
        bool odstHooked = false;
        bool odstAttempted = false;
        uintptr_t odstCameraArrayRva = 0;
        uint64_t odstNextAttemptMs = 0;
        OdstCameraRearmGate odstRearmGate;
        OdstPauseRearmGate odstPauseRearmGate;
        bool odstPresentationPrepared = false;
        bool odstManualRecoveryPending = false;
        // generation awaiting a manual-recovery retry, 0 = none
        uint32_t reachManualRecoveryPending = 0;
        uint32_t halo4ManualRecoveryPending = 0;
        uint32_t halo2ManualColdRecoveryPending = 0;
        // Module range captured by the level-load gate on the tick it actually sampled (reset every tick).
        // Later stages reuse the SAME range and only run on a sampled tick: if the range query failed, the
        // gate never ran and activeLevelRunning is only its fail-open default.
        struct GateSample
        {
            uintptr_t base = 0;
            size_t size = 0;
            bool sampled = false;
        };
        GateSample halo4Gate, halo2Gate;
    };
    // ---- Per-title TitleHookSupervisorThread ticks (Phase 3 step 4) ----------------------------------------------------
    // Each title's poll block, moved verbatim out of TitleHookSupervisorThread. TitleHookSupervisorThread calls them in the original order,
    // with the shared steps (tunable profile, level-running publication, world-collision log) between them.
    struct TitleTickContext
    {
        const TitleDescriptor* activeTitle; // nullptr outside a supported title
        bool activeLevelRunning;            // this tick's level-load gate result for the active title
        uint64_t now;
        const TitleAdapterRuntimeSnapshot* runtime = nullptr; // install-machine steps only
        TitleHookPlan hookPlan = TitleHookPlan::None;         // install-machine steps only
    };

    // Core items that stay in game.cpp and that Reach code also uses.
    using ReachCollisionResolveFn = uint8_t(__fastcall*)(
        const float* start, const float* desired, float* accepted, int32_t ignoredObject);
    constexpr uint32_t kDiagnosticFpAgreement = 4u;
    constexpr uint32_t kDiagnosticGuard = 0x40000000u;
    // Turret gyro (turret_gyro_logic.h): the carrier hull heading/pitch of a
    // Reach mounted gun, published every camera frame whatever the view-follow
    // setting, for the aim stick's feed-forward. Same torn-write discipline as
    // the roll-stable follow publication.
    struct ReachGyroHullPublication
    {
        std::atomic<uint32_t> sequence{0};
        std::atomic<uint32_t> generation{0};
        std::atomic<uint64_t> sampleMs{0};
        std::atomic<float> yaw{0.0f}, pitch{0.0f};
        std::atomic<uint32_t> identity{0};
    };

    // Defined in game.cpp (namespace game_core; the first by halo3_collision_dispatch.inl, the second by visual_hand_palette.inl).
    uint8_t LegacyCollisionSchedulerDetourBody(LegacyWorldCollisionFeature& feature, uint64_t flags, int32_t mode,
        const float* start, const float* desired, int32_t ignoredObjectA, int32_t ignoredObjectB,
        int32_t ignoredObjectC, void* result);
    bool ApplyBoneHandVisualOffsets(BoneMatrix* palette, int count, uint64_t leftMask, uint64_t rightMask,
        int leftWrist, int rightWrist, float units, bool leftHanded, bool anatomical);
    extern PlayerViewLivenessGate g_reachLevelLoadGate;
    extern LegacyWorldCollisionFeature g_reachWorldCollision;
    extern ReachGyroHullPublication g_reachGyroHull;
    // Declared after ReachGyroHullPublication: their parameters name it.
    void GyroPublishHull(ReachGyroHullPublication& p, uint32_t generation, uint32_t identity, const float forward[3]);
    void GyroClearHull(ReachGyroHullPublication& p);
    bool GyroReadHull(ReachGyroHullPublication& p, turret_gyro::Hull& out, uint32_t& identityOut);
    // Core names the Halo 4 code uses (defined in game.cpp, namespace game_core).
    // The composer sees authored bones immediately before Halo applies its
    // camera-lag rotation to every bone except camera_control. Cache the full
    // wrist->camera_control relation with an atomic seqlock. The visible
    // palette hook uses it to synthesize a lag-consistent camera_control bone,
    // then removes the common lag as one rigid transform without changing the
    // weapon's already-correct authored barrel alignment.
    struct AtomicBoneMatrix
    {
        std::atomic<uint32_t> sequence{0};
        std::atomic<float> value[13]{};
    };
    // Reserve (stowed) weapon identity for holsters, all six titles.
    // Reach, Halo 3, ODST and Halo 4 read their proven unit inventories;
    // Halo 2 and CE (no proven unit-inventory reader) use the weapon held
    // before the current one while the unit still owns it
    // (reserve_weapon::PreviousHeld). Identities are learned from the held
    // first-person model (reserve_weapon_logic.h). Runs on each title's
    // render-model observation path; a busy flag keeps a second caller out
    // instead of blocking it.
    struct ReserveWeaponState
    {
        std::atomic_flag busy = ATOMIC_FLAG_INIT;
        reserve_weapon::Tracker tracker;
        reserve_weapon::PreviousHeld previous;
        uint32_t trackerGeneration = 0;
        // Halo 2 and CE identities are 64-bit, so generation and identity
        // are published separately (0 identity = Unknown).
        std::atomic<uint32_t> publishedGeneration{0};
        std::atomic<uint64_t> publishedIdentity{0};
        // Diagnostics: the last observation.
        std::atomic<uint64_t> diagCalls{0}, diagHeld{0}, diagCandidateIdentity{0}, diagCandidateSince{0};
        std::atomic<uint32_t> diagPrimary{reserve_weapon::kNoHandle}, diagReserve{reserve_weapon::kNoHandle},
            diagCandidateHandle{reserve_weapon::kNoHandle}, diagPrevious{reserve_weapon::kNoHandle};
        std::atomic<bool> diagRead{false};
    };
    bool NormalizedBasis(const BoneMatrix& matrix, float out[9]);
    void ReserveWeaponObserveLocked(ReserveWeaponState& state, uint32_t generation, bool read, uint32_t primary,
        uint32_t reserve, uint64_t heldIdentity);
    bool ReserveWeaponEnter(ReserveWeaponState& state, uint32_t generation);
    bool ResolveHalo4FrameCap(uintptr_t base, size_t size, volatile int32_t*& cap, volatile float*& target);
    // Defined by visual_hand_palette.inl, included in namespace game_core.
    void Halo4ApplyVisualHandOffsets(BoneMatrix* solved, float units, bool leftHanded, bool anatomical);
    extern std::atomic<float> g_worldScale;
    extern PlayerViewLivenessGate g_halo4LevelLoadGate;
    extern ReserveWeaponState g_reserveWeapon[6];
    // Core state the title units read (defined in game.cpp, namespace game_core); moved here from
    // halo3_family_internal.h in E2 round 4, so that header names only Halo 3 family code.
    TitleAdapterRuntimeSnapshot RuntimeSnapshot(uint64_t nowMs);
    bool ControllerWorldPoseEx(bool left, float basis[9], float pos[3], float& scale);
    bool LoadCameraBasis(float basis[9]);
    bool ApplyControllerToMarkerBonesFromRoot(
        BoneMatrix root, BoneMatrix* bones, int count, int wrist, int leftWrist, bool dual);
    bool ManualVrRecoveryFor(GameTitle title);
    extern std::atomic<bool> g_autoVrUserVeto;
    extern std::atomic<bool> g_autoVrOwned;
    extern std::atomic<float> g_zoomFactor;
    extern std::atomic<float> g_camX;
    extern std::atomic<float> g_camY;
    extern std::atomic<float> g_camZ;
    extern std::atomic<float> g_baseCamX;
    extern std::atomic<float> g_baseCamY;
    extern std::atomic<float> g_baseCamZ;
    extern std::atomic<float> g_camFwd[3];
    extern std::atomic<float> g_camUp[3];
    extern std::atomic<float> g_worldUp[3];
    extern std::atomic<bool> g_worldUpInit;
    extern PlayerViewLivenessGate g_odstLevelLoadGate;
    extern thread_local FpInterpolationContext g_fpInterpolationContexts[2];
    extern thread_local BoneMatrix g_fpUnmodifiedInterpolations[2][64];
    extern thread_local BoneMatrix g_scopeHiddenPalette[64];
    extern std::atomic<float> g_barrelInWrist[2][3];
    extern std::atomic<bool> g_barrelInWristValid[2];
    extern thread_local float g_fpPaletteCollisionCorrection[2][3];
    // Core names the Halo 3 runtime uses (defined in game.cpp, namespace game_core; E2 round 4).
    // ---- Driving context: per-class steering mode for the seat the
    // player drives right now, published by whichever title's follow pass
    // runs, read by the wheel and the trigger throttle on the input thread.
    struct VehicleSteerContext
    {
        std::atomic<int> mode{vehicle_wheel::Point}; // for the current driver seat
        std::atomic<bool> driver{false}, armed{false}, modeFollow{false};
        std::atomic<bool> aircraft{false}; // driving a Banshee/Hornet/Falcon-class aircraft
        std::atomic<uint8_t> title{0};
        std::atomic<uint64_t> sampleMs{0};
    };
    // ---- Halo 3 / ODST install machines (Phase 3 step 5) -------------------------------------------------
    // Moved verbatim out of TitleHookSupervisorThread. Lifecycle steps retire/rearm and report whether the title may install;
    // install steps attempt it. TitleHookSupervisorThread calls them in the original order.
    enum class InstallStep
    {
        Continue,
        RestartTick
    };
    bool Halo2SeatAuthorsSteeringNow();
    // Cross-title seat dispatchers (game.cpp): they ask Halo 2, Halo 3, ODST and Reach.
    bool SeatAuthorsSteeringNow();
    bool SeatUsesWheel();
    unsigned char* Halo3LoadedTagDefinition(uint32_t datum);
    bool InstallHalo3CameraHooks(uintptr_t base, size_t size, uint32_t runtimeGeneration);
    LegacyWorldCollisionFeature* LegacyCollisionForTitle(GameTitle title);
    bool LegacyReadWeaponRenderModelChecksum(GameTitle title, uint16_t renderModelTag, uint32_t& checksum);
    const LegacyWeaponCollisionBounds* LegacyWeaponBoundsForTitle(GameTitle title, uint32_t checksum);
    bool RemoveHalo3CameraHooks();
    bool VehicleSteerContextFresh();
    extern std::atomic<const char*> g_armFailurePublished;
    extern std::atomic<int> g_armFailureSide;
    extern std::atomic<bool> g_enginePauseValidated;
    extern uint32_t* g_engineTlsIndex;
    extern std::atomic<bool> g_halo2SteeringAuthor;
    extern std::atomic<bool> g_halo2SteeringWheel;
    extern PlayerViewLivenessGate g_halo3LevelLoadGate;
    extern std::atomic<uint32_t> g_halo3RuntimeGeneration;
    extern void** g_halo3TagDataBase;
    extern void** g_halo3TagInstanceTable;
    extern std::atomic<bool> g_hooked;
    extern std::atomic<int> g_motionBlurVarCount;
    extern std::atomic<uint32_t> g_turretSwallowsGrips;
    extern std::atomic<uint32_t> g_vehicleExitButtons;
    extern VehicleSteerContext g_vehicleSteer;
    // Defined by body_visibility_switch.inl (its include site is in namespace game_core).
    extern std::atomic<int> g_bodyVarCount;
    extern std::atomic<uint32_t> g_bodyVarGeneration;
    // The H3 scope camera forward diagnostic (defined in game.cpp, namespace game_core).
    extern std::atomic<float> g_h3ScopeFwd[3];
    extern std::atomic<bool> g_h3ScopeFwdBarrel;
    extern std::atomic<uint64_t> g_h3ScopeFwdAgeMs;
    extern std::atomic<uint64_t> g_h3ScopeFwdBuiltMs;
    // Core items the Halo 2 runtime uses (defined in game.cpp, namespace game_core).
    void ClearAimClamped();
    void Halo2FrameCapTick(uintptr_t base, size_t size, uint32_t generation, bool coldWorkAllowed);
    bool OccupiedTurretSeat();
    void PublishAimClamped(float vrYaw, float vrPitch, bool turret);
    extern std::atomic<uint64_t> g_halo2SteeringAuthorMs;
    extern std::atomic<uint64_t> g_halo2PauseIntentMs;
    // Core items Halo 3's camera install (titles/h3/halo3_camera_install.cpp) uses: anonymous game.cpp items and
    // the functions of the Halo 3 .inl files game.cpp includes in game_core (contact melee, weapon haptics,
    // avatar). Defined in game.cpp.
    bool Halo3ContactMeleeReady();
    bool InstallHalo3ContactMelee(uintptr_t base, size_t size, uint32_t generation);
    bool InstallHalo3WeaponHaptics(uintptr_t base, size_t size, uint32_t generation);
    bool RemoveHalo3WeaponHaptics();
    void ReportHalo3WeaponHaptics();
    bool InstallHalo3Avatar(uintptr_t base, size_t size, uint32_t generation);
    bool RemoveHalo3Avatar();
    void ReportHalo3Avatar();
    void BeginHalo3AvatarPair(const void* centerCamera);
    void EndHalo3AvatarPair();
    void ObserveHalo3AvatarFp(uint16_t tag, const BoneMatrix* destination);
    bool DisableAndRemoveHalo3ContactMelee();
    void ReportHalo3ContactMelee();
    void Halo3PublishContactHand(int hand, const FpInterpolationContext& context, const BoneMatrix& root,
        const BoneMatrix* solved, const float points[][3], int count, int32_t unit, uint32_t generation);
    bool Halo3RedirectContactVector(uintptr_t caller, uint64_t flags, int32_t mode, int32_t ignoredA, int32_t ignoredB,
        int32_t ignoredC, void* result, uint8_t& returned);
    bool DesiredWristWorld(bool left, BoneMatrix& out, float& meshScale, float* collisionCorrection = nullptr);
    extern std::atomic<bool> g_renderHooked;
    extern std::atomic<float> g_projectionTanX;
    extern std::atomic<float> g_projectionTanY;
    extern std::atomic<float> g_renderHalfFovX;
    extern std::atomic<float> g_renderHalfFovY;
    extern std::atomic<int> g_fpBoneCount[2];
    extern std::atomic<int> g_fpWristIndex[2];
    extern std::atomic<uint64_t> g_fpWristDescendants[2];
    extern std::atomic<int> g_fpLWristIndex[2];
    extern LegacyWorldCollisionFeature g_halo3WorldCollision;
    extern std::atomic<uint32_t> g_authoredCrosshairColorState;
    extern thread_local uint32_t g_authoredCrosshairColorStateAccum;
    bool FindFirstPersonWeapon(BoneMatrix* bones, int& outSlot, unsigned char*& outWeapon);
    bool GetControllerFirstPersonTransform(int slot, float target[3], float desired[9]);
    extern alignas(8) volatile uintptr_t g_fpSkipBounds[4];
    // Defined by .inl files game.cpp includes in game_core (body_visibility_switch, halo3_collision_dispatch,
    // halo3_melee_selection via halo3_contact_melee_runtime); Halo 3's camera install calls them.
    void ResolveBodyVars(uintptr_t base, size_t size);
    void ApplyBodySetting();
    uint8_t __fastcall Halo3CollisionResolveDetour(uint64_t flags, int32_t mode, const float* start,
        const float* desired, int32_t ignoredObjectA, int32_t ignoredObjectB, int32_t ignoredObjectC, void* result);
    const uint8_t* Halo3MeleeSelectionObject(uint32_t handle, uint8_t kind);
} // namespace game_core

// Defined in game.cpp at file scope; halo4_vr_turn.inl calls it.
void Game_NoteSnapTurn(GameTitle title) noexcept;
