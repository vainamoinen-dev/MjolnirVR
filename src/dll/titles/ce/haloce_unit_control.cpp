// CE unit control: tracked movement and controller steering/aim for the local biped and verified
// vehicle seats (including the frozen turret-aim reference), plus a test-only weapon give.
// Polled by the CE title tick in game.cpp; haloce_hud.cpp asks it for seated aim.
// tests/haloce_unit_control_runtime_tests.cpp compiles it directly. Optional: refusal keeps stock input.
#include "haloce_unit_control.h"
#include "../../core/hook_install_check.h" // MinHook create/remove go through the install-time detour check
#include "haloce_controls.h"
#include "haloce_network_input.h"
#include "haloce_stereo_core.h"
#include "haloce_native_bindings.h"
#include "../../core/hook_quiescence.h"
#include "../../core/title_adapter.h"
#include "../../../common/titles/ce/haloce_controls_logic.h"
#include "../../../common/titles/ce/haloce_unit_control_logic.h"
#include "../../../common/titles/ce/haloce_vehicle_frame.h"
#include "../../../common/qa_give_logic.h"
#include "../../core/native_vehicle_first_person.h"
#include <cstring>
#include <atomic>
#include <cmath>
#include "../../../common/minhook_lifecycle.h"
#include "../../../common/log.h"
#include <windows.h>
#include <intrin.h>
#include <MinHook.h>
#include "../../core/guard_count.h"
bool Config_CeTurretAimFrozen() noexcept; // config.cpp (ce_turret_aim_frozen)

namespace
{
    using namespace halo_ce;
    // Turret aim (found in headset testing: the CE Warthog turret spun with no input).
    // The seated packet's aim is the hand's direction relative to the native
    // camera's heading, and a gunner's camera rides the turret, so a turning
    // turret turned the aim, which turned the turret again. With
    // ce_turret_aim_frozen a non-driver seat aims relative to the camera basis
    // captured in hull coordinates at seat entry, re-placed on the moving hull
    // (hull turns and bumps still carry the reference; the turret does not).
    haloce_vehicle_frame::FrozenReference frozenSeat;
    std::atomic<float> turretAimYawDeg{0}, turretCameraYawDeg{0};
    std::atomic<int> turretLogSeat{-1};
    std::atomic<uint64_t> turretLogMs{};
    bool FreezeSeatCamera(int16_t seat, Camera& camera) noexcept
    {
        haloce_vehicle_frame::Frame vf{};
        if (!HaloCEControls_ReadVehicleFrame(vf) || vf.seat != seat)
            return false; // keep the reference
        haloce_vehicle_frame::Vec3 f{camera.forward.x, camera.forward.y, camera.forward.z},
            u{camera.up.x, camera.up.y, camera.up.z};
        if (!frozenSeat.Apply(vf, seat, f, u))
            return false;
        turretCameraYawDeg.store(frozenSeat.cameraYawDeg, std::memory_order_relaxed);
        camera.forward = {f.x, f.y, f.z};
        camera.up = {u.x, u.y, u.z};
        return true;
    }
    using UnitControlFn = void(__fastcall*)(uint32_t, const UnitControlPacket*, int32_t);
    using MovementFn = void(__fastcall*)(void*);
    using ObjectGetFn = uintptr_t(__fastcall*)(uint32_t, uint32_t);
    void* target{};
    void* original{};
    void* movementTarget{};
    void* movementOriginal{};
    HMODULE retained{};
    uintptr_t moduleBase{};
    bool enabled{};
    bool movementEnabled{};
    std::atomic<bool> active{}, retiring{}, ready{}, vehicleReady{};
    std::atomic<uint32_t> generation{}, callbacks{};
    uint32_t rejectedGeneration{};
    uintptr_t rejectedBase{};
    enum class PendingCleanup
    {
        None,
        Disable,
        Quiescence,
        Remove,
        Callbacks
    };
    PendingCleanup pendingCleanup{};
    bool pinFailureReported{};
    std::atomic<uint64_t> bodies{}, aims{}, stock{}, declined{}, exceptions{};
    std::atomic<uint64_t> movements{}, movementStock{}, movementDeclined{};
    std::atomic<uint64_t> vehicles{}, vehicleDeclined{};
    uint64_t lastReport{};

    // Test-only give-all (env MJOLNIRVR_QA_GIVE_WEAPON=all at process start).
    // Retail halo1.dll cheat_all_weapons: no arguments, walks the player list and every loaded weapon tag.
    constexpr uint32_t kQaCheatAllWeaponsRva = 0xb21794;
    // MJOLNIRVR_QA_GIVE_WEAPON=sniper: cheat_all_weapons' own spawn helper (list of 16-byte tag
    // references, index at +0xC; item i lands 1.5 world units out at yaw + step*(i - count/2),
    // 0.8 up) given a one-entry list, so the sniper drops straight ahead of the player.
    constexpr uint32_t kQaSpawnWeaponListRva = 0xb21e84, kQaTagIterateRva = 0xa9b43c, kQaTagNameRva = 0xa9b288;
    // The spawn helper's own position/facing readers (object origin; object forward+up), used
    // read-only to log where the sniper should land and how far the player is from it.
    constexpr uint32_t kQaObjectOriginRva = 0xb37de0, kQaObjectFacingRva = 0xb37f98;
    constexpr contract::Entry kQaGiveEntries[]{
        {"qa_cheat_all_weapons", kQaCheatAllWeaponsRva,
            "40 53 48 81 EC 50 01 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 84 24 40 01 00 00 48 8B 0D ?? ?? ?? ?? "
            "33 DB 4C 8B 0D ?? ?? ?? ?? 8B C3 4C 8B 15 ?? ?? ?? ??",
            true},
        {"qa_spawn_weapon_list", kQaSpawnWeaponListRva,
            "48 8B C4 48 89 58 08 48 89 70 18 55 57 41 54 41 56 41 57 48 8D 68 B8 48 81 EC 20 01 00 00", true},
        // A leaf routine: no .pdata unwind entry, so verification with unwind=true fails here.
        {"qa_tag_iterate", kQaTagIterateRva, "48 8B 05 ?? ?? ?? ?? 83 CA FF 44 0F BF 41 08 4C 8B C9 44 8B 58 0C",
            false},
        {"qa_tag_name", kQaTagNameRva, "48 83 EC 28 33 D2 83 F9 FF 74 22 4C 8B 05 ?? ?? ?? ?? 4D 85 C0 74 16", true},
        {"qa_object_origin", kQaObjectOriginRva,
            "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B 35 ?? ?? ?? ?? 4C 8B D2 0F B7 C9", true},
        {"qa_object_facing", kQaObjectFacingRva,
            "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B 35 ?? ?? ?? ?? 4D 8B D0 0F B7 C9", true}};
    bool Current() noexcept;
    qa_give::OneShot qaShot;
    bool qaVerified{};
    const char* qaFailure{};
    // After a sniper give: the expected landing point, and 1 Hz player distance/bearing to
    // it plus the held weapon, until the held weapon changes or 120 s pass.
    bool qaTrack{};
    float qaLanding[3]{};
    uint32_t qaHeldAtGive{0xffffffffu};
    uint64_t qaTrackStartMs{}, qaTrackLogMs{};
    bool QaUnitPose(uint32_t unit, float pos[3], float fwd[3]) noexcept
    {
        float up[3]{};
        __try
        {
            reinterpret_cast<void(__fastcall*)(uint32_t, float*)>(moduleBase + kQaObjectOriginRva)(unit, pos);
            reinterpret_cast<void(__fastcall*)(uint32_t, float*, float*)>(moduleBase + kQaObjectFacingRva)(
                unit, fwd, up);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading the CE unit pose for QA faulted");
            return false;
        }
        return std::isfinite(pos[0]) && std::isfinite(pos[1]) && std::isfinite(pos[2]) && std::isfinite(fwd[0]) &&
               std::isfinite(fwd[1]);
    }
    void QaTrackPickup(const HaloCELocalPlayerState& state) noexcept
    {
        const uint64_t now = GetTickCount64();
        if (!qaTrack || now - qaTrackLogMs < 1000)
            return;
        qaTrackLogMs = now;
        float pos[3]{}, fwd[3]{};
        if (!QaUnitPose(state.unit, pos, fwd))
            return;
        const float dx = qaLanding[0] - pos[0], dy = qaLanding[1] - pos[1], dz = qaLanding[2] - pos[2];
        const float bearing = (std::atan2(dy, dx) - std::atan2(fwd[1], fwd[0])) * 57.2957795f;
        const float rel = std::remainder(bearing, 360.0f);
        const bool changed = state.weapon != qaHeldAtGive;
        LOG("QA give sniper: player at (%.2f,%.2f,%.2f) wu; landing point %.2f wu away (%.2f m, height %+.2f wu), "
            "%+.0f deg from facing; held weapon %08X%s",
            pos[0], pos[1], pos[2], std::sqrt(dx * dx + dy * dy), std::sqrt(dx * dx + dy * dy) * 3.048f, dz, rel,
            state.weapon, changed ? " (CHANGED since the give)" : "");
        if (changed || now - qaTrackStartMs > 120000)
            qaTrack = false;
    }
    void QaGiveAll(uint32_t unit) noexcept
    {
        if (qaShot.Done())
        {
            HaloCELocalPlayerState tracked{};
            if (qaTrack && unit != 0xffffffffu && HaloCEControls_GetLocalPlayerState(tracked) && unit == tracked.unit)
                QaTrackPickup(tracked);
            return;
        }
        HaloCELocalPlayerState state{};
        // Only the local player's own unit updates count; other units' updates must not reset the stable run.
        if (unit == 0xffffffffu || !HaloCEControls_GetLocalPlayerState(state) || unit != state.unit)
            return;
        qa_give::Inputs in{};
        in.bindingVerified = qaVerified;
        in.localSimulation = !HaloCENetworkInput_UsesNativeSimulation();
        in.playerProven = Current() && state.player != 0xffffffffu && state.hasControlledUnit && state.onFoot &&
                          state.generation == generation.load(std::memory_order_acquire) && !state.nativePaused &&
                          !state.nativeCinematicFlag && !state.nativeInputBlocked && !state.nativeLookBlocked;
        switch (qaShot.Step(in))
        {
        case qa_give::Action::Skip:
            LOG("%s: skipped: %s%s%s",
                qaShot.Requested() == qa_give::Request::Sniper ? "QA give sniper" : "QA give-all weapons",
                qaShot.Reason(), qaFailure ? " at " : "", qaFailure ? qaFailure : "");
            break;
        case qa_give::Action::Call:
            __try
            {
                if (qaShot.Requested() == qa_give::Request::Sniper)
                {
                    // Same iterator layout cheat_all_weapons builds: index word +8, -1 at +0xC, group +0x10.
                    alignas(8) uint8_t iterator[0x20]{};
                    *reinterpret_cast<int32_t*>(iterator + 0xC) = -1;
                    *reinterpret_cast<uint32_t*>(iterator + 0x10) = 0x77656170; // 'weap'
                    const auto next = reinterpret_cast<int32_t(__fastcall*)(void*)>(moduleBase + kQaTagIterateRva);
                    const auto name = reinterpret_cast<const char*(__fastcall*)(int32_t)>(moduleBase + kQaTagNameRva);
                    int32_t found = -1;
                    const char* foundName = "";
                    for (int32_t tag = next(iterator), guard = 0; tag != -1 && guard < 1024;
                        tag = next(iterator), ++guard)
                    {
                        const char* path = name(tag);
                        if (path && std::strstr(path, "sniper rifle"))
                        {
                            found = tag;
                            foundName = path;
                            break;
                        }
                    }
                    if (found == -1)
                        LOG("QA give sniper: skipped: no loaded weapon tag named \"sniper rifle\" in this map");
                    else
                    {
                        alignas(8) uint8_t entry[0x10]{};
                        *reinterpret_cast<int32_t*>(entry + 0xC) = found;
                        // The helper's own placement for a one-entry list: 1.5 wu along the unit's
                        // horizontal facing, 0.8 wu up (from its disassembly; the item then falls).
                        float pos[3]{}, fwd[3]{};
                        if (QaUnitPose(state.unit, pos, fwd))
                        {
                            const float yaw = std::atan2(fwd[1], fwd[0]);
                            qaLanding[0] = pos[0] + std::cos(yaw) * 1.5f;
                            qaLanding[1] = pos[1] + std::sin(yaw) * 1.5f;
                            qaLanding[2] = pos[2];
                            qaHeldAtGive = state.weapon;
                            qaTrack = true;
                            qaTrackStartMs = qaTrackLogMs = GetTickCount64();
                            LOG("QA give sniper: player at (%.2f,%.2f,%.2f) wu facing %.0f deg; the sniper spawns at "
                                "(%.2f,%.2f,%.2f) wu (0.8 up, then falls). Pickup in CE = native action/use = HOLD B under the "
                                "Immersive bindings (X is sprint, no effect in CE); walk onto it, no aim needed",
                                pos[0], pos[1], pos[2], yaw * 57.2957795f, qaLanding[0], qaLanding[1], pos[2] + 0.8f);
                        }
                        reinterpret_cast<void(__fastcall*)(void*, uint16_t)>(moduleBase + kQaSpawnWeaponListRva)(
                            entry, 1);
                        LOG("QA give sniper: spawned '%s' (tag 0x%X) 1.5 world units straight ahead of the player, 0.8 up "
                            "(native cheat_all_weapons spawn helper, halo1 RVA 0x%X)",
                            foundName, unsigned(found), kQaSpawnWeaponListRva);
                    }
                }
                else
                {
                    reinterpret_cast<void(__fastcall*)()>(moduleBase + kQaCheatAllWeaponsRva)();
                    LOG("QA give-all weapons: called (native cheat_all_weapons, halo1 RVA 0x%X)",
                        kQaCheatAllWeaponsRva);
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                GUARD_COUNT("the CE QA weapon give faulted");
                LOG("QA give-all weapons: skipped: native call faulted");
            }
            break;
        default:
            break;
        }
    }

    bool Current() noexcept
    {
        return active.load(std::memory_order_acquire) && !retiring.load(std::memory_order_acquire) &&
               TitleAdapter_GetActiveTitle() == GameTitle::HaloCE &&
               TitleAdapter_GetGeneration(GameTitle::HaloCE) == generation.load(std::memory_order_acquire);
    }
    bool Owner(uint32_t unit, HaloCELocalPlayerState& state, RenderContext& context) noexcept
    {
        return Current() && HaloCEControls_GetLocomotionFrame(state, context) && unit != 0xffffffffu &&
               unit == state.unit && state.player != 0xffffffffu &&
               state.generation == generation.load(std::memory_order_acquire) &&
               state.generation == context.tracking.generation &&
               OnFootControls({state.hasControlledUnit, state.onFoot, state.nativePerspective == 0,
                   state.nativeInputBlocked, state.nativeLookBlocked, state.nativePaused, state.nativeCinematicFlag,
                   context.tracking.controllers.controlsPresentationBlocked}) &&
               Current() && HaloCE_RenderContextCurrent(context);
    }
    bool CopyPacket(const UnitControlPacket* source, UnitControlPacket& output) noexcept
    {
        if (!source)
            return false;
        __try
        {
            output = *source;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("copying the CE input packet faulted");
            return false;
        }
    }
    bool VehicleOwner(uint32_t unit, HaloCELocalPlayerState& state, RenderContext& context, int16_t& seat) noexcept
    {
        if (!Current() || !vehicleReady.load(std::memory_order_acquire) || !HaloCEControls_GetLocalPlayerState(state) ||
            !HaloCE_GetGameplayContext(context) || unit == 0xffffffffu || unit != state.unit ||
            state.player == 0xffffffffu || !state.hasControlledUnit || state.onFoot || state.parent == 0xffffffffu ||
            !(state.parent >> 16) || (state.nativePerspective != 0 && state.nativePerspective != 1) ||
            state.inputUser < 0 || state.inputUser >= 4 || state.nativeInputBlocked || state.nativeLookBlocked ||
            state.nativePaused || state.nativeCinematicFlag || !context.tracking.controllers.padValid ||
            context.tracking.controllers.controlsPresentationBlocked ||
            state.generation != generation.load(std::memory_order_acquire) ||
            state.generation != context.tracking.generation)
            return false;
        // A following camera alone does not prove a vehicle.
        // Verify the seated local biped's direct parent with CE's native vehicle
        // object lookup. The seat is an identity only; no tag array is indexed.
        __try
        {
            const auto get = reinterpret_cast<ObjectGetFn>(moduleBase + contract::player_state::state_object_try_get);
            const uintptr_t occupant = get(unit, 1);
            if (!occupant || *reinterpret_cast<const uint32_t*>(occupant + 0xd8) != state.parent ||
                !get(state.parent, 2))
                return false;
            seat = *reinterpret_cast<const int16_t*>(occupant + 0x2d0);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading the CE vehicle owner faulted");
            exceptions.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        return seat >= 0 && Current() && vehicleReady.load(std::memory_order_acquire) &&
               HaloCE_RenderContextCurrent(context);
    }
    void UnitControlBody(uint32_t unit, const UnitControlPacket* source, int32_t clientUpdate, uintptr_t caller)
    {
        const auto native = reinterpret_cast<UnitControlFn>(original);
        if (!native)
            return;
        // Network prediction and authority must consume the same outgoing action.
        // Never replace a consumed network packet with a fresh local XR sample.
        if (HaloCENetworkInput_UsesNativeSimulation())
        {
            stock.fetch_add(1, std::memory_order_relaxed);
            native(unit, source, clientUpdate);
            return;
        }
        UnitControlPacket packet{}, candidate{};
        HaloCELocalPlayerState state{}, latest{};
        RenderContext context{}, latestContext{};
        bool aim{};
        if (caller == moduleBase + 0xad0d5b && ready.load(std::memory_order_acquire) && Owner(unit, state, context))
        {
            // Persistent support grip: primaryAim may carry support steering for a
            // different weapon incarnation. This packet may consume it only while
            // this invocation's current validated owner proves the durable
            // relationship owner and the frozen pose's support provenance is
            // continuous with the relationship across the two reads. A failed
            // relationship read, a released relationship whose frozen publication
            // still carries support-derived geometry, an owner change or a
            // relationship epoch change across the two reads fails closed to the
            // stock packet. With the feature off the decision is the base one.
            const bool supportWired = VR_SupportGripWiredForTitle(GameTitle::HaloCE);
            const CeSupportInvocation before =
                CeEvaluateSupportInvocation(supportWired, state, context.tracking.controllers.primaryAimSupportDerived);
            RenderContext effective = context;
            const bool carrierAvailable = !before.detach || context.tracking.controllers.independentPrimaryAim.valid;
            if (before.detach && carrierAvailable)
                effective.tracking.controllers.primaryAim = context.tracking.controllers.independentPrimaryAim;
            if (carrierAvailable && CopyPacket(source, packet) &&
                BuildTrackedUnitControl(effective, packet, candidate, aim) && Owner(unit, latest, latestContext))
            {
                const CeSupportInvocation after = CeEvaluateSupportInvocation(
                    supportWired, latest, latestContext.tracking.controllers.primaryAimSupportDerived);
                const bool usedSupport =
                    supportWired && !before.detach && context.tracking.controllers.primaryAimSupportDerived;
                const bool supportContinuityOk =
                    !usedSupport ||
                    (!after.detach && after.readable && before.readable && after.engaged && before.engaged &&
                        after.epoch == before.epoch && after.generation == before.generation &&
                        after.unit == before.unit && after.weapon == before.weapon);
                const bool decisionStable = before.detach == after.detach;
                if (state.player == latest.player && state.inputUser == latest.inputUser && state.unit == latest.unit &&
                    supportContinuityOk && decisionStable &&
                    context.referenceRevision == latestContext.referenceRevision &&
                    context.rendererEpoch == latestContext.rendererEpoch &&
                    context.tracking.spaceEpoch == latestContext.tracking.spaceEpoch &&
                    ready.load(std::memory_order_acquire) && HaloCE_RenderContextCurrent(context))
                {
                    // The original packet and native camera/input-angle state remain intact.
                    // The engine owns validation, native interpolation and grenade release.
                    native(unit, &candidate, clientUpdate);
                    bodies.fetch_add(1, std::memory_order_relaxed);
                    if (aim)
                        aims.fetch_add(1, std::memory_order_relaxed);
                    else
                        declined.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
            }
        }
        int16_t seat = -1, latestSeat = -1;
        if (caller == moduleBase + 0xad0d5b && ready.load(std::memory_order_acquire) &&
            VehicleOwner(unit, state, context, seat))
        {
            // Persistent support grip: BuildTrackedVehicleControl injects the same
            // primaryAim carrier, so the seat path applies the identical
            // current-owner policy: a failed read, a release discontinuity or an
            // owner/epoch change between the two reads fails closed to stock.
            const bool supportWired = VR_SupportGripWiredForTitle(GameTitle::HaloCE);
            const CeSupportInvocation before =
                CeEvaluateSupportInvocation(supportWired, state, context.tracking.controllers.primaryAimSupportDerived);
            RenderContext effective = context;
            const bool carrierAvailable = !before.detach || context.tracking.controllers.independentPrimaryAim.valid;
            if (before.detach && carrierAvailable)
                effective.tracking.controllers.primaryAim = context.tracking.controllers.independentPrimaryAim;
            // Only leaving a turret seat (or a vehicle/seat change inside
            // Apply) re-captures; a failed frame read keeps the frozen reference.
            if (Config_CeTurretAimFrozen() && seat > 0)
                FreezeSeatCamera(seat, effective.camera);
            else
                frozenSeat.Reset();
            if (carrierAvailable && CopyPacket(source, packet) &&
                BuildTrackedVehicleControl(effective, packet, candidate) &&
                VehicleOwner(unit, latest, latestContext, latestSeat))
            {
                const CeSupportInvocation after = CeEvaluateSupportInvocation(
                    supportWired, latest, latestContext.tracking.controllers.primaryAimSupportDerived);
                const bool usedSupport =
                    supportWired && !before.detach && context.tracking.controllers.primaryAimSupportDerived;
                const bool supportContinuityOk =
                    !usedSupport ||
                    (!after.detach && after.readable && before.readable && after.engaged && before.engaged &&
                        after.epoch == before.epoch && after.generation == before.generation &&
                        after.unit == before.unit && after.weapon == before.weapon);
                const bool decisionStable = before.detach == after.detach;
                if (state.player == latest.player && state.inputUser == latest.inputUser && state.unit == latest.unit &&
                    state.parent == latest.parent && seat == latestSeat &&
                    state.nativePerspective == latest.nativePerspective && supportContinuityOk && decisionStable &&
                    context.referenceRevision == latestContext.referenceRevision &&
                    context.rendererEpoch == latestContext.rendererEpoch &&
                    context.tracking.spaceEpoch == latestContext.tracking.spaceEpoch &&
                    ready.load(std::memory_order_acquire) && vehicleReady.load(std::memory_order_acquire) &&
                    HaloCE_RenderContextCurrent(context))
                {
                    // Native driver/gunner forwarding selects which occupant controls
                    // the parent. Physics, seat limits, throttle and actions stay native.
                    native(unit, &candidate, clientUpdate);
                    vehicles.fetch_add(1, std::memory_order_relaxed);
                    if (seat > 0 && frozenSeat.seat == seat)
                    {
                        const Vec3 seatAim = ReadUnitControl<Vec3>(candidate, 0x28);
                        turretAimYawDeg.store(
                            frozenSeat.HullYawDeg({seatAim.x, seatAim.y, seatAim.z}), std::memory_order_relaxed);
                        turretLogSeat.store(seat, std::memory_order_relaxed);
                        turretLogMs.store(GetTickCount64(), std::memory_order_release);
                    }
                    return;
                }
            }
            vehicleDeclined.fetch_add(1, std::memory_order_relaxed);
        }
        stock.fetch_add(1, std::memory_order_relaxed);
        native(unit, source, clientUpdate);
    }
    __declspec(noinline) void __fastcall UnitControlHook(
        uint32_t unit, const UnitControlPacket* packet, int32_t clientUpdate)
    {
        const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
        callbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            UnitControlBody(unit, packet, clientUpdate, caller);
            QaGiveAll(unit);
        }
        __finally
        {
            if (AbnormalTermination())
                exceptions.fetch_add(1, std::memory_order_relaxed);
            callbacks.fetch_sub(1, std::memory_order_release);
        }
    }
    bool ReadMovement(void* data, uint32_t& unit, UnitMovementBasis& basis) noexcept
    {
        if (!data)
            return false;
        __try
        {
            std::memcpy(&unit, data, sizeof(unit));
            std::memcpy(&basis, static_cast<uint8_t*>(data) + 0x14, sizeof(basis));
            return Finite(basis.forward) && Finite(basis.aim);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading the CE unit movement faulted");
            return false;
        }
    }
    bool WriteMovement(void* data, const UnitMovementBasis& basis) noexcept
    {
        if (!data)
            return false;
        __try
        {
            std::memcpy(static_cast<uint8_t*>(data) + 0x14, &basis, sizeof(basis));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("writing the CE unit movement faulted");
            return false;
        }
    }
    void MovementBody(void* data, uintptr_t caller)
    {
        const auto native = reinterpret_cast<MovementFn>(movementOriginal);
        if (!native)
            return;
        if (HaloCENetworkInput_UsesNativeSimulation())
        {
            movementStock.fetch_add(1, std::memory_order_relaxed);
            native(data);
            return;
        }
        uint32_t unit{};
        UnitMovementBasis before{}, candidate{};
        HaloCELocalPlayerState state{}, latest{};
        RenderContext context{}, latestContext{};
        if ((caller != moduleBase + contract::unit_control::movement_consumer_return &&
                caller != moduleBase + contract::unit_control::movement_consumer_return_secondary) ||
            !ready.load(std::memory_order_acquire) || !ReadMovement(data, unit, before) ||
            !Owner(unit, state, context) || !BuildUnitMovementBasis(context, candidate) ||
            !Owner(unit, latest, latestContext) || state.player != latest.player ||
            state.inputUser != latest.inputUser || state.unit != latest.unit ||
            context.referenceRevision != latestContext.referenceRevision ||
            context.rendererEpoch != latestContext.rendererEpoch ||
            context.tracking.spaceEpoch != latestContext.tracking.spaceEpoch ||
            !ready.load(std::memory_order_acquire) || !HaloCE_RenderContextCurrent(context))
        {
            movementStock.fetch_add(1, std::memory_order_relaxed);
            native(data);
            return;
        }
        if (!WriteMovement(data, candidate))
        {
            (void)WriteMovement(data, before);
            movementDeclined.fetch_add(1, std::memory_order_relaxed);
            native(data);
            return;
        }
        // Only private movement-input vectors change. Native collision, slope,
        // acceleration, output velocity and the live unit's aiming remain native.
        __try
        {
            native(data);
            movements.fetch_add(1, std::memory_order_relaxed);
        }
        __finally
        {
            if (!WriteMovement(data, before))
                movementDeclined.fetch_add(1, std::memory_order_relaxed);
        }
    }
    __declspec(noinline) void __fastcall MovementHook(void* data)
    {
        const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
        callbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            MovementBody(data, caller);
        }
        __finally
        {
            if (AbnormalTermination())
                exceptions.fetch_add(1, std::memory_order_relaxed);
            callbacks.fetch_sub(1, std::memory_order_release);
        }
    }
    bool Remove() noexcept
    {
        active = false;
        retiring = true;
        ready = false;
        vehicleReady = false;
        const auto pending = [](PendingCleanup stage, const char* reason)
        {
            if (pendingCleanup != stage)
                LOG("CE body/controller grenade aim cleanup pending: %s; optional feature stock, module/trampoline retained",
                    reason);
            pendingCleanup = stage;
            return false;
        };
        if (enabled)
        {
            const auto result = MCCVR_DisableHookForRetirement(target);
            if (result != MH_OK && result != MH_ERROR_DISABLED)
                return pending(PendingCleanup::Disable, "hook disable failed");
            enabled = false;
        }
        if (movementEnabled)
        {
            const auto result = MCCVR_DisableHookForRetirement(movementTarget);
            if (result != MH_OK && result != MH_ERROR_DISABLED)
                return pending(PendingCleanup::Disable, "movement hook disable failed");
            movementEnabled = false;
        }
        const void* functions[]{
            reinterpret_cast<const void*>(&UnitControlHook), reinterpret_cast<const void*>(&MovementHook)};
        const void* originals[]{original, movementOriginal};
        if ((target || movementTarget) && !WaitForNativeDetourQuiescence(functions, originals, 2, callbacks))
            return pending(PendingCleanup::Quiescence, "callback quiescence not proved");
        if (target)
        {
            const auto result = MCCVR_RemoveHookChecked(target);
            if (result != MH_OK && result != MH_ERROR_NOT_CREATED)
                return pending(PendingCleanup::Remove, "hook removal failed");
            target = original = nullptr;
        }
        if (movementTarget)
        {
            const auto result = MCCVR_RemoveHookChecked(movementTarget);
            if (result != MH_OK && result != MH_ERROR_NOT_CREATED)
                return pending(PendingCleanup::Remove, "movement hook removal failed");
            movementTarget = movementOriginal = nullptr;
        }
        if (callbacks.load(std::memory_order_acquire))
            return pending(PendingCleanup::Callbacks, "callback still active");
        target = original = movementTarget = movementOriginal = nullptr;
        if (retained)
        {
            FreeLibrary(retained);
            retained = nullptr;
        }
        moduleBase = 0;
        generation = 0;
        retiring = false;
        pendingCleanup = PendingCleanup::None;
        return true;
    }
    bool Install(size_t size) noexcept
    {
        const char* failure{};
        const NativeContractSet contracts{contract::unit_control::entries, contract::unit_control::witnesses,
            contract::unit_control::relatives, contract::unit_control::pointers};
        if (!VerifyNativeFeatureBindings(moduleBase, size, generation.load(), contracts, failure))
        {
            LOG("CE body/controller grenade aim stock fallback: %s", failure ? failure : "binding failure");
            return false;
        }
        // Check the independent vehicle evidence before any detour can alter a
        // native entry signature. Failure affects only this new optional path.
        const NativeContractSet vehicleContracts{contract::vehicle::entries, contract::vehicle::witnesses,
            contract::vehicle::relatives, contract::vehicle::pointers};
        const bool vehicleVerified =
            VerifyNativeFeatureBindings(moduleBase, size, generation.load(), vehicleContracts, failure);
        if (!vehicleVerified)
            LOG("CE vehicle controls stock fallback: %s; on-foot controls and stereo remain active",
                failure ? failure : "binding failure");
        {
            char value[16]{};
            const DWORD length = GetEnvironmentVariableA("MJOLNIRVR_QA_GIVE_WEAPON", value, sizeof value);
            const auto request =
                length && length < sizeof value ? qa_give::ParseRequest(value) : qa_give::Request::None;
            qaShot = qa_give::OneShot(request);
            qaVerified = false;
            qaFailure = nullptr;
            if (request != qa_give::Request::None)
            {
                const NativeContractSet giveContracts{kQaGiveEntries, {}, {}, {}};
                qaVerified = VerifyNativeFeatureBindings(moduleBase, size, generation.load(), giveContracts, qaFailure);
                if (qaVerified)
                    qaFailure = nullptr;
                LOG("%s: requested by environment; native routines %s; waits for about 2 s of a proven on-foot player",
                    request == qa_give::Request::Sniper ? "QA give sniper" : "QA give-all weapons",
                    qaVerified ? "verified" : "NOT verified");
            }
            else if (length)
                LOG("QA give: MJOLNIRVR_QA_GIVE_WEAPON is set but not recognised (%s); use \"all\" or \"sniper\"",
                    length < sizeof value ? value : "value too long");
        }
        void* location = reinterpret_cast<void*>(moduleBase + contract::unit_control::unit_control_set);
        auto result = MCCVR_CreateHookChecked(location, reinterpret_cast<void*>(&UnitControlHook), &original);
        if (result != MH_OK)
        {
            LOG("CE body/controller grenade aim stock fallback: create status %d", result);
            return false;
        }
        target = location;
        location = reinterpret_cast<void*>(moduleBase + contract::unit_control::movement_consumer);
        result = MCCVR_CreateHookChecked(location, reinterpret_cast<void*>(&MovementHook), &movementOriginal);
        if (result != MH_OK)
        {
            LOG("CE body/controller grenade aim stock fallback: movement create status %d", result);
            return false;
        }
        movementTarget = location;
        result = MH_EnableHook(target);
        if (result != MH_OK)
        {
            LOG("CE body/controller grenade aim stock fallback: enable status %d", result);
            return false;
        }
        enabled = true;
        result = MH_EnableHook(movementTarget);
        if (result != MH_OK)
        {
            LOG("CE body/controller grenade aim stock fallback: movement enable status %d", result);
            return false;
        }
        movementEnabled = true;
        ready = true;
        vehicleReady = vehicleVerified;
        if (vehicleReady.load())
            LOG("CE vehicle controls installed: tracked aiming-hand facing/aim/looking in local seated native packets; native following camera, driver/gunner forwarding and throttle retained; Original and Anniversary");
        LOG("CE unit control installed: local on-foot head facing/looking, controller aiming; private native-camera movement basis, native grenade release retained");
        return true;
    }
}

bool HaloCEUnitControl_Poll(uintptr_t base, size_t size, uint32_t gen, bool isActive) noexcept
{
    {
        // Turret-aim log, 1 Hz while a non-driver seat aims: the aim's and the turret
        // camera's yaw in the hull (a spin shows as the turret yaw running away).
        static uint64_t nextMs = 0;
        const uint64_t now = GetTickCount64(), at = turretLogMs.load(std::memory_order_acquire);
        if (now >= nextMs && at && now >= at && now - at <= 1000)
        {
            nextMs = now + 1000;
            LOG("CE turret aim: seat %d aim yaw %.1f turret yaw %.1f deg in the hull (reference %s)",
                turretLogSeat.load(), turretAimYawDeg.load(), turretCameraYawDeg.load(),
                Config_CeTurretAimFrozen() ? "frozen at seat entry" : "turret camera");
        }
    }
    if (retained && (!isActive || base != moduleBase || gen != generation.load() || retiring.load()))
        if (!Remove())
            return false;
    active.store(isActive, std::memory_order_release);
    if (!isActive || !base || !gen)
        return false;
    if (rejectedBase == base && rejectedGeneration == gen)
        return false;
    if (!retained)
    {
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(base), &retained))
        {
            if (!pinFailureReported)
                LOG("CE body/controller grenade aim stock fallback: module retention failed; retry pending");
            pinFailureReported = true;
            return false;
        }
        pinFailureReported = false;
        moduleBase = base;
        generation = gen;
        retiring = false;
    }
    if (!ready.load() && !Install(size))
    {
        rejectedBase = base;
        rejectedGeneration = gen;
        // A partial optional hook owns its trampoline and module until removal
        // is proved. Retry cleanup on later polls without retrying this binding.
        (void)Remove();
        return false;
    }
    const uint64_t now = GetTickCount64();
    if (now - lastReport >= 2000)
    {
        lastReport = now;
        LOG("CE unit control gen=%u ready=%d bodies=%llu aims=%llu stock=%llu aimDeclined=%llu movement=%llu movementStock=%llu movementDeclined=%llu exceptions=%llu vehicleReady=%d vehicles=%llu vehicleDeclined=%llu",
            gen, ready.load(), bodies.load(), aims.load(), stock.load(), declined.load(), movements.load(),
            movementStock.load(), movementDeclined.load(), exceptions.load(), vehicleReady.load(), vehicles.load(),
            vehicleDeclined.load());
    }
    return Current() && ready.load();
}

bool HaloCEUnitControl_VehicleAimCurrent(
    const HaloCELocalPlayerState& player, const halo_ce::RenderContext& context) noexcept
{
    callbacks.fetch_add(1, std::memory_order_acq_rel);
    bool admitted = false;
    __try
    {
        HaloCELocalPlayerState current{}, latest{};
        RenderContext frame{}, latestFrame{};
        int16_t seat = -1, latestSeat = -1;
        admitted =
            ready.load(std::memory_order_acquire) && VehicleOwner(player.unit, current, frame, seat) &&
            VehicleOwner(player.unit, latest, latestFrame, latestSeat) && player.generation == current.generation &&
            player.player == current.player && player.inputUser == current.inputUser &&
            player.parent == current.parent && !player.onFoot &&
            (player.nativePerspective == 0 || player.nativePerspective == 1) && current.player == latest.player &&
            current.inputUser == latest.inputUser && current.parent == latest.parent && seat == latestSeat &&
            player.nativePerspective == current.nativePerspective &&
            current.nativePerspective == latest.nativePerspective &&
            context.tracking.generation == frame.tracking.generation &&
            context.referenceRevision == frame.referenceRevision && context.rendererEpoch == frame.rendererEpoch &&
            context.tracking.spaceEpoch == frame.tracking.spaceEpoch &&
            frame.referenceRevision == latestFrame.referenceRevision &&
            frame.rendererEpoch == latestFrame.rendererEpoch &&
            frame.tracking.spaceEpoch == latestFrame.tracking.spaceEpoch &&
            frame.tracking.controllers.primaryAim.valid && latestFrame.tracking.controllers.primaryAim.valid &&
            ready.load(std::memory_order_acquire) && Current() && HaloCE_RenderContextCurrent(context);
    }
    __finally
    {
        callbacks.fetch_sub(1, std::memory_order_release);
    }
    return admitted;
}
