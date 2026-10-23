// Halo Reach physical contact melee: the title's native rows, detours, per-tick hand drain and install/remove.
// A failed install retires what it created; retirement keeps any hook it cannot remove for a retry.
#pragma once
// Included once, by src/dll/core/game.cpp; also compiled by
//   tests/reach_contact_melee_runtime_golden_tests.cpp. Not a standalone header: it uses what the
//   host defines above its include line, namely BoneMatrix, FpExplicitPoseTargets,
//   FpInterpolationContext, LegacyBuildMappedWeaponBounds, LegacyCollisionIgnoredObject,
//   ReachContactMeleeReady, ReachPublishContactHand, ReachVerifyRel32Call,
//   ReportReachContactMelee, g_enabled, g_reachCamera, g_reachCinematicLocked,
//   g_reachFpPairScope, g_reachWorldCollision, kLegacyCollisionWeaponSamples.
// Included in game.cpp after ReachVehicleObjectData. All native locations here
// are Reach-specific HREK homologs.
#include "../../../common/titles/reach/reach_contact_world.h"
#include "../../core/muzzle_hook_set.h"
using ReachContactUpdateFn = uint8_t(__fastcall*)(uint32_t);
using ReachContactBuildFn = void(__fastcall*)(uint32_t, int32_t, uint32_t*);
using ReachContactConsumeFn = void(__fastcall*)(uint32_t, uint64_t, int16_t, float, uint8_t, uint32_t*, void*);
using ReachContactDamageFn = void(__fastcall*)(uint32_t, int32_t, const void*, const void*, const float*);
struct ReachContactRuntime
{
    std::atomic<bool> enabled{false}, faulted{false}, processing{false};
    // Counts the two contact detours (they also count in the camera's
    // activeCallbacks), so a failed install can retire them on its own.
    std::atomic<uint32_t> callbacks{0};
    std::atomic<uint64_t> queries{0}, contacts{0}, submitted[2]{}, rejected{0}, overflow{0}, predicted{0};
    // The hook targets muzzle_hook_set.h creates, in install order
    // ([0] damage-direction, [1] simulation update). The Reach camera teardown
    // (DisableAndRemoveReachHooks) removes them in its own table order.
    void* targets[2]{};
    ReachContactDamageFn damageOriginal = nullptr;
    ReachContactUpdateFn original = nullptr;
    ReachContactBuildFn build = nullptr;
    ReachContactConsumeFn consume = nullptr;
    uint8_t(__fastcall* playback)() = nullptr;
    ContactMeleeQueue queue[2];
    contact_melee::Hand hands[2];
    uint32_t generation = 0;
} g_reachContact;
int16_t ReachContactPredictionMode()
{
    auto** slots = reinterpret_cast<void**>(__readgsqword(0x58));
    const auto index = *reinterpret_cast<const uint32_t*>(g_reachCamera.base + kReachEngineTlsIndexRva);
    if (!slots || index >= 0x200 || !slots[index])
        return -1;
    // Matched consumer reads game globals from TLS +48 and tests +11 ==4
    // for prediction. Its event route preserves the exact hit for the host.
    const auto globals = *reinterpret_cast<const uint8_t* const*>(static_cast<const uint8_t*>(slots[index]) + 0x48);
    if (!globals || (!globals[0] && !globals[1]) || globals[0x1DA])
        return -1;
    return globals[0x11] == 4 ? 1 : 0;
}

#include "reach_contact_melee_backend.inl"

bool ReachContactMeleeReady()
{
    return g_reachContact.enabled.load(std::memory_order_acquire) &&
           !g_reachContact.faulted.load(std::memory_order_acquire);
}

void ReachPublishContactHand(int hand, const FpExplicitPoseTargets& targets, const FpInterpolationContext& context,
    const BoneMatrix& root, const BoneMatrix* solved, const float points[][3], int count, int32_t unit,
    uint32_t generation)
{
    if (!ReachContactMeleeReady() || !g_config.physical_melee || hand < 0 || hand > 1 || unit == -1 || count <= 0 ||
        count > 64 || !points || !solved || generation != g_reachContact.generation)
        return;
    ContactMeleePacket packet{};
    packet.generation = generation;
    packet.publishedAtMs = GetTickCount64();
    auto& frame = packet.frame;
    frame.timeNs = targets.contactTimeNs;
    frame.serial = targets.contactSerial;
    frame.referenceEpoch = targets.contactReference;
    frame.unit = static_cast<uint32_t>(unit);
    frame.transform = targets.contactSpace[hand];
    frame.rigidMotion = true;
    frame.controllerPose = targets.contactController[hand];
    const uint64_t mask = hand ? context.wristDescendants : context.lWristDescendants;
    frame.shape = (mask ^ (uint64_t(context.count) << 48)) * 1099511628211ull;
    float worldPoints[contact_melee::kMaxPoints][3]{};
    memcpy(worldPoints, points, sizeof(float) * 3 * count);
    const unsigned handNodes = static_cast<unsigned>(count);
    bool holdsWeapon = false;
    if (hand == 1)
    {
        uint64_t weaponShape = 0;
        if (LegacyBuildMappedWeaponBounds(
                GameTitle::HaloReach, context, root, solved, worldPoints + count, weaponShape))
        {
            count += kLegacyCollisionWeaponSamples;
            frame.shape = (frame.shape ^ weaponShape) * 1099511628211ull;
            holdsWeapon = true;
        }
    }
    // Reach's support hand may carry a second weapon's palette.
    if (hand == 0 && g_reachFpPairScope.secondaryWeaponPresent)
        holdsWeapon = true;
    frame.bareHandPoints = fist_melee::GatedPoints(g_config.melee_fist, handNodes, holdsWeapon);
    frame.fist = VR_IsBareFist(hand);
    frame.shape = frame.shape ? frame.shape : 1;
    frame.count = static_cast<unsigned>(count);
    for (int i = 0; i < count; ++i)
    {
        const contact_melee::Point raw{worldPoints[i][0] - targets.collisionCorrection[hand][0],
            worldPoints[i][1] - targets.collisionCorrection[hand][1],
            worldPoints[i][2] - targets.collisionCorrection[hand][2]};
        frame.points[i] = frame.transform.Tracking(raw);
    }
    // Invalid tracking/reference frames are deliberately queued to reset the
    // simulation consumer rather than bridging a recenter or dropped pose.
    if (!g_reachContact.queue[hand].Push(packet))
        g_reachContact.overflow.fetch_add(1, std::memory_order_relaxed);
}

void ReachContactTick(uint32_t unit)
{
    if (!ReachContactMeleeReady() || !g_reachCamera.armed.load(std::memory_order_acquire) ||
        unit != static_cast<uint32_t>(LegacyCollisionIgnoredObject(GameTitle::HaloReach)))
        return;
    bool expected = false;
    if (!g_reachContact.processing.compare_exchange_strong(expected, true, std::memory_order_acquire))
        return;
    __try
    {
        const uint64_t now = GetTickCount64();
        // The live camera-core generation, as every other title checks.
        const bool admit = !exclusive_input::Active() && g_config.physical_melee &&
                           g_enabled.load(std::memory_order_acquire) &&
                           g_reachContact.generation == g_reachCamera.generation.load(std::memory_order_acquire) &&
                           VR_IsStereoEnabled() && !g_reachCinematicLocked.load(std::memory_order_acquire) &&
                           g_reachContact.playback && !g_reachContact.playback() && g_reachCamera.unitInVehicle &&
                           !g_reachCamera.unitInVehicle(static_cast<int32_t>(unit));
        contact_melee::DrainHands<ContactMeleePacket>(
            g_reachContact.queue, g_reachContact.hands, admit, g_reachContact.generation, unit, now,
            ClampPhysicalMeleeSwingSpeed(g_config.physical_melee_swing_speed),
            contact_melee::DrainPolicy{8, 100, true, contact_melee::RearmNs(g_config.physical_melee_cooldown_s)},
            [&](int)
            {
                ReachContactBackend backend{};
                backend.owner = unit;
                return backend;
            },
            [&](int hand, contact_melee::ContactResult result)
            {
                if (result == contact_melee::ContactResult::Applied)
                {
                    g_reachContact.submitted[hand].fetch_add(1, std::memory_order_relaxed);
                    VR_PulseContactHaptics(hand == 0, feedback::Strength(feedback::Cue::MeleeHit));
                }
                else if (result == contact_melee::ContactResult::NativeRejected)
                    g_reachContact.rejected.fetch_add(1, std::memory_order_relaxed);
            });
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("the Reach contact tick faulted");
        g_reachContactQuery.active = false;
        g_reachContactDamage.active = false;
        g_reachContact.faulted.store(true, std::memory_order_release);
    }
    g_reachContact.processing.store(false, std::memory_order_release);
}

__declspec(noinline) uint8_t __fastcall ReachContactUpdateDetour(uint32_t unit)
{
    g_reachCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
    g_reachContact.callbacks.fetch_add(1, std::memory_order_acq_rel);
    uint8_t result = 0;
    __try
    {
        if (g_reachContact.original)
            result = g_reachContact.original(unit);
        ReachContactTick(unit);
    }
    __finally
    {
        g_reachContact.callbacks.fetch_sub(1, std::memory_order_acq_rel);
        g_reachCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return result;
}

// Install follows muzzle_hook_set.h: create both hooks, then enable both;
// a failed install retires what it created. Retirement is this hook set's
// own, as in Halo 2 and Halo 4, for a failed install and for the camera
// teardown alike (RemoveReachCameraCore calls it before the camera table,
// which does not list these two hooks). Retirement disables both, waits for
// both detours to drain, then removes each (MH_OK or MH_ERROR_NOT_CREATED
// clears the target and its trampoline, anything else keeps them for a retry).
bool RemoveReachContactMelee();
MCCVR_MUZZLE_HOOK_OPS(ReachContactHookOps, RemoveReachContactMelee, true);
// The three clean-up lines print only when a retirement cannot finish.
constexpr HookSetLogText kReachContactHookText{"Reach contact melee unavailable: hook creation failed",
    "Reach contact melee unavailable: hook enable failed",
    "Reach contact melee cleanup: disable failed; retained hooks",
    "Reach contact melee cleanup: callbacks busy; retained hooks",
    "Reach contact melee cleanup: removal failed; retained bookkeeping"};
bool RemoveReachContactMelee()
{
    g_reachContact.enabled.store(false, std::memory_order_release);
    const void* functions[]{reinterpret_cast<const void*>(&ReachContactDamageDetour),
        reinterpret_cast<const void*>(&ReachContactUpdateDetour)};
    void** originals[]{
        reinterpret_cast<void**>(&g_reachContact.damageOriginal), reinterpret_cast<void**>(&g_reachContact.original)};
    if (!RetireMuzzleHooks<ReachContactHookOps>(
            g_reachContact.targets, originals, 2, functions, 2, g_reachContact.callbacks, kReachContactHookText))
        return false;
    // The native helpers the simulation detour called (the camera table cleared
    // them with its row; nothing calls them once both hooks are gone).
    g_reachContact.build = nullptr;
    g_reachContact.consume = nullptr;
    g_reachContact.playback = nullptr;
    return true;
}

bool InstallReachContactMelee(uintptr_t base, size_t size, uint32_t generation)
{
    struct Binding
    {
        uintptr_t rva;
        const char* pattern;
    };
    constexpr Binding bindings[]{
        {0x49F138,
            "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 54 41 56 41 57 48 83 EC 20 8B 15 ?? ?? ?? ?? 40 32 FF"},
        {0x4919D4,
            "48 8B C4 48 89 58 20 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 A8 FE FF FF 48 81 EC 20 02 00 00 0F 29 70 B8 0F 29 78 A8 44 0F 29 40 98 44 0F 29 48 88 44 0F 29 90 78 FF FF FF 44 0F 29 98 68 FF FF FF 44 0F 29 A0 58 FF FF FF 44 0F 29 A8 48 FF FF FF 44 0F 29 B0 38 FF FF FF 44 0F 29 B8 28 FF FF FF 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 78 65 48 8B 04 25 58 00 00 00 49 8B D8"},
        {0x4924F0,
            "48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 F1 48 81 EC C0 00 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 07"},
        // HREK 119360: initialized game globals and game-options playback flag.
        {0x58EEC,
            "48 83 EC 28 E8 ?? ?? ?? ?? 32 D2 84 C0 74 27 8B 0D ?? ?? ?? ?? 65 48 8B 04 25 58 00 00 00 41 B8 48 00 00 00 48 8B 04 C8 4A 8B 0C 00 38 91 DA 01 00 00 74 02 B2 01 8A C2 48 83 C4 28 C3"},
        {0x492D68,
            "48 8B C4 48 89 58 10 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 E8 FD FF FF 48 81 EC E0 02 00 00 0F 29 70 B8 0F 29 78 A8 44 0F 29 40 98 44 0F 29 48 88"},
        // HREK world branch: physical point/normal/material and glass tuple.
        {0x492234,
            "F6 45 FD 08 8B 45 EC 89 43 14 8B 45 B0 F2 0F 11 43 34 F2 0F 10 45 CC 89 43 3C 8B 45 D4 F2 0F 11 43 40 89 43 48"},
        {0x492268, "0F B6 45 02 89 43 18 0F B6 45 FE 89 43 1C 8B 45 DC 89 43 28 8B 45 F4 89 43 2C"},
        {0x4931D8,
            "66 44 3B 6B 20 74 26 8B 43 28 44 0F BF 4B 22 44 0F BF 43 20 8B 53 24 8B 4B 18 89 44 24 28 48 8D 44 24 70 48 89 44 24 20"}};
    g_reachContact.enabled.store(false, std::memory_order_release);
    if (!g_reachWorldCollision.original || g_reachContact.targets[0] || g_reachContact.targets[1])
        return false;
    for (const auto& binding : bindings)
    {
        if (!UniqueSignatureAt(base, size, binding.rva, binding.pattern, MCCVR_SIG_FIND))
        {
            LOG("Reach contact melee unavailable: native binding +0x%llX missing/ambiguous",
                static_cast<unsigned long long>(binding.rva));
            return false;
        }
    }
    if (!ReachVerifyRel32Call(base, 0x4A04D0, 0x49F138) || !ReachVerifyRel32Call(base, 0x50BA00, 0x4919D4) ||
        !ReachVerifyRel32Call(base, 0x50BCA7, 0x4924F0) || !ReachVerifyRel32Call(base, 0x491E89, 0x12969C) ||
        !ReachVerifyRel32Call(base, 0x58EF0, 0x588E0) || !ReachVerifyRel32Call(base, 0x492860, 0x491100) ||
        !ReachVerifyRel32Call(base, 0x491865, 0x492D68) || !ReachVerifyRel32Call(base, 0x493200, 0x15AF28))
    {
        LOG("Reach contact melee unavailable: native caller verification failed");
        return false;
    }
    g_reachContact.generation = generation;
    g_reachContact.faulted.store(false);
    g_reachContact.processing.store(false);
    for (int hand = 0; hand < 2; ++hand)
    {
        g_reachContact.queue[hand].Reset();
        g_reachContact.hands[hand].Reset();
    }
    g_reachContact.build = reinterpret_cast<ReachContactBuildFn>(base + 0x4919D4);
    g_reachContact.consume = reinterpret_cast<ReachContactConsumeFn>(base + 0x4924F0);
    g_reachContact.playback = reinterpret_cast<uint8_t(__fastcall*)()>(base + 0x58EEC);
    const MuzzleHook hooks[]{{0x492D68, reinterpret_cast<void*>(&ReachContactDamageDetour),
                                 reinterpret_cast<void**>(&g_reachContact.damageOriginal)},
        {0x49F138, reinterpret_cast<void*>(&ReachContactUpdateDetour),
            reinterpret_cast<void**>(&g_reachContact.original)}};
    if (!InstallMuzzleHooks<ReachContactHookOps>(base, hooks, 2, g_reachContact.targets, kReachContactHookText))
        return false;
    g_reachContact.enabled.store(true, std::memory_order_release);
    LOG("Reach contact melee installed: per-hand mesh/weapon sweeps, native exact-target melee damage, independent world-collision toggle; headset verification pending");
    return true;
}

void ReportReachContactMelee()
{
    if (!g_reachContact.targets[1])
        return;
    LOG("Reach contact melee: enabled=%d fault=%d queries=%llu contacts=%llu submissions(L/R)=%llu/%llu nativeRejected=%llu queueDrops=%llu predictedRequests=%llu",
        ReachContactMeleeReady() ? 1 : 0, g_reachContact.faulted.load() ? 1 : 0, g_reachContact.queries.exchange(0),
        g_reachContact.contacts.exchange(0), g_reachContact.submitted[0].exchange(0),
        g_reachContact.submitted[1].exchange(0), g_reachContact.rejected.exchange(0),
        g_reachContact.overflow.exchange(0), g_reachContact.predicted.exchange(0));
}
