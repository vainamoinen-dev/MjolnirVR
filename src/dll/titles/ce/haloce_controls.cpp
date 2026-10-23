// CE controls: verified native local-player state, VR turning, look tests, physical crouch,
// vehicle identity and the occupied-vehicle frame. Polled by the CE title tick in game.cpp; the
// other CE files and vr.cpp read the state through haloce_controls.h.
// tests/haloce_controls_runtime_tests.cpp compiles this file directly. The native state stays
// available even when an optional hook (turn, look tests) cannot be installed.
#include "haloce_controls.h"
#include "../../core/hook_install_check.h"        // MinHook create/remove go through the install-time detour check
void Game_NoteSnapTurn(GameTitle title) noexcept; // game.cpp
#include "../../../common/titles/ce/haloce_vehicle_frame.h"
#include "../../core/physical_crouch_camera.h"
#include "../../../common/physical_crouch_native_read.h"
#include "../../../common/titles/ce/haloce_crouch_contract.h"
#include "../../../common/titles/ce/haloce_vehicle_identity_contract.h"
#include "../../core/native_vehicle_first_person.h"
#include "haloce_stereo_core.h"
#include "haloce_native_bindings.h"
#include "../../core/hook_quiescence.h"
#include "../../core/title_adapter.h"
#include "../../../common/titles/ce/haloce_controls_logic.h"
#include "../../../common/head_look_test.h"
#include "../../../common/minhook_lifecycle.h"
#include "../../../common/log.h"
#include <windows.h>
#include <intrin.h>
#include <MinHook.h>
#include "../../core/guard_count.h"
bool Config_CeVehicleEyeProvisional() noexcept; // config.cpp (ce_vehicle_eye_provisional)

namespace
{
    using namespace halo_ce;
    using ObjectGetFn = uintptr_t(__fastcall*)(uint32_t, uint32_t);
    using WeaponOwnerFn = uint32_t(__fastcall*)(uint32_t);
    using PerspectiveFn = int16_t(__fastcall*)(int16_t);
    using DatumGetFn = uintptr_t(__fastcall*)(uintptr_t, uint32_t);
    using TurnFn = void(__fastcall*)(int32_t, float, float);
    HMODULE retainedModule{};
    uintptr_t moduleBase{};
    void* turnTarget{};
    void* turnOriginal{};
    bool turnEnabled{};
    std::atomic<uint32_t> generation{}, callbacks{};
    std::atomic<bool> active{}, retiring{}, stateReady{}, turnReady{};
    std::atomic<bool> vehicleViewReady{};
    std::atomic<uint64_t> lastOwned{}, observed{}, applied{}, refused{}, exceptions{};
    uint32_t stateFailedGeneration{}, turnFailedGeneration{};
    // Look tutorial (contract group look_tests): CE's action-test word, whose
    // bits 7-10 are player_action_test_look_relative_up/down/left/right.
    // The builder (+0xA9A42C) sets them from the sign of the input's yaw/pitch
    // deltas; the five hs evaluates tell us which ones a script is polling.
    std::atomic<bool> lookTestsReady{};
    std::atomic<bool> lookTutorialEnabled{true}; // ce_tutorial_head_look
    uintptr_t lookTestGlobals{};
    uint32_t lookTestsFailedGeneration{};
    struct LookHook
    {
        void* target{};
        void* original{};
        bool enabled{};
    };
    LookHook lookBuilder{};
    LookHook lookEvaluates[head_look_test::PollCount]{};
    head_look_test::PollTracker lookPolls;
    std::atomic<uint64_t> lookFedBits{}, lookFedTicks{};
    double qpcSeconds{};
    uint64_t lastReport{};
    thread_local ControlTurnState turnState;
    thread_local VehicleTurnFollow vehicleFollow;
    struct Callback
    {
        Callback() { callbacks.fetch_add(1, std::memory_order_acq_rel); }
        ~Callback() { callbacks.fetch_sub(1, std::memory_order_release); }
    };
    bool StateCurrent() noexcept
    {
        return stateReady.load(std::memory_order_acquire) && active.load(std::memory_order_acquire) &&
               !retiring.load(std::memory_order_acquire) && TitleAdapter_GetActiveTitle() == GameTitle::HaloCE &&
               TitleAdapter_GetGeneration(GameTitle::HaloCE) == generation.load(std::memory_order_acquire);
    }
    bool TurnCurrent() noexcept
    {
        return turnReady.load(std::memory_order_acquire) && StateCurrent();
    }
#include "haloce_vehicle_identity.inl"

    static bool ReadNativePaused(bool& paused) noexcept
    {
        if (!StateCurrent())
            return false;
        const auto before = generation.load(std::memory_order_acquire);
        uint8_t value{};
        __try
        {
            const uintptr_t clock = *reinterpret_cast<const uintptr_t*>(moduleBase + 0x2e9fd68);
            if (!clock || *reinterpret_cast<const uint8_t*>(clock) != 1)
                return false;
            value = *reinterpret_cast<const uint8_t*>(clock + 2);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading CE's pause flag faulted");
            exceptions.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        if (value > 1 || !StateCurrent() || before != generation.load(std::memory_order_acquire))
            return false;
        paused = value != 0;
        return true;
    }

    static bool ReadInputSuppressed(bool& suppressed) noexcept
    {
        if (!StateCurrent())
            return false;
        const auto before = generation.load(std::memory_order_acquire);
        bool candidate{};
        __try
        {
            const uintptr_t mapping = *reinterpret_cast<const uintptr_t*>(moduleBase + 0x2ea2d90);
            if (!mapping)
                return false;
            const uint32_t player = *reinterpret_cast<const uint32_t*>(mapping + 0xb8);
            if (player == UINT32_MAX || !(player >> 16))
                return false;
            int input = -1;
            for (int i = 0; i < 4; ++i)
                if (*reinterpret_cast<const uint32_t*>(mapping + 4 + i * 4) == player)
                {
                    if (input != -1)
                        return false;
                    input = i;
                }
            if (input < 0)
                return false;
            // Same input-suppression predicates consumed by A9915C; no unit lookup and no
            // stronger claim about why the native engine suppresses input.
            candidate = *reinterpret_cast<const uint8_t*>(moduleBase + 0x2d9b960 + input * 0xf8 + 0x59) != 0 ||
                        (*reinterpret_cast<const int32_t*>(moduleBase + 0x1c34fc8) == 0 &&
                            *reinterpret_cast<const int32_t*>(moduleBase + 0x1b85760) != -1);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading CE's input suppression faulted");
            exceptions.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        if (!StateCurrent() || before != generation.load(std::memory_order_acquire))
            return false;
        suppressed = candidate;
        return true;
    }

    static bool ReadLocalPlayerState(HaloCELocalPlayerState& state) noexcept
    {
        if (!StateCurrent())
            return false;
        HaloCELocalPlayerState candidate{};
        __try
        {
            candidate.generation = generation.load(std::memory_order_acquire);
            const uintptr_t mapping = *reinterpret_cast<const uintptr_t*>(moduleBase + 0x2ea2d90);
            const uintptr_t control = *reinterpret_cast<const uintptr_t*>(moduleBase + 0x2d8fe70);
            const uintptr_t players = *reinterpret_cast<const uintptr_t*>(moduleBase + 0x1c40480);
            const uintptr_t clock = *reinterpret_cast<const uintptr_t*>(moduleBase + 0x2e9fd68);
            const uintptr_t cinematic = *reinterpret_cast<const uintptr_t*>(moduleBase + 0x2ea0208);
            if (!mapping || !control || !players || !clock || !cinematic ||
                *reinterpret_cast<const uint8_t*>(clock) != 1)
                return false;
            candidate.nativePaused = *reinterpret_cast<const uint8_t*>(clock + 2) != 0;
            candidate.nativeCinematicFlag = *reinterpret_cast<const uint8_t*>(cinematic + 0xa) != 0;
            candidate.player = *reinterpret_cast<const uint32_t*>(mapping + 0xb8);
            // The native table API accepts salt zero as an index lookup. Our
            // published ownership requires a full salted datum instead.
            if (candidate.player == 0xffffffffu || !(candidate.player >> 16) ||
                *reinterpret_cast<const uint16_t*>(players + 0x22) != 0xc20)
                return false;
            const uintptr_t player = reinterpret_cast<DatumGetFn>(moduleBase + contract::player_state::state_datum_get)(
                players, candidate.player);
            if (!player)
                return false;
            candidate.unit = *reinterpret_cast<const uint32_t*>(player + 0x64);
            // Match the native input->output routing by complete player identity.
            for (int16_t input = 0; input < 4; ++input)
                if (*reinterpret_cast<const uint32_t*>(mapping + 4 + input * 4) == candidate.player)
                {
                    if (candidate.inputUser != -1)
                        return false;
                    candidate.inputUser = input;
                }
            if (candidate.inputUser < 0)
                return false;
            const uintptr_t director = moduleBase + 0x2d9b960 + size_t(candidate.inputUser) * 0xf8;
            candidate.nativeLookBlocked = *reinterpret_cast<const uint8_t*>(director + 0x58) != 0;
            candidate.nativeInputBlocked = *reinterpret_cast<const uint8_t*>(director + 0x59) != 0 ||
                                           (*reinterpret_cast<const int32_t*>(moduleBase + 0x1c34fc8) == 0 &&
                                               *reinterpret_cast<const int32_t*>(moduleBase + 0x1b85760) != -1);
            candidate.nativePerspective =
                reinterpret_cast<PerspectiveFn>(moduleBase + contract::player_state::state_user_perspective)(0);
            if (candidate.nativePerspective < 0 || candidate.nativePerspective > 3)
                return false;
            candidate.nativePreparesFirstPerson = candidate.nativePerspective == 0;
            if (*reinterpret_cast<const uint32_t*>(control + 0x170) != candidate.unit ||
                *reinterpret_cast<const uint32_t*>(control + 0x10 + size_t(candidate.inputUser) * 0x58) !=
                    candidate.unit)
                return false;
            const auto get = reinterpret_cast<ObjectGetFn>(moduleBase + contract::player_state::state_object_try_get);
            if (candidate.unit != 0xffffffffu)
            {
                if (!(candidate.unit >> 16))
                    return false;
                const uintptr_t unit = get(candidate.unit, 3);
                if (!unit || *reinterpret_cast<const uint32_t*>(unit + 0x1f8) != candidate.player)
                    return false;
                candidate.parent = *reinterpret_cast<const uint32_t*>(unit + 0xd8);
                candidate.onFoot = candidate.parent == 0xffffffffu;
                candidate.hasControlledUnit = true;
                const uintptr_t users = *reinterpret_cast<const uintptr_t*>(moduleBase + 0x2d9cd90);
                if (users)
                {
                    const uint32_t weapon = *reinterpret_cast<const uint32_t*>(users + 8);
                    candidate.hasFirstPersonUserRecord = true;
                    candidate.weaponSlotPresent = weapon != 0xffffffffu;
                    candidate.firstPersonVisible = (*reinterpret_cast<const uint8_t*>(users) & 1) != 0;
                    if (weapon != 0xffffffffu && get(weapon, 4) &&
                        reinterpret_cast<WeaponOwnerFn>(moduleBase + contract::player_state::state_weapon_owner)(
                            weapon) == candidate.unit)
                        candidate.weapon = weapon;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading CE's local player state faulted");
            exceptions.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        if (!StateCurrent() || candidate.generation != generation.load(std::memory_order_acquire))
            return false;
        state = candidate;
        return true;
    }

    bool AdmittedContext(HaloCELocalPlayerState& state, RenderContext& context) noexcept
    {
        if (!ReadLocalPlayerState(state) || !HaloCE_GetGameplayContext(context) ||
            state.generation != context.tracking.generation || !context.tracking.controllers.padValid)
            return false;
        return OnFootControls({state.hasControlledUnit, state.onFoot, state.nativePreparesFirstPerson,
            state.nativeInputBlocked, state.nativeLookBlocked, state.nativePaused, state.nativeCinematicFlag,
            context.tracking.controllers.controlsPresentationBlocked});
    }

    bool VehicleTurnFrame(
        const HaloCELocalPlayerState& state, const RenderContext& context, int16_t& seat, float& hullYaw) noexcept
    {
        if (!vehicleViewReady.load(std::memory_order_acquire) || !StateCurrent() ||
            !context.tracking.controllers.vehicleMotion || !context.tracking.controllers.padValid ||
            context.tracking.controllers.controlsPresentationBlocked ||
            state.generation != context.tracking.generation || !state.hasControlledUnit || state.onFoot ||
            (state.nativePerspective != 0 && state.nativePerspective != 1) || state.parent == UINT32_MAX ||
            !(state.parent >> 16) || state.nativeInputBlocked || state.nativeLookBlocked || state.nativePaused ||
            state.nativeCinematicFlag)
            return false;
        __try
        {
            const auto get = reinterpret_cast<ObjectGetFn>(moduleBase + contract::player_state::state_object_try_get);
            const uintptr_t occupant = get(state.unit, 1), parent = get(state.parent, 2);
            if (!occupant || !parent || *reinterpret_cast<const uint32_t*>(occupant + 0xd8) != state.parent ||
                *reinterpret_cast<const uint32_t*>(parent + 0xd8) != UINT32_MAX)
                return false;
            // HCEEK vehicle physics 8E1B60 and retail B45178 pass these actual
            // orientation vectors to their matrix builders. No desired aim read.
            const Vec3 forward = *reinterpret_cast<const Vec3*>(parent + 0x30);
            const Vec3 up = *reinterpret_cast<const Vec3*>(parent + 0x3c);
            seat = *reinterpret_cast<const int16_t*>(occupant + 0x2d0);
            if (seat < 0 || !Finite(forward) || !Finite(up) || std::fabs(Dot(forward, forward) - 1) > 0.05f ||
                std::fabs(Dot(up, up) - 1) > 0.05f || std::fabs(Dot(forward, up)) > 0.05f ||
                forward.x * forward.x + forward.y * forward.y < 0.0001f)
                return false;
            hullYaw = std::atan2(forward.y, forward.x);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading the CE vehicle turn frame faulted");
            exceptions.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        return StateCurrent() && state.generation == generation.load(std::memory_order_acquire) &&
               HaloCE_RenderContextCurrent(context);
    }

    bool AdmittedTurnContext(
        HaloCELocalPlayerState& state, RenderContext& context, bool& seated, int16_t& seat, float& hullYaw) noexcept
    {
        seated = false;
        if (AdmittedContext(state, context))
            return true;
        seated = VehicleTurnFrame(state, context, seat, hullYaw);
        return seated;
    }

    void TurnBody(int32_t inputUser, float yawDelta, float pitchDelta, uintptr_t caller)
    {
        const auto original = reinterpret_cast<TurnFn>(turnOriginal);
        if (!original)
            return;
        if (caller != moduleBase + 0xa99660 || !TurnCurrent())
        {
            original(inputUser, yawDelta, pitchDelta);
            return;
        }
        observed.fetch_add(1, std::memory_order_relaxed);
        HaloCELocalPlayerState state{};
        RenderContext context{};
        LARGE_INTEGER counter{};
        QueryPerformanceCounter(&counter);
        const double nowSeconds = static_cast<double>(counter.QuadPart) * qpcSeconds;
        bool seated = false;
        int16_t seat = -1;
        float hullYaw{};
        const bool admitted = AdmittedTurnContext(state, context, seated, seat, hullYaw);
        if (state.inputUser >= 0 && inputUser != state.inputUser)
        {
            original(inputUser, yawDelta, pitchDelta);
            return;
        }
        if (!admitted)
        {
            (void)turnState.Step(context, false, nowSeconds);
            vehicleFollow = {};
            lastOwned = 0;
            refused.fetch_add(1, std::memory_order_relaxed);
            original(inputUser, yawDelta, pitchDelta);
            return;
        }
        float turn = turnState.Step(context, true, nowSeconds, seated);
        // Count snaps only in snap mode (a fast smooth tick can exceed 5 degrees).
        {
            const auto& turnRig = context.tracking.controllers;
            if (turn != 0.0f && !UseSmoothVrTurn(turnRig.turnSmooth, turnRig.vehicleSmoothTurn, seated))
                Game_NoteSnapTurn(GameTitle::HaloCE);
        }
        if (seated)
            turn += vehicleFollow.Step(context, state.unit, state.parent, seat, hullYaw, nowSeconds);
        else
            vehicleFollow = {};
        if (!TurnCurrent() || !HaloCE_RenderContextCurrent(context))
        {
            lastOwned = 0;
            original(inputUser, yawDelta, pitchDelta);
            return;
        }
        // Let the native updater normalize yaw and apply its own angular clamps.
        // HMD pitch is already part of the render pose; raw native pitch
        // would create a second independently accumulated look rotation.
        original(inputUser, turn, 0.0f);
        lastOwned.store(GetTickCount64(), std::memory_order_release);
        applied.fetch_add(1, std::memory_order_relaxed);
    }

    void TurnDispatch(int32_t inputUser, float yawDelta, float pitchDelta, uintptr_t caller)
    {
        callbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            TurnBody(inputUser, yawDelta, pitchDelta, caller);
        }
        __finally
        {
            if (AbnormalTermination())
            {
                lastOwned = 0;
                exceptions.fetch_add(1, std::memory_order_relaxed);
            }
            callbacks.fetch_sub(1, std::memory_order_release);
        }
    }
    __declspec(noinline) void __fastcall TurnHook(int32_t inputUser, float yawDelta, float pitchDelta)
    {
        // Own the cleanup boundary at the actual hook entry. A wrapper tail-call
        // to TurnDispatch compiles as a leaf without x64 unwind metadata; the
        // retirement range resolver then rejects it forever after one camera gap.
        // Retain the older dispatch below the feature boundary for its existing
        // direct fixture coverage, but keep native caller identity at this entry.
        const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
        callbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            TurnBody(inputUser, yawDelta, pitchDelta, caller);
        }
        __finally
        {
            if (AbnormalTermination())
            {
                lastOwned = 0;
                exceptions.fetch_add(1, std::memory_order_relaxed);
            }
            callbacks.fetch_sub(1, std::memory_order_release);
        }
    }

    // ---- Look tutorial (method from the earlier CE VR mod's UpdatePitchYaw hook) ----
    // the earlier CE VR mod feeds (head - facing) as CE's native look delta, so the game's own
    // action-test builder sees real look input. Here the builder alone gets the
    // head's look: for its call only, the input's yaw/pitch deltas carry the
    // direction the head just turned (head_look_test::Step, 5 degrees), then the
    // real values are restored before anything else reads them - facing, view,
    // aim and movement never change. Only directions a script is polling are fed
    // (head_look_test::FeedBits); nothing is fed while no look test is polled.
    using LookBuilderFn = void(__fastcall*)(unsigned char*);
    using LookEvaluateFn = void(__fastcall*)(uint64_t, uint64_t, uint64_t, uint64_t);
    constexpr float kLookFeedRadians = head_look_test::kStepRadians;
    template <int Poll>
    void __fastcall LookEvaluateHook(uint64_t a, uint64_t b, uint64_t c, uint64_t d)
    {
        callbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            lookPolls.Note(static_cast<head_look_test::Poll>(Poll), GetTickCount64());
            const auto original = reinterpret_cast<LookEvaluateFn>(lookEvaluates[Poll].original);
            if (original)
                original(a, b, c, d);
        }
        __finally
        {
            callbacks.fetch_sub(1, std::memory_order_release);
        }
    }
    // The look bits to feed this builder call (0 = leave the input untouched).
    uint64_t LookBitsToFeed() noexcept
    {
        static head_look_test::Anchor anchor;
        const uint64_t now = GetTickCount64();
        const head_look_test::PollSnapshot polls = head_look_test::Snapshot(lookPolls, now);
        bool anyPoll = false;
        for (bool polled : polls.polled)
            anyPoll |= polled;
        HaloCELocalPlayerState state{};
        bool paused = true;
        float q[4]{}, p[3]{};
        if (!anyPoll || !lookTestsReady.load(std::memory_order_acquire) || !StateCurrent() ||
            !lookTutorialEnabled.load(std::memory_order_relaxed) || !ReadLocalPlayerState(state) ||
            !state.hasControlledUnit || state.nativeLookBlocked || !ReadNativePaused(paused) || paused ||
            !VR_GetHeadPose(q, p))
        {
            anchor = {};
            return 0;
        }
        float yaw{}, pitch{};
        head_look_test::YawPitchFromQuat(q, yaw, pitch);
        const uint64_t counted = head_look_test::Step(anchor, yaw, pitch);
        if (!counted)
            return 0;
        uint32_t word{};
        __try
        {
            auto* globals = *reinterpret_cast<unsigned char* volatile*>(lookTestGlobals);
            if (!globals)
                return 0;
            word = *reinterpret_cast<volatile uint32_t*>(globals);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading CE's look-test globals faulted");
            exceptions.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }
        return head_look_test::FeedBits(polls, counted, word);
    }
    void LookBuilderBody(unsigned char* input)
    {
        const auto original = reinterpret_cast<LookBuilderFn>(lookBuilder.original);
        if (!original)
            return;
        const uint64_t feed = input ? LookBitsToFeed() : 0;
        if (!feed)
        {
            original(input);
            return;
        }
        // Builder input (+0xA9A4B0..): +0x0C yaw delta (> 0 = left), +0x10 pitch
        // delta (> 0 = up).
        float* yaw = reinterpret_cast<float*>(input + 0x0C);
        float* pitch = reinterpret_cast<float*>(input + 0x10);
        const float savedYaw = *yaw, savedPitch = *pitch;
        if (feed & head_look_test::kLeftBit)
            *yaw = kLookFeedRadians;
        else if (feed & head_look_test::kRightBit)
            *yaw = -kLookFeedRadians;
        if (feed & head_look_test::kUpBit)
            *pitch = kLookFeedRadians;
        else if (feed & head_look_test::kDownBit)
            *pitch = -kLookFeedRadians;
        __try
        {
            original(input);
        }
        __finally
        {
            *yaw = savedYaw;
            *pitch = savedPitch;
        }
        lookFedBits.fetch_or(feed, std::memory_order_relaxed);
        lookFedTicks.fetch_add(1, std::memory_order_relaxed);
    }
    void __fastcall LookBuilderHook(unsigned char* input)
    {
        callbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            LookBuilderBody(input);
        }
        __finally
        {
            callbacks.fetch_sub(1, std::memory_order_release);
        }
    }
    bool DisableLookHooks() noexcept
    {
        LookHook* hooks[]{&lookBuilder, &lookEvaluates[0], &lookEvaluates[1], &lookEvaluates[2], &lookEvaluates[3],
            &lookEvaluates[4]};
        for (LookHook* hook : hooks)
        {
            if (!hook->target || !hook->enabled)
                continue;
            const auto result = MCCVR_DisableHookForRetirement(hook->target);
            if (result != MH_OK && result != MH_ERROR_DISABLED)
                return false;
            hook->enabled = false;
        }
        return true;
    }
    bool RemoveLookHooks() noexcept
    {
        LookHook* hooks[]{&lookBuilder, &lookEvaluates[0], &lookEvaluates[1], &lookEvaluates[2], &lookEvaluates[3],
            &lookEvaluates[4]};
        for (LookHook* hook : hooks)
        {
            if (hook->target)
            {
                const auto removed = MCCVR_RemoveHookChecked(hook->target);
                if (removed != MH_OK && removed != MH_ERROR_NOT_CREATED)
                    return false;
            }
            *hook = {};
        }
        lookPolls.Clear();
        return true;
    }
    // Optional: refusal leaves the look tutorial stick/mouse-only, as stock.
    bool InstallLookTests(uintptr_t base, size_t size, uint32_t gen) noexcept
    {
        const char* failure{};
        const NativeContractSet contracts{contract::look_tests::entries, contract::look_tests::witnesses,
            contract::look_tests::relatives, contract::look_tests::pointers};
        if (!VerifyNativeFeatureBindings(base, size, gen, contracts, failure))
        {
            LOG("CE look tutorial stock fallback: %s", failure ? failure : "binding failure");
            return false;
        }
        struct Spec
        {
            LookHook* hook;
            uint32_t rva;
            void* detour;
        };
        const Spec specs[]{
            {&lookBuilder, contract::look_tests::action_test_update, reinterpret_cast<void*>(&LookBuilderHook)},
            {&lookEvaluates[0], contract::look_tests::look_test_up_evaluate,
                reinterpret_cast<void*>(&LookEvaluateHook<0>)},
            {&lookEvaluates[1], contract::look_tests::look_test_down_evaluate,
                reinterpret_cast<void*>(&LookEvaluateHook<1>)},
            {&lookEvaluates[2], contract::look_tests::look_test_left_evaluate,
                reinterpret_cast<void*>(&LookEvaluateHook<2>)},
            {&lookEvaluates[3], contract::look_tests::look_test_right_evaluate,
                reinterpret_cast<void*>(&LookEvaluateHook<3>)},
            {&lookEvaluates[4], contract::look_tests::look_test_all_evaluate,
                reinterpret_cast<void*>(&LookEvaluateHook<4>)}};
        for (const Spec& spec : specs)
        {
            void* target = reinterpret_cast<void*>(base + spec.rva);
            const auto created = MCCVR_CreateHookChecked(target, spec.detour, &spec.hook->original);
            if (created != MH_OK)
            {
                LOG("CE look tutorial stock fallback: create status %d at +0x%X", created, spec.rva);
                (void)RemoveLookHooks();
                return false;
            }
            spec.hook->target = target;
        }
        lookTestGlobals = base + contract::look_tests::action_test_globals;
        for (const Spec& spec : specs)
        {
            const auto enabled = MH_EnableHook(spec.hook->target);
            if (enabled != MH_OK)
            {
                LOG("CE look tutorial stock fallback: enable status %d at +0x%X", enabled, spec.rva);
                if (!DisableLookHooks())
                    return false; // retained until ordinary retirement
                (void)RemoveLookHooks();
                lookTestGlobals = 0;
                return false;
            }
            spec.hook->enabled = true;
        }
        lookTestsReady = true;
        LOG("CE look tutorial: action-test builder and the five look evaluates hooked; while a script polls a "
            "look test, head look reaches the builder as look input (ce_tutorial_head_look=%d)",
            lookTutorialEnabled.load() ? 1 : 0);
        return true;
    }
    void LookTutorialReport() noexcept
    {
        const uint64_t fed = lookFedBits.exchange(0, std::memory_order_relaxed);
        const uint64_t ticks = lookFedTicks.exchange(0, std::memory_order_relaxed);
        if (!ticks)
            return;
        char letters[5];
        head_look_test::Letters(fed, letters);
        LOG("CE look tutorial: head look fed to the game's look tests %s on %llu builder tick(s)", letters,
            static_cast<unsigned long long>(ticks));
    }

    bool Remove() noexcept
    {
        active = false;
        retiring = true;
        stateReady = false;
        turnReady = false;
        vehicleViewReady = false;
        vehicleIdentityReady = false;
        lastOwned = 0;
        lookTestsReady = false;
        lookTestGlobals = 0;
        if (turnTarget && turnEnabled)
        {
            const auto result = MCCVR_DisableHookForRetirement(turnTarget);
            if (result != MH_OK && result != MH_ERROR_DISABLED)
                return false;
            turnEnabled = false;
        }
        if (!DisableLookHooks())
            return false;
        const void* functions[]{reinterpret_cast<const void*>(&TurnHook), reinterpret_cast<const void*>(&TurnDispatch),
            reinterpret_cast<const void*>(&TurnBody),
            reinterpret_cast<const void*>(&HaloCEControls_GetLocalPlayerState),
            reinterpret_cast<const void*>(&HaloCEControls_OwnsLookStick),
            reinterpret_cast<const void*>(&HaloCEControls_MapMoveStick),
            reinterpret_cast<const void*>(&HaloCEControls_GetLocomotionFrame),
            reinterpret_cast<const void*>(&HaloCEControls_GetNativePaused),
            reinterpret_cast<const void*>(&HaloCEControls_PhysicalCrouchCorrection),
            reinterpret_cast<const void*>(&HaloCEControls_ReadVehicleCameraOwner),
            reinterpret_cast<const void*>(&LookBuilderHook), reinterpret_cast<const void*>(&LookEvaluateHook<0>),
            reinterpret_cast<const void*>(&LookEvaluateHook<1>), reinterpret_cast<const void*>(&LookEvaluateHook<2>),
            reinterpret_cast<const void*>(&LookEvaluateHook<3>), reinterpret_cast<const void*>(&LookEvaluateHook<4>)};
        const void* trampolines[]{turnOriginal, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
            nullptr, lookBuilder.original, lookEvaluates[0].original, lookEvaluates[1].original,
            lookEvaluates[2].original, lookEvaluates[3].original, lookEvaluates[4].original};
        static_assert(std::size(functions) == std::size(trampolines));
        if (!WaitForNativeDetourQuiescence(functions, trampolines, std::size(functions), callbacks))
            return false;
        if (turnTarget && MCCVR_RemoveHookChecked(turnTarget) != MH_OK)
            return false;
        turnTarget = turnOriginal = nullptr;
        if (!RemoveLookHooks())
            return false;
        if (retainedModule)
        {
            FreeLibrary(retainedModule);
            retainedModule = nullptr;
        }
        moduleBase = 0;
        generation = 0;
        retiring = false;
        return true;
    }
#include "haloce_physical_crouch.inl"

    bool InstallState(uintptr_t base, size_t size, uint32_t gen) noexcept
    {
        const char* failure{};
        const NativeContractSet contracts{contract::player_state::entries, contract::player_state::witnesses,
            contract::player_state::relatives, contract::player_state::pointers};
        if (!VerifyNativeFeatureBindings(base, size, gen, contracts, failure))
        {
            LOG("CE native control state unavailable: %s", failure ? failure : "binding failure");
            return false;
        }
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(base), &retainedModule))
            return false;
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        if (frequency.QuadPart <= 0)
        {
            FreeLibrary(retainedModule);
            retainedModule = nullptr;
            return false;
        }
        qpcSeconds = 1.0 / static_cast<double>(frequency.QuadPart);
        PrepareCrouchCamera(base, size, gen);
        PrepareVehicleIdentity(base, size, gen);
        moduleBase = base;
        generation = gen;
        active = true;
        retiring = false;
        stateReady = true;
        LOG("CE native control state verified independently: player/input/output ownership and native admission flags");
        return true;
    }
    bool InstallTurn(uintptr_t base, size_t size, uint32_t gen) noexcept
    {
        const char* failure{};
        const NativeContractSet contracts{contract::controls::entries, contract::controls::witnesses,
            contract::controls::relatives, contract::controls::pointers};
        if (!VerifyNativeFeatureBindings(base, size, gen, contracts, failure))
        {
            LOG("CE VR turn stock fallback: %s", failure ? failure : "binding failure");
            return false;
        }
        const NativeContractSet vehicleContracts{contract::vehicle::entries, contract::vehicle::witnesses,
            contract::vehicle::relatives, contract::vehicle::pointers};
        const NativeContractSet viewContracts{contract::vehicle_view::entries, contract::vehicle_view::witnesses,
            contract::vehicle_view::relatives, contract::vehicle_view::pointers};
        vehicleViewReady = VerifyNativeFeatureBindings(base, size, gen, vehicleContracts, failure) &&
                           VerifyNativeFeatureBindings(base, size, gen, viewContracts, failure);
        if (!vehicleViewReady.load())
            LOG("CE vehicle view stock fallback: %s; on-foot turn and accepted steering retained",
                failure ? failure : "binding failure");
        void* target = reinterpret_cast<void*>(base + contract::controls::input_angle_delta);
        auto result = MCCVR_CreateHookChecked(target, reinterpret_cast<void*>(&TurnHook), &turnOriginal);
        if (result != MH_OK)
        {
            LOG("CE VR turn stock fallback: create status %d", result);
            return false;
        }
        turnTarget = target;
        result = MH_EnableHook(target);
        if (result != MH_OK)
        {
            LOG("CE VR turn stock fallback: enable status %d", result);
            // Keep an unenabled failed entry retained until ordinary retirement;
            // state readers remain independently available.
            return false;
        }
        turnEnabled = true;
        turnReady = true;
        LOG("CE VR turn installed: exact native input delta phase, shared snap latch/smooth rate; vehicle view=%d (optional actual hull yaw follow)",
            vehicleViewReady.load() ? 1 : 0);
        return true;
    }
}

static void VehicleFrameReport() noexcept;  // defined with the vehicle frame reader
static void VehicleMotionSample() noexcept; // likewise
void HaloCEControls_SetLookTutorial(bool enabled) noexcept
{
    lookTutorialEnabled.store(enabled, std::memory_order_relaxed);
}
bool HaloCEControls_Poll(uintptr_t base, size_t size, uint32_t gen, bool isActive) noexcept
{
    active.store(isActive, std::memory_order_release);
    if (retainedModule && (!isActive || base != moduleBase || gen != generation.load() || retiring.load()))
        if (!Remove())
            return false;
    if (!isActive || !base || !gen)
        return false;
    if (!stateReady.load() && gen != stateFailedGeneration)
        if (!InstallState(base, size, gen))
            stateFailedGeneration = gen;
    if (stateReady.load() && !turnReady.load() && gen != turnFailedGeneration)
        if (!InstallTurn(base, size, gen))
            turnFailedGeneration = gen;
    if (stateReady.load() && !lookTestsReady.load() && gen != lookTestsFailedGeneration)
        if (!InstallLookTests(base, size, gen))
            lookTestsFailedGeneration = gen;
    VehicleMotionSample();
    const uint64_t now = GetTickCount64();
    if (now - lastReport >= 2000)
    {
        lastReport = now;
        LOG("CE controls gen=%u state=%d turn=%d observed=%llu applied=%llu stock=%llu exceptions=%llu", gen,
            stateReady.load(), turnReady.load(), observed.load(), applied.load(), refused.load(), exceptions.load());
        LookTutorialReport();
        VehicleFrameReport();
    }
    return TurnCurrent();
}
bool HaloCEControls_GetLocalPlayerState(HaloCELocalPlayerState& state) noexcept
{
    Callback callback;
    return ReadLocalPlayerState(state);
}
static bool WeaponOwnedByBody(uint32_t weapon, uint32_t unit) noexcept
{
    __try
    {
        const auto get = reinterpret_cast<ObjectGetFn>(moduleBase + contract::player_state::state_object_try_get);
        return get(weapon, 4) &&
               reinterpret_cast<WeaponOwnerFn>(moduleBase + contract::player_state::state_weapon_owner)(weapon) == unit;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("checking the CE weapon owner faulted");
        exceptions.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
}
bool HaloCEControls_WeaponOwnedBy(uint32_t weapon, uint32_t unit) noexcept
{
    // The same proven weapon-object and native owner checks as the local
    // player's held weapon (ReadLocalPlayerState).
    Callback callback;
    if (!StateCurrent() || weapon == 0xffffffffu || !(weapon >> 16) || unit == 0xffffffffu)
        return false;
    return WeaponOwnedByBody(weapon, unit);
}
// Occupied-vehicle frame (haloce_vehicle_frame.h): hull position/forward/up and
// the seat, through the contract-proven object getter, re-read for tearing and
// checked against the occupant's own position. False: no frame (stock).
std::atomic<uint64_t> vehicleFrameRefused{};
// Motion proof of the +0x18 position offset (worker thread only; the state is
// published for readers on other threads).
haloce_vehicle_frame::MotionProof vehicleMotionProof;
std::atomic<int> vehicleProofState{0}; // 0 pending, 1 proven, 2 refuted
static bool SampleVehicleFrameBody(haloce_vehicle_frame::Frame& frame, haloce_vehicle_frame::Vec3& velocity) noexcept;
// Hull speed of the occupied vehicle (m/s), for the "CE seat proof" line.
std::atomic<float> vehicleSpeedMps{0.0f};
static bool ReadVehicleFrameBody(haloce_vehicle_frame::Frame& frame) noexcept
{
    // The motion proof needs three agreeing windows of real driving
    // (>= 1.8 m/s); in headset testing it never left Pending, so the eye
    // never moved. With ce_vehicle_eye_provisional the frame is used while the
    // proof is pending (Check() already validated it: finite, orthonormal,
    // occupant in range); a REFUTED proof still keeps it stock.
    const int proof = vehicleProofState.load(std::memory_order_acquire);
    if (proof == 2 || (proof == 0 && !Config_CeVehicleEyeProvisional()))
        return false;
    haloce_vehicle_frame::Vec3 velocity{};
    return SampleVehicleFrameBody(frame, velocity);
}
void HaloCEControls_VehicleProofStatus(int& state, int& agree, int& disagree, float& speedMps) noexcept
{
    state = vehicleProofState.load(std::memory_order_acquire);
    agree = vehicleMotionProof.Agree();
    disagree = vehicleMotionProof.Disagree();
    speedMps = vehicleSpeedMps.load(std::memory_order_relaxed);
}
// Worker: feed the motion proof (every CE controls poll).
static void VehicleMotionSample() noexcept
{
    haloce_vehicle_frame::Frame frame{};
    haloce_vehicle_frame::Vec3 velocity{};
    // Unknown speed: fail closed.
    if (!SampleVehicleFrameBody(frame, velocity))
    {
        vehicleSpeedMps.store(std::numeric_limits<float>::quiet_NaN(), std::memory_order_relaxed);
        return;
    }
    {
        // world units per tick -> m/s (30 ticks/s, 1 wu = 3.048 m)
        const float wuPerTick = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z);
        vehicleSpeedMps.store(std::isfinite(wuPerTick) ? wuPerTick * 30.0f * 3.048f : 0.0f, std::memory_order_relaxed);
    }
    if (vehicleProofState.load(std::memory_order_relaxed) != 0)
        return;
    vehicleMotionProof.Feed(frame.vehicle, frame.position, velocity, GetTickCount64());
    const auto state = vehicleMotionProof.Current();
    if (state == haloce_vehicle_frame::MotionProof::State::Pending)
        return;
    const bool proven = state == haloce_vehicle_frame::MotionProof::State::Proven;
    vehicleProofState.store(proven ? 1 : 2, std::memory_order_release);
    LOG("CE vehicle frame: position offset %s by motion (%d agreeing, %d disagreeing samples)%s",
        proven ? "PROVEN" : "REFUTED", vehicleMotionProof.Agree(), vehicleMotionProof.Disagree(),
        proven ? "" : "; vehicle frame stays stock");
}
// Refusal diagnostics: a count per reason, and the raw values of the first
// refused sample for each vehicle (copied here, logged by the worker report).
std::atomic<uint64_t> vehicleFrameRejects[6]{};
struct VehicleFrameRefusalSample
{
    haloce_vehicle_frame::Frame frame{};
    haloce_vehicle_frame::Vec3 occupant{};
    haloce_vehicle_frame::Reject reject{};
};
std::atomic<uint32_t> refusalLoggedVehicle{UINT32_MAX};
std::atomic<bool> refusalPending{false};
VehicleFrameRefusalSample refusalSample{};
static void NoteVehicleFrameRefusal(const haloce_vehicle_frame::Frame& frame,
    const haloce_vehicle_frame::Vec3& occupant, haloce_vehicle_frame::Reject reject) noexcept
{
    if (refusalLoggedVehicle.load(std::memory_order_relaxed) == frame.vehicle ||
        refusalPending.load(std::memory_order_acquire))
        return;
    refusalSample = {frame, occupant, reject};
    refusalLoggedVehicle.store(frame.vehicle, std::memory_order_relaxed);
    refusalPending.store(true, std::memory_order_release);
}
static void VehicleFrameRefusalReport() noexcept
{
    if (!refusalPending.load(std::memory_order_acquire))
        return;
    const auto s = refusalSample;
    refusalPending.store(false, std::memory_order_release);
    const auto& f = s.frame;
    const auto& o = s.occupant;
    const float dx = f.position.x - o.x, dy = f.position.y - o.y, dz = f.position.z - o.z;
    LOG("CE vehicle frame: first refused sample for vehicle %08X seat %d (%s): hull pos (%.3f,%.3f,%.3f) "
        "fwd (%.3f,%.3f,%.3f) up (%.3f,%.3f,%.3f) |fwd|^2 %.3f |up|^2 %.3f fwd.up %.3f; occupant +0x18 "
        "(%.3f,%.3f,%.3f) |occupant| %.3f, hull-occupant %.3f",
        f.vehicle, int(f.seat), haloce_vehicle_frame::RejectName(s.reject), f.position.x, f.position.y, f.position.z,
        f.forward.x, f.forward.y, f.forward.z, f.up.x, f.up.y, f.up.z, haloce_vehicle_frame::Dot(f.forward, f.forward),
        haloce_vehicle_frame::Dot(f.up, f.up), haloce_vehicle_frame::Dot(f.forward, f.up), o.x, o.y, o.z,
        std::sqrt(haloce_vehicle_frame::Dot(o, o)), std::sqrt(dx * dx + dy * dy + dz * dz));
}
static bool SampleVehicleFrameBody(haloce_vehicle_frame::Frame& frame, haloce_vehicle_frame::Vec3& velocity) noexcept
{
    using haloce_vehicle_frame::Vec3;
    HaloCELocalPlayerState state{};
    if (!vehicleViewReady.load(std::memory_order_acquire) || !ReadLocalPlayerState(state) || !state.hasControlledUnit ||
        state.onFoot || state.parent == UINT32_MAX || !(state.parent >> 16))
        return false;
    __try
    {
        const auto get = reinterpret_cast<ObjectGetFn>(moduleBase + contract::player_state::state_object_try_get);
        const uintptr_t unit = get(state.unit, 1), parent = get(state.parent, 2);
        if (!unit || !parent || *reinterpret_cast<const uint32_t*>(unit + 0xd8) != state.parent ||
            *reinterpret_cast<const uint32_t*>(parent + 0xd8) != UINT32_MAX)
            return false;
        haloce_vehicle_frame::Frame sample{};
        sample.position = *reinterpret_cast<const Vec3*>(parent + 0x18);
        const Vec3 sampledVelocity = *reinterpret_cast<const Vec3*>(parent + 0x24);
        sample.forward = *reinterpret_cast<const Vec3*>(parent + 0x30);
        sample.up = *reinterpret_cast<const Vec3*>(parent + 0x3c);
        sample.seat = *reinterpret_cast<const int16_t*>(unit + 0x2d0);
        sample.vehicle = state.parent;
        const Vec3 occupant = *reinterpret_cast<const Vec3*>(unit + 0x18);
        if (get(state.unit, 1) != unit || get(state.parent, 2) != parent ||
            *reinterpret_cast<const uint32_t*>(unit + 0xd8) != state.parent ||
            *reinterpret_cast<const int16_t*>(unit + 0x2d0) != sample.seat || !StateCurrent() ||
            state.generation != generation.load(std::memory_order_acquire))
            return false;
        const haloce_vehicle_frame::Reject reject = haloce_vehicle_frame::Check(sample, occupant);
        if (reject != haloce_vehicle_frame::Reject::None)
        {
            vehicleFrameRefused.fetch_add(1, std::memory_order_relaxed);
            vehicleFrameRejects[static_cast<size_t>(reject)].fetch_add(1, std::memory_order_relaxed);
            NoteVehicleFrameRefusal(sample, occupant, reject);
            return false;
        }
        sample.occupant = occupant;
        sample.occupantRelative = haloce_vehicle_frame::OccupantIsRelative(sample, occupant);
        frame = sample;
        velocity = sampledVelocity;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("sampling the CE vehicle frame faulted");
        exceptions.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
}
bool HaloCEControls_ReadVehicleFrame(haloce_vehicle_frame::Frame& frame) noexcept
{
    Callback callback;
    return ReadVehicleFrameBody(frame);
}
// Worker: one line when the occupied vehicle/seat (or the frame's validity) changes.
static void VehicleFrameReport() noexcept
{
    VehicleFrameRefusalReport();
    static uint32_t loggedVehicle = UINT32_MAX;
    static int16_t loggedSeat = -2;
    static uint64_t loggedRefused = 0;
    haloce_vehicle_frame::Frame frame{};
    haloce_vehicle_frame::Vec3 velocity{};
    const bool ok = SampleVehicleFrameBody(frame, velocity);
    const uint64_t refusedCount = vehicleFrameRefused.load(std::memory_order_relaxed);
    const uint32_t vehicle = ok ? frame.vehicle : UINT32_MAX;
    const int16_t seat = ok ? frame.seat : int16_t(-1);
    if (vehicle == loggedVehicle && seat == loggedSeat && refusedCount == loggedRefused)
        return;
    if (ok)
        LOG("CE vehicle frame: vehicle %08X seat %d pos (%.2f,%.2f,%.2f) fwd (%.2f,%.2f,%.2f) up (%.2f,%.2f,%.2f); "
            "refused %llu; offset proof %s",
            vehicle, int(seat), frame.position.x, frame.position.y, frame.position.z, frame.forward.x, frame.forward.y,
            frame.forward.z, frame.up.x, frame.up.y, frame.up.z, (unsigned long long)refusedCount,
            vehicleProofState.load() == 1   ? "proven"
            : vehicleProofState.load() == 2 ? "refuted"
                                            : "pending (drive to prove)");
    else if (loggedVehicle != UINT32_MAX || refusedCount != loggedRefused)
        LOG("CE vehicle frame: none (on foot, or the sample failed validation; refused %llu)",
            (unsigned long long)refusedCount);
    loggedVehicle = vehicle;
    loggedSeat = seat;
    loggedRefused = refusedCount;
}
static bool ReadVehicleCameraOwnerBody(NativeVehicleCameraOwner& owner) noexcept
{
    HaloCELocalPlayerState state{};
    if (!vehicleViewReady.load(std::memory_order_acquire) || !ReadLocalPlayerState(state) || !state.hasControlledUnit ||
        state.onFoot || state.nativeInputBlocked || state.nativeLookBlocked || state.nativePaused ||
        state.nativeCinematicFlag || (state.nativePerspective != 0 && state.nativePerspective != 1))
        return false;
    __try
    {
        const auto get = reinterpret_cast<ObjectGetFn>(moduleBase + contract::player_state::state_object_try_get);
        const uintptr_t unit = get(state.unit, 1), parent = get(state.parent, 2);
        if (!unit || !parent || *reinterpret_cast<const uint32_t*>(unit + 0xd8) != state.parent)
            return false;
        const auto seat = *reinterpret_cast<const int16_t*>(unit + 0x2d0);
        if (seat < 0 || !StateCurrent() || state.generation != generation.load(std::memory_order_acquire))
            return false;
        const uint64_t identity = ReadVehicleModelIdentity(parent);
        if (get(state.unit, 1) != unit || get(state.parent, 2) != parent ||
            *reinterpret_cast<const uint32_t*>(unit + 0xd8) != state.parent ||
            *reinterpret_cast<const int16_t*>(unit + 0x2d0) != seat || !StateCurrent() ||
            state.generation != generation.load(std::memory_order_acquire))
            return false;
        owner = {state.generation, state.unit, state.parent, seat, unit, identity};
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("reading the CE vehicle camera owner faulted");
        return false;
    }
}
bool HaloCEControls_ReadVehicleCameraOwner(NativeVehicleCameraOwner& owner) noexcept
{
    Callback callback;
    return ReadVehicleCameraOwnerBody(owner);
}
float HaloCEControls_PhysicalCrouchCorrection(
    uint32_t expectedGeneration, uint64_t epoch, float physicalDown, bool allowed) noexcept
{
    Callback callback;
    return CrouchCameraBody(expectedGeneration, epoch, physicalDown, allowed);
}

bool HaloCEControls_GetNativePaused(bool& paused, bool* suppressed, bool* inputKnown) noexcept
{
    Callback callback;
    if (inputKnown)
        *inputKnown = suppressed && ReadInputSuppressed(*suppressed);
    return ReadNativePaused(paused);
}
bool HaloCEControls_OwnsLookStick() noexcept
{
    Callback callback;
    const uint64_t stamp = lastOwned.load(std::memory_order_acquire), now = GetTickCount64();
    HaloCELocalPlayerState state{};
    RenderContext context{};
    bool seated = false;
    int16_t seat = -1;
    float hullYaw{};
    return TurnCurrent() && stamp && now >= stamp && now - stamp < 150 &&
           AdmittedTurnContext(state, context, seated, seat, hullYaw) && HaloCE_RenderContextCurrent(context);
}
bool HaloCEControls_MapMoveStick(float x, float y, float& outputX, float& outputY) noexcept
{
    Callback callback;
    HaloCELocalPlayerState state{};
    RenderContext context{};
    float candidateX{}, candidateY{};
    if (!AdmittedContext(state, context) || !HeadRelativeMovement(context, x, y, candidateX, candidateY) ||
        !StateCurrent() || !HaloCE_RenderContextCurrent(context))
        return false;
    outputX = candidateX;
    outputY = candidateY;
    return true;
}
bool HaloCEControls_GetLocomotionFrame(HaloCELocalPlayerState& state, RenderContext& context) noexcept
{
    Callback callback;
    HaloCELocalPlayerState candidateState{};
    RenderContext candidateContext{};
    if (!AdmittedContext(candidateState, candidateContext) || !StateCurrent() ||
        !HaloCE_RenderContextCurrent(candidateContext))
        return false;
    state = candidateState;
    context = candidateContext;
    return true;
}
