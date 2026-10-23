// Controller input: hooks MCC's XInput calls so the VR controllers appear as a gamepad (menus
// included), merges a physical pad, and applies chords, head gestures, vehicle controls and
// F1 menu routing. Installed from game.cpp; tools/generate_haptics_runtime_fixture.py extracts
// parts of it for tests. Its only writes are its own hooks and import-table slots.
#include <windows.h>
#include <Xinput.h>
#include <cmath>
#include <atomic>
#include <mutex>
#include <cstring>
#include <iterator>
#include <MinHook.h>
#include "game.h"
#include "hook_install_check.h" // MinHook create/remove go through the install-time detour check
#include "roomscale.h"
#include "../../common/roomscale_logic.h"
#include "vr.h"
#include "menu.h"
#include "title_adapter.h"
#include "../titles/reach/reach_action_test_guard.h"
void Game_NoteHalo2PauseIntent(uint64_t nowMs); // game.cpp
#include "../../common/log.h"
#include "../../common/config.h"
#include "../../common/titles/h2/halo2_render_logic.h"
#include "../../common/input_logic.h"
#include "../../common/title_traits.h"
#include "../../common/xinput_packet.h"
#include "native_menu_pointer.h"
#include "d3d11_hook.h"
#include "sigscan.h"
#include "hook_quiescence.h"
#include "../../common/minhook_lifecycle.h"
#include "../../common/gated_hook.h"
#include "../../common/exclusive_input.h"
#include "../../common/flashlight_input.h"
#include "../../common/weapon_interaction_logic.h"
#include "../../common/titles/odst/odst_bringup_logic.h"
#include "../../common/scope_action_input.h"
#include "../../common/physical_crouch_input.h"
#include "physical_crouch_camera.h"
#include "guard_count.h"

namespace
{
    // Pad 0 as MCC last received it (fault logger and Reach guard context).
    std::atomic<uint64_t> g_lastPadDeliveryMs{0};
    std::atomic<uint32_t> g_lastPadButtons{0};
    // Neutral pad while Reach loads (reach_neutral_pad_while_loading).
    std::atomic<uint64_t> g_loadingPadSuppressed{0};
    // Physical crouch availability (input thread writes, F1 menu reads).
    std::atomic<uint64_t> g_crouchMissingKey{0};
    uint64_t g_crouchMissingSinceMs = 0;
    std::atomic<bool> g_crouchUnavailable{false};
}
bool Input_PhysicalCrouchUnavailable(GameTitle title)
{
    const uint64_t key = g_crouchMissingKey.load(std::memory_order_relaxed);
    return g_crouchUnavailable.load(std::memory_order_acquire) && key && static_cast<GameTitle>(key & 0xFF) == title;
}

// VR input. MCC reads gamepads through XInputGetState; hooking it lets the
// mod present the Sense controllers as a gamepad the game already understands
// (menus included), and substitute the right stick with the aim steering from
// Game_ComputeAimStick. If no physical gamepad is plugged in, a connected one
// is fabricated. The game then runs all input through its normal sensitivity
// and turn-rate paths, keeping bullets, reticle, vehicles and turrets correct.

namespace
{
    using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
    using XInputGetCapsFn = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);
    using XInputSetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
    // 3 candidate DLLs x 2 entry points (XInputGetState by name, and the
    // undocumented XInputGetStateEx at ordinal 100 that some titles use).
    XInputGetStateFn g_origGetState[6] = {};
    // MCC only polls XInputGetState for slots that XInputGetCapabilities says
    // are connected. With no physical pad powered on, that check fails and the
    // game NEVER reads input — so the connection check is hooked too and
    // reports a standard gamepad on slot 0 whenever the VR controllers live.
    XInputGetCapsFn g_origGetCaps[3] = {};
    XInputSetStateFn g_origSetState[3] = {};
    std::atomic<bool> g_overrideLogged{false};
    MenuChordDetector g_menuChord;
    MenuChordDetector g_pauseChord;
    // MCC reads the pad from two threads; the merge's state
    // machines (chords, menu edge, D-pad gesture, wheel/turret/grab, H4 snap turn) are
    // plain statics, so one poll at a time builds the merged pad. Recursive: import
    // shims may re-enter the hooked export on the same thread.
    std::recursive_mutex g_padMergeLock;
    DpadGesture g_dpadGesture;
    std::atomic<uint64_t> g_dpadGestureUsedMs{0}; // the head D-pad last produced input
    SharedScopeActionInput g_scopeInput;          // one scope state for every MCC pad reader
    thread_local PhysicalCrouchInput g_physicalCrouchInput;
    thread_local vr_mapping::Mapper g_vrActionMapper;
    std::atomic<uint64_t> g_startPulseUntilMs{0};

    // The last button masks fed to the game, newest at g_fedHead-1.
    // One slot per CHANGE of mask (a held button is one entry), so a 600 ms
    // window around an engine event is a handful of entries. Lock-free, the
    // reporter tolerates a torn slot (it only names a mask and an age).
    constexpr unsigned kFedButtonSlots = 16;
    std::atomic<uint32_t> g_fedButtons[kFedButtonSlots]{};
    std::atomic<uint64_t> g_fedButtonsMs[kFedButtonSlots]{};
    std::atomic<unsigned> g_fedHead{0};
    std::atomic<uint32_t> g_fedLastMask{0xFFFFFFFFu};
    std::atomic<uint64_t> g_fedBackMs{0};

    void NoteFedButtons(WORD mask) noexcept
    {
        if (g_fedLastMask.load(std::memory_order_relaxed) == mask)
            return;
        g_fedLastMask.store(mask, std::memory_order_relaxed);
        const unsigned slot = g_fedHead.fetch_add(1, std::memory_order_relaxed) % kFedButtonSlots;
        g_fedButtonsMs[slot].store(GetTickCount64(), std::memory_order_relaxed);
        g_fedButtons[slot].store(mask, std::memory_order_release);
        if (mask & XINPUT_GAMEPAD_BACK)
            g_fedBackMs.store(GetTickCount64(), std::memory_order_relaxed);
    }

    // What the PHYSICAL pad reported, before any swallowing, so a
    // renderer switch can be tied to (or cleared of) the player's own pad.
    std::atomic<uint32_t> g_physicalLastMask{0};
    std::atomic<uint64_t> g_physicalBackMs{0};
    void NotePhysicalButtons(WORD mask) noexcept
    {
        g_physicalLastMask.store(mask, std::memory_order_relaxed);
        if (mask & XINPUT_GAMEPAD_BACK)
            g_physicalBackMs.store(GetTickCount64(), std::memory_order_relaxed);
    }
    // The head-gesture stick click in Halo 2 is a deliberate
    // renderer switch only when HELD (a brief click does nothing), so the
    // moment it is honoured is unmistakable in the log and it can never
    // flip the renderer by accident.
    std::atomic<uint64_t> g_halo2GestureClickSinceMs{0};
    std::atomic<bool> g_halo2GestureClickSent{false};
    constexpr uint64_t kHalo2GestureClickHoldMs = 350;

    // Map |v| in 0..1 to a raw stick value that clears MCC's inner deadzone,
    // so small corrections still produce movement.
    SHORT ToRawStick(float v)
    {
        if (fabsf(v) < 1e-3f)
            return 0;
        const float floor = 9000.0f; // typical XInput deadzone is ~7849
        float raw = floor + fabsf(v) * (32767.0f - floor);
        if (raw > 32767.0f)
            raw = 32767.0f;
        return (SHORT)(v < 0 ? -raw : raw);
    }

    // Plain analog mapping for the game's OWN menus (pause/settings/shell): map
    // |v| in 0..1 straight to the full stick range with NO deadzone floor, so
    // MCC's own menu deadzone rejects a small off-axis component. Without this a
    // near-vertical push like (0.15, 0.98) had its 0.15 minor axis floored past
    // the deadzone by ToRawStick, so up/down leaked into left/right.
    SHORT ToRawMenuStick(float v)
    {
        if (v > 1.0f)
            v = 1.0f;
        else if (v < -1.0f)
            v = -1.0f;
        return (SHORT)(v * 32767.0f);
    }

    // Per-title rules for the head-held D-pad gesture.
    //  dpadLeftHandBesideHead: CE uses the physical left hand beside the LEFT of
    //    the head, independent of weapon handedness and dpad_hand.
    //  clickIsGraphicsSwitch: the gesture's stick click is the Classic <->
    //    Anniversary switch (a bounded Back press), not a plain Back.
    struct TitleGestureRules
    {
        bool dpadLeftHandBesideHead = false;
        bool clickIsGraphicsSwitch = false;
        const char* logName = "";
    };

    TitleGestureRules TitleGestureRulesFor(GameTitle title)
    {
        const TitleTraits* traits = FindTitleTraits(title);
        if (!traits)
            return {};
        return {traits->dpadLeftHandBesideHead, traits->dpadClickIsGraphicsSwitch, traits->gestureLogName};
    }

    // Titles whose headset owns look pitch through a native servo loop, checked
    // in this order after the VR aim/look owners. Halo 2 also snap-turns;
    // Halo 4 has no VR turn or aim loop yet, so its engine keeps turning
    // the body, the aim and the view together.
    struct HeadPitchLookRow
    {
        bool (*owns)();
        bool (*snapStick)(float&);
        bool (*pitchStick)(float&);
    };
    const HeadPitchLookRow kHeadPitchLook[] = {
        {&Game_Halo2OwnsLookPitch, &Game_ComputeHalo2SnapStick, &Game_ComputeHalo2PitchStick},
        {&Game_Halo4OwnsLookPitch, nullptr, &Game_ComputeHalo4PitchStick},
    };

    const HeadPitchLookRow* ActiveHeadPitchLook()
    {
        for (const HeadPitchLookRow& row : kHeadPitchLook)
            if (row.owns())
                return &row;
        return nullptr;
    }

    // What the game sees of a chord's two inputs this poll: a lone
    // first press is held back, a held-back tap is delivered as a short pulse.
    bool ChordInput(bool pressed, const MenuChordResult& chord, uint8_t bit) noexcept
    {
        return (pressed && !(chord.withhold & bit)) || (chord.pulse & bit) != 0;
    }
    uint32_t ChordSources(uint32_t sources, const MenuChordResult& chord, int first, int second) noexcept
    {
        const uint32_t a = vr_mapping::Bit(first), b = vr_mapping::Bit(second);
        if (chord.withhold & 1)
            sources &= ~a;
        if (chord.withhold & 2)
            sources &= ~b;
        if (chord.pulse & 1)
            sources |= a;
        if (chord.pulse & 2)
            sources |= b;
        return sources;
    }
    WORD ChordButtons(WORD buttons, const MenuChordResult& chord, WORD first, WORD second) noexcept
    {
        if (chord.withhold & 1)
            buttons &= static_cast<WORD>(~first);
        if (chord.withhold & 2)
            buttons &= static_cast<WORD>(~second);
        if (chord.pulse & 1)
            buttons |= first;
        if (chord.pulse & 2)
            buttons |= second;
        return buttons;
    }

    void MergeVrPad(XINPUT_STATE* state)
    {
        VrPadState pad;
        VR_GetPadState(pad);
        {
            // Deliberate VR input edges (for the menu laser's focus etiquette).
            static std::atomic<uint32_t> previousInput{0};
            const uint32_t inputNow = (pad.a ? 1u : 0u) | (pad.b ? 2u : 0u) | (pad.x ? 4u : 0u) | (pad.y ? 8u : 0u) |
                                      (pad.clickL ? 16u : 0u) | (pad.clickR ? 32u : 0u) | (pad.menu ? 64u : 0u) |
                                      (pad.trigL > 0.5f ? 128u : 0u) | (pad.trigR > 0.5f ? 256u : 0u) |
                                      (pad.gripL > vr_mapping::kGripPress ? 512u : 0u) |
                                      (pad.gripR > vr_mapping::kGripPress ? 1024u : 0u) |
                                      (pad.moveX * pad.moveX + pad.moveY * pad.moveY > 0.25f ? 2048u : 0u) |
                                      (pad.turnX * pad.turnX + pad.turnY * pad.turnY > 0.25f ? 4096u : 0u);
            if (inputNow & ~previousInput.exchange(inputNow, std::memory_order_relaxed))
                NativeMenuPointer_NoteVrInput();
        }
        {
            // An A held as the laser's click must not become a gameplay A
            // (jump) when that click closes the menu: drain it until release.
            static std::atomic<bool> drainLatch{false}; // process-wide: any poll thread
            bool drainA = drainLatch.load(std::memory_order_relaxed);
            const bool pointerOwnsA = pad.exclusiveInput && !pad.thumbrestDpad && !Menu_IsOpen();
            if (pointerOwnsA)
                drainA = pad.a;
            else if (drainA)
            {
                if (pad.a)
                    pad.a = false;
                else
                    drainA = false;
            }
            drainLatch.store(drainA, std::memory_order_relaxed);
        }
        if (!pad.valid)
        {
            g_vrActionMapper.ready = false;
            g_scopeInput.Suspend();
            g_physicalCrouchInput.Suspend(g_config.physical_crouch);
            PhysicalCrouchCamera_Invalidate();
            Roomscale_Input(false, 0, 0);
            return;
        }

        const bool sharedGameplayInput = Game_AllowsSharedGameplayFeatures();
        const MenuChordResult chord = g_menuChord.Update(GetTickCount64(), pad.clickL, pad.clickR);
        if (chord.toggled)
        {
            // The same one-shot chord edge owns both actions. Game_Recenter
            // resets every title's immersive camera reference and the OpenXR
            // room-fixed origin used by theatre; opening F1 on that edge then
            // places its head-locked panel from the newly centered pose.
            Game_Recenter();
            Menu_Toggle();
            LOG("L3+R3: recentered immersive/theatre space and toggled F1 menu");
        }

        static thread_local ExclusiveInputHolds physicalHolds;
        static thread_local ExclusiveInputHolds vrMenuHolds;
        const bool menuOpen = Menu_IsOpen();
        vrMenuHolds.ApplyVr(pad, menuOpen, false);
        const bool pointerConfirm = pad.exclusiveInput && !pad.thumbrestDpad && !Menu_IsOpen();
        physicalHolds.ApplyPhysical(state->Gamepad, pad.exclusiveInput || Menu_IsOpen(), pointerConfirm);
        // D-pad head gesture: hold the configured controller (F1 menu:
        // left by default) up next to your head and the left stick becomes the
        // D-pad (menu navigation, grenade switching); lower it and the stick
        // walks again. No menu detection — the player chooses the mode with
        // the gesture, anywhere.
        bool dpadMode = false;
        {
            // Zone with hysteresis, routing latched while held.
            bool insideEnter = false, insideStay = false;
            float hq[4], hp[3], cq[4], cp[3];
            const TitleGestureRules gestureRules = TitleGestureRulesFor(TitleAdapter_GetActiveTitle());
            const bool ce = gestureRules.dpadLeftHandBesideHead;
            const bool haveController =
                VR_GetPhysicalControllerPose(ce ? 0 : (g_config.dpad_hand == 0 ? 0 : 1), cq, cp);
            if (haveController && VR_GetHeadPose(hq, hp))
            {
                const float dx = hp[0] - cp[0], dy = hp[1] - cp[1], dz = hp[2] - cp[2];
                insideEnter = DpadHeadWithinRadius(hp, cp, g_config.dpad_head_radius);
                insideStay = DpadHeadWithinRadius(hp, cp, g_config.dpad_head_radius, DpadGesture::kExitMarginM);
                // CE's requested switch uses the physical left hand beside
                // the left of the head, independent of weapon handedness.
                if (ce)
                {
                    const float rightX = 1.0f - 2.0f * (hq[1] * hq[1] + hq[2] * hq[2]);
                    const float rightY = 2.0f * (hq[0] * hq[1] + hq[3] * hq[2]);
                    const float rightZ = 2.0f * (hq[0] * hq[2] - hq[3] * hq[1]);
                    const float side = dx * rightX + dy * rightY + dz * rightZ;
                    insideEnter = insideEnter && side > DpadGesture::kSideEnterM;
                    insideStay = insideStay && side > DpadGesture::kSideStayM;
                }
            }
            dpadMode = g_dpadGesture.Update(insideEnter, insideStay, pad.clickL, pad.moveX, pad.moveY);
        }

        // The physical pad's own Y/B count for the pause chord too, so a
        // Steam Controller pauses the same way the VR controllers do.
        const WORD physicalButtons = state->Gamepad.wButtons;
        MenuChordResult pauseChord{};
        if (!menuOpen && !pad.exclusiveInput && g_config.y_b_start_chord && Game_AllowsPauseToggleInput())
        {
            pauseChord = g_pauseChord.Update(GetTickCount64(), pad.y || (physicalButtons & XINPUT_GAMEPAD_Y) != 0,
                pad.b || (physicalButtons & XINPUT_GAMEPAD_B) != 0);
            if (pauseChord.toggled)
                Input_RequestPauseToggle();
        }
        else
        {
            g_pauseChord.Reset();
        }

        const auto currentTitle = TitleAdapter_GetActiveTitle();
        const int mappingTitle = weapon_interaction::TitleIndex(currentTitle);
        const auto inputMode = TitleAdapter_GetRuntimeMode();
        const bool gameplayMode = (inputMode == RuntimeMode::Gameplay || inputMode == RuntimeMode::Vehicle ||
                                      inputMode == RuntimeMode::Turret) &&
                                  !VR_IsPausePresentation() && !VR_IsPausePresentationTarget() &&
                                  !VR_IsCutsceneTheaterActive();
        const uint64_t mappingEpoch = (uint64_t(TitleAdapter_GetGeneration(currentTitle)) << 32) |
                                      (uint64_t(currentTitle) << 24) | pad.profileEpoch;
        const bool scopeFeatureAvailable = g_config.scope_enabled && Game_IsHeadTracking() && Game_HasScopeRenderer();
        const bool scopeAvailable = scopeFeatureAvailable && gameplayMode;
        // Zoom only ever lives in a separate window (lens or visor). Native
        // full-screen zoom stays suppressed in VR, even in a game that has
        // no zoom window yet (Halo CE until its zoom render exists).
        const bool suppressNativeZoom = g_config.scope_enabled && Game_IsHeadTracking() && gameplayMode;
        if (suppressNativeZoom && !scopeAvailable)
        {
            static uint64_t loggedKey = 0;
            const uint64_t key = (uint64_t(TitleAdapter_GetGeneration(currentTitle)) << 8) | uint64_t(currentTitle);
            if (loggedKey != key)
            {
                loggedKey = key;
                LOG("zoom: native full-screen zoom suppressed in VR; this title has no zoom window yet");
            }
        }
        const int zoomSource =
            mappingTitle >= 0 && g_config.vr_action_mapping
                ? vr_mapping::Resolve(vr_mapping::Zoom, g_config.vr_bindings[mappingTitle][vr_mapping::Zoom])
                : vr_mapping::RightClick;
        const uint32_t collectedSources = ScopeAndActionSources(pad, dpadMode);
        // Changing D-pad gesture routes while the stick is held is cancellation,
        // not the release edge that toggles a scope. Face/trigger bindings are
        // unaffected by entering the optional head D-pad gesture.
        const unsigned scopeRoute =
            zoomSource >= vr_mapping::DpadUp ? (pad.thumbrestDpad ? 2u : (dpadMode ? 1u : 0u)) : 0u;
        const ScopeToggleUpdate scope = g_scopeInput.Update(scopeFeatureAvailable, collectedSources, zoomSource,
            mappingEpoch, scopeRoute, scopeAvailable && !menuOpen && (!pad.exclusiveInput || pad.thumbrestDpad),
            chord.consumeClicks || pauseChord.consumeClicks);
        if (scope.changed && scopeAvailable)
        {
            VR_RequestScopeToggle();
            LOG("universal scope: mapped input release toggled body-safe scope state");
        }
        if (!scopeFeatureAvailable)
            VR_SetScopeActive(false);
        if (menuOpen)
        {
            g_vrActionMapper.ready = false;
            g_physicalCrouchInput.Suspend(g_config.physical_crouch);
            PhysicalCrouchCamera_Invalidate();
            Roomscale_Input(false, 0, 0);
            state->Gamepad = {};
            return;
        }

        if (pad.exclusiveInput && !Menu_IsOpen())
        {
            g_physicalCrouchInput.Suspend(g_config.physical_crouch);
            PhysicalCrouchCamera_Invalidate();
            Roomscale_Input(false, 0, 0);
            (void)Game_GestureMeleeInput(GetTickCount64()); // drains a pending gesture pulse
            g_pauseChord.Reset();
            g_dpadGesture.Reset();
            // The VR A is the laser's click while the pointer owns the menu
            // (vr.cpp); only a physical gamepad's A still passes as A.
            const WORD confirm = pointerConfirm && (state->Gamepad.wButtons & XINPUT_GAMEPAD_A) ? XINPUT_GAMEPAD_A : 0;
            state->Gamepad = {};
            // A pause request's Start pulse survives the laser (a slow
            // poller such as ODST would otherwise miss it); it is no longer zeroed here.
            state->Gamepad.wButtons =
                static_cast<WORD>(confirm | (GetTickCount64() < g_startPulseUntilMs.load() ? XINPUT_GAMEPAD_START : 0));
            if (pad.thumbrestDpad)
            {
                const auto title = TitleAdapter_GetActiveTitle();
                const int index = weapon_interaction::TitleIndex(title);
                if (g_config.vr_action_mapping && index >= 0 && gameplayMode)
                {
                    vr_mapping::Transports native{};
                    const uint64_t now = GetTickCount64();
                    const bool ready = Game_ReadVrActionBindings(native, now);
                    if (suppressNativeZoom)
                        native[vr_mapping::Zoom] = 0;
                    const auto actions =
                        g_vrActionMapper.ApplyDetailed(collectedSources, g_config.vr_bindings[index], native,
                            (uint64_t(TitleAdapter_GetGeneration(title)) << 32) | (uint64_t(title) << 24) |
                                pad.profileEpoch,
                            ready,
                            g_config.disable_flashlight_input ||
                                (g_config.flashlight_suppress_on_two_hand && pad.supportAimActive));
                    const uint32_t mapped = actions.transports;
                    state->Gamepad.wButtons = static_cast<WORD>(mapped);
                    if (mapped & (1u << 16))
                        state->Gamepad.bLeftTrigger = 255;
                    if (mapped & (1u << 17))
                        state->Gamepad.bRightTrigger = 255;
                }
                else
                {
                    g_vrActionMapper.ready = false;
                    state->Gamepad.wButtons = DpadDirectionButtons(pad.dpadX, pad.dpadY);
                }
            }
            else
                g_vrActionMapper.ready = false;
            NoteFedButtons(state->Gamepad.wButtons);
            return;
        }

        // Halo 3 ground driver seats only (inert everywhere else): read the
        // virtual steering wheel from THIS poll's pad, before the buttons below
        // are built. Both grips down together is the wheel's own gesture and is
        // the only case where their buttons are withheld — a lone right grip is
        // never touched, so it keeps dismounting the vehicle.
        Game_Halo3UpdateVehicleWheel(pad);
        Game_UpdateTurretHandles(pad);
        Game_UpdateVehicleViewToggle(pad);
        Game_UpdateFlightStick(pad);
        Game_UpdateGrabPickup(pad);
        Game_UpdateGripHeld(pad);
        // Halo 4's VR turn advances from this same shared pad sample,
        // before the right-stick block below decides who writes the axes.
        Game_Halo4UpdateVrTurn(pad);
        const bool wheelGesture = Game_Halo3VehicleSwallowsGrips();
        const bool flightStickGrip = Game_FlightStickSwallowsPrimaryGrip() || Game_GripHeldSwallowsPrimaryGrip();
        const bool grabPickupGrip = Game_GrabPickupSwallowsSupportGrip();
        const uint32_t grabPickupButtons = Game_GrabPickupButtons();

        // VR defaults are semantic and shared. Each verified native adapter
        // reads the current MCC layout; Reach no longer trades LT and X.
        const uint64_t inputNow = GetTickCount64();
        vr_mapping::Transports nativeActions{};
        const bool semanticMode = g_config.vr_action_mapping && mappingTitle >= 0 && gameplayMode;
        const bool physicalCrouchEnabled = g_config.physical_crouch;
        const bool nativeReady = (semanticMode || physicalCrouchEnabled) && gameplayMode &&
                                 Game_ReadVrActionBindings(nativeActions, inputNow);
        const bool nativeMapping = semanticMode && nativeReady;
        float crouchHeadQuat[4]{}, crouchHeadPos[3]{};
        const bool crouchTracking = physicalCrouchEnabled && inputMode == RuntimeMode::Gameplay && gameplayMode &&
                                    Game_IsHeadTracking() && Game_IsPositionalTracking() && VR_IsStereoEnabled() &&
                                    VR_RoomscaleTrackingFresh() && VR_GetHeadPose(crouchHeadQuat, crouchHeadPos);
        const uint32_t crouchButtons =
            g_physicalCrouchInput.Update(currentTitle, TitleAdapter_GetGeneration(currentTitle), crouchHeadPos[1],
                VR_PhysicalCrouchEpoch(), inputNow, physicalCrouchEnabled, crouchTracking && nativeReady,
                g_config.physical_crouch_depth_m, nativeActions[vr_mapping::Crouch]);
        {
            // Crouch needs the game's own crouch action; the camera witnesses
            // only lower the view and log their own fallback.
            const uint32_t crouchTransport = nativeActions[vr_mapping::Crouch];
            const bool missing = physicalCrouchEnabled && inputMode == RuntimeMode::Gameplay && gameplayMode &&
                                 (!nativeReady || !crouchTransport);
            const uint64_t key = (uint64_t(TitleAdapter_GetGeneration(currentTitle)) << 8) | uint64_t(currentTitle);
            if (!missing || g_crouchMissingKey.load(std::memory_order_relaxed) != key)
            {
                g_crouchMissingSinceMs = missing ? inputNow : 0;
                g_crouchMissingKey.store(missing ? key : 0, std::memory_order_relaxed);
                g_crouchUnavailable.store(false, std::memory_order_release);
            }
            else if (g_crouchMissingSinceMs && inputNow - g_crouchMissingSinceMs >= 2000 &&
                     !g_crouchUnavailable.exchange(true, std::memory_order_acq_rel))
                LOG("crouch: physical crouch is on but this title's crouch action is unavailable "
                    "(MCC layout has no crouch binding or cannot be read); bind a crouch button in F1 > Mappings");
        }
        {
            static bool previousCrouch = false;
            static uint64_t lastCrouchLogMs = 0;
            const bool crouching = crouchButtons != 0;
            if (crouching && !previousCrouch && inputNow - lastCrouchLogMs >= 250)
            {
                LOG("Action fired: crouch via physical crouch");
                lastCrouchLogMs = inputNow;
            }
            previousCrouch = crouching;
        }
        PhysicalCrouchCamera_Publish(currentTitle, TitleAdapter_GetGeneration(currentTitle), VR_PhysicalCrouchEpoch(),
            inputNow, physicalCrouchEnabled && crouchTracking && nativeReady, crouchButtons != 0);
        uint32_t actionSources = collectedSources;
        // A chord's first press is held back (a tap delivered late).
        actionSources = ChordSources(actionSources, pauseChord, vr_mapping::Y, vr_mapping::B);
        actionSources = ChordSources(actionSources, chord, vr_mapping::LeftClick, vr_mapping::RightClick);
        // At the head the stick click is Back, never the ability (a delivered tap included).
        if (dpadMode)
            actionSources &= ~vr_mapping::Bit(vr_mapping::LeftClick);
        if (dpadMode && !pad.thumbrestDpad)
        {
            // The gesture's directions come from the hysteresis latch.
            using namespace vr_mapping;
            const uint16_t d = g_dpadGesture.Directions(pad.moveX, pad.moveY);
            actionSources &= ~(Bit(DpadUp) | Bit(DpadDown) | Bit(DpadLeft) | Bit(DpadRight));
            actionSources |= (d & 1 ? Bit(DpadUp) : 0) | (d & 2 ? Bit(DpadDown) : 0) | (d & 4 ? Bit(DpadLeft) : 0) |
                             (d & 8 ? Bit(DpadRight) : 0);
        }
        if (suppressNativeZoom)
            nativeActions[vr_mapping::Zoom] = 0;
        const bool swallowX = Game_VehicleViewSwallowsX(); // seated: X is the seat-view switch, not a game action
        if (swallowX)
            actionSources &= ~vr_mapping::Bit(vr_mapping::X);
        if (wheelGesture)
            actionSources &= ~(vr_mapping::Bit(vr_mapping::PrimaryGrip) | vr_mapping::Bit(vr_mapping::SupportGrip));
        if (flightStickGrip)
            actionSources &= ~vr_mapping::Bit(vr_mapping::PrimaryGrip);
        if (grabPickupGrip)
            actionSources &= ~vr_mapping::Bit(vr_mapping::SupportGrip);
        const vr_mapping::Overrides emptyMappings{};
        auto mappingTransports = nativeActions;
        auto semanticActions = g_vrActionMapper.ApplyDetailed(actionSources,
            mappingTitle >= 0 ? g_config.vr_bindings[mappingTitle] : emptyMappings, mappingTransports,
            (uint64_t(TitleAdapter_GetGeneration(currentTitle)) << 32) | (uint64_t(currentTitle) << 24) |
                pad.profileEpoch,
            nativeMapping,
            g_config.disable_flashlight_input || (g_config.flashlight_suppress_on_two_hand && pad.supportAimActive));
        if (suppressNativeZoom)
            semanticActions.actions &= ~(1u << vr_mapping::Zoom);
        {
            // Test-assertable, edge-triggered, at most every 250 ms per action:
            // "Action fired: <action> via <input>".
            static uint32_t previousActions = 0;
            static uint64_t lastLogMs[vr_mapping::Count]{};
            const uint32_t rising = semanticActions.actions & ~previousActions;
            previousActions = semanticActions.actions;
            const struct
            {
                vr_mapping::Action action;
                const char* name;
            } logged[]{
                {vr_mapping::Equipment, "ability"}, {vr_mapping::Sprint, "sprint"}, {vr_mapping::Reload, "reload"}};
            for (const auto& entry : logged)
            {
                if (!(rising & (1u << entry.action)) || inputNow - lastLogMs[entry.action] < 250)
                    continue;
                lastLogMs[entry.action] = inputNow;
                const int source = mappingTitle >= 0 ? vr_mapping::Resolve(entry.action,
                                                           g_config.vr_bindings[mappingTitle][entry.action])
                                                     : vr_mapping::Unbound;
                LOG("Action fired: %s via %s", entry.name,
                    source >= 0 && source < vr_mapping::SourceCount ? vr_mapping::kSourceShortNames[source]
                                                                    : "unknown");
            }
            // The same native Reload/Use button held is Halo's use/pick-up/enter:
            // "Action fired: use via <input> hold" once per press after 400 ms.
            static uint64_t reloadDownMs = 0;
            static bool useLogged = false;
            if (semanticActions.actions & (1u << vr_mapping::Reload))
            {
                if (rising & (1u << vr_mapping::Reload))
                {
                    reloadDownMs = inputNow;
                    useLogged = false;
                }
                if (!useLogged && reloadDownMs && inputNow - reloadDownMs >= 400)
                {
                    useLogged = true;
                    const int source = mappingTitle >= 0 ? vr_mapping::Resolve(vr_mapping::Reload,
                                                               g_config.vr_bindings[mappingTitle][vr_mapping::Reload])
                                                         : vr_mapping::Unbound;
                    LOG("Action fired: use via %s hold", source >= 0 && source < vr_mapping::SourceCount
                                                             ? vr_mapping::kSourceShortNames[source]
                                                             : "unknown");
                }
            }
            else
            {
                reloadDownMs = 0;
                useLogged = false;
            }
        }
        const uint32_t semanticButtons = semanticActions.transports;
        const bool xPressed = pad.x && !swallowX;
        const float leftTrigger = pad.trigL;

        WORD btn = ChordButtons(state->Gamepad.wButtons, pauseChord, XINPUT_GAMEPAD_Y, XINPUT_GAMEPAD_B);
        if (suppressNativeZoom)
            btn &= ~XINPUT_GAMEPAD_RIGHT_THUMB;
        const bool gameClickL = ChordInput(pad.clickL, chord, 1);
        if (!semanticMode && pad.a)
            btn |= XINPUT_GAMEPAD_A;
        if (!semanticMode && ChordInput(pad.b, pauseChord, 2))
            btn |= XINPUT_GAMEPAD_B;
        if (!semanticMode && xPressed)
            btn |= XINPUT_GAMEPAD_X;
        if (!semanticMode && ChordInput(pad.y, pauseChord, 1))
            btn |= XINPUT_GAMEPAD_Y;
        if (!semanticMode && gameClickL)
            btn |= XINPUT_GAMEPAD_LEFT_THUMB;
        if (!semanticMode && ChordInput(pad.clickR, chord, 2) && !suppressNativeZoom)
            btn |= XINPUT_GAMEPAD_RIGHT_THUMB;
        btn |= static_cast<WORD>(semanticButtons);
        btn |= static_cast<WORD>(crouchButtons);
        static bool previousMenu = false;
        const bool menuEdge = pad.menu && !previousMenu;
        if (menuEdge)
        {
            Game_NoteHalo2PauseIntent(inputNow);
            if (Game_IsCameraOnlyBringup())
            {
                // OpenXR exposes the reserved Menu action as a short edge. ODST
                // polls XInput on a different cadence and missed that edge in
                // the headset test, so retain a normal Start press long enough
                // to cross its polling boundary. This branch is private-ODST
                // only; Halo 3 and normal OFF builds keep their existing path.
                g_startPulseUntilMs.store(inputNow + 350);
                LOG("ODST input: Menu/Start latched for native polling");
            }
            if (PausePresentationInputAllowed(sharedGameplayInput) && !Game_HasAuthoritativePauseState())
                VR_RequestPausePresentation(!VR_IsPausePresentationTarget());
        }
        previousMenu = pad.menu;
        if (pad.menu || inputNow < g_startPulseUntilMs.load())
            btn |= XINPUT_GAMEPAD_START;
        if (!wheelGesture && !semanticMode)
        {
            if (pad.gripL > vr_mapping::kGripPress && !pad.weaponConsumeSupport && !grabPickupGrip)
                btn |= XINPUT_GAMEPAD_LEFT_SHOULDER;
            if (pad.gripR > vr_mapping::kGripPress && !pad.weaponConsumePrimary && !flightStickGrip)
                btn |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
        }
        const bool weaponGesture = pad.weaponConsumeSupport || pad.weaponConsumePrimary || pad.weaponButtons;
        const uint32_t gestureMelee = weaponGesture ? 0 : Game_GestureMeleeInput(inputNow);
        btn |= static_cast<WORD>(gestureMelee & 0xFFFF);
        btn |= static_cast<WORD>(pad.weaponButtons & 0xFFFF);
        btn |= static_cast<WORD>(Game_VehicleWheelButtons() & 0xFFFF); // grip-hold vehicle exit
        btn |= static_cast<WORD>(grabPickupButtons & 0xFFFF);          // Reach grab pickup: held Use
        state->Gamepad.wButtons = btn;
        NoteFedButtons(btn);

        const BYTE tl = semanticMode ? ((semanticButtons & (1u << 16)) ? 255 : 0) : (BYTE)(leftTrigger * 255.0f);
        const BYTE tr = semanticMode ? ((semanticButtons & (1u << 17)) ? 255 : 0) : (BYTE)(pad.trigR * 255.0f);
        if (tl > state->Gamepad.bLeftTrigger)
            state->Gamepad.bLeftTrigger = tl;
        if (tr > state->Gamepad.bRightTrigger)
            state->Gamepad.bRightTrigger = tr;
        if (crouchButtons & (1u << 16))
            state->Gamepad.bLeftTrigger = 255;
        if (crouchButtons & (1u << 17))
            state->Gamepad.bRightTrigger = 255;
        if (gestureMelee & (1u << 16))
            state->Gamepad.bLeftTrigger = 255;
        if (gestureMelee & (1u << 17))
            state->Gamepad.bRightTrigger = 255;
        if (pad.weaponButtons & (1u << 16))
            state->Gamepad.bLeftTrigger = 255;
        if (pad.weaponButtons & (1u << 17))
            state->Gamepad.bRightTrigger = 255;

        // The optional thumb-rest gesture uses the physical RIGHT stick. Its
        // axes were consumed at XR publication, including every title's turn
        // and scope consumers. Preserve head-gesture clicks and left movement.
        if (pad.thumbrestDpad && !semanticMode)
        {
            btn |= DpadDirectionButtons(pad.dpadX, pad.dpadY);
            state->Gamepad.wButtons = btn;
            NoteFedButtons(btn);
        }

        const bool physicalMove =
            std::abs(int(state->Gamepad.sThumbLX)) > 7849 || std::abs(int(state->Gamepad.sThumbLY)) > 7849;
        float locomotionX = pad.moveX, locomotionY = pad.moveY;
        const bool running = !dpadMode && !Menu_IsOpen() && !VR_IsPausePresentation() &&
                             !VR_IsPausePresentationTarget() && !VR_IsCutsceneTheaterActive() &&
                             Game_MoveStickIsLocomotion() && PhysicalRunning_Move(locomotionX, locomotionY);
        Roomscale_Input(RoomscaleGameplayEligible(TitleAdapter_GetActiveTitle(), TitleAdapter_GetRuntimeMode()) &&
                            !dpadMode && !physicalMove && Game_IsHeadTracking() && Game_IsPositionalTracking() &&
                            VR_IsStereoEnabled() && !VR_IsPausePresentation() && !VR_IsPausePresentationTarget() &&
                            !VR_IsCutsceneTheaterActive(),
            locomotionX, locomotionY);

        if (dpadMode)
        {
            // Directions from the hysteresis latch (0.5 on, 0.4 off).
            const uint16_t directions = g_dpadGesture.Directions(pad.moveX, pad.moveY);
            if (!semanticMode)
                btn |= static_cast<WORD>(directions);
            // The flashlight head touch on the same hand stands down for a
            // touch in which the D-pad produced input.
            if (directions || (pad.clickL && !chord.consumeClicks))
                g_dpadGestureUsedMs.store(GetTickCount64(), std::memory_order_relaxed);
            // Left stick CLICK becomes the controller's left centre button
            // (Back/View) while the D-pad gesture is held. ODST puts its map
            // and objectives screen behind that button and VR players had no
            // way to reach it. This is deliberately scoped to the gesture: the
            // stick is already acting as a D-pad here, so its click has no
            // other meaning, and normal play keeps L3 untouched.
            const TitleGestureRules gestureRules = TitleGestureRulesFor(TitleAdapter_GetActiveTitle());
            const bool graphicsSwitch = gestureRules.clickIsGraphicsSwitch;
            if (gameClickL && !chord.consumeClicks)
            {
                btn &= ~XINPUT_GAMEPAD_LEFT_THUMB;
                if (!graphicsSwitch)
                {
                    // ODST/Reach/Halo 3/Halo 4: the click is the Back button.
                    btn |= XINPUT_GAMEPAD_BACK;
                }
                else
                {
                    // In Halo 2 the click IS the Back button, as in
                    // ODST - one Back press per click (an earlier hold-to-switch
                    // was not what players wanted). Back is the engine's
                    // Classic <-> Anniversary switch; the renderer switch
                    // guard honours it because the mod fed it.
                    const uint64_t now = GetTickCount64();
                    if (!g_halo2GestureClickSinceMs.load(std::memory_order_relaxed))
                    {
                        g_halo2GestureClickSinceMs.store(now, std::memory_order_relaxed);
                        g_halo2GestureClickSent.store(false, std::memory_order_relaxed);
                        LOG("%s - head-gesture stick click: Back pressed "
                            "(the Classic/Anniversary switch)",
                            gestureRules.logName);
                    }
                    if (now - g_halo2GestureClickSinceMs.load(std::memory_order_relaxed) < kHalo2GestureClickHoldMs)
                        btn |= XINPUT_GAMEPAD_BACK;
                }
            }
            else if (graphicsSwitch)
            {
                g_halo2GestureClickSinceMs.store(0, std::memory_order_relaxed);
                g_halo2GestureClickSent.store(false, std::memory_order_relaxed);
            }
            state->Gamepad.wButtons = btn;
            NoteFedButtons(btn);
            // No walking while navigating.
            state->Gamepad.sThumbLX = 0;
            state->Gamepad.sThumbLY = 0;
            static std::atomic<bool> gestureLogged{false};
            if (!gestureLogged.exchange(true))
                LOG("D-pad gesture active (right controller held at head)");
        }
        else if (Game_MoveStickIsLocomotion())
        {
            // Gameplay locomotion (UNCHANGED, headset-confirmed): the left stick
            // walks the player, rotated head-relative so forward = gaze, and
            // each axis floored past MCC's inner deadzone so small corrections
            // still move. This path runs only while the game is actually using
            // the stick to move the character.
            float mx = locomotionX, my = locomotionY;
            const bool roomscale = !running && Roomscale_Move(mx, my);
            Game_MapMoveStick(mx, my);
            if (((roomscale || running) && mx * mx + my * my > 1e-6f) || mx * mx + my * my > 0.02f)
            {
                RadialMoveStick(mx, my, state->Gamepad.sThumbLX, state->Gamepad.sThumbLY);
            }
        }
        else
        {
            // In-game menus (pause/settings) and any other non-locomotion state:
            // the game reads the left stick as MENU NAVIGATION, not movement.
            // Pass it through like a plain gamepad — no head-relative rotation
            // and no per-axis deadzone floor — so a near-vertical push stays
            // vertical and MCC's own menu deadzone rejects the minor axis.
            // Keeps up/down from registering as left/right. Character
            // movement is untouched; that path is the locomotion branch above.
            state->Gamepad.sThumbLX = ToRawMenuStick(pad.moveX);
            state->Gamepad.sThumbLY = ToRawMenuStick(pad.moveY);
        }

        {
            // In an unarmed driver seat with Wheel/Stick steering the
            // triggers are throttle and brake/reverse (A is no longer brake).
            float throttle = 0.0f;
            if (Game_VehicleTriggerThrottle(pad.trigR, pad.trigL, throttle))
            {
                state->Gamepad.bLeftTrigger = 0;
                state->Gamepad.bRightTrigger = 0;
                const float stickY = state->Gamepad.sThumbLY / 32767.0f;
                if (std::fabs(throttle) > std::fabs(stickY))
                    state->Gamepad.sThumbLY = ToRawStick(throttle);
            }
        }

        // Right stick: hand-steered aim when active; otherwise pass the raw
        // stick through (classic stick aiming, and it does nothing in menus).
        // While VR aim is active the turn stick is consumed by the snap/smooth
        // logic in game.cpp instead.
        float rx = 0, ry = 0;
        if (Game_ComputeAimStick(rx, ry))
        {
            state->Gamepad.sThumbRX = ToRawStick(rx);
            state->Gamepad.sThumbRY = ToRawStick(ry);
            if (!g_overrideLogged.exchange(true))
                LOG("VR aim override active (right stick steered by the controller)");
        }
        else if (Game_VrOwnsLookStick())
        {
            // Match Halo 3 camera ownership in every armed camera-core title:
            // ApplyVrTurn consumes turnX directly from the OpenXR pad, while
            // the tracked HMD exclusively owns pitch. Do not also feed either
            // axis into the stock camera/aim integrator.
            state->Gamepad.sThumbRX = 0;
            state->Gamepad.sThumbRY = 0;
        }
        else if (const HeadPitchLookRow* look = ActiveHeadPitchLook())
        {
            // The headset owns pitch through a closed loop onto the engine's own
            // look pitch (the vertical axis never reaches the game); the engine
            // keeps yaw. A title with snap turning owns RX until its native body
            // reaches the discrete target; otherwise the stick turns natively.
            float snapRx = 0.0f;
            if (look->snapStick && look->snapStick(snapRx))
                state->Gamepad.sThumbRX = ToRawStick(snapRx);
            else if (fabsf(pad.turnX) > 0.15f)
                state->Gamepad.sThumbRX = ToRawStick(pad.turnX);
            float servoRy = 0.0f;
            state->Gamepad.sThumbRY = look->pitchStick(servoRy) ? ToRawStick(servoRy) : 0;
        }
        else
        {
            const bool scopeActive = VR_IsScopeActive();
            // Scope magnification owns the vertical axis while the scope is
            // open. Horizontal snap/smooth turning remains available.
            if (scopeActive)
                state->Gamepad.sThumbRY = 0;
            if (fabsf(pad.turnX) > 0.15f || (!scopeActive && fabsf(pad.turnY) > 0.15f))
            {
                state->Gamepad.sThumbRX = ToRawStick(pad.turnX);
                state->Gamepad.sThumbRY = scopeActive ? 0 : ToRawStick(pad.turnY);
            }
        }
    }

    DWORD ProcessGetStateMerged(DWORD r, DWORD user, XINPUT_STATE* state)
    {
        if (user != 0 || !state)
            return r;
        const std::lock_guard<std::recursive_mutex> mergeLock(g_padMergeLock); // one merge at a time
        // Scope exit covers both tracked-controller and physical-pad-only
        // branches, after all gesture pulses and pad merging have completed.
        struct FlashlightFilterScope
        {
            XINPUT_STATE* state;
            ~FlashlightFilterScope() noexcept
            {
                static thread_local flashlight_input::Filter filter;
                const auto title = TitleAdapter_GetActiveTitle();
                const int index = weapon_interaction::TitleIndex(title);
                const bool gameplay = index >= 0 && Game_AllowsSharedGameplayFeatures() && !Menu_IsOpen() &&
                                      !VR_IsPausePresentation() && !VR_IsPausePresentationTarget() &&
                                      !VR_IsCutsceneTheaterActive();
                vr_mapping::Transports native{};
                const bool semantic = g_config.vr_action_mapping && Game_ReadVrActionBindings(native, GetTickCount64());
                filter.Apply(state->Gamepad, g_config.disable_flashlight_input && !semantic, gameplay,
                    static_cast<uint32_t>(title), index >= 0 ? g_config.flashlight_button[index] : -1);
            }
        } flashlightFilter{state};
        // Connection presence is INDEPENDENT of the shared-input gate. With no
        // physical gamepad the mod owns slot 0 and must always present it
        // connected and idle -- even while shared input is gated off (e.g. the
        // brief window mid ODST title-exit teardown). Returning NOT_CONNECTED
        // there hands MCC a disconnect edge it latches, so the menu stays dead
        // after the gate reopens (the post-Save&Quit dead controller, and the
        // cross-title input drop when switching between Halo games). A real
        // physical pad (r == ERROR_SUCCESS) passes through untouched.
        const bool ownVirtualSlot = (r != ERROR_SUCCESS);
        if (ownVirtualSlot)
        {
            *state = {};
            r = ERROR_SUCCESS;
        }
        // Controller admission is separate from shared gameplay ownership.
        // Private ODST camera-only and Reach controller-only stages may expose
        // ordinary gamepad input while motion aim and every title-runtime
        // gameplay transform stay blocked.
        // Gate only decides whether to MERGE VR motion, not whether the pad
        // exists: hold the connection above and skip the merge when gated off.
        if (!Game_AllowsSharedControllerInput())
        {
            g_vrActionMapper.ready = false;
            g_scopeInput.Suspend();
            g_physicalCrouchInput.Suspend(g_config.physical_crouch);
            PhysicalCrouchCamera_Invalidate();
            Roomscale_Input(false, 0, 0);
            return r;
        }
        // In Halo 2 the pad's Back/View button is MCC's instant
        // Classic <-> Anniversary renderer switch. A physical pad (Steam
        // Controller) reaches the game through this same hook, so unless the
        // player opted in, that button never reaches Halo 2.
        if (!ownVirtualSlot)
            NotePhysicalButtons(state->Gamepad.wButtons);
        if (!ownVirtualSlot && !g_config.halo2_gamepad_graphics_switch &&
            (state->Gamepad.wButtons & XINPUT_GAMEPAD_BACK) != 0 && TitleAdapter_GetActiveTitle() == GameTitle::Halo2)
        {
            state->Gamepad.wButtons &= ~XINPUT_GAMEPAD_BACK;
            static std::atomic<bool> loggedBackSwallow{false};
            if (!loggedBackSwallow.exchange(true))
                LOG("Halo 2 - the gamepad's Back/View press was swallowed "
                    "(it is the Classic/Anniversary graphics switch); set "
                    "halo2_gamepad_graphics_switch = 1 to pass it through");
        }
        VrPadState pad;
        VR_GetPadState(pad);
        if (!pad.valid)
        {
            g_vrActionMapper.ready = false;
            g_scopeInput.Suspend();
            g_physicalCrouchInput.Suspend(g_config.physical_crouch);
            PhysicalCrouchCamera_Invalidate();
            Roomscale_Input(false, 0, 0);
            // No VR controllers: the physical pad still gets the Y+B pause
            // chord and the Start pulse it produces.
            // Gated on the F1 menu like the VR path.
            if (!ownVirtualSlot && g_config.y_b_start_chord && !Menu_IsOpen() && Game_AllowsPauseToggleInput())
            {
                const uint64_t now = GetTickCount64();
                const WORD phys = state->Gamepad.wButtons;
                const MenuChordResult chord =
                    g_pauseChord.Update(now, (phys & XINPUT_GAMEPAD_Y) != 0, (phys & XINPUT_GAMEPAD_B) != 0);
                if (chord.toggled)
                    Input_RequestPauseToggle();
                state->Gamepad.wButtons =
                    ChordButtons(state->Gamepad.wButtons, chord, XINPUT_GAMEPAD_Y, XINPUT_GAMEPAD_B);
                if (now < g_startPulseUntilMs.load())
                    state->Gamepad.wButtons |= XINPUT_GAMEPAD_START;
            }
            if (Menu_IsOpen())
                state->Gamepad = {};
            return r;
        }
        MergeVrPad(state);
        return r;
    }

    // Slot 0 as MCC sees it: the merged pad, numbered like a real controller
    // (common/xinput_packet.h) so an idle pad never reads as activity.
    DWORD ProcessGetState(DWORD r, DWORD user, XINPUT_STATE* state)
    {
        r = ProcessGetStateMerged(r, user, state);
        if (user == 0 && state && r == ERROR_SUCCESS)
        {
            static std::mutex packetLock;
            static PacketNormalizer packets;
            // While Reach loads, MCC still gets a connected pad but a neutral
            // one: no buttons, sticks or triggers reach the title until its
            // level runs (tests the early-input theory for the +0x65BA9
            // crash). Our own shim only; nothing in the module is touched.
            // Only before Reach's first level of this session has run (one-shot
            // per title generation): later 'loading' states include MCC's Save &
            // Quit confirmation, whose A/B the neutral pad swallowed (975 polls
            // in one headset log), with or without the builder guard.
            static uint32_t reachRanGeneration = 0;
            const bool reachActive = TitleAdapter_GetActiveTitle() == GameTitle::HaloReach;
            const uint32_t reachGeneration = reachActive ? TitleAdapter_GetGeneration(GameTitle::HaloReach) : 0;
            if (reachActive && TitleAdapter_GetRuntimeMode() == RuntimeMode::Gameplay)
                reachRanGeneration = reachGeneration;
            const bool reachLoading = g_config.reach_neutral_pad_while_loading && reachActive &&
                                      TitleAdapter_GetRuntimeMode() == RuntimeMode::Loading &&
                                      reachRanGeneration != reachGeneration;
            static bool wasReachLoading = false;
            if (reachLoading)
            {
                const XINPUT_GAMEPAD& g = state->Gamepad;
                if (g.wButtons || g.bLeftTrigger || g.bRightTrigger || g.sThumbLX || g.sThumbLY || g.sThumbRX ||
                    g.sThumbRY)
                    g_loadingPadSuppressed.fetch_add(1, std::memory_order_relaxed);
                state->Gamepad = {};
            }
            else if (wasReachLoading)
            {
                const uint64_t suppressed = g_loadingPadSuppressed.exchange(0, std::memory_order_relaxed);
                if (suppressed)
                    LOG("Reach loading: held the pad neutral; %llu poll(s) carried input that the "
                        "title did not see",
                        static_cast<unsigned long long>(suppressed));
            }
            wasReachLoading = reachLoading;
            std::lock_guard<std::mutex> lock(packetLock);
            state->dwPacketNumber = packets.Next(state->Gamepad);
            g_lastPadDeliveryMs.store(GetTickCount64(), std::memory_order_relaxed);
            g_lastPadButtons.store(state->Gamepad.wButtons, std::memory_order_relaxed);
        }
        return r;
    }

    thread_local unsigned g_getStateDepth = 0;

    template <int Slot>
    DWORD WINAPI GetStateHook(DWORD user, XINPUT_STATE* state)
    {
        InputPollMergeScope poll(g_getStateDepth);
        const DWORD result = g_origGetState[Slot](user, state);
        return poll.IsOutermost() ? ProcessGetState(result, user, state) : result;
    }

    DWORD(WINAPI* const g_hooks[6])(DWORD, XINPUT_STATE*) = {
        &GetStateHook<0>, &GetStateHook<1>, &GetStateHook<2>, &GetStateHook<3>, &GetStateHook<4>, &GetStateHook<5>};

    DWORD ProcessGetCaps(DWORD r, DWORD user, XINPUT_CAPABILITIES* caps)
    {
        if (user != 0 || !caps || r == ERROR_SUCCESS)
            return r;
        // Fabricate a standard wired gamepad UNCONDITIONALLY: MCC enumerates
        // controllers once at startup, often seconds before SteamVR activates
        // our actions — gating this on live VR data made the whole input
        // system a startup race. An idle fabricated pad is harmless. This must
        // stay independent of Game_AllowsSharedControllerInput(): a gate-closed
        // NOT_CONNECTED here during a title transition makes MCC drop the pad
        // for good (dead menu after Save & Quit, cross-title input loss).
        *caps = {};
        caps->Type = XINPUT_DEVTYPE_GAMEPAD;
        caps->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
        caps->Gamepad.wButtons = 0xF3FF;
        caps->Gamepad.bLeftTrigger = 0xFF;
        caps->Gamepad.bRightTrigger = 0xFF;
        caps->Gamepad.sThumbLX = 0x7FFF;
        caps->Gamepad.sThumbLY = 0x7FFF;
        caps->Gamepad.sThumbRX = 0x7FFF;
        caps->Gamepad.sThumbRY = 0x7FFF;
        caps->Vibration.wLeftMotorSpeed = 0xFFFF;
        caps->Vibration.wRightMotorSpeed = 0xFFFF;
        static std::atomic<bool> logged{false};
        if (!logged.exchange(true))
            LOG("reporting virtual gamepad as connected (no physical pad found)");
        return ERROR_SUCCESS;
    }

    template <int Slot>
    DWORD WINAPI GetCapsHook(DWORD user, DWORD flags, XINPUT_CAPABILITIES* caps)
    {
        return ProcessGetCaps(g_origGetCaps[Slot](user, flags, caps), user, caps);
    }

    DWORD(WINAPI* const g_capsHooks[3])(DWORD, DWORD, XINPUT_CAPABILITIES*) = {
        &GetCapsHook<0>, &GetCapsHook<1>, &GetCapsHook<2>};

    DWORD ProcessSetState(DWORD result, DWORD user, XINPUT_VIBRATION* vibration)
    {
        if (user != 0 || !vibration)
            return result;
        const DWORD connectedResult = static_cast<DWORD>(NormalizeVirtualXInputSetStateResult(result, user, true));
        if (!Game_HasTitleCapability(TitleCapability_Haptics))
        {
            // A title/arm transition can close the capability gate before the
            // game's zero-motor update arrives. Clear our retained request on
            // every gated call so an earlier rumble cannot resume when the
            // next title becomes armed. Capability policy suppresses only the
            // effect: slot 0 remains the same connected virtual controller
            // exposed by GetState/GetCapabilities.
            VR_SetGameHaptics(0.0f);
            return connectedResult;
        }
        VR_SetGameHaptics(BlendXInputMotors(vibration->wLeftMotorSpeed, vibration->wRightMotorSpeed));
        return connectedResult;
    }

    template <int Slot>
    DWORD WINAPI SetStateHook(DWORD user, XINPUT_VIBRATION* vibration)
    {
        return ProcessSetState(g_origSetState[Slot](user, vibration), user, vibration);
    }

    DWORD(WINAPI* const g_setStateHooks[3])(DWORD, XINPUT_VIBRATION*) = {
        &SetStateHook<0>, &SetStateHook<1>, &SetStateHook<2>};

    // Steam Input redirects MCC's calls at the IMPORT TABLE, so they jump to
    // Steam's handler without ever executing the DLL export we detour — the
    // reason controls kept working or dying depending on session timing. These
    // shims are written INTO the game's import table, wrapping whatever was
    // there (Steam's handler or the real export), and the claim is re-asserted
    // periodically so a later Steam patch can't take the slot back.
    XInputGetStateFn g_iatPrevGetState = nullptr;
    XInputGetCapsFn g_iatPrevGetCaps = nullptr;
    XInputSetStateFn g_iatPrevSetState = nullptr;

    // Distinct MCC call sites polling the virtual pad (user 0), for the log
    // line "MCC pad readers": Unreal's menu reader, and MCC's own gameplay
    // reader once its input system is active.
    std::atomic<uintptr_t> g_padCallers[4]{};
    std::atomic<uint32_t> g_padCallerPolls[4]{};
    void NotePadCaller(uintptr_t caller) noexcept
    {
        if (!caller)
            return;
        for (size_t i = 0; i < std::size(g_padCallers); ++i)
        {
            uintptr_t seen = g_padCallers[i].load(std::memory_order_relaxed);
            if (!seen && g_padCallers[i].compare_exchange_strong(seen, caller, std::memory_order_relaxed))
                seen = caller;
            if (seen == caller)
            {
                g_padCallerPolls[i].fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
    }

    DWORD WINAPI IatGetStateShim(DWORD user, XINPUT_STATE* state)
    {
        InputPollMergeScope poll(g_getStateDepth);
        const DWORD r = g_iatPrevGetState ? g_iatPrevGetState(user, state) : ERROR_DEVICE_NOT_CONNECTED;
        if (!poll.IsOutermost())
            return r;
        if (user == 0)
            NotePadCaller(reinterpret_cast<uintptr_t>(_ReturnAddress()));
        return ProcessGetState(r, user, state);
    }

    DWORD WINAPI IatGetCapsShim(DWORD user, DWORD flags, XINPUT_CAPABILITIES* caps)
    {
        const DWORD r = g_iatPrevGetCaps ? g_iatPrevGetCaps(user, flags, caps) : ERROR_DEVICE_NOT_CONNECTED;
        return ProcessGetCaps(r, user, caps);
    }

    DWORD WINAPI IatSetStateShim(DWORD user, XINPUT_VIBRATION* vibration)
    {
        const DWORD r = g_iatPrevSetState ? g_iatPrevSetState(user, vibration) : ERROR_DEVICE_NOT_CONNECTED;
        return ProcessSetState(r, user, vibration);
    }

    bool ClaimIatSlot(void** slot, void* shim, void** prevOut, const char* what)
    {
        if (!slot || *slot == shim)
            return false;
        DWORD oldProtect;
        if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect))
            return false;
        *prevOut = *slot;
        *slot = shim;
        VirtualProtect(slot, sizeof(void*), oldProtect, &oldProtect);
        LOG("claimed game import-table slot for %s (previous handler %p)", what, *prevOut);
        return true;
    }
}

void Input_DescribeRecentButtons(char* buffer, size_t bytes, uint64_t sinceMs)
{
    if (!buffer || !bytes)
        return;
    buffer[0] = 0;
    const uint64_t now = GetTickCount64();
    const unsigned head = g_fedHead.load(std::memory_order_acquire);
    size_t used = 0;
    unsigned listed = 0;
    for (unsigned back = 1; back <= kFedButtonSlots && back <= head; ++back)
    {
        const unsigned slot = (head - back) % kFedButtonSlots;
        const uint64_t at = g_fedButtonsMs[slot].load(std::memory_order_relaxed);
        const uint32_t mask = g_fedButtons[slot].load(std::memory_order_acquire);
        if (at > now || now - at > sinceMs)
            break;
        const int written = _snprintf_s(buffer + used, bytes - used, _TRUNCATE, "%s0x%04X@-%llums", listed ? " " : "",
            mask & 0xFFFFu, static_cast<unsigned long long>(now - at));
        if (written <= 0)
            break;
        used += static_cast<size_t>(written);
        ++listed;
    }
    if (!listed)
        _snprintf_s(buffer, bytes, _TRUNCATE, "none (mask unchanged for >%llu ms; last 0x%04X)",
            static_cast<unsigned long long>(sinceMs), g_fedLastMask.load(std::memory_order_relaxed) & 0xFFFFu);
}

void Input_Halo2SwitchInputEvidence(Halo2SwitchInputEvidence& evidence)
{
    evidence = {};
    const uint64_t now = GetTickCount64();
    evidence.tabDown = (GetAsyncKeyState(VK_TAB) & 0x8000) != 0;
    evidence.physicalBackDown = (g_physicalLastMask.load(std::memory_order_relaxed) & XINPUT_GAMEPAD_BACK) != 0;
    const uint64_t physicalBackMs = g_physicalBackMs.load(std::memory_order_relaxed);
    evidence.physicalBackAgeMs = physicalBackMs && physicalBackMs <= now ? now - physicalBackMs : UINT64_MAX;
    evidence.physicalBackPassThrough = g_config.halo2_gamepad_graphics_switch;
    const uint64_t fedBackMs = g_fedBackMs.load(std::memory_order_relaxed);
    evidence.virtualBackAgeMs = fedBackMs && fedBackMs <= now ? now - fedBackMs : UINT64_MAX;
}

uint64_t Input_DpadGestureUsedMs()
{
    return g_dpadGestureUsedMs.load(std::memory_order_relaxed);
}

// The Menu button on an in-game pause menu (vr.cpp decides, on the
// raw Menu edge). Sent as one 350 ms Start, the same pulse a pause request
// uses, so a slow poller (ODST) sees it; the laser branch passes it through.
void Input_RequestPauseResume()
{
    g_startPulseUntilMs.store(GetTickCount64() + 350);
    const bool authoritative = Game_HasAuthoritativePauseState();
    if (!authoritative)
        VR_RequestPausePresentation(false);
    LOG("pause menu: Menu pressed on the in-game pause; injecting Start to resume (presentation control=%s)",
        authoritative ? "native engine flag" : "edge fallback, target=stereo 3D");
}

void Input_RequestPauseToggle()
{
    if (!Game_AllowsPauseToggleInput())
        return;
    const bool paused = !VR_IsPausePresentationTarget();
    // Hold Start long enough to cross MCC's input polling boundary, then let
    // the normal released state provide the edge needed by a later toggle.
    g_startPulseUntilMs = GetTickCount64() + 350;
    const bool authoritative = Game_HasAuthoritativePauseState();
    if (!authoritative)
        VR_RequestPausePresentation(paused);
    LOG("pause fallback: injecting Start, presentation control=%s%s",
        authoritative ? "native engine flag" : "edge fallback, target=",
        authoritative ? "" : (paused ? "head-locked 2D" : "stereo 3D"));
}

int Input_ClaimXInputIat()
{
    const BYTE* base = reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    if (!base)
        return 0;
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress)
        return 0;
    int claims = 0;
    for (auto desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); desc->Name; ++desc)
    {
        const char* dllName = reinterpret_cast<const char*>(base + desc->Name);
        if (_stricmp(dllName, "xinput1_4.dll") != 0 && _stricmp(dllName, "xinput1_3.dll") != 0 &&
            _stricmp(dllName, "xinput9_1_0.dll") != 0)
            continue;
        auto names = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + desc->OriginalFirstThunk);
        auto iat = reinterpret_cast<IMAGE_THUNK_DATA*>(const_cast<BYTE*>(base) + desc->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++iat)
        {
            void** slot = reinterpret_cast<void**>(&iat->u1.Function);
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
            {
                const WORD ordinal = static_cast<WORD>(IMAGE_ORDINAL64(names->u1.Ordinal));
                // MCC imports XInputGetState from xinput1_3 by ordinal 2. This
                // is the exact IAT slot Steam Input replaces, so claiming it
                // removes the session-timing race that made controls vanish.
                if (ordinal == 2 || ordinal == 100)
                    claims += ClaimIatSlot(slot, reinterpret_cast<void*>(&IatGetStateShim),
                        reinterpret_cast<void**>(&g_iatPrevGetState), "XInputGetState ordinal");
                else if (ordinal == 3)
                    claims += ClaimIatSlot(slot, reinterpret_cast<void*>(&IatSetStateShim),
                        reinterpret_cast<void**>(&g_iatPrevSetState), "XInputSetState ordinal");
                else if (ordinal == 4)
                    claims += ClaimIatSlot(slot, reinterpret_cast<void*>(&IatGetCapsShim),
                        reinterpret_cast<void**>(&g_iatPrevGetCaps), "XInputGetCapabilities ordinal");
                continue;
            }
            auto imp = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (!strcmp(imp->Name, "XInputGetState"))
                claims += ClaimIatSlot(slot, reinterpret_cast<void*>(&IatGetStateShim),
                    reinterpret_cast<void**>(&g_iatPrevGetState), "XInputGetState");
            else if (!strcmp(imp->Name, "XInputSetState"))
                claims += ClaimIatSlot(slot, reinterpret_cast<void*>(&IatSetStateShim),
                    reinterpret_cast<void**>(&g_iatPrevSetState), "XInputSetState");
            else if (!strcmp(imp->Name, "XInputGetCapabilities"))
                claims += ClaimIatSlot(slot, reinterpret_cast<void*>(&IatGetCapsShim),
                    reinterpret_cast<void**>(&g_iatPrevGetCaps), "XInputGetCapabilities");
        }
    }
    return claims;
}

// MCC's input-system activity flag: MCC's input
// update skips every device poll - all GAMEPLAY input - unless this byte is
// set, and only its window handler's WM_ACTIVATE(active)/WM_SETFOCUS sets it.
// At start-up the splash window's late deactivation can clear it after the
// game window's activation set it, and the (kept-active) game window never
// re-activates. Proven twice at first use: the update's test (cmp [rip+d],r12b)
// and the handler's setter (mov byte [rip+d],1) must be unique and resolve to
// the same byte. Any failed proof: no read, stock behaviour.
namespace
{
    const volatile uint8_t* ProveMccInputFlag() noexcept
    {
        uintptr_t base = 0;
        size_t size = 0;
        // The game executable itself (Steam: MCC-Win64-Shipping.exe, Store:
        // MCCWinStore-Win64-Shipping.exe): the process's main module.
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(GetModuleHandleW(nullptr));
        if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE)
            return nullptr;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const BYTE*>(dos) + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return nullptr;
        base = reinterpret_cast<uintptr_t>(dos);
        size = nt->OptionalHeader.SizeOfImage;
        constexpr const char* kUpdate =
            "48 8D 05 ?? ?? ?? ?? 48 89 44 24 48 48 8B C8 FF 15 ?? ?? ?? ?? 90 45 33 E4 44 38 25 ?? ?? ?? ?? 0F 84";
        constexpr const char* kSetter = "48 8B C3 48 8B C8 E8 ?? ?? ?? ?? C6 05 ?? ?? ?? ?? 01 C6 05 ?? ?? ?? ?? 01";
        auto unique = [&](const char* pattern) -> uintptr_t
        {
            const uintptr_t hit = sig::Find(base, size, pattern);
            return hit && !sig::Find(hit + 1, base + size - hit - 1, pattern) ? hit : 0;
        };
        const uintptr_t update = unique(kUpdate), setter = unique(kSetter);
        if (!update || !setter)
            return nullptr;
        const uintptr_t tested = sig::RipTarget(update + 28, update + 32);
        const uintptr_t written = sig::RipTarget(setter + 13, setter + 18);
        if (!tested || tested != written || tested < base || tested >= base + size)
            return nullptr;
        return reinterpret_cast<const volatile uint8_t*>(tested);
    }
    __declspec(noinline) int ReadMccInputFlag(const volatile uint8_t* flag) noexcept
    {
        __try
        {
            return *flag;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading an MCC input flag faulted");
            return -1;
        }
    }
}

// Worker (every input refresh): keep MCC's gameplay input active, and log the
// pad readers. While the proven flag reads 0 and the game window exists, the
// window thread re-sends WM_SETFOCUS (<=3 tries per inactive stretch, 2 s
// apart), in VR only and only while MCC's window or no window is in front. Config mcc_input_focus; no F1 control (rule 3 exemption: an internal
// compatibility fix with nothing player-facing to tune).
void Input_KeepMccInputActive(uint64_t now)
{
    {
        static int loggedReaders = -1;
        constexpr uint32_t kMinPolls = 10; // per ~2 s check: a continuous reader, not a one-off probe
        int readers = 0;
        char list[256]{};
        size_t used = 0;
        const uintptr_t exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        for (size_t i = 0; i < std::size(g_padCallers); ++i)
        {
            const uintptr_t caller = g_padCallers[i].load(std::memory_order_relaxed);
            if (!caller || g_padCallerPolls[i].exchange(0, std::memory_order_relaxed) < kMinPolls)
                continue;
            ++readers;
            if (used < sizeof(list))
            {
                const int n =
                    snprintf(list + used, sizeof(list) - used, " exe+0x%llX", (unsigned long long)(caller - exe));
                if (n > 0)
                    used += size_t(n);
            }
        }
        if (readers != loggedReaders)
        {
            LOG("MCC pad readers: %d active (%s )%s", readers, list,
                readers >= 2 ? " - MCC's gameplay reader polls the pad" : "");
            loggedReaders = readers;
        }
    }
    static bool tried = false;
    static const volatile uint8_t* flag = nullptr;
    if (!tried)
    {
        tried = true;
        flag = ProveMccInputFlag();
        LOG("MCC input focus: input-system flag %s",
            flag ? "proven (update test and window-handler setter agree)"
                 : "not proven for this MCC build; focus re-send disabled (stock)");
    }
    static int attempts = 0;
    static uint64_t lastAttemptMs = 0;
    if (!flag || !g_config.mcc_input_focus || !VR_IsStereoEnabled() || Menu_IsOpen())
        return;
    const HWND window = D3D_GameWindow();
    const HWND foreground = GetForegroundWindow();
    const int value = ReadMccInputFlag(flag);
    if (value > 0)
        attempts = 0;
    // A deliberate switch to another application clears the flag legitimately:
    // re-send only while MCC's window, or no window at all (headset), is in front.
    if (value != 0 || !window || !IsWindow(window) || (foreground && foreground != window))
        return;
    if (attempts >= 3 || (lastAttemptMs && now - lastAttemptMs < 2000))
        return;
    ++attempts;
    lastAttemptMs = now;
    NativeMenuPointer_ResendInputFocus();
    LOG("MCC input focus: MCC's input system is inactive (missed its window's activation); re-sending "
        "WM_SETFOCUS (attempt %d/3, %u delivered so far)",
        attempts, NativeMenuPointer_FocusResent());
}

// Pad-first menus: MCC
// switches its active input device - and with it the whole UI between
// controller and keyboard/mouse mode (prompts, focus, hover) - in
// SetActiveDevice, called only by its auto-switch when a device produces input.
// The menu laser moves the mouse, so MCC flipped to KBM under it and the pad's
// A/B/stick fought a different highlight. While VR is on, a switch TO a
// keyboard/mouse device is refused, so MCC stays in gamepad mode and the laser
// is a layer on top. Evidence for the device layout (5 slots at system+8,
// byte +0x801 = keyboard/mouse device) is the read-only RE in that doc; it is
// not re-proven at runtime, so the read is SEH-guarded and an unreadable or
// unexpected device always falls through to MCC's own switch.
// Proven at first use: the function's signature is unique at its pinned RVA,
// its single caller edge holds, and the device table / active-slot globals
// decode from its own instructions. Any failed proof: no hook (stock).
// Lifecycle: installed while mcc_pad_first_menus is on (F1), retired (disable,
// quiescence, remove) when it is turned off; a hook that cannot retire yet is
// kept disabled and retried, and re-enabled if the switch comes back on. There
// is no DLL-shutdown retire path in this mod (it lives for the whole MCC
// process, like the XInput import/export hooks), so an installed hook simply
// ends with the process.
namespace
{
    using SetActiveDeviceFn = void(__fastcall*)(void* self, int slot);
    SetActiveDeviceFn g_origSetActiveDevice = nullptr;
    void* g_padFirstTarget = nullptr;
    bool g_padFirstEnabled = false;  // the hook at g_padFirstTarget is enabled (worker only)
    uintptr_t g_mccDeviceSystem = 0; // address of the input-system pointer
    std::atomic<uint32_t> g_padFirstKept{0}, g_padFirstCallbacks{0}, g_padFirstAllowedPaused{0};
    // Diagnostic: the device kind MCC was last switched to through this detour
    // (-1 unknown, 0 gamepad, 1 keyboard/mouse), and whether that was during a pause.
    std::atomic<int> g_mccDeviceKind{-1};
    std::atomic<bool> g_mccDeviceKindPaused{false};

    __declspec(noinline) int MccDeviceIsKeyboardMouse(int slot) noexcept
    {
        __try
        {
            const uintptr_t system = *reinterpret_cast<const uintptr_t*>(g_mccDeviceSystem);
            if (!system || slot < 0 || slot > 4)
                return -1;
            const uintptr_t device = *reinterpret_cast<const uintptr_t*>(system + 8 + uintptr_t(slot) * 8);
            if (!device)
                return -1;
            return *reinterpret_cast<const uint8_t*>(device + 0x801) != 0 ? 1 : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading the MCC input device faulted");
            return -1;
        }
    }

    __declspec(noinline) void __fastcall SetActiveDeviceDetour(void* self, int slot)
    {
        g_padFirstCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            const int kind = MccDeviceIsKeyboardMouse(slot);
            const bool paused = VR_IsPausePresentation();
            const bool toMouse = g_config.mcc_pad_first_menus && VR_IsStereoEnabled() && kind == 1;
            if (toMouse && !PadFirstAllowsMouseMode(paused))
                g_padFirstKept.fetch_add(1, std::memory_order_relaxed); // stay on the current (gamepad) device
            else if (g_origSetActiveDevice)
            {
                if (toMouse)
                    g_padFirstAllowedPaused.fetch_add(1, std::memory_order_relaxed);
                g_origSetActiveDevice(self, slot);
                g_mccDeviceKindPaused.store(paused, std::memory_order_relaxed);
                g_mccDeviceKind.store(kind, std::memory_order_release);
            }
        }
        __finally
        {
            g_padFirstCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    // The proven hook address, or 0.
    uintptr_t ProvePadFirstMenus() noexcept
    {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(GetModuleHandleW(nullptr));
        if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE)
            return 0;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const BYTE*>(dos) + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return 0;
        const uintptr_t base = reinterpret_cast<uintptr_t>(dos);
        const size_t size = nt->OptionalHeader.SizeOfImage;
        constexpr const char* kSetActive =
            "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 20 57 41 56 41 57 48 83 EC 30 48 8B 05 ?? ?? ?? ?? "
            "45 33 FF 48 63 F2 48 8B E9 4C 8B 74 F0 08 4D 85 F6 0F 84 ?? ?? ?? ?? 39 71 50 74 ?? E8";
        const uintptr_t hit = sig::Find(base, size, kSetActive);
        if (!hit || sig::Find(hit + 1, base + size - hit - 1, kSetActive))
            return 0;
        // The single caller (the auto-switch) must call exactly this function.
        const auto* call = reinterpret_cast<const uint8_t*>(base + 0x8CD385);
        if (hit - base != 0x8CEF8C || size <= 0x8CF0CA || call[0] != 0xE8 ||
            base + 0x8CD385 + 5 + *reinterpret_cast<const int32_t*>(call + 1) != hit)
            return 0;
        // Its own instructions name the globals: mov rax,[rip+d] (input system)
        // and, at the end, mov [rip+d],esi (active slot).
        const uintptr_t system = sig::RipTarget(hit + 0x1B, hit + 0x1F);
        const auto* write = reinterpret_cast<const uint8_t*>(base + 0x8CF0C4);
        if (write[0] != 0x89 || write[1] != 0x35)
            return 0;
        const uintptr_t active = sig::RipTarget(base + 0x8CF0C6, base + 0x8CF0CA);
        if (system < base || system >= base + size || active != system + 0xC)
            return 0;
        g_mccDeviceSystem = system;
        return hit;
    }

    bool RetirePadFirstMenus() noexcept
    {
        if (!g_padFirstTarget)
            return true;
        if (!HookDisableStatusIsSafe(MCCVR_DisableHookForRetirement(g_padFirstTarget)))
            return false;
        g_padFirstEnabled = false;
        const void* functions[]{reinterpret_cast<const void*>(&SetActiveDeviceDetour)};
        const void* trampolines[]{reinterpret_cast<const void*>(g_origSetActiveDevice)};
        if (!WaitForNativeDetourQuiescence(functions, trampolines, 1, g_padFirstCallbacks))
            return false;
        const MH_STATUS removed = MCCVR_RemoveHookChecked(g_padFirstTarget);
        if (removed != MH_OK && removed != MH_ERROR_NOT_CREATED)
            return false;
        g_padFirstTarget = nullptr;
        g_origSetActiveDevice = nullptr;
        return true;
    }
}

// Worker (every input refresh): follow the switch, report changes.
void Input_PadFirstMenus()
{
    static bool proved = false;
    static uintptr_t address = 0;
    if (!proved)
    {
        proved = true;
        address = ProvePadFirstMenus();
        LOG("MCC pad-first menus: active-device switch %s",
            address ? "proven" : "not proven for this MCC build; stock device switching");
    }
    if (g_config.mcc_pad_first_menus && address && !g_padFirstTarget)
    {
        const GatedHook hook =
            InstallGatedHookWith([](void* t, void* d, void** o) { return MCCVR_CreateHookChecked(t, d, o); },
                [](void* t) { return MH_EnableHook(t); }, [](void* t) { return MCCVR_DisableHookForRetirement(t); },
                [](void* t) { return MCCVR_RemoveHookChecked(t); }, g_padFirstTarget, reinterpret_cast<void*>(address),
                reinterpret_cast<void*>(&SetActiveDeviceDetour), reinterpret_cast<void**>(&g_origSetActiveDevice));
        LOG("MCC pad-first menus: %s", hook.enabled    ? "hooked (VR keeps MCC in gamepad mode)"
                                       : hook.retained ? "hook failed and could not be backed out; kept for retirement"
                                                       : "hook failed; stock device switching");
        g_padFirstEnabled = hook.enabled;
        if (!hook.enabled && !hook.retained)
            address = 0; // do not retry every refresh
    }
    else if (g_config.mcc_pad_first_menus && g_padFirstTarget && !g_padFirstEnabled)
    {
        // Switched back on while an earlier retire was still pending (disabled,
        // not yet removed): enable the existing hook again.
        static bool reenableFailureLogged = false;
        g_padFirstEnabled = MH_EnableHook(g_padFirstTarget) == MH_OK;
        if (g_padFirstEnabled || !reenableFailureLogged)
            LOG("MCC pad-first menus: %s", g_padFirstEnabled
                                               ? "re-enabled the pending hook"
                                               : "could not re-enable the pending hook; retried quietly every refresh");
        reenableFailureLogged = !g_padFirstEnabled;
    }
    else if (!g_config.mcc_pad_first_menus && g_padFirstTarget)
    {
        LOG("MCC pad-first menus: switched off; %s",
            RetirePadFirstMenus() ? "hook retired (stock device switching)" : "hook busy, retire retried next refresh");
    }
    static uint32_t loggedAllowed = 0;
    const uint32_t allowed = g_padFirstAllowedPaused.load(std::memory_order_relaxed);
    if (allowed != loggedAllowed)
    {
        LOG("MCC pad-first menus: allowed keyboard/mouse mode %u time(s) during a pause "
            "(MCC's own dialogs take laser clicks only if they open in that mode; a pad press switches back)",
            allowed);
        loggedAllowed = allowed;
    }
    {
        // Diagnostic: each change of MCC's active device kind, so a run shows
        // which mode MCC was in when the Save & Quit dialog opened and when the
        // laser clicked (the dialog must open in keyboard/mouse mode).
        static int loggedKind = -1;
        const int kind = g_mccDeviceKind.load(std::memory_order_acquire);
        if (kind != loggedKind && kind >= 0)
        {
            const char* frame = "";
            const char* delivery = "";
            NativeMenuPointer_Status(frame, delivery);
            LOG("MCC active device: now %s (%s; pointer %s/%s)", kind ? "KEYBOARD/MOUSE" : "GAMEPAD",
                g_mccDeviceKindPaused.load(std::memory_order_relaxed) ? "during a pause" : "not paused", frame,
                delivery);
            loggedKind = kind;
        }
    }
    static uint32_t logged = 0;
    const uint32_t kept = g_padFirstKept.load(std::memory_order_relaxed);
    if (kept != logged)
    {
        LOG("MCC pad-first menus: kept the gamepad active %u time(s) (mouse/laser input would have switched "
            "MCC to keyboard/mouse mode)",
            kept);
        logged = kept;
    }
}

int Input_InstallXInputHook()
{
    // MCC may load any of these depending on OS/build, and it can load them
    // LATE (well after our first attempt) — the caller keeps retrying forever.
    // Safe to call repeatedly: already-hooked slots are skipped.
    const wchar_t* candidates[3] = {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};
    int hooked = 0;
    for (int i = 0; i < 3; ++i)
    {
        HMODULE mod = GetModuleHandleW(candidates[i]);
        if (!mod)
            continue;
        for (int j = 0; j < 2; ++j)
        {
            const int slot = i * 2 + j;
            if (g_origGetState[slot])
            {
                ++hooked;
                continue;
            }
            void* target = reinterpret_cast<void*>(
                j == 0 ? GetProcAddress(mod, "XInputGetState")
                       : GetProcAddress(mod, reinterpret_cast<LPCSTR>(100))); // XInputGetStateEx
            if (!target)
                continue;
            if (MCCVR_CreateHookChecked(target, reinterpret_cast<void*>(g_hooks[slot]),
                    reinterpret_cast<void**>(&g_origGetState[slot])) == MH_OK &&
                MH_EnableHook(target) == MH_OK)
            {
                LOG("XInputGetState%s hooked in %ls", j == 0 ? "" : "Ex", candidates[i]);
                ++hooked;
            }
            else
            {
                g_origGetState[slot] = nullptr;
            }
        }
        if (!g_origGetCaps[i])
        {
            void* capsTarget = reinterpret_cast<void*>(GetProcAddress(mod, "XInputGetCapabilities"));
            if (capsTarget &&
                MCCVR_CreateHookChecked(capsTarget, reinterpret_cast<void*>(g_capsHooks[i]),
                    reinterpret_cast<void**>(&g_origGetCaps[i])) == MH_OK &&
                MH_EnableHook(capsTarget) == MH_OK)
            {
                LOG("XInputGetCapabilities hooked in %ls", candidates[i]);
            }
            else
            {
                g_origGetCaps[i] = nullptr;
            }
        }
        if (!g_origSetState[i])
        {
            void* setTarget = reinterpret_cast<void*>(GetProcAddress(mod, "XInputSetState"));
            if (setTarget &&
                MCCVR_CreateHookChecked(setTarget, reinterpret_cast<void*>(g_setStateHooks[i]),
                    reinterpret_cast<void**>(&g_origSetState[i])) == MH_OK &&
                MH_EnableHook(setTarget) == MH_OK)
            {
                LOG("XInputSetState hooked in %ls", candidates[i]);
            }
            else
            {
                g_origSetState[i] = nullptr;
            }
        }
    }
    static bool warned = false;
    if (!hooked && !warned)
    {
        LOG("no XInput module loaded yet — retrying until one appears");
        warned = true;
    }
    return hooked;
}

// Pad 0 as MCC last received it (lock-free; read by the fault logger).
uint64_t Input_LastPadDeliveryMs() noexcept
{
    return g_lastPadDeliveryMs.load(std::memory_order_relaxed);
}
uint32_t Input_LastPadButtons() noexcept
{
    return g_lastPadButtons.load(std::memory_order_relaxed);
}
