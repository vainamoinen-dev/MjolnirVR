#pragma once
// Shared native contact-melee runtime for the two Halo 3-engine titles,
// Halo 3 and ODST. Included after the title-owned
// native getters; each title's *_contact_melee_runtime.inl declares its
// runtime/scope globals, a traits struct holding ONLY what differs (RVAs,
// signatures, call edges, TLS offsets, impulse offsets, admission sources,
// log text) and its own noinline detours, then forwards to these templates.
//
// Traits T supply:
//   static LegacyContactRuntime& Runtime(); static LegacyContactScope& Scope();
//   static const uint32_t* TlsIndex(); kObjectTableOffset, kGlobalsOffset;
//   static void* WorldCollisionOriginal();
//   kRedirectCaller, kDamageCaller (return-site RVAs), kImpulseA/kImpulseB;
//   kTitle, static uint32_t RuntimeGeneration(), static uint32_t PlayerUnit(),
//   static bool UnitInVehicle(int32_t), kCarriesHand;
//   install rows and exact log lines (see Install below).
#include "muzzle_hook_set.h"
using LegacyContactUpdateFn = uint8_t(__fastcall*)(uint32_t);
using LegacyContactMeleeFn = void(__fastcall*)(uint32_t, int32_t, int16_t, float);
using LegacyContactDamageFn = void(__fastcall*)(void*, uint32_t, int16_t, int16_t, uint16_t, void*);
struct LegacyContactRuntime
{
    std::atomic<bool> enabled{false}, faulted{false}, processing{false};
    std::atomic<uint32_t> callbacks{0};
    std::atomic<uint64_t> queries{0}, contacts{0}, submitted[2]{}, rejected{0}, overflow{0}, predicted{0};
    uintptr_t base = 0;
    uint32_t generation = 0;
    // The hook targets the shared muzzle_hook_set.h helper owns, in
    // install order ([0] damage, [1] simulation update), and the detours they jump to.
    void* targets[2]{};
    const void* detours[2]{};
    LegacyContactUpdateFn updateOriginal = nullptr;
    LegacyContactDamageFn damageOriginal = nullptr;
    LegacyContactMeleeFn melee = nullptr;
    GameIsPlaybackFn playback = nullptr;
    const uint64_t* flags = nullptr;
    ContactMeleeQueue queue[2];
    contact_melee::Hand hands[2];
    std::atomic<uint64_t> secondaryAtMs{0};
};

struct LegacyContactScope
{
    bool active = false, submitted = false, worldMatched = false;
    uint32_t owner = UINT32_MAX, target = UINT32_MAX;
    unsigned rays = 0;
    contact_melee::Sweep sweep{};
    contact_melee::Hit expected{};
    float direction[3]{};
    int hand = 1; // Controller role: support/secondary=0, main=1 (titles with kCarriesHand).
};

struct LegacyContactBinding
{
    uintptr_t rva;
    const char* pattern;
};
struct LegacyContactEdge
{
    uintptr_t call, target;
};
// Exact per-title lines. binding takes the RVA (%llX, unsigned long long);
// report takes the nine counters; the others take no argument.
struct LegacyContactLogText
{
    const char* binding;
    const char* edge;
    // One line per install/retire step (muzzle_hook_set.h HookSetLogText).
    const char* create;
    const char* enable;
    const char* disable;
    const char* busy;
    const char* removal;
    const char* installed;
    const char* report;
};

template <class T>
bool LegacyContactMeleeReady()
{
    return T::Runtime().enabled.load(std::memory_order_acquire) &&
           !T::Runtime().faulted.load(std::memory_order_acquire);
}

template <class T>
const uint8_t* LegacyContactTls()
{
    const uint32_t* index = T::TlsIndex();
    if (!index || *index >= 0x200)
        return nullptr;
    auto** slots = reinterpret_cast<void**>(__readgsqword(0x58));
    return slots ? static_cast<const uint8_t*>(slots[*index]) : nullptr;
}

// Title-owned object table (H3EK datum_get 364640, same layout in ODST):
// stride +24, live extent +3C, entries +48, signed salt at entry +0. Reject
// stale handles before any engine call.
template <class T>
bool LegacyContactObject(uint32_t handle, bool requireBiped = false)
{
    if (handle == UINT32_MAX || !(handle >> 16))
        return false;
    const auto* tls = LegacyContactTls<T>();
    if (!tls)
        return false;
    const auto* table = *reinterpret_cast<const uint8_t* const*>(tls + T::kObjectTableOffset);
    if (!table || *reinterpret_cast<const int32_t*>(table + 0x24) != 0x18)
        return false;
    const int32_t count = *reinterpret_cast<const int32_t*>(table + 0x3C);
    if (count <= 0 || count > 0x10000 || (handle & 0xFFFF) >= static_cast<uint32_t>(count))
        return false;
    const auto* entries = *reinterpret_cast<const uint8_t* const*>(table + 0x48);
    if (!entries)
        return false;
    const auto* entry = entries + (handle & 0xFFFF) * 0x18;
    return *reinterpret_cast<const uint16_t*>(entry) == static_cast<uint16_t>(handle >> 16) && entry[3] < 32 &&
           (!requireBiped || entry[3] == 0) && *reinterpret_cast<const void* const*>(entry + 0x10);
}

// H3EK collision 652A10 (type 1) / helper 654590 (type 3) and the ODST kit
// 6A3B30 / 6A56B0 share this result layout: surface words +18..27, material
// +28, type-3 section ID +3C, structure/object identity words +4C..58. The
// melee consumer's target==-1 branch dispatches the selected surface natively.
inline bool LegacyContactWorldHit(const uint8_t* bytes, contact_melee::Hit& hit)
{
    if (!bytes)
        return false;
    const uint32_t type = *reinterpret_cast<const uint32_t*>(bytes);
    if (type != 1 && type != 3)
        return false;
    hit = {};
    hit.unit = UINT32_MAX;
    hit.kind = contact_melee::HitKind::WorldSurface;
    hit.world.nativeType = type;
    memcpy(hit.world.nativeData, bytes + 0x18, 4 * sizeof(uint32_t));
    memcpy(hit.world.nativeData + 4, bytes + 0x3C, sizeof(uint32_t));
    memcpy(hit.world.nativeData + 5, bytes + 0x4C, 4 * sizeof(uint32_t));
    memcpy(&hit.world.material, bytes + 0x28, sizeof(hit.world.material));
    memcpy(&hit.fraction, bytes + 4, sizeof(hit.fraction));
    memcpy(&hit.position, bytes + 8, sizeof(hit.position));
    memcpy(&hit.normal, bytes + 0x2C, sizeof(hit.normal));
    hit.world.valid = hit.world.nativeData[5] != UINT32_MAX && hit.world.material != -1 &&
                      std::isfinite(hit.fraction) && hit.fraction >= 0 && hit.fraction <= 1 &&
                      contact_melee::Finite(hit.position) && contact_melee::Finite(hit.normal);
    return hit.world.valid;
}

// The native melee enumerates 25 rays. Use only its zero-priority centre
// sample, with the physical segment; never allow a head-directed ray.
template <class T>
bool LegacyRedirectContactVector(uintptr_t caller, uint64_t flags, int32_t mode, int32_t ignoredA, int32_t ignoredB,
    int32_t ignoredC, void* result, uint8_t& returned)
{
    auto& scope = T::Scope();
    if (!scope.active || caller != T::Runtime().base + T::kRedirectCaller ||
        static_cast<uint32_t>(ignoredA) != scope.owner)
        return false;
    returned = 0;
    if (++scope.rays != 13 || !result || !T::WorldCollisionOriginal())
        return true;
    const auto delta = contact_melee::Subtract(scope.sweep.end, scope.sweep.start);
    const float start[]{scope.sweep.start.x, scope.sweep.start.y, scope.sweep.start.z};
    const float vector[]{delta.x, delta.y, delta.z};
    auto original = reinterpret_cast<LegacyCollisionTestVectorFn>(T::WorldCollisionOriginal());
    returned = original(flags, mode, start, vector, ignoredA, ignoredB, ignoredC, result);
    if (returned)
    {
        const auto* bytes = static_cast<const uint8_t*>(result);
        // Reject a changed/occluded target before the native builder can
        // reparent it, run aim assist, or apply an unrelated contact.
        if (scope.expected.kind == contact_melee::HitKind::WorldSurface)
        {
            contact_melee::Hit actual{};
            if (!LegacyContactWorldHit(bytes, actual) || !contact_melee::SameWorldSurface(scope.expected, actual))
                returned = 0;
            else
                scope.worldMatched = true;
        }
        else if (*reinterpret_cast<const uint32_t*>(bytes) != 4 ||
                 *reinterpret_cast<const uint32_t*>(bytes + 0x40) != scope.target)
            returned = 0;
    }
    return true;
}

// Inner work of each title's noinline damage detour; `caller` is that
// detour's own return address. The detour itself keeps the callbacks counter
// and the __try/__finally, so it stays a real frame with unwind data: the
// teardown quiescence check resolves every detour's code range through
// RtlLookupFunctionEntry and refuses a detour without one. The constructor's
// stack event carries the two native impulse vectors at T::kImpulseA/B
// (H3 +3C/+48, ODST +40/+4C).
template <class T>
void LegacyContactDamageBody(
    void* event, uint32_t target, int16_t region, int16_t node, uint16_t material, void* result, uintptr_t caller)
{
    auto& runtime = T::Runtime();
    auto& scope = T::Scope();
    auto* bytes = static_cast<uint8_t*>(event);
    const bool own = scope.active && caller == runtime.base + T::kDamageCaller;
    if (!own || (bytes && *reinterpret_cast<const uint32_t*>(bytes + 0x18) == scope.owner && target == scope.target &&
                    LegacyContactObject<T>(target)))
    {
        if (own)
        {
            memcpy(bytes + T::kImpulseA, scope.direction, sizeof(scope.direction));
            memcpy(bytes + T::kImpulseB, scope.direction, sizeof(scope.direction));
        }
        if (runtime.damageOriginal)
        {
            runtime.damageOriginal(event, target, region, node, material, result);
            if (own)
                scope.submitted = true;
        }
    }
}

template <class T>
struct LegacyContactBackend
{
    uint32_t owner = UINT32_MAX;
    int hand = 1;
    bool Query(const contact_melee::Sweep& sweep, contact_melee::Hit& hit) noexcept
    {
        // Native collision initializes/uses fields through +63 (H3EK 652A10 /
        // retail 1FD748; ODST kit 6A3B30 / retail 22F80C). Oversized aligned
        // scratch is local to this query; no guessed structure reaches the engine.
        auto& runtime = T::Runtime();
        alignas(16) uint8_t result[0x80]{};
        const auto delta = contact_melee::Subtract(sweep.end, sweep.start);
        const float start[]{sweep.start.x, sweep.start.y, sweep.start.z};
        const float vector[]{delta.x, delta.y, delta.z};
        auto original = reinterpret_cast<LegacyCollisionTestVectorFn>(T::WorldCollisionOriginal());
        runtime.queries.fetch_add(1, std::memory_order_relaxed);
        if (!original || !runtime.flags ||
            !original(*runtime.flags, 1, start, vector, static_cast<int32_t>(owner), -1, -1, result) ||
            (*reinterpret_cast<const uint32_t*>(result) != 4 && *reinterpret_cast<const uint32_t*>(result) != 1))
            return false;
        if (*reinterpret_cast<const uint32_t*>(result) == 4)
        {
            memcpy(&hit.unit, result + 0x40, sizeof(hit.unit));
            if (hit.unit == owner || !LegacyContactObject<T>(hit.unit))
                return false;
            memcpy(&hit.fraction, result + 4, sizeof(hit.fraction));
            memcpy(&hit.position, result + 8, sizeof(hit.position));
            memcpy(&hit.normal, result + 0x2C, sizeof(hit.normal));
            hit.object = true;
            hit.kind = contact_melee::HitKind::Object;
        }
        else if (!LegacyContactWorldHit(result, hit))
            return false;
        runtime.contacts.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    bool Apply(uint32_t unit, const contact_melee::Hit& hit, const contact_melee::Sweep& sweep) noexcept
    {
        auto& runtime = T::Runtime();
        auto& scope = T::Scope();
        const bool objectHit = hit.kind == contact_melee::HitKind::Object || hit.object;
        const bool worldHit = hit.kind == contact_melee::HitKind::WorldSurface && hit.unit == UINT32_MAX &&
                              !hit.object && hit.world.valid;
        if (unit != owner || !LegacyContactObject<T>(owner, true) || (!objectHit && !worldHit) ||
            (objectHit && !LegacyContactObject<T>(hit.unit)))
            return false;
        const auto* tls = LegacyContactTls<T>();
        const auto* globals = tls ? *reinterpret_cast<const uint8_t* const*>(tls + T::kGlobalsOffset) : nullptr;
        if (!globals || (!globals[0] && !globals[1]))
            return false;
        const int16_t mode = globals[0x11] == 4 ? 1 : 0;
        const auto delta = contact_melee::Subtract(sweep.end, sweep.start);
        const float length = std::sqrt(contact_melee::Dot(delta, delta));
        if (!std::isfinite(length) || length <= 1e-6f)
            return false;
        scope = {};
        scope.active = true;
        scope.owner = owner;
        scope.target = hit.unit;
        scope.sweep = sweep;
        scope.expected = hit;
        scope.direction[0] = delta.x / length;
        scope.direction[1] = delta.y / length;
        scope.direction[2] = delta.z / length;
        if constexpr (T::kCarriesHand)
            scope.hand = hand;
        // 0x79 selects the first authored melee damage response (H3EK A5DE20 /
        // retail 35A9A4; ODST kit AD9810 / retail 39F5AC). It requests no
        // animation or button action.
        __try
        {
            runtime.melee(owner, 0x79, mode, 1.0f);
        }
        __finally
        {
            scope.active = false;
        }
        if (mode == 1)
            runtime.predicted.fetch_add(1, std::memory_order_relaxed);
        return scope.rays == 25 && (mode == 1 || (objectHit ? scope.submitted : scope.worldMatched));
    }
};

template <class T>
void LegacyContactTick(uint32_t unit)
{
    auto& runtime = T::Runtime();
    if (!LegacyContactMeleeReady<T>() || TitleAdapter_GetActiveTitle() != T::kTitle || !T::HasPlayerUnit() ||
        unit != T::PlayerUnit())
        return;
    bool expected = false;
    if (!runtime.processing.compare_exchange_strong(expected, true, std::memory_order_acquire))
        return;
    __try
    {
        const uint64_t now = GetTickCount64();
        int32_t scene = -1, shot = -1;
        const bool admit = !exclusive_input::Active() && g_config.physical_melee &&
                           g_enabled.load(std::memory_order_acquire) && VR_IsStereoEnabled() &&
                           runtime.generation == T::RuntimeGeneration() && runtime.playback && !runtime.playback() &&
                           ReadCinematicControl(scene, shot) == CinematicControlState::PlayerControlled &&
                           T::HasUnitInVehicle() && !T::UnitInVehicle(static_cast<int32_t>(unit));
        contact_melee::DrainHands<ContactMeleePacket>(
            runtime.queue, runtime.hands, admit, runtime.generation, unit, now,
            ClampPhysicalMeleeSwingSpeed(g_config.physical_melee_swing_speed),
            contact_melee::DrainPolicy{8, 100, true, contact_melee::RearmNs(g_config.physical_melee_cooldown_s)},
            [&](int hand)
            {
                LegacyContactBackend<T> backend{};
                backend.owner = unit;
                if constexpr (T::kCarriesHand)
                    backend.hand = hand;
                return backend;
            },
            [&](int hand, contact_melee::ContactResult result)
            {
                if (result == contact_melee::ContactResult::Applied)
                {
                    runtime.submitted[hand].fetch_add(1, std::memory_order_relaxed);
                    VR_PulseContactHaptics(hand == 0, feedback::Strength(feedback::Cue::MeleeHit));
                }
                else if (result == contact_melee::ContactResult::NativeRejected)
                    runtime.rejected.fetch_add(1, std::memory_order_relaxed);
            });
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("the physical melee contact tick faulted");
        T::Scope().active = false;
        runtime.faulted.store(true, std::memory_order_release);
    }
    runtime.processing.store(false, std::memory_order_release);
}

// Install and retire follow muzzle_hook_set.h, shared with the
// barrel sets: create both hooks, then enable both; a failed install retires what
// it created. Retirement disables both (an unmapped module counts as disabled),
// waits for the detours and trampolines to drain, then removes each, stopping at
// the first removal that fails and keeping it and the next for the retry.
template <class T>
bool LegacyContactRetire();
template <class T>
struct LegacyContactHookOps
{
    static MH_STATUS Create(void* t, void* d, void** o) { return MCCVR_CreateHookChecked(t, d, o); }
    static MH_STATUS Enable(void* t) { return MH_EnableHook(t); }
    static MH_STATUS Disable(void* t) { return MCCVR_DisableHookForRetirement(t); }
    static MH_STATUS Remove(void* t) { return MCCVR_RemoveHookChecked(t); }
    static bool Quiesce(const void* const* f, const void* const* o, size_t n, const std::atomic<uint32_t>& c)
    {
        return WaitForNativeDetourQuiescence(f, o, n, c);
    }
    static bool Retire() { return LegacyContactRetire<T>(); }
    template <class... Args>
    static void Log(const char* format, Args... args)
    {
        LOG(format, args...);
    }
};
template <class T>
constexpr HookSetLogText LegacyContactHookText()
{
    return {T::kLog.create, T::kLog.enable, T::kLog.disable, T::kLog.busy, T::kLog.removal};
}
// The hooks only; the title's DisableAndRemove* also retires its melee selection first.
template <class T>
bool LegacyContactRetire()
{
    auto& runtime = T::Runtime();
    runtime.enabled.store(false, std::memory_order_release);
    void** originals[]{
        reinterpret_cast<void**>(&runtime.damageOriginal), reinterpret_cast<void**>(&runtime.updateOriginal)};
    return RetireMuzzleHooks<LegacyContactHookOps<T>>(
        runtime.targets, originals, 2, runtime.detours, 2, runtime.callbacks, LegacyContactHookText<T>());
}
template <class T>
bool LegacyContactInstall(uintptr_t base, size_t size, uint32_t generation, void* damageDetour, void* updateDetour)
{
    auto& runtime = T::Runtime();
    runtime.enabled.store(false, std::memory_order_release);
    if (!T::WorldCollisionOriginal() || runtime.targets[0] || runtime.targets[1])
        return false;
    for (const auto& binding : T::kBindings)
    {
        if (!UniqueSignatureAt(base, size, binding.rva, binding.pattern, MCCVR_SIG_FIND))
        {
            LOG(T::kLog.binding, static_cast<unsigned long long>(binding.rva));
            return false;
        }
    }
    bool edges = T::ExtraProof(base);
    for (const auto& edge : T::kEdges)
        edges = edges && NativeCallEdge(base, edge.call, edge.target);
    if (!edges)
    {
        LOG(T::kLog.edge);
        return false;
    }
    const uintptr_t flags = base + T::kFlagsNext + *reinterpret_cast<const int32_t*>(base + T::kFlagsDisplacement);
    if (flags != base + T::kFlags || flags + sizeof(uint64_t) > base + size)
        return false;
    runtime.base = base;
    runtime.generation = generation;
    runtime.flags = reinterpret_cast<const uint64_t*>(flags);
    runtime.melee = reinterpret_cast<LegacyContactMeleeFn>(base + T::kMelee);
    runtime.playback = reinterpret_cast<GameIsPlaybackFn>(base + T::kPlayback);
    runtime.faulted.store(false);
    runtime.processing.store(false);
    runtime.secondaryAtMs.store(0);
    for (int hand = 0; hand < 2; ++hand)
    {
        runtime.queue[hand].Reset();
        runtime.hands[hand].Reset();
    }
    runtime.detours[0] = damageDetour;
    runtime.detours[1] = updateDetour;
    const MuzzleHook hooks[]{
        {static_cast<uint32_t>(T::kDamage), damageDetour, reinterpret_cast<void**>(&runtime.damageOriginal)},
        {static_cast<uint32_t>(T::kUpdate), updateDetour, reinterpret_cast<void**>(&runtime.updateOriginal)}};
    if (!InstallMuzzleHooks<LegacyContactHookOps<T>>(base, hooks, 2, runtime.targets, LegacyContactHookText<T>()))
        return false;
    runtime.enabled.store(true, std::memory_order_release);
    T::AfterInstall(base, size);
    LOG(T::kLog.installed);
    return true;
}

template <class T>
void LegacyContactPublishHand(int hand, const FpInterpolationContext& context, const BoneMatrix& root,
    const BoneMatrix* solved, const float points[][3], int count, int32_t unit, uint32_t generation)
{
    auto& runtime = T::Runtime();
    if (!LegacyContactMeleeReady<T>() || !g_config.physical_melee || hand < 0 || hand > 1 || unit == -1 || !points ||
        !solved || count <= 0 || count > 64 || generation != runtime.generation || !g_baseCamValid.load())
        return;
    const uint64_t now = GetTickCount64();
    if (context.slot == 1)
        runtime.secondaryAtMs.store(now, std::memory_order_release);
    const uint64_t secondary = runtime.secondaryAtMs.load(std::memory_order_acquire);
    // A present secondary palette (slot 1) owns support-hand publication, so
    // alternating primary/secondary skeletons never reseed every swing. This
    // also works when World collision is disabled (its telemetry is unrelated).
    if (hand == 0 && context.slot == 0 && secondary && now >= secondary && now - secondary <= 100)
        return;
    VrContactTrackingSnapshot tracking{};
    if (!VR_GetContactTrackingSnapshot(tracking))
        return;
    ContactMeleePacket packet{};
    packet.publishedAtMs = now;
    packet.generation = generation;
    auto& frame = packet.frame;
    frame.serial = tracking.serial;
    frame.timeNs = tracking.timeNs;
    frame.unit = static_cast<uint32_t>(unit);
    frame.rigidMotion = true;
    const auto& controller = tracking.hands[hand];
    const bool poseValid =
        controller.valid && frame.controllerPose.SetPose(controller.orientation, controller.position);
    // Exact on-foot position mapping used by ControllerWorldPoseEx. Express
    // both samples in this current actor frame, excluding walking/turning.
    const float sh = sinf(g_headYawRef), ch = cosf(g_headYawRef);
    const float cg = cosf(g_gameYawRef), sg = sinf(g_gameYawRef);
    frame.transform.unitsPerMetre = g_worldScale.load();
    frame.transform.axis[0] = {cg * sh + sg * ch, sg * sh - cg * ch, 0};
    frame.transform.axis[1] = {0, 0, 1};
    frame.transform.axis[2] = {-cg * ch + sg * sh, -sg * ch - cg * sh, 0};
    const auto reference = frame.transform.World({g_headPosRef[0], g_headPosRef[1], g_headPosRef[2]});
    frame.transform.origin = {
        g_baseCamX.load() - reference.x, g_baseCamY.load() - reference.y, g_baseCamZ.load() - reference.z};
    if (poseValid && tracking.referenceEpoch)
    {
        uint64_t epoch = (14695981039346656037ull ^ tracking.referenceEpoch) * 1099511628211ull;
        epoch = (epoch ^ generation) * 1099511628211ull;
        epoch = (epoch ^ uint64_t(tracking.twoHandAimActive)) * 1099511628211ull;
        const float settings[]{g_headPosRef[0], g_headPosRef[1], g_headPosRef[2], g_headYawRef, g_config.gun_scale,
            g_config.left_hand_scale, g_config.gun_forward_m, g_config.gun_right_m, g_config.gun_up_m,
            g_config.left_hand_forward_m, g_config.gun_pitch_deg, g_config.gun_yaw_deg, g_config.gun_roll_deg,
            g_config.barrel_pitch_deg, g_config.barrel_yaw_deg, g_config.barrel_roll_deg};
        for (float value : settings)
        {
            uint32_t bits = 0;
            memcpy(&bits, &value, sizeof(bits));
            epoch = (epoch ^ bits) * 1099511628211ull;
        }
        frame.referenceEpoch = epoch ? epoch : 1;
    }
    const uint64_t mask = hand ? context.wristDescendants : context.lWristDescendants;
    frame.shape = (mask ^ (uint64_t(context.count) << 48) ^ uint64_t(context.slot)) * 1099511628211ull;
    float world[contact_melee::kMaxPoints][3]{};
    memcpy(world, points, sizeof(float) * 3 * count);
    const unsigned handNodes = static_cast<unsigned>(count);
    bool holdsWeapon = false;
    if ((hand == 1 && context.slot == 0) || (hand == 0 && context.slot == 1))
    {
        uint64_t weaponShape = 0;
        if (LegacyBuildMappedWeaponBounds(T::kTitle, context, root, solved, world + count, weaponShape))
        {
            count += kLegacyCollisionWeaponSamples;
            frame.shape = (frame.shape ^ weaponShape) * 1099511628211ull;
            holdsWeapon = true;
        }
    }
    frame.bareHandPoints = fist_melee::GatedPoints(g_config.melee_fist, handNodes, holdsWeapon);
    frame.fist = VR_IsBareFist(hand);
    frame.shape = frame.shape ? frame.shape : 1;
    frame.count = static_cast<unsigned>(count);
    for (int i = 0; i < count; ++i)
        frame.points[i] = frame.transform.Tracking({world[i][0] - g_fpPaletteCollisionCorrection[hand][0],
            world[i][1] - g_fpPaletteCollisionCorrection[hand][1],
            world[i][2] - g_fpPaletteCollisionCorrection[hand][2]});
    if (!runtime.queue[hand].Push(packet))
        runtime.overflow.fetch_add(1, std::memory_order_relaxed);
}

template <class T>
void LegacyContactReport()
{
    auto& runtime = T::Runtime();
    LOG(T::kLog.report, LegacyContactMeleeReady<T>() ? 1 : 0, runtime.faulted.load() ? 1 : 0,
        runtime.queries.exchange(0), runtime.contacts.exchange(0), runtime.submitted[0].exchange(0),
        runtime.submitted[1].exchange(0), runtime.rejected.exchange(0), runtime.overflow.exchange(0),
        runtime.predicted.exchange(0));
}
