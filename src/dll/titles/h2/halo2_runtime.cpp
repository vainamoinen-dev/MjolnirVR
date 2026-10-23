// halo2_runtime.cpp: the Halo 2 runtime glue that lived in game.cpp (moved unchanged): the stereo lifecycle
// publication, the observation and stereo ticks, AutoVr presentation claim and 6DOF tick, controller aim and
// look-pitch sticks, the aim-assist override and the seat and gyro-hull publishers (Game_Halo2* in game.h).
// Core reaches it through halo2_bridge.h and game.h; it reaches core through game_internal.h. The frame cap,
// the steering-author block and the pause intent stay in game.cpp.
#include "../../core/game_internal.h"
#include "halo2_bridge.h"
using namespace game_core;
using namespace halo2_bridge;

namespace
{
    // Matches kHalo2Capabilities in title_registry.cpp. ControllerAim
    // owns the visible first-person hands/gun mesh and the bullet direction
    // only. The physical right stick still turns the character and camera and
    // the headset still owns pitch, because Game_ComputeAimStick refuses
    // Halo 2 before this mask is ever consulted.
    constexpr uint32_t kHalo2Stereo6DofRuntimeCapabilities =
        TitleCapability_Stereo | TitleCapability_ControllerAim | TitleCapability_Hud | TitleCapability_RuntimeModes |
        TitleCapability_RoomScale | TitleCapability_ControllerInput | TitleCapability_Haptics;
    constexpr uint32_t kHalo2TemporalRuntimeCapabilities = TitleCapability_Stereo;
    constinit std::atomic<uint64_t> g_halo2TurretSeatMs{0};
    constinit std::atomic<bool> g_halo2TurretSeat{false};
} // namespace
namespace halo2_bridge
{
    bool Halo2TurretSeatNow()
    {
        const uint64_t at = g_halo2TurretSeatMs.load(std::memory_order_acquire);
        const uint64_t now = GetTickCount64();
        return TitleAdapter_GetActiveTitle() == GameTitle::Halo2 && g_halo2TurretSeat.load(std::memory_order_relaxed) &&
               at && now >= at && now - at <= 200 && Halo2Observer6Dof_VehicleControlActive();
    }
} // namespace halo2_bridge
namespace
{
    struct Halo2AimAssistOverride
    {
        uint8_t* slot = nullptr;
        uint8_t stock = 0;
        uint64_t reassertions = 0;
    };
    constinit Halo2AimAssistOverride g_halo2AimAssistOverride{};
    bool PublishHalo2StereoLifecycle(uint32_t generation)
    {
        if (!generation)
            return false;
        TitleRuntimeLifecycle lifecycle{};
        lifecycle.installed = Halo2Stereo_Installed() || Halo2AnniversaryStereo_Installed();
        lifecycle.armed = Halo2Stereo_Armed() || Halo2AnniversaryStereo_Armed();
        lifecycle.teardownRequested = lifecycle.installed && !lifecycle.armed;
        lifecycle.enabledCapabilities =
            lifecycle.installed && lifecycle.armed ? kHalo2Stereo6DofRuntimeCapabilities : TitleCapability_None;

        static uint32_t publishedGeneration = 0;
        static uint32_t publishedState = 0xFFFFFFFFu;
        const uint32_t state = (lifecycle.installed ? 1u : 0u) | (lifecycle.armed ? 2u : 0u) |
                               (lifecycle.teardownRequested ? 4u : 0u) | (lifecycle.enabledCapabilities << 3);
        if (generation == publishedGeneration && state == publishedState)
            return true;
        const bool published = TitleAdapter_PublishLifecycle(GameTitle::Halo2, generation, lifecycle);
        if (published)
        {
            publishedGeneration = generation;
            publishedState = state;
        }
        return published;
    }
    constinit ReachGyroHullPublication g_halo2GyroHull; // same layout, Halo 2 mounted gunners
} // namespace
namespace halo2_bridge
{
    // Halo 2 observer, render-mode guard, render probe and native HUD: they run before the tunable profile.
    void TickHalo2Observation(const TitleTickContext& c)
    {
        const TitleDescriptor* activeTitle = c.activeTitle;
        const bool activeLevelRunning = c.activeLevelRunning;
        const uint64_t pollNow = c.now;
        (void)pollNow;
        const bool halo2Active = activeTitle && activeTitle->title == GameTitle::Halo2;
        const uintptr_t& halo2GateBase = g_supervisorTitles.halo2Gate.base;
        const size_t& halo2GateSize = g_supervisorTitles.halo2Gate.size;
        const bool& halo2GateSampled = g_supervisorTitles.halo2Gate.sampled;
        if (!halo2Active)
            Halo2ColdObservation_Rearm();
        {
            // The observer is the one camera root BOTH halo2
            // renderers consume, so this owns the headset pose in the
            // classic and the remastered mode alike.
            const uint32_t halo2ObserverGeneration = TitleAdapter_GetGeneration(GameTitle::Halo2);
            const bool halo2ObserverVrAvailable = !g_vrRuntimeFailureLatched.load(std::memory_order_acquire);
            (void)Halo2Observer6Dof_Poll(halo2GateBase, halo2GateSize, halo2ObserverGeneration,
                halo2Active && halo2GateSampled && halo2ObserverVrAvailable, activeLevelRunning,
                Halo2ColdObservation_Passed(halo2ObserverGeneration),
                Halo2ColdObservation_ObserverResultArray(halo2ObserverGeneration));
        }
        {
            // Look tutorials (halo2_look_tests.h): head look reaches the
            // action-test builder while a script polls a look test.
            // Installed/removed with the observer's optional features.
            Halo2LookTests_SetEnabled(g_config.halo2_tutorial_head_look);
            Halo2LookTests_Report();
        }
        {
            // The renderer switch guard lives in both graphics
            // modes: it is the applier that flips between them.
            const uint32_t halo2GuardGeneration = TitleAdapter_GetGeneration(GameTitle::Halo2);
            (void)Halo2RenderModeGuard_Poll(halo2GateBase, halo2GateSize, halo2GuardGeneration,
                halo2Active && halo2GateSampled, activeLevelRunning, Halo2ColdObservation_Passed(halo2GuardGeneration));
        }
        {
            // Own the proven Blam gameplay-CHUD draw once for the
            // title, before either renderer-specific stereo core polls.
            // This exposes one exact TLS scope to the field-proven D3D11
            // HUD shaders across a Classic/Anniversary switch.
            const uint32_t halo2HudGeneration = TitleAdapter_GetGeneration(GameTitle::Halo2);
            const bool halo2HudVrAvailable = !g_vrRuntimeFailureLatched.load(std::memory_order_acquire);
            (void)Halo2NativeHud_Poll(halo2GateBase, halo2GateSize, halo2HudGeneration,
                halo2Active && halo2GateSampled && halo2HudVrAvailable, activeLevelRunning,
                Halo2ColdObservation_Passed(halo2HudGeneration));
        }
    }
    // Halo 2 stereo cores (Anniversary, then classic): they run after the tunable profile.
    void TickHalo2Stereo(const TitleTickContext& c)
    {
        const TitleDescriptor* activeTitle = c.activeTitle;
        const bool activeLevelRunning = c.activeLevelRunning;
        const uint64_t pollNow = c.now;
        (void)pollNow;
        const bool halo2Active = activeTitle && activeTitle->title == GameTitle::Halo2;
        const uintptr_t& halo2GateBase = g_supervisorTitles.halo2Gate.base;
        const size_t& halo2GateSize = g_supervisorTitles.halo2Gate.size;
        const bool& halo2GateSampled = g_supervisorTitles.halo2Gate.sampled;
        {
            // The classic core cannot serve Anniversary: with
            // those graphics selected render_player_window is never
            // called at all. This runs the remastered scene render once
            // per eye instead, and only while that renderer is live.
            const uint32_t halo2AnniversaryGeneration = TitleAdapter_GetGeneration(GameTitle::Halo2);
            const bool halo2AnniversaryVrAvailable = !g_vrRuntimeFailureLatched.load(std::memory_order_acquire);
            // The frame cap, in both renderers (classic and Anniversary).
            Halo2FrameCapTick(halo2GateBase, halo2GateSize, halo2AnniversaryGeneration,
                halo2Active && halo2GateSampled && halo2AnniversaryVrAvailable && activeLevelRunning);
            (void)Halo2AnniversaryStereo_Poll(halo2GateBase, halo2GateSize, halo2AnniversaryGeneration,
                halo2Active && halo2GateSampled && halo2AnniversaryVrAvailable, activeLevelRunning,
                Halo2ColdObservation_Passed(halo2AnniversaryGeneration),
                Halo2ColdObservation_GraphicsMode(halo2AnniversaryGeneration) == Halo2GraphicsMode::Remastered,
                Halo2ColdObservation_ObserverResultArray(halo2AnniversaryGeneration));
        }
        {
            const uint32_t halo2Generation = TitleAdapter_GetGeneration(GameTitle::Halo2);
            const bool halo2VrRuntimeAvailable = !g_vrRuntimeFailureLatched.load(std::memory_order_acquire);
            const bool halo2CoreReady = Halo2Stereo_Poll(halo2GateBase, halo2GateSize, halo2Generation,
                halo2Active && halo2GateSampled && halo2VrRuntimeAvailable, activeLevelRunning,
                Halo2ColdObservation_Passed(halo2Generation),
                Halo2ColdObservation_ClassicRenderTreeRuns(halo2Generation));
            PublishHalo2StereoLifecycle(halo2Generation);
            // The Anniversary core polled just above; while it is the
            // armed one, the classic core being stock is not a reason
            // to revoke the title's heartbeat every tick.
            if (halo2Generation && !halo2CoreReady && !Halo2AnniversaryStereo_Armed())
                TitleAdapter_ClearHeartbeat(GameTitle::Halo2, halo2Generation);
        }
    }
} // namespace halo2_bridge
bool Game_ComputeHalo2SnapStick(float& outRx)
{
    outRx = 0;
    static Halo4PitchServo servo;
    static uint32_t generation = 0;
    static uint64_t serial = 0, changedAt = 0;
    static float held = 0;
    const auto reset = [&]()
    {
        Halo4ResetPitchServo(servo);
        serial = changedAt = 0;
        held = 0;
    };
    if (!Game_Halo2OwnsLookPitch() || Halo2Observer6Dof_VehicleControlActive())
    {
        reset();
        generation = 0;
        return false;
    }
    if (Menu_IsOpen() || VR_IsPausePresentation() || VR_IsCutsceneTheaterActive())
    {
        reset();
        return true;
    }
    Halo2ObserverPosePublication publication{};
    if (!Halo2Observer6Dof_ReadPublishedPose(publication) || !publication.serial || !publication.generation)
    {
        reset();
        return !g_config.turn_smooth;
    }
    if (generation != publication.generation)
    {
        reset();
        generation = publication.generation;
    }
    if (!publication.snapTurnPending)
    {
        reset();
        return !g_config.turn_smooth;
    }
    const uint64_t now = GetTickCount64();
    if (serial == publication.serial)
    {
        if (!changedAt || now < changedAt || now - changedAt > 250)
        {
            Halo4ResetPitchServo(servo);
            held = 0;
            return true;
        }
        outRx = held;
        return true;
    }
    serial = publication.serial;
    changedAt = now;
    const float engine = std::atan2(publication.stock.forward[1], publication.stock.forward[0]);
    const float target = publication.snapTurnTargetYaw;
    if (!std::isfinite(engine) || !std::isfinite(target))
    {
        reset();
        return true;
    }
    // Preserve native look-rate/deadzone handling. The target comes only from
    // a deliberate thumbstick edge, not the rejected on-foot hand-aim servo.
    const float continuous =
        servo.haveLastEnginePitch ? servo.lastEnginePitch + WrapPi(engine - servo.lastEnginePitch) : engine;
    held = Halo4PitchServoStep(servo, continuous, continuous + WrapPi(target - engine), kHalo4PitchServoGain);
    if (held == 0 && servo.axis.resting)
        Halo2Observer6Dof_SnapTurnSettled(publication.generation, target);
    outRx = held;
    return true;
}

bool Game_Halo2OwnsLookPitch()
{
    return TitleAdapter_GetActiveTitle() == GameTitle::Halo2 && Halo2Observer6Dof_Armed() &&
           g_enabled.load(std::memory_order_acquire) && VR_IsStereoEnabled();
}

// Same construction as Game_ComputeHalo4PitchStick: the engine's
// pitch is read from the observer the core already publishes every frame
// (the stock camera it found), the target is the head's pitch, and the
// shared servo measures the stick's direction and resolution itself. The
// observer core composes the head onto a yaw-only camera, so the view never
// double-pitches while the engine converges.
bool Game_ComputeHalo2PitchStick(float& outRy)
{
    outRy = 0.0f;
    static Halo4PitchServo servo;
    static uint64_t lastSerial = 0;
    static uint64_t lastSerialChangeMs = 0;
    static float heldCommand = 0.0f;
    static std::atomic<int> lastBlock{-1};
    auto blocked = [](int reason, const char* what)
    {
        if (lastBlock.exchange(reason) != reason)
            LOG("Halo 2 look pitch: loop idle: %s", what);
        return false;
    };
    if (!Game_Halo2OwnsLookPitch())
    {
        Halo4ResetPitchServo(servo);
        lastSerial = 0;
        lastSerialChangeMs = 0;
        heldCommand = 0.0f;
        return false;
    }
    Halo2ObserverPosePublication publication{};
    if (!Halo2Observer6Dof_ReadPublishedPose(publication) || !publication.generation || !publication.serial)
    {
        return blocked(1, "the observer has not published a camera yet");
    }
    const uint64_t now = GetTickCount64();
    bool freshFrame = false;
    if (publication.serial != lastSerial)
    {
        lastSerial = publication.serial;
        lastSerialChangeMs = now;
        freshFrame = true;
    }
    else if (!lastSerialChangeMs || now - lastSerialChangeMs > 250)
    {
        Halo4ResetPitchServo(servo);
        heldCommand = 0.0f;
        return blocked(2, "no observer-owned Halo 2 frame in the last 250 ms");
    }
    // MCC polls XInput several times per rendered frame (measured): step once
    // per new publication and hold the command across the polls in between.
    if (!freshFrame)
    {
        outRy = heldCommand;
        return true;
    }
    float q[4], p[3];
    float headPitch = 0.0f;
    float enginePitch = 0.0f;
    if (!VR_GetHeadPose(q, p) || !Halo2HeadPitchRadians(q, headPitch))
    {
        Halo4ResetPitchServo(servo);
        heldCommand = 0.0f;
        return blocked(3, "headset not tracked");
    }
    if (!Halo2EnginePitchRadians(publication.stock, enginePitch))
    {
        Halo4ResetPitchServo(servo);
        heldCommand = 0.0f;
        return blocked(4, "the engine camera pitch could not be read");
    }
    lastBlock.store(0, std::memory_order_relaxed);
    const float target = Clamp(headPitch, -1.5f, 1.5f);
    outRy = Halo4PitchServoStep(servo, enginePitch, target, kHalo4PitchServoGain);
    heldCommand = outRy;
    static uint64_t lastReportMs = 0;
    if (now - lastReportMs >= 5000)
    {
        lastReportMs = now;
        LOG("Halo 2 look pitch: head %.1f deg, engine %.1f deg, command %.2f, "
            "direction %+.0f, commanded=%llu parked=%llu",
            headPitch * 57.29578f, enginePitch * 57.29578f, outRy, servo.direction,
            static_cast<unsigned long long>(servo.commandedFrames),
            static_cast<unsigned long long>(servo.parkedFrames));
    }
    return true;
}
bool Game_UsesHalo2NativeHudLayout()
{
    return TitleAdapter_GetActiveTitle() == GameTitle::Halo2 && Halo2NativeHud_Armed() &&
           (Halo2Stereo_Armed() || Halo2AnniversaryStereo_Armed()) && Game_HasTitleCapability(TitleCapability_Hud);
}
// Halo 2 presentation claim state shared by the claim gate (runs first every tick) and the 6DOF section.
struct Halo2PresentationState
{
    bool wasClaimContext = false;
    bool pauseClearRequested = false;
    bool cadenceStateKnown = false;
    bool cadenceWasSupported = false;
    uint64_t cadenceLastLogMs = 0;
    bool claimContext = false;         // this tick
    bool enteringClaimContext = false; // this tick
};
constinit static Halo2PresentationState g_halo2Presentation;
namespace halo2_bridge
{
    // Some title-specific branches return early. Close H2's atomic claim gate before any of them can take
    // ownership, even when the ordinary H2 presentation cleanup is deferred.
    void AutoVrHalo2ClaimGate()
    {
        auto& h = g_halo2Presentation;
        h.claimContext = TitleAdapter_GetActiveTitle() == GameTitle::Halo2;
        h.enteringClaimContext = h.claimContext && !h.wasClaimContext;
        if (!h.claimContext && h.wasClaimContext)
        {
            Halo2Stereo_SetPresentationReady(false);
            h.pauseClearRequested = false;
            h.cadenceStateKnown = false;
        }
        h.wasClaimContext = h.claimContext;
    }
    // Halo 2 simultaneous stereo + 6DOF presentation. Returns true when Halo 2 owned the tick.
    bool AutoVrTickHalo2Stereo6Dof()
    {
        bool& halo2PauseClearRequested = g_halo2Presentation.pauseClearRequested;
        bool& halo2CadenceStateKnown = g_halo2Presentation.cadenceStateKnown;
        bool& halo2CadenceWasSupported = g_halo2Presentation.cadenceWasSupported;
        uint64_t& halo2CadenceLastLogMs = g_halo2Presentation.cadenceLastLogMs;
        const bool halo2ClaimContext = g_halo2Presentation.claimContext;
        const bool enteringHalo2ClaimContext = g_halo2Presentation.enteringClaimContext;
        static bool wasHalo2Stereo6DofContext = false;
        static Halo2PauseResumeClock halo2PauseResumeClock;
        static Halo2PauseEntryClock halo2PauseEntryClock;
        if (halo2ClaimContext)
        {
            // This atomic gate starts false in the H2 core. Publish false before
            // touching inherited presentation state so no render hook can claim a
            // frame during the comfort-fade back from another title's 2D pause.
            if (enteringHalo2ClaimContext)
                Halo2Stereo_SetPresentationReady(false);
            wasHalo2Stereo6DofContext = true;
            // Either Halo 2 core makes stereo usable. Consulting only the
            // classic core forced stereo OFF within ~100 ms of every switch to
            // Anniversary graphics ("M2 alternate-eye stereo OFF"), which is
            // why the Anniversary detour fired 791 times and rendered 0 pairs.
            const bool halo2StereoUsable = Halo2Stereo_Armed() || Halo2AnniversaryStereo_Armed();
            const bool pauseTarget = VR_IsPausePresentationTarget();
            const bool pausePresentation = VR_IsPausePresentation();

            // Y+B is only an edge fallback for H2, so selecting Resume
            // inside the native pause menu produces no second Start/Y+B edge for
            // the VR presentation. Do not guess from A/B. Prove that the H2EK
            // game-time clock first froze while the head-locked pause screen was
            // actually displayed, then require two distinct native ticks after it
            // starts advancing again before requesting stereo.
            bool halo2GameTimeInitialized = false;
            uint32_t halo2GameTimeTick = 0;
            const bool halo2GameTimeValid =
                pausePresentation && Halo2ColdObservation_ReadGameTime(halo2GameTimeInitialized, halo2GameTimeTick);
            if (halo2PauseResumeClock.Update(
                    true, pausePresentation, halo2GameTimeValid, halo2GameTimeInitialized, halo2GameTimeTick))
            {
                Halo2Stereo_SetPresentationReady(false);
                VR_RequestPausePresentation(false);
                LOG("Halo 2 pause presentation: native game-time clock resumed; "
                    "restoring stereo 3D");
            }
            // Entry: Menu/Start, then the same clock stops (no native pause flag
            // is proven for H2; the clock is).
            if (const uint64_t intent = g_halo2PauseIntentMs.exchange(0, std::memory_order_acq_rel))
                halo2PauseEntryClock.NoteIntent(intent);
            if (halo2PauseEntryClock.Pending())
            {
                bool entryInitialized = false;
                uint32_t entryTick = 0;
                const bool entryValid = Halo2ColdObservation_ReadGameTime(entryInitialized, entryTick);
                if (halo2PauseEntryClock.Update(GetTickCount64(), g_config.halo2_start_pause,
                        pauseTarget || pausePresentation, entryValid, entryInitialized, entryTick))
                {
                    VR_RequestPausePresentation(true);
                    LOG("Halo 2 pause presentation: Menu/Start froze the game-time clock; "
                        "switching to head-locked 2D");
                }
            }

            const bool mustClearForeignPause =
                Halo2MustClearForeignPause(true, enteringHalo2ClaimContext, pauseTarget, pausePresentation);
            Halo2PreparedCadenceSnapshot halo2Cadence{};
            const bool halo2CadenceCurrent = VR_Halo2GetCurrentPreparedCadence(halo2Cadence);
            const bool halo2CadenceSupported =
                halo2CadenceCurrent && Halo2PreparedCadenceSupported(
                                           halo2Cadence.predictedDisplayPeriodNs, halo2Cadence.predictedDisplayDeltaNs);
            const float halo2TargetCadenceHz = Halo2CadenceHz(halo2Cadence.predictedDisplayPeriodNs);
            const float halo2DeliveredCadenceHz = Halo2CadenceHz(halo2Cadence.predictedDisplayDeltaNs);
            if (mustClearForeignPause || !halo2CadenceSupported)
                Halo2Stereo_SetPresentationReady(false);
            if (mustClearForeignPause)
            {
                // VR_RequestPausePresentation requeues the comfort fade whenever
                // current != target. Issue exactly one request for this clear
                // episode; repeating it every Present would restart FadeOut before
                // it could ever finish.
                if (Halo2ShouldRequestForeignPauseClear(
                        true, enteringHalo2ClaimContext, pauseTarget, pausePresentation, halo2PauseClearRequested))
                {
                    VR_RequestPausePresentation(false);
                    LOG("Halo 2 presentation: clearing foreign pause/head-lock "
                        "state before stereo claims");
                    halo2PauseClearRequested = true;
                }
            }
            else
                halo2PauseClearRequested = false;

            // Stable xrWaitFrame periods produce no render-thread log traffic. A
            // two-second bound also prevents a runtime hovering at an exact range
            // boundary from alternating BLOCKED/OPEN every Present. Reset state on a
            // fresh title entry so each test run records its actual admission.
            if (enteringHalo2ClaimContext)
                halo2CadenceStateKnown = false;
            const uint64_t halo2CadenceNowMs = GetTickCount64();
            const bool halo2CadenceChanged =
                !halo2CadenceStateKnown || halo2CadenceSupported != halo2CadenceWasSupported;
            const bool halo2CadenceLogReady = !halo2CadenceStateKnown || halo2CadenceNowMs < halo2CadenceLastLogMs ||
                                              halo2CadenceNowMs - halo2CadenceLastLogMs >= 2000;
            if (halo2CadenceChanged && halo2CadenceLogReady)
            {
                if (halo2CadenceSupported)
                {
                    LOG("Halo 2 cadence gate OPEN: xrWaitFrame target %.1fHz and "
                        "predicted-display delivery %.1fHz (advisory 4-1000Hz "
                        "liveness band; a slow frame still gets its pair)",
                        halo2TargetCadenceHz, halo2DeliveredCadenceHz);
                }
                else
                {
                    LOG("Halo 2 cadence gate BLOCKED: xrWaitFrame target %.1fHz, "
                        "predicted-display delivery %.1fHz (current=%d); both "
                        "must be within the 4-1000Hz liveness band, keeping "
                        "stock unclaimed presentation",
                        halo2TargetCadenceHz, halo2DeliveredCadenceHz, halo2CadenceCurrent ? 1 : 0);
                }
                halo2CadenceStateKnown = true;
                halo2CadenceWasSupported = halo2CadenceSupported;
                halo2CadenceLastLogMs = halo2CadenceNowMs;
            }

            // Match the universal auto_vr contract. A manual F2 veto owns the
            // remainder of this level; a config-off transition releases only the
            // presentation that auto_vr itself armed.
            if ((!g_config.auto_vr && !ManualVrRecoveryFor(GameTitle::Halo2)) ||
                g_autoVrUserVeto.load(std::memory_order_acquire))
            {
                Halo2Stereo_SetPresentationReady(false);
                if (g_autoVrOwned.load(std::memory_order_acquire))
                {
                    g_enabled.store(false, std::memory_order_release);
                    g_autoVrOwned.store(false, std::memory_order_release);
                    VR_DetachGamePresentation();
                }
                return true;
            }

            const bool presentationMayClaim = Halo2PresentationMayClaim(true, halo2StereoUsable, true,
                VR_IsPausePresentationTarget(), VR_IsPausePresentation(), halo2Cadence.predictedDisplayPeriodNs,
                halo2Cadence.predictedDisplayDeltaNs);
            if (presentationMayClaim)
            {
                if (!g_enabled.load(std::memory_order_relaxed) || !VR_IsStereoEnabled())
                {
                    g_enabled.store(true, std::memory_order_release);
                    g_needRecenter.store(true, std::memory_order_release);
                    Halo2Stereo_RequestRecenter();
                    Halo2Observer6Dof_RequestRecenter();
                    Halo2AnniversaryStereo_RequestRecenter();
                    g_autoVrOwned.store(true, std::memory_order_release);
                    if (!VR_IsStereoEnabled())
                        VR_ToggleStereo();
                    LOG("Halo 2 camera core armed: a complete exact-current "
                        "pair will use simultaneous stereo + 6DOF; an unclaimed "
                        "no-pair frame keeps the stock screen-quad path, while a "
                        "partially claimed failure drops that frame only and the "
                        "core stays armed");
                }
                // Publish last: g_enabled, recenter, and the shared stereo path are
                // fully ready before a game render is allowed to claim ownership.
                Halo2Stereo_SetPresentationReady(true);
            }
            else
            {
                Halo2Stereo_SetPresentationReady(false);
                if (!halo2StereoUsable && (g_enabled.load(std::memory_order_relaxed) || VR_IsStereoEnabled() ||
                                              g_autoVrOwned.load(std::memory_order_relaxed)))
                {
                    g_enabled.store(false, std::memory_order_release);
                    g_autoVrOwned.store(false, std::memory_order_release);
                    VR_DetachGamePresentation();
                }
            }
            return true;
        }
        if (wasHalo2Stereo6DofContext)
        {
            Halo2Stereo_SetPresentationReady(false);
            wasHalo2Stereo6DofContext = false;
            halo2PauseResumeClock.Reset();
            halo2PauseClearRequested = false;
            halo2CadenceStateKnown = false;
            g_enabled.store(false, std::memory_order_release);
            g_autoVrOwned.store(false, std::memory_order_release);
            g_autoVrUserVeto.store(false, std::memory_order_release);
            VR_DetachGamePresentation();
        }
        return false;
    }
} // namespace halo2_bridge
bool Game_Halo2ControllerAimActive()
{
    // Ownership is granted only after the registered visible render
    // consumer has accepted both hands and the held model recently. Rejected
    // interpolator and post-return packet paths remain permanently separate.
    return kHalo2VisibleConsumerControllerOwnershipEnabled && TitleAdapter_GetActiveTitle() == GameTitle::Halo2 &&
           Game_HasTitleCapability(TitleCapability_ControllerAim) && Halo2Observer6Dof_Armed() &&
           Halo2Observer6Dof_FinalPaletteArmed() && g_enabled.load(std::memory_order_acquire) &&
           g_vrAim.load(std::memory_order_acquire) && VR_IsStereoEnabled();
}
namespace halo2_bridge
{
    // H2EK units.cpp proves stick look updates unit->aiming_vector at
    // +0x174. weapons.cpp's native firing helper copies that exact vector into the
    // direction later passed to weapon_barrel_simulation_action and projectile
    // creation. Close the ordinary stick loop on the stock observer camera, the
    // same native aim feedback the headset-proven pitch loop already uses.
    bool ComputeHalo2ControllerAimStick(const float controllerOrientation[4], const float controllerPosition[3],
        const float /*headOrientation*/[4], const float /*headPosition*/[3], float& outRx, float& outRy)
    {
        static Halo4PitchServo yawServo;
        static Halo4PitchServo pitchServo;
        static uint64_t lastSerial = 0;
        static uint64_t lastSerialChangeMs = 0;
        static uint32_t lastGeneration = 0;
        static uint32_t lastVehicleUnit = UINT32_MAX, lastVehicleParent = UINT32_MAX;
        static int16_t lastVehicleSeat = -1;
        static uint64_t lastCallMs = 0;
        static float heldRx = 0.0f;
        static float heldRy = 0.0f;
        static Halo2CameraBasis steeringReference{};
        // Earlier input-only investigation remains inactive. The observer now
        // publishes one vehicle reference consumed by view, hands and steering.
        constexpr bool useFrozenSteeringReference = false;
        static uint64_t referenceEpoch = 0;
        static bool haveSteeringReference = false;
        static std::atomic<int> lastBlock{-1};
        auto blocked = [&](int reason, const char* what)
        {
            if (lastBlock.exchange(reason) != reason)
                LOG("Halo 2 controller aim: loop idle: %s", what);
            ClearAimClamped();
            Halo4ResetPitchServo(yawServo);
            Halo4ResetPitchServo(pitchServo);
            haveSteeringReference = false;
            lastSerial = 0;
            lastSerialChangeMs = 0;
            heldRx = heldRy = 0.0f;
            return false;
        };
        Halo2ObserverPosePublication publication{};
        if (!Halo2Observer6Dof_ReadPublishedPose(publication) || !publication.generation || !publication.serial)
        {
            return blocked(1, "the observer has not published a camera yet");
        }
        if (!publication.vehicleReferenceValid)
            return blocked(9, "no coherent vehicle view/controller reference; controller steering withheld");
        const uint64_t now = GetTickCount64();
        if (publication.generation != lastGeneration || publication.vehicleUnit != lastVehicleUnit ||
            publication.vehicleParent != lastVehicleParent || publication.vehicleSeat != lastVehicleSeat ||
            !lastCallMs || now < lastCallMs || now - lastCallMs > 100 ||
            referenceEpoch != publication.snapshot.trackingSpaceEpoch)
        {
            Halo4ResetPitchServo(yawServo);
            Halo4ResetPitchServo(pitchServo);
            haveSteeringReference = false;
            lastSerial = lastSerialChangeMs = 0;
            heldRx = heldRy = 0.0f;
        }
        lastGeneration = publication.generation;
        referenceEpoch = publication.snapshot.trackingSpaceEpoch;
        lastVehicleUnit = publication.vehicleUnit;
        lastVehicleParent = publication.vehicleParent;
        lastVehicleSeat = publication.vehicleSeat;
        lastCallMs = now;
        if (publication.serial == lastSerial)
        {
            if (!lastSerialChangeMs || now - lastSerialChangeMs > 250)
                return blocked(2, "no observer-owned Halo 2 frame in the last 250 ms");
            outRx = heldRx;
            outRy = heldRy;
            return true;
        }
        lastSerial = publication.serial;
        lastSerialChangeMs = now;

        // The native camera is the FEEDBACK, not the target's world reference.
        // Reusing its changing yaw in the target makes a held hand offset command
        // perpetual rotation. Keep a room-to-world heading until re-entry/recenter;
        // translate it with the vehicle without feeding camera yaw back into it.
        if constexpr (useFrozenSteeringReference)
            if (!haveSteeringReference)
            {
                steeringReference = publication.stock;
                referenceEpoch = publication.snapshot.trackingSpaceEpoch;
                haveSteeringReference = true;
            }
        memcpy(steeringReference.position, publication.stock.position, sizeof(steeringReference.position));
        Halo2CameraBasis controller{};
        const float worldScale = Game_GetWorldScale();
        const bool carrierBuilt = useFrozenSteeringReference
                                      ? Halo2BuildStableControllerCarrier(steeringReference,
                                            publication.referenceOrientation, publication.referencePosition,
                                            controllerOrientation, controllerPosition, worldScale, 0.0f, controller)
                                      : Halo2BuildStableControllerCarrier(Halo2ControllerReference(publication),
                                            publication.referenceOrientation, publication.referencePosition,
                                            controllerOrientation, controllerPosition, worldScale, 0.0f, controller);
        if (!carrierBuilt)
        {
            return blocked(3, "the controller carrier could not be built");
        }

        // Projectiles originate at the engine's view, while the reticle lies on
        // the hand ray. Aim the native view-origin ray through that reticle point.
        const float rangeMeters = Clamp(g_config.crosshair_distance_m, 2.0f, 50.0f);
        const float rangeWorld = rangeMeters * worldScale;
        float direction[3]{};
        float lengthSquared = 0.0f;
        for (int axis = 0; axis < 3; ++axis)
        {
            const float target = controller.position[axis] + controller.forward[axis] * rangeWorld;
            direction[axis] = target - publication.tracked.position[axis];
            lengthSquared += direction[axis] * direction[axis];
        }
        const float length = std::sqrt(lengthSquared);
        if (!std::isfinite(length) || length < 1.0e-4f)
            return blocked(4, "the controller sight line was not finite");
        for (float& component : direction)
            component /= length;

        const float desiredYaw = std::atan2(direction[1], direction[0]);
        const float desiredPitch = std::asin(Clamp(direction[2], -1.0f, 1.0f));
        const float engineYaw = std::atan2(publication.stock.forward[1], publication.stock.forward[0]);
        float enginePitch = 0.0f;
        if (!std::isfinite(desiredYaw) || !std::isfinite(desiredPitch) || !std::isfinite(engineYaw) ||
            !Halo2EnginePitchRadians(publication.stock, enginePitch))
        {
            return blocked(5, "the native aim feedback was not finite");
        }
        // atan2 wraps at +/-pi. Keep the servo's feedback continuous across that
        // seam or one legitimate turn would look like a 360-degree reversal and
        // poison its learned X-axis direction.
        const float continuousEngineYaw = yawServo.haveLastEnginePitch
                                              ? yawServo.lastEnginePitch + WrapPi(engineYaw - yawServo.lastEnginePitch)
                                              : engineYaw;
        const float nearestDesiredYaw = continuousEngineYaw + WrapPi(desiredYaw - engineYaw);
        outRx = Halo4PitchServoStep(yawServo, continuousEngineYaw, nearestDesiredYaw, kHalo4PitchServoGain);
        outRy = Halo4PitchServoStep(pitchServo, enginePitch, desiredPitch, kHalo4PitchServoGain);
        heldRx = outRx;
        heldRy = outRy;
        // Turret gyro (Halo 2 mounted gunners): the hull rate fed forward. This
        // servo LEARNS each axis' stick direction, so the compensation goes
        // through that learned direction; the servo's own lastCommand stays the
        // servo's (the gyro cancels the hull, leaving the servo's motion observed).
        {
            static turret_gyro::Gyro gyro;
            static uint64_t lastGyroLogMs = 0;
            turret_gyro::Hull hull{};
            uint32_t unusedId = 0;
            const bool source = OccupiedTurretSeat() && GyroReadHull(g_halo2GyroHull, hull, unusedId);
            turret_gyro::Settings gs{};
            gs.strength = g_config.turret_gyro_strength;
            const turret_gyro::Rates rates = turret_gyro::LegacyRatesFor(0, g_config.turret_gyro_rate_scale);
            gs.rateYaw = rates.yaw;
            gs.ratePitch = rates.pitch;
            const turret_gyro::Output g = gyro.Update(g_config.vehicle_turret_gyro && source, hull, engineYaw, now, gs);
            if (g.active)
            {
                // g.rx assumes +stick lowers yaw; the learned direction says which
                // way +stick moves this engine's yaw/pitch. Each axis waits until
                // that direction is learned, so a wrong first guess cannot double
                // the hull drag.
                const float compX =
                    turret_gyro::DirectionLearned(yawServo.directionEvidence, kHalo4PitchServoEvidenceCap)
                        ? -yawServo.direction * g.rx
                        : 0.0f;
                const float compY =
                    turret_gyro::DirectionLearned(pitchServo.directionEvidence, kHalo4PitchServoEvidenceCap)
                        ? pitchServo.direction * g.ry
                        : 0.0f;
                outRx = Clamp(outRx + compX, -1.0f, 1.0f);
                outRy = Clamp(outRy + compY, -1.0f, 1.0f);
                if (now - lastGyroLogMs >= 5000)
                {
                    LOG("turret gyro: title=%d hull dps=(%.1f,%.1f) comp=(%.2f,%.2f)",
                        static_cast<int>(GameTitle::Halo2), g.pitchDps, g.yawDps, compX, compY);
                    lastGyroLogMs = now;
                }
            }
        }
        // Turret seat: the gun is rate-capped, so a hand flick outruns it; the
        // shots leave the BARREL, and the reticle rides it (as Halo 3/ODST/Reach
        // turrets). Every other Halo 2 seat keeps the hand reticle.
        {
            float handForward[3]{};
            const float localForward[3]{0.0f, 0.0f, -1.0f};
            RotateByQuat(controllerOrientation, localForward, handForward);
            const float handYaw = std::atan2(handForward[0], -handForward[2]);
            const float handPitch = std::asin(Clamp(handForward[1], -1.0f, 1.0f));
            float vrYaw = 0.0f, vrPitch = 0.0f;
            if (kAimServoTurretReticleRidesBarrel && OccupiedTurretSeat() && std::isfinite(handYaw) &&
                std::isfinite(handPitch))
            {
                halo2_vehicle_roles::BarrelReticle(
                    handYaw, handPitch, desiredYaw, desiredPitch, engineYaw, enginePitch, vrYaw, vrPitch);
                PublishAimClamped(vrYaw, vrPitch, true);
            }
            else
            {
                ClearAimClamped();
            }
        }
        lastBlock.store(0, std::memory_order_relaxed);
        static uint64_t lastReportMs = 0;
        if (now - lastReportMs >= 5000)
        {
            lastReportMs = now;
            LOG("Halo 2 controller aim: target yaw/pitch %.1f/%.1f deg, native "
                "%.1f/%.1f, command %.2f/%.2f, learned direction %+.0f/%+.0f",
                desiredYaw * 57.29578f, desiredPitch * 57.29578f, engineYaw * 57.29578f, enginePitch * 57.29578f, outRx,
                outRy, yawServo.direction, pitchServo.direction);
        }
        return true;
    }
} // namespace halo2_bridge
bool Game_Halo2TryDisableAimAssist(uintptr_t moduleBase, size_t moduleSize)
{
    // A stale slot would mean the preceding module generation did not reach
    // its normal teardown. Restore it before resolving anything in the new
    // generation; this feature never owns or gates the Halo 2 camera core.
    Game_Halo2RestoreAimAssist();

    auto* const slot = static_cast<uint8_t*>(
        FindDebugVarSlot(moduleBase, moduleSize, kHalo2DisableAimAssistDebugVar, kHalo2DebugVarTypeBoolean));
    uint8_t stock = 0;
    if (!slot || !SafeReadByte(slot, &stock) || !Halo2AimAssistDebugValueValid(stock))
    {
        LOG("Halo 2 aim-assist suppression StockFallback: official "
            "H2EK boolean `%s` did not resolve to a readable boolean slot; "
            "stock aim assist remains active and camera/stereo are unaffected",
            kHalo2DisableAimAssistDebugVar);
        return false;
    }

    const uint8_t disabled = Halo2AimAssistDebugValue(true, stock);
    uint8_t verified = 0;
    if (!SafeWriteByte(slot, disabled) || !SafeReadByte(slot, &verified) || verified != disabled)
    {
        // The first write may have landed even when the verification read did
        // not. Make a best-effort rollback before abandoning this feature.
        (void)SafeWriteByte(slot, stock);
        LOG("Halo 2 aim-assist suppression StockFallback: `%s` was "
            "resolved but could not be set and verified; stock value %u was "
            "restored best-effort and camera/stereo remain active",
            kHalo2DisableAimAssistDebugVar, static_cast<unsigned>(stock));
        return false;
    }

    g_halo2AimAssistOverride = {slot, stock, 0};
    LOG("Halo 2 aim-assist suppression Installed: official H2EK "
        "boolean `%s` set to 1 (stock %u) only for VR-owned Halo 2 gameplay; "
        "camera friction/adhesion, weapon magnetism and melee assistance are "
        "disabled together; teardown restores the captured stock value",
        kHalo2DisableAimAssistDebugVar, static_cast<unsigned>(stock));
    return true;
}

void Game_Halo2MaintainDisabledAimAssist()
{
    auto& state = g_halo2AimAssistOverride;
    if (!state.slot)
        return;
    uint8_t current = 0;
    const uint8_t disabled = Halo2AimAssistDebugValue(true, state.stock);
    if (SafeReadByte(state.slot, &current) && current == disabled)
        return;
    uint8_t verified = 0;
    if (!SafeWriteByte(state.slot, disabled) || !SafeReadByte(state.slot, &verified) || verified != disabled)
    {
        LOG("Halo 2 aim-assist suppression LOST: `%s` could not be "
            "reasserted after the engine changed it; this optional feature "
            "returns to StockFallback and camera/stereo remain active",
            kHalo2DisableAimAssistDebugVar);
        state.slot = nullptr;
        return;
    }
    ++state.reassertions;
    LOG("Halo 2 aim-assist suppression: `%s` was reset by the engine "
        "and reasserted (correction %llu)",
        kHalo2DisableAimAssistDebugVar, static_cast<unsigned long long>(state.reassertions));
}

void Game_Halo2RestoreAimAssist()
{
    auto& state = g_halo2AimAssistOverride;
    if (!state.slot)
        return;
    const uint8_t* const slot = state.slot;
    const uint8_t stock = Halo2AimAssistDebugValue(false, state.stock);
    const uint64_t reassertions = state.reassertions;
    state = {};
    uint8_t verified = 0;
    const bool restored =
        SafeWriteByte(const_cast<uint8_t*>(slot), stock) && SafeReadByte(slot, &verified) && verified == stock;
    LOG("Halo 2 aim-assist suppression removed: stock value %u %s "
        "after %llu engine reassertions; camera-core teardown continues "
        "independently",
        static_cast<unsigned>(stock), restored ? "restored" : "restore FAILED",
        static_cast<unsigned long long>(reassertions));
}
void Game_Halo2PublishGyroHull(const float* forward, uint32_t seatKey)
{
    // Observer thread: the root hull forward of a mounted (non-stationary)
    // gunner seat, or null to clear. seatKey changes with the seat.
    if (forward)
        GyroPublishHull(g_halo2GyroHull, seatKey ? seatKey : 1u, 0u, forward);
    else
        GyroClearHull(g_halo2GyroHull);
}
void Game_Halo2PublishTurretSeat(bool turret)
{
    static bool logged = false;
    if (turret != logged)
    {
        LOG("Halo 2 turret seat: %s",
            turret ? "entered (turret handles available; the reticle rides the barrel)" : "left");
        logged = turret;
    }
    g_halo2TurretSeat.store(turret, std::memory_order_relaxed);
    g_halo2TurretSeatMs.store(GetTickCount64(), std::memory_order_release);
}
void Game_Halo2PublishSteeringAuthor(bool authors, bool wheelClass)
{
    g_halo2SteeringAuthor.store(authors, std::memory_order_relaxed);
    g_halo2SteeringWheel.store(authors && wheelClass, std::memory_order_relaxed);
    g_halo2SteeringAuthorMs.store(GetTickCount64(), std::memory_order_release);
}