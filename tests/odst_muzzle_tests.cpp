// Tests the ODST muzzle feature: the shot, fire, view, camera and aim detours, the muzzle publication,
// the reserve weapon and melee response readers, and the hook install/remove lifecycle.
// Compiles the odst_muzzle_*.inl and odst_melee_response.inl files from src/dll/titles/odst against fake
// units, tags and native functions; built as halomccvr_odst_muzzle_tests. MinHook calls go to fixtures,
// so no real hook is installed and no game code runs.
#include <Windows.h>
#include <MinHook.h>
#include "hook_checked_forwarders.h" // DLL hook sites call MinHook through the checked names
#include "../src/dll/core/muzzle_hook_set.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <algorithm>
#include "../src/common/dual_weapon_aim_logic.h"
#include "../src/common/weapon_muzzle.h"
#include "../src/dll/core/guard_count.h"
namespace lens_anchor
{ // the real header pulls in weapon_profiles.h (its own tests cover it)
    // The stub fills barrel 0 from the gun's node 0, as the real Fill does for a kit lens row,
    // so the shared publication fixtures can see the lens switch.
    template <class X>
    bool Fill(weapon_muzzle::Palette& palette, GameTitle fixtureTitle, uint64_t identity, uint32_t count,
        const weapon_muzzle::Receipt& base, X xf)
    {
        if (!count || count > 64)
            return false;
        weapon_muzzle::Marker marker{};
        marker.title = fixtureTitle;
        marker.identity = identity;
        marker.nodeCount = uint16_t(count);
        marker.forward[0] = 1;
        marker.up[2] = 1;
        weapon_muzzle::Ray ray{};
        if (!xf(marker, ray))
            return false;
        weapon_muzzle::Receipt receipt = base;
        receipt.barrel = 0;
        receipt.ray = ray;
        palette.barrels[0] = receipt;
        return true;
    }
}
#include "legacy_muzzle_publication_fixtures.h"
#include "../src/common/reserve_weapon_logic.h"
#include "../src/common/weapon_inventory_logic.h"
#include "../src/common/native_shot_target_lease.h"
#include "../src/common/exclusive_input.h"
static unsigned checks{};
static void Check(bool value, const char* name)
{
    ++checks;
    if (!value)
    {
        std::fprintf(stderr, "FAIL: %s\n", name);
        std::exit(1);
    }
}
static constexpr uint32_t fixtureOwner = 0x12340001, fixturePrimary = 0x56780002, secondary = 0x789A0003,
                          fixtureTarget = 0x22220005;
static constexpr uintptr_t imageBase = 0x180000000;
static constexpr size_t kOdstTlsObjectTableOffset = 0x20;
static std::atomic<uint32_t> g_odstRuntimeGeneration{7};
static std::atomic<bool> g_vrAim{true}, g_enabled{true}, g_odstMuzzleBindingsReady{true};
static struct
{
    bool gun_barrel_aim = true, left_handed = false, independent_dual_aim = false, scope_lens_node_anchor = true;
    float crosshair_distance_m = 10, gun_yaw_deg = 0, gun_pitch_deg = 0, gun_roll_deg = 0;
} g_config;
// The lens stub records what it receives, as the H3 test's lens ray does (barrel 0).
static void LegacyPublishLensAnchor(GameTitle, uint8_t, const weapon_muzzle::Palette& lens) noexcept
{
    legacy_publication_fixture::RecordReceipt('L', lens.barrels[0]);
}
// The palette the publication hands the reticle (last call).
static weapon_muzzle::Palette lastReticlePalette{};
static GameTitle lastReticleTitle{};
static unsigned reticlePublishes = 0;
static void LegacyPublishReticleRay(GameTitle fixtureTitle, uint8_t, const weapon_muzzle::Palette& palette) noexcept
{
    lastReticleTitle = fixtureTitle;
    lastReticlePalette = palette;
    ++reticlePublishes;
    legacy_publication_fixture::RecordReceipt('R', palette.barrels[0]);
}
static float Game_GetWorldScale()
{
    return 1.f;
}
static std::atomic<bool> g_baseCamValid{true};
static std::atomic<float> g_worldScale{1}, g_baseCamX{0}, g_baseCamY{0}, g_baseCamZ{0};
static float g_headYawRef = 0, g_gameYawRef = 0, g_headPosRef[3]{};
[[maybe_unused]] static void BuildTrackedGameBasisFromFrame(
    const float* orientation, bool, bool, int, int, float* basis)
{
    std::memcpy(basis, orientation, 12);
    basis[4] = basis[8] = 1;
}
[[maybe_unused]] static void BasisFromAngles(float, float, float, float* basis)
{
    basis[0] = basis[4] = basis[8] = 1;
}
[[maybe_unused]] static void MultiplyBases(const float* basis, const float*, float* result)
{
    std::memcpy(result, basis, 36);
}
[[maybe_unused]] static void VR_ObserveSecondaryWeaponPresentation(GameTitle, uint32_t) {}
static RecordingMuzzleStore g_odstMuzzles; // records each publish
static thread_local bool g_legacyCollisionOwnedQuery = false;
static alignas(8) uint8_t tlsBytes[0x600]{}, fixtureTable[0x60]{}, fixtureEntries[6 * 0x18]{}, fixtureUsers[0x4F38]{},
    unitBytes[0x400]{}, replacementBytes[0x400]{}, primaryBytes[0x200]{}, secondaryBytes[0x200]{};
static uint8_t* fixtureSlots[]{tlsBytes};
static uint32_t tlsIndex = 0;
static uint32_t* g_odstEngineTlsIndex = &tlsIndex;
static uint32_t localUnit = fixtureOwner;
static bool seated = false, cinematic = false, stereo = true, validTracking = true;
static GameTitle fixtureTitle = GameTitle::Halo3ODST;
static int32_t Player(int32_t)
{
    return int32_t(localUnit);
}
static bool Seated(int32_t)
{
    return seated;
}
static auto g_odstPlayerUnitGetter = &Player;
static auto g_odstUnitInVehicle = &Seated;
static const uint8_t* OdstContactTls()
{
    return tlsBytes;
}
static bool OdstContactObject(uint32_t handle)
{
    if (handle == fixtureTarget)
        return true;
    if (handle == UINT32_MAX || (handle & 0xFFFF) >= 4 || !(handle >> 16))
        return false;
    const auto* entry = fixtureEntries + (handle & 0xFFFF) * 0x18;
    return *reinterpret_cast<const uint16_t*>(entry) == uint16_t(handle >> 16) &&
           *reinterpret_cast<const void* const*>(entry + 0x10);
}
static GameTitle TitleAdapter_GetActiveTitle()
{
    return fixtureTitle;
}
static bool VR_IsStereoEnabled()
{
    return stereo;
}
static CinematicControlState ReadOdstCinematicControl(int32_t&, int32_t&)
{
    return cinematic ? CinematicControlState::AuthoredLocked : CinematicControlState::PlayerControlled;
}
struct VrContactTrackingSnapshot
{
    uint64_t serial = 11, referenceEpoch = 3;
    int64_t timeNs = 1000000000;
    struct
    {
        bool valid = true;
        float position[3]{}, orientation[4]{1, 0, 0, 0};
    } hands[2];
};
static VrContactTrackingSnapshot fixtureTracking;
static bool VR_GetContactTrackingSnapshot(VrContactTrackingSnapshot& out)
{
    out = fixtureTracking;
    return validTracking;
}
static int ResolveEquippedWeaponSlot(uint32_t weapon, uint32_t a, uint32_t b, bool, bool)
{
    return weapon == a ? 0 : weapon == b ? 1 : -1;
}
struct BoneMatrix
{
    float scale = 1, rotation[9]{1, 0, 0, 0, 1, 0, 0, 0, 1}, translation[3]{};
};
struct FpInterpolationContext
{
    bool valid = true;
    int player = 0, slot = 0;
    uint32_t generation = 7, muzzleUnit = fixtureOwner, muzzleWeapon = fixturePrimary;
};
static struct
{
    bool armed = true;
    VrContactTrackingSnapshot anatomicalTracking;
} g_fpStereoSolveScope;
static uint32_t liveChecksum = 0;
static int liveCount = 0;
static bool lookupFault = false;
static int LegacyAnatomicalRenderNodeCount(GameTitle value, uint16_t tag, uint32_t* checksum)
{
    Check(value == GameTitle::Halo3ODST && tag == 123, "ODST model lookup");
    if (lookupFault)
        RaiseException(0xE04D3202, 0, 0, nullptr);
    *checksum = liveChecksum;
    return liveCount;
}
#include "../src/dll/titles/odst/odst_muzzle_ownership.inl"
// The ODST melee response reader over the same fake unit/inventory.
#include "../src/common/titles/h3/halo3_melee_selection_logic.h"
static alignas(8) unsigned char fixtureUnitTag[0x400]{}, weaponTag[0x400]{};
static unsigned char* OdstLoadedTagDefinition(uint32_t datum)
{
    return datum == 0x1111 ? fixtureUnitTag : datum == 0x2222 ? weaponTag : nullptr;
}
#include "../src/dll/titles/odst/odst_melee_response.inl"
#define __readgsqword(offset) reinterpret_cast<uint64_t>(::fixtureSlots)
#include "../src/dll/titles/odst/odst_muzzle_publication.inl"
#undef __readgsqword
// The shared publication fixtures over this test's host globals.
struct OdstPublicationFixture
{
    using Context = FpInterpolationContext;
    using Node = BoneMatrix;
    static constexpr GameTitle kTitle = GameTitle::Halo3ODST;
    static bool HasLens(const weapon_muzzle::Marker&) { return true; } // the stub Fill above
    static void Publish(uint16_t tag, const Context& c, const Node* d, uint32_t g)
    {
        OdstPublishMuzzlePalette(tag, c, d, g);
    }
    static auto& Config() { return g_config; }
    static std::atomic<bool>& BindingsReady() { return g_odstMuzzleBindingsReady; }
    static std::atomic<uint32_t>& RuntimeGeneration() { return g_odstRuntimeGeneration; }
    static auto& Scope() { return g_fpStereoSolveScope; }
    static uint32_t Owner() { return fixtureOwner; }
    static uint32_t Weapon(int slot) { return slot ? secondary : fixturePrimary; }
    static void SetSeated(bool value) { seated = value; }
    static void SetLookup(uint32_t checksum, int count)
    {
        liveChecksum = checksum;
        liveCount = count;
    }
    static uint32_t LookupChecksum() { return liveChecksum; }
    static int LookupCount() { return liveCount; }
    static void SetLookupFault(bool value) { lookupFault = value; }
};
static thread_local uintptr_t caller{};
#define _ReturnAddress() reinterpret_cast<void*>(::caller)
static void Game_FormatShotFrame(GameTitle, char* out, size_t size)
{
    if (out && size)
        out[0] = 0;
} // stand-in: empty frame text
// Golden: the last per-shot line, so its text is pinned.
static char shotLog[512]{};
static unsigned shotLogs = 0;
#define LOG(...) ((void)std::snprintf(shotLog, sizeof shotLog, __VA_ARGS__), (void)++shotLogs)
#include "../src/dll/titles/odst/odst_muzzle_shots.inl"
#undef LOG
#undef _ReturnAddress

static unsigned fireCalls{}, queryCalls{}, markerCalls{}, aimCalls{}, cameraCalls{}, viewCalls{};
static uint64_t markerCount = 1;
static bool expectMuzzle = true, queryFault = false, preflightFault = false, markerFault = false, fireFault = false,
            nestedFire = false, omitView = false, replaceStorage = false, changeOwner = false, changeGeneration = false,
            nativeChangesTarget = false, invalidTarget = false, nonfiniteTarget = false, changeOtherSlot = false,
            nonfiniteDirection = false; // for the acquire goldens (case 6 below)
static void __fastcall NativeView(uint32_t unit, uint64_t, float*, float* point, float* camera)
{
    ++viewCalls;
    Check(unit == fixtureOwner, "native query owner");
    point[0] = point[1] = point[2] = 10;
    camera[0] = camera[1] = camera[2] = 20;
}
static int32_t __fastcall NativeCamera(uint32_t, float* point, float* direction)
{
    ++cameraCalls;
    point[0] = point[1] = point[2] = 10;
    direction[0] = 1;
    direction[1] = direction[2] = 0;
    return 17;
}
static void __fastcall NativeQuery(
    int32_t user, uint8_t flags, float* direction, int16_t zoom, float* control, void* out)
{
    ++queryCalls;
    Check(user == 2 && zoom == 3, "native input user/zoom retained independently of output user");
    const bool active = g_odstMuzzleShot.active;
    if (active)
        Check(g_legacyCollisionOwnedQuery, "auxiliary query cannot schedule extra collision/melee");
    if (queryFault)
        RaiseException(0xE0421111, 0, 0, nullptr);
    float point[3]{}, camera[3]{};
    const auto previous = caller;
    caller = imageBase + 0x160647;
    if (!omitView)
        OdstMuzzleViewDetour(fixtureOwner, 0x12345678, direction, point, camera);
    if (active && !omitView)
        Check(point[0] == 2 && point[1] == 2.5f && camera[2] == 4, "query starts at clipped visible muzzle");
    if (flags & 2)
    {
        caller = imageBase + 0x160A5D;
        OdstMuzzleViewDetour(fixtureOwner, 0, direction, point, camera);
        if (active)
            Check(point[1] == 2.5f && camera[0] == 2, "native lead query shares muzzle origin");
    }
    caller = previous;
    std::memset(out, 0, 0x28);
    auto* words = static_cast<uint32_t*>(out);
    words[0] = 1;
    words[2] = invalidTarget ? 0xABCD0005 : fixtureTarget;
    words[1] = words[3] = UINT32_MAX;
    auto* values = static_cast<float*>(out);
    values[4] = .25f;
    values[5] = .75f;
    if (nonfiniteTarget)
        values[8] = NAN;
    control[0] = .1f;
    control[1] = .2f;
    control[2] = .3f;
    if (replaceStorage)
        *reinterpret_cast<uint8_t**>(fixtureEntries + 0x18 + 0x10) = replacementBytes;
    if (changeOwner)
        *reinterpret_cast<uint32_t*>(primaryBytes + 0x160) = fixtureOwner ^ 0x10000;
    if (changeGeneration)
        g_odstRuntimeGeneration = 8;
    if (active && changeOtherSlot)
        unitBytes[0x277] = 0xFF;
    if (active && nonfiniteDirection)
        direction[0] = NAN;
}
static void __fastcall NativeAim(
    uint32_t unit, float* point, float* direction, uint64_t velocity, float* offset, uint8_t project, uint8_t use)
{
    ++aimCalls;
    if (velocity != 0xFEDCBA9876543210ull)
    {
        Check(g_legacyCollisionOwnedQuery && unit == fixtureOwner && velocity && !offset && !project && !use,
            "ODST private obstruction preflight ABI");
        if (preflightFault)
            RaiseException(0xE0422222, 0, 0, nullptr);
        Check(point[0] == 2 && point[1] == 3 && point[2] == 4 && direction[1] == 1,
            "native clamp receives authored muzzle");
        reinterpret_cast<float*>(velocity)[0] = 11;
        point[1] = 2.5f;
        return;
    }
    if (expectMuzzle)
        Check(!offset && !project && !use && point[1] == 2.5f && direction[1] == 1,
            "later origin helper preserves clamped barrel without native projection");
    else
        Check(offset && project && use, "stock helper arguments retained");
}
static uint64_t __fastcall NativeMarkers(
    uint32_t object, uint32_t name, void* data, int16_t capacity, uint8_t original, uint8_t interpolate)
{
    ++markerCalls;
    Check((object == fixturePrimary || object == secondary) && name == 0xD5 && capacity == 64 && original == 0xAA &&
              interpolate == 0xBB,
        "ODST six-argument native marker ABI preserved");
    if (markerFault)
        RaiseException(0xE0423333, 0, 0, nullptr);
    std::memset(data, 0x5A, 0x70);
    return markerCount;
}
static uint64_t __fastcall NativeFire(uint32_t weapon, int16_t barrel, void* data, int32_t index, uint8_t predicted)
{
    ++fireCalls;
    Check(barrel == 0 && data == reinterpret_cast<void*>(0x1234) && index == -7 && predicted == 0xFA,
        "ODST five-argument outer fire ABI");
    const auto previous = caller;
    alignas(float) uint8_t marker[0x70]{};
    caller = imageBase + 0x3AF4CA;
    Check(OdstMuzzleMarkersDetour(weapon, 0xD5, marker, 64, 0xAA, 0xBB) == markerCount,
        "full native marker result preserved");
    if (expectMuzzle)
    {
        Check(reinterpret_cast<float*>(marker + 0x60)[1] == 2.5f && reinterpret_cast<float*>(marker + 0x3C)[1] == 1 &&
                  reinterpret_cast<float*>(marker + 0x54)[2] == 1,
            "committed muzzle with native clipping and authored roll");
        for (size_t i = 0; i < 0x3C; ++i)
            Check(marker[i] == 0x5A, "local marker and world scale untouched");
        for (size_t i = 0x6C; i < 0x70; ++i)
            Check(marker[i] == 0x5A, "native marker flags untouched");
        Check(*reinterpret_cast<uint32_t*>(unitBytes + 0x230) == fixtureTarget,
            "ODST direct homing sees independent native target");
    }
    else
        for (auto value : marker)
            Check(value == 0x5A, "refused muzzle preserves every native marker byte");
    float point[3]{}, direction[3]{}, offset[3]{};
    caller = imageBase + 0x3AEB2B;
    OdstMuzzleAimDetour(fixtureOwner, point, direction, 0xFEDCBA9876543210ull, offset, 1, 1);
    caller = imageBase + 0x161204;
    Check(OdstMuzzleCameraDetour(fixtureOwner, point, direction) == 17, "assist perspective result retained");
    if (expectMuzzle)
        Check(point[0] == 2 && point[1] == 2.5f && point[2] == 4 && direction[1] == 1,
            "later assist uses clamped muzzle");
    if (nestedFire && weapon == fixturePrimary)
    {
        const auto request = g_odstMuzzleRequest;
        Check(OdstMuzzleFireDetour(secondary, 0, data, -7, 0xFA) == 0x123456789ABCDEF0ull,
            "nested native return retained");
        Check(g_odstMuzzleRequest.weapon == fixturePrimary && g_odstMuzzleRequest.lease == request.lease &&
                  g_odstMuzzleShot.active,
            "nested firing restores outer target and scope");
    }
    if (nativeChangesTarget)
        *reinterpret_cast<uint32_t*>(unitBytes + 0x230) = 0xABCD0006;
    caller = previous;
    if (fireFault)
        RaiseException(0xE0424444, 0, 0, nullptr);
    return 0x123456789ABCDEF0ull;
}
static void Reset(bool dual = false)
{
    std::memset(unitBytes, 0, sizeof(unitBytes));
    std::memset(fixtureEntries, 0, sizeof(fixtureEntries));
    *reinterpret_cast<uint8_t**>(tlsBytes + kOdstTlsObjectTableOffset) = fixtureTable;
    *reinterpret_cast<uint8_t**>(fixtureTable + 0x48) = fixtureEntries;
    *reinterpret_cast<uint8_t**>(tlsBytes + 0x598) = fixtureUsers;
    uint32_t handles[]{fixtureOwner, fixturePrimary, secondary};
    uint8_t* objects[]{unitBytes, primaryBytes, secondaryBytes};
    for (int i = 0; i < 3; ++i)
    {
        auto* entry = fixtureEntries + (i + 1) * 0x18;
        *reinterpret_cast<uint16_t*>(entry) = uint16_t(handles[i] >> 16);
        entry[3] = i ? 2 : 0;
        *reinterpret_cast<uint8_t**>(entry + 0x10) = objects[i];
    }
    unitBytes[0x276] = 0;
    unitBytes[0x277] = dual ? 1 : 0xFF;
    *reinterpret_cast<uint32_t*>(unitBytes + 0x27C) = fixturePrimary;
    *reinterpret_cast<uint32_t*>(unitBytes + 0x280) = secondary;
    *reinterpret_cast<uint32_t*>(unitBytes + 0x2B8) = UINT32_MAX;
    *reinterpret_cast<uint32_t*>(unitBytes + 0x230) = 0x44440007;
    for (auto* weapon : {primaryBytes, secondaryBytes})
    {
        weapon[0x155] = 1;
        *reinterpret_cast<uint32_t*>(weapon + 0x160) = fixtureOwner;
    }
    *reinterpret_cast<uint32_t*>(fixtureUsers + 0x3C) = fixturePrimary;
    *reinterpret_cast<uint32_t*>(fixtureUsers + 0x2740 + 0x3C) = secondary;
    localUnit = fixtureOwner;
    seated = cinematic = false;
    stereo = validTracking = true;
    fixtureTitle = GameTitle::Halo3ODST;
    g_config = {};
    g_config.gun_barrel_aim = true;
    fixtureTracking = {};
    g_fpStereoSolveScope.anatomicalTracking = fixtureTracking;
    g_odstMuzzleBindingsReady = true;
    g_odstRuntimeGeneration = 7;
    g_enabled = g_vrAim = true;
    exclusive_input::active = false;
    auto& feature = g_odstMuzzle;
    feature.base = imageBase;
    feature.generation = 7;
    feature.installedAtMs = 1;
    feature.enabled = true;
    feature.faulted = false;
    feature.callbacks = 0;
    feature.fireOriginal = NativeFire;
    feature.aimOriginal = NativeAim;
    feature.queryOriginal = NativeQuery;
    feature.viewOriginal = NativeView;
    feature.cameraOriginal = NativeCamera;
    feature.markersOriginal = NativeMarkers;
    g_odstMuzzleShot = {};
    g_odstMuzzleRequest = {};
    g_odstMuzzleCapture = {};
    g_odstMuzzleQueryContext = {fixtureOwner, 7, 2, 0, 3, GetTickCount64()};
    g_legacyCollisionOwnedQuery = false;
    fireCalls = queryCalls = markerCalls = aimCalls = cameraCalls = viewCalls = 0;
    markerCount = 1;
    expectMuzzle = true;
    queryFault = preflightFault = markerFault = fireFault = nestedFire = omitView = replaceStorage = changeOwner =
        changeGeneration = nativeChangesTarget = invalidTarget = nonfiniteTarget = false;
    changeOtherSlot = nonfiniteDirection = false;
    for (uint8_t slot = 0; slot < 2; ++slot)
    {
        weapon_muzzle::Palette sample{};
        sample.barrels[0] = {GameTitle::Halo3ODST, 7, fixtureOwner, slot ? secondary : fixturePrimary, 1, 3, 11,
            GetTickCount64(), 1000000000, slot, 0, false, {{2, 3, 4}, {0, 1, 0}, {0, 0, 1}}};
        Check(g_odstMuzzles.Publish(GameTitle::Halo3ODST, slot, sample), "committed ODST fixture");
    }
}
static uint64_t Shoot()
{
    return OdstMuzzleFireDetour(fixturePrimary, 0, reinterpret_cast<void*>(0x1234), -7, 0xFA);
}
static bool FaultingShot()
{
    __try
    {
        (void)Shoot();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return true;
    }
    return false;
}
static void ShotTests()
{
    Reset();
    Check(Shoot() == 0x123456789ABCDEF0ull, "native fire return retained");
    Check(fireCalls == 1 && queryCalls == 1 && markerCalls == 1 &&
              *reinterpret_cast<uint32_t*>(unitBytes + 0x230) == 0x44440007,
        "single shot restores native target");
    Reset(true);
    nestedFire = true;
    g_odstMuzzleQueryContext.flags = 2;
    (void)Shoot();
    Check(fireCalls == 2 && queryCalls == 2 && *reinterpret_cast<uint32_t*>(unitBytes + 0x230) == 0x44440007 &&
              !g_odstMuzzleShot.active,
        "nested and lead queries restore LIFO");
    for (int reason = 0; reason < 24; ++reason)
    {
        Reset();
        expectMuzzle = false;
        switch (reason)
        {
        case 0:
            markerCount = 0;
            break;
        case 1:
            markerCount = 2;
            break;
        case 2:
            g_odstMuzzle.enabled = false;
            break;
        case 3:
            g_odstMuzzle.faulted = true;
            break;
        case 4:
            g_odstMuzzle.generation = 8;
            break;
        case 5:
            g_config.gun_barrel_aim = false;
            break;
        case 6:
            fixtureTracking.referenceEpoch = 4;
            break;
        case 7:
            g_config.left_handed = true;
            break;
        case 8:
            exclusive_input::active = true;
            break;
        case 9:
            cinematic = true;
            break;
        case 10:
            seated = true;
            break;
        case 11:
            g_odstMuzzleQueryContext.sampleMs = GetTickCount64() - 200;
            break;
        case 12:
            unitBytes[0x276] = 4;
            break;
        case 13:
            unitBytes[0x277] = 4;
            break;
        case 14:
            primaryBytes[0x155] = 0;
            break;
        case 15:
            *reinterpret_cast<uint32_t*>(primaryBytes + 0x160) = fixtureOwner ^ 0x10000;
            break;
        case 16:
            *reinterpret_cast<uint32_t*>(unitBytes + 0x2B8) = 0xABCD0006;
            break;
        case 17:
            omitView = true;
            break;
        case 18:
            replaceStorage = true;
            break;
        case 19:
            changeOwner = true;
            break;
        case 20:
            changeGeneration = true;
            break;
        case 21:
            invalidTarget = true;
            break;
        case 22:
            nonfiniteTarget = true;
            break;
        case 23:
            fixtureTracking.hands[1].valid = false;
            break;
        }
        (void)Shoot();
        Check(fireCalls == 1 && !g_odstMuzzleShot.active && !g_odstMuzzleRequest.lease && !g_legacyCollisionOwnedQuery,
            "refused adaptation forwards once and balances all scopes");
    }
    for (int fault = 0; fault < 4; ++fault)
    {
        Reset();
        queryFault = fault == 0;
        preflightFault = fault == 1;
        markerFault = fault == 2;
        fireFault = fault == 3;
        if (fault < 2)
            expectMuzzle = false;
        Check(FaultingShot() == (fault >= 2), "optional faults isolate; native faults propagate");
        Check(fireCalls == 1 && !g_odstMuzzle.callbacks && !g_odstMuzzleShot.active && !g_odstMuzzleRequest.lease &&
                  !g_legacyCollisionOwnedQuery,
            "fault never replays native fire or leaks scopes");
        if (fault < 2)
            Check(g_odstMuzzle.faulted, "optional fault recorded");
    }
    Reset();
    nativeChangesTarget = true;
    (void)Shoot();
    Check(*reinterpret_cast<uint32_t*>(unitBytes + 0x230) == 0xABCD0006, "native targeting change wins over old lease");
    Reset();
    float dir[3]{0, 1, 0}, control[3]{};
    uint8_t nativeTarget[0x28]{};
    g_odstMuzzleQueryContext = {};
    OdstMuzzleQueryDetour(2, 0, dir, 3, control, nativeTarget);
    Check(g_odstMuzzleQueryContext.unit == fixtureOwner && g_odstMuzzleQueryContext.inputUser == 2,
        "ordinary native query captures its real owner and input user");
    g_odstMuzzleQueryContext = {};
    OdstMuzzleQueryDetour(2, 1, dir, 3, control, nativeTarget);
    Check(!g_odstMuzzleQueryContext.sampleMs, "lead-only fallback does not replace ordinary context");
    Reset();
    std::thread isolated(
        []
        {
            Check(!g_odstMuzzleQueryContext.sampleMs && !g_odstMuzzleRequest.lease,
                "native query and firing scope are thread local");
        });
    isolated.join();
}
static void PublicationTests()
{
    Reset(true);
    BoneMatrix destination[64];
    for (auto& node : destination)
    {
        node.scale = 2;
        node.translation[0] = 2;
        node.translation[1] = 3;
        node.translation[2] = 4;
    }
    for (const auto& marker : weapon_muzzle::kMarkers)
    {
        if (marker.title != GameTitle::Halo3ODST)
            continue;
        liveChecksum = uint32_t(marker.identity);
        liveCount = marker.nodeCount;
        for (uint8_t slot = 0; slot < 2; ++slot)
        {
            FpInterpolationContext context;
            context.slot = slot;
            context.muzzleWeapon = slot ? secondary : fixturePrimary;
            OdstPublishMuzzlePalette(123, context, destination, 7);
            weapon_muzzle::Receipt receipt{};
            weapon_muzzle::Ray expected{};
            Check(weapon_muzzle::Transform(
                      marker, 2, destination[marker.node].rotation, destination[marker.node].translation, expected),
                "authored ODST marker setup");
            Check(g_odstMuzzles.Read(GameTitle::Halo3ODST, 7, fixtureOwner, context.muzzleWeapon, 3, GetTickCount64(),
                      1000000000, slot, marker.barrel, false, receipt) &&
                      receipt.ray.position[0] == expected.position[0],
                "final ODST destination and full weapon identity published");
            Check(lastReticleTitle == GameTitle::Halo3ODST &&
                      (marker.barrel != 0 || lastReticlePalette.barrels[0].ray.position[0] == expected.position[0]),
                "z64: the committed ODST palette is what the reticle ray receives");
            context.muzzleWeapon ^= 0x10000;
            OdstPublishMuzzlePalette(123, context, destination, 7);
            Check(!g_odstMuzzles.Read(GameTitle::Halo3ODST, 7, fixtureOwner, slot ? secondary : fixturePrimary, 3,
                      GetTickCount64(), 1000000000, slot, marker.barrel, false, receipt),
                "weapon swap invalidates stale ODST palette");
        }
    }
    uint32_t unit = 0, weapon = 0;
    *reinterpret_cast<uint32_t*>(fixtureUsers + 0x3C) = fixturePrimary ^ 0x10000;
    Check(!OdstCaptureMuzzleOwner(0, 0, unit, weapon), "full FP slot and inventory must agree");
    Check(!OdstCaptureMuzzleOwner(1, 0, unit, weapon) && !OdstCaptureMuzzleOwner(0, 2, unit, weapon),
        "foreign player and slot rejected");
}

static uintptr_t lifecycleBase{};
static unsigned bindingScan{}, createCalls{}, removeCalls{}, logged{};
static int missingBinding = -1, ambiguousBinding = -1, failCreate = -1, failEnable = -1, failDisable = -1,
           failRemove = -1;
static bool ingress = false, quiesced = false, hookExists[6]{}, hookEnabled[6]{};
static constexpr uint32_t hookRvas[]{0x3AF230, 0x396B7C, 0x1604E0, 0x160FA0, 0x242340, 0x37F514};
namespace sig
{
    static uintptr_t Find(uintptr_t base, size_t, const char*)
    {
        constexpr uint32_t bindings[]{0x139F3C, 0x3AF230, 0x396B7C, 0x1604E0, 0x160FA0, 0x242340, 0x37F514, 0x1610D4,
            0x2E9E65, 0x2E9E8E, 0x39AB60, 0x3AE994, 0x3AEA59, 0x3AE9F7, 0x160FF8, 0x3AB1CC};
        const unsigned number = bindingScan++ / 2;
        Check(number < 16, "bounded own ODST binding inventory");
        if (int(number) == missingBinding)
            return 0;
        return base == lifecycleBase ? base + bindings[number] : int(number) == ambiguousBinding ? base + 0x100 : 0;
    }
}
static int HookIndex(void* target)
{
    for (int i = 0; i < 6; ++i)
        if (reinterpret_cast<uintptr_t>(target) == lifecycleBase + hookRvas[i])
            return i;
    Check(false, "only verified ODST optional targets");
    return -1;
}
static MH_STATUS FixtureCreate(void* target, void* detour, void** original)
{
    const int i = HookIndex(target);
    ++createCalls;
    const void* expected[]{reinterpret_cast<void*>(&OdstMuzzleFireDetour),
        reinterpret_cast<void*>(&OdstMuzzleAimDetour), reinterpret_cast<void*>(&OdstMuzzleQueryDetour),
        reinterpret_cast<void*>(&OdstMuzzleViewDetour), reinterpret_cast<void*>(&OdstMuzzleCameraDetour),
        reinterpret_cast<void*>(&OdstMuzzleMarkersDetour)};
    Check(detour == expected[i] && original, "exact ODST hook ABI entry");
    if (i == failCreate)
        return MH_ERROR_MEMORY_ALLOC;
    hookExists[i] = true;
    *original = reinterpret_cast<void*>(uintptr_t(0x4000 + i * 0x100));
    return MH_OK;
}
static MH_STATUS FixtureEnable(void* target)
{
    const int i = HookIndex(target);
    Check(hookExists[i], "created before enable");
    if (i == failEnable)
        return MH_ERROR_MEMORY_PROTECT;
    hookEnabled[i] = true;
    return MH_OK;
}
static MH_STATUS MCCVR_DisableHookForRetirement(void* target)
{
    const int i = HookIndex(target);
    if (i == failDisable)
        return MH_ERROR_MEMORY_PROTECT;
    hookEnabled[i] = false;
    return MH_OK;
}
static MH_STATUS FixtureRemove(void* target)
{
    const int i = HookIndex(target);
    ++removeCalls;
    Check(!hookEnabled[i] && quiesced && !ingress && !g_odstMuzzle.callbacks,
        "no ODST trampoline removed before complete drain");
    if (i == failRemove)
        return MH_ERROR_MEMORY_PROTECT;
    hookExists[i] = false;
    return MH_OK;
}
static bool WaitForNativeDetourQuiescence(
    const void* const* functions, const void* const* originals, size_t count, const std::atomic<uint32_t>& callbacks)
{
    Check(count == 6, "all six detour ingress ranges protected");
    const void* expected[]{reinterpret_cast<void*>(&OdstMuzzleFireDetour),
        reinterpret_cast<void*>(&OdstMuzzleAimDetour), reinterpret_cast<void*>(&OdstMuzzleQueryDetour),
        reinterpret_cast<void*>(&OdstMuzzleViewDetour), reinterpret_cast<void*>(&OdstMuzzleCameraDetour),
        reinterpret_cast<void*>(&OdstMuzzleMarkersDetour)};
    for (int i = 0; i < 6; ++i)
        Check(functions[i] == expected[i] &&
                  (!hookExists[i] || originals[i] == reinterpret_cast<void*>(uintptr_t(0x4000 + i * 0x100))),
            "exact retained detour and trampoline pairs");
    quiesced = !callbacks.load() && !ingress;
    return quiesced;
}
#define MH_CreateHook FixtureCreate
#define MCCVR_CreateHookChecked FixtureCreate
#define MH_EnableHook FixtureEnable
#define MH_RemoveHook FixtureRemove
#define MCCVR_RemoveHookChecked FixtureRemove
static char lifecycleLog[512]{}; // golden: the last lifecycle line
#define LOG(...) (++logged, (void)std::snprintf(lifecycleLog, sizeof lifecycleLog, __VA_ARGS__))
#include "../src/dll/titles/odst/odst_muzzle_lifecycle.inl"
#undef MH_CreateHook
#undef MCCVR_CreateHookChecked
#undef MH_EnableHook
#undef MH_RemoveHook
#undef MCCVR_RemoveHookChecked
#undef LOG
static void LifecycleTests()
{
    constexpr size_t size = 0x400000;
    auto* image = static_cast<uint8_t*>(VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Check(image != nullptr, "bounded lifecycle image");
    lifecycleBase = reinterpret_cast<uintptr_t>(image);
    const uint32_t edges[][2]{{0x3AF4C5, 0x37F514}, {0x3AF889, 0x3AE8A4}, {0x3AEB26, 0x396B7C}, {0x3AED8A, 0x1610D4},
        {0x110719, 0x1604E0}, {0x160642, 0x160FA0}, {0x160A58, 0x160FA0}, {0x1611FF, 0x242340}};
    for (const auto& edge : edges)
    {
        image[edge[0]] = 0xE8;
        *reinterpret_cast<int32_t*>(image + edge[0] + 1) = int32_t(edge[1] - edge[0] - 5);
    }
    auto reset = []
    {
        bindingScan = createCalls = removeCalls = 0;
        quiesced = ingress = false;
        missingBinding = ambiguousBinding = failCreate = failEnable = failDisable = failRemove = -1;
        std::memset(hookExists, 0, sizeof(hookExists));
        std::memset(hookEnabled, 0, sizeof(hookEnabled));
        for (auto& target : g_odstMuzzle.targets)
            target = nullptr;
        g_odstMuzzle.fireOriginal = nullptr;
        g_odstMuzzle.aimOriginal = nullptr;
        g_odstMuzzle.queryOriginal = nullptr;
        g_odstMuzzle.viewOriginal = nullptr;
        g_odstMuzzle.cameraOriginal = nullptr;
        g_odstMuzzle.markersOriginal = nullptr;
        g_odstMuzzle.enabled = false;
        g_odstMuzzleBindingsReady = false;
        g_odstMuzzle.callbacks = 0;
    };
    for (int i = 0; i < 16; ++i)
    {
        reset();
        missingBinding = i;
        Check(!InstallOdstMuzzle(lifecycleBase, size, 7) && createCalls == 0, "missing own proof stays stock");
        reset();
        ambiguousBinding = i;
        Check(!InstallOdstMuzzle(lifecycleBase, size, 7) && createCalls == 0, "ambiguous own proof stays stock");
    }
    for (int i = 0; i < 6; ++i)
    {
        reset();
        failCreate = i;
        Check(!InstallOdstMuzzle(lifecycleBase, size, 7) && !g_odstMuzzle.enabled && !g_odstMuzzleBindingsReady,
            "partial create remains disabled");
        Check(RemoveOdstMuzzle(), "partial create cleanup");
        reset();
        failEnable = i;
        Check(!InstallOdstMuzzle(lifecycleBase, size, 7) && !g_odstMuzzle.enabled && !g_odstMuzzleBindingsReady,
            "partial enable remains disabled");
        Check(RemoveOdstMuzzle(), "partial enable cleanup");
        reset();
        Check(InstallOdstMuzzle(lifecycleBase, size, 7), "all exact hooks installed");
        failDisable = i;
        Check(!RemoveOdstMuzzle() && removeCalls == 0, "failed disable retains all trampoline dependencies");
        failDisable = -1;
        Check(RemoveOdstMuzzle(), "disable retry succeeds");
        reset();
        Check(InstallOdstMuzzle(lifecycleBase, size, 7), "all exact hooks installed");
        failRemove = i;
        Check(!RemoveOdstMuzzle() && g_odstMuzzle.targets[i], "failed removal retains exact hook for retry");
        failRemove = -1;
        Check(RemoveOdstMuzzle(), "removal retry succeeds");
    }
    for (const auto& edge : edges)
    {
        reset();
        image[edge[0]] ^= 1;
        Check(!InstallOdstMuzzle(lifecycleBase, size, 7) && createCalls == 0,
            "changed downstream edge prevents adaptation");
        image[edge[0]] ^= 1;
    }
    reset();
    Check(InstallOdstMuzzle(lifecycleBase, size, 7) && g_odstMuzzleBindingsReady && g_odstMuzzle.enabled,
        "fixture installed; D6 G6: install publishes bindings ready");
    ingress = true;
    Check(!RemoveOdstMuzzle() && removeCalls == 0, "zero counter with thread in ingress retains code");
    ingress = false;
    g_odstMuzzle.callbacks = 1;
    Check(!RemoveOdstMuzzle() && removeCalls == 0, "active callback retains code");
    g_odstMuzzle.callbacks = 0;
    Check(RemoveOdstMuzzle() && !g_odstMuzzleBindingsReady, "drained cleanup clears publication authority");
    // Golden: the exact lifecycle and trajectory lines.
    g_config.gun_barrel_aim = true;
    reset();
    missingBinding = 0;
    logged = 0;
    Check(!InstallOdstMuzzle(lifecycleBase, size, 7) && logged == 1 &&
              !std::strcmp(lifecycleLog, "ODST muzzle StockFallback: missing/ambiguous binding +139F3C"),
        "M2b: exact ODST StockFallback line");
    reset();
    logged = 0;
    Check(InstallOdstMuzzle(lifecycleBase, size, 7) && logged == 1 &&
              !std::strcmp(lifecycleLog,
                  "ODST muzzle Installed: optional committed FP barrel with native targeting/obstruction; option=1"),
        "M2b: exact ODST Installed line");
    logged = 0;
    Check(!InstallOdstMuzzle(lifecycleBase, size, 7) && logged == 0 && createCalls == 6,
        "M2b: a held ODST set refuses a second install silently");
    g_odstMuzzle.applied = 3;
    g_odstMuzzle.refused = 1;
    g_odstMuzzle.queries = 2;
    g_odstMuzzle.restoreRefused = 4;
    g_odstMuzzle.faulted = false;
    logged = 0;
    ReportOdstMuzzle();
    Check(logged == 1 &&
              !std::strcmp(lifecycleLog,
                  "ODST barrel trajectory: enabled=1 fault=0 applied=3 refused=1 queries=2 restoreRefused=4 option=1"),
        "M2b: exact ODST trajectory report");
    logged = 0;
    ReportOdstMuzzle();
    Check(logged == 1 &&
              !std::strcmp(lifecycleLog,
                  "ODST barrel trajectory: enabled=1 fault=0 applied=0 refused=0 queries=0 restoreRefused=0 option=1"),
        "M2b: the ODST report resets its counters");
    g_odstMuzzle.callbacks = 1;
    logged = 0;
    Check(!RemoveOdstMuzzle() && logged == 1 &&
              !std::strcmp(lifecycleLog, "ODST muzzle CleanupRequired: callbacks or ingress busy"),
        "M2b: exact ODST CleanupRequired line");
    g_odstMuzzle.callbacks = 0;
    Check(RemoveOdstMuzzle(), "M2b: report fixture retired");
    logged = 0;
    ReportOdstMuzzle();
    Check(logged == 0, "M2b: no ODST report line once retired");
    Check(VirtualFree(image, 0, MEM_RELEASE) != 0, "lifecycle fixture released");
}
static void ReserveTests()
{
    uint32_t held = 0, stowed = 0;
    Reset();
    Check(OdstReadReserveWeapon(fixtureOwner, held, stowed) && held == fixturePrimary && stowed == secondary,
        "ODST reserve is the stowed owned weapon");
    unitBytes[0x276] = 1;
    Check(OdstReadReserveWeapon(fixtureOwner, held, stowed) && held == secondary && stowed == fixturePrimary,
        "ODST swap flips primary and reserve");
    Reset();
    secondaryBytes[0x155] = 0;
    Check(OdstReadReserveWeapon(fixtureOwner, held, stowed) && stowed == reserve_weapon::kNoHandle,
        "ODST: unowned weapon is no reserve");
    Reset();
    unitBytes[0x276] = 4;
    Check(!OdstReadReserveWeapon(fixtureOwner, held, stowed), "ODST: invalid primary role refused");
    Reset();
}
static void MeleeResponseTests()
{
    // ODST offsets: unit tag unarmed +1C4, weapon tag response +25C(+C/+1C),
    // charged flag 19C bit 14 / response 2DC, clash 2FC(+C/+1C), material 32C, power +184.
    Reset(true);
    *reinterpret_cast<uint32_t*>(unitBytes) = 0x1111;
    *reinterpret_cast<uint32_t*>(secondaryBytes) = 0x2222;
    std::memset(fixtureUnitTag, 0, sizeof fixtureUnitTag);
    std::memset(weaponTag, 0, sizeof weaponTag);
    *reinterpret_cast<uint32_t*>(fixtureUnitTag + 0x1C4) = 77;
    *reinterpret_cast<uint32_t*>(weaponTag + 0x25C + 0xC) = 55;
    *reinterpret_cast<uint32_t*>(weaponTag + 0x25C + 0x1C) = 56;
    *reinterpret_cast<uint32_t*>(weaponTag + 0x2FC + 0xC) = 57;
    *reinterpret_cast<uint32_t*>(weaponTag + 0x2FC + 0x1C) = 58;
    *reinterpret_cast<uint32_t*>(weaponTag + 0x2DC + 0xC) = 65;
    *reinterpret_cast<uint32_t*>(weaponTag + 0x2DC + 0x1C) = 66;
    weaponTag[0x32C] = 0x44;
    *reinterpret_cast<float*>(secondaryBytes + 0x184) = 0.5f;
    Halo3MeleeResponse r{};
    bool armed = false;
    Check(OdstReadPhysicalMeleeResponse(fixtureOwner, 0, r, armed) && armed && r.damage == 55 && r.effect == 56 &&
              r.clashDamage == 57 && r.clashEffect == 58 && r.material == (0x44 & 0x3f),
        "A4-08: an armed ODST support hand strikes with its own weapon's melee response");
    *reinterpret_cast<uint32_t*>(weaponTag + 0x19C) = 1u << 14;
    *reinterpret_cast<float*>(secondaryBytes + 0x184) = 1.0f;
    Check(OdstReadPhysicalMeleeResponse(fixtureOwner, 0, r, armed) && r.damage == 65 && r.effect == 66 &&
              r.clashDamage == UINT32_MAX,
        "A4-08: a charged ODST weapon uses its charged response, no clash");
    unitBytes[0x277] = 0xFF;
    Check(OdstReadPhysicalMeleeResponse(fixtureOwner, 0, r, armed) && !armed && r.damage == 77 && r.material == 3,
        "A4-08: an empty ODST support hand strikes with the unit's unarmed damage (a fist, not the sword)");
    unitBytes[0x277] = 1;
    secondaryBytes[0x155] = 0;
    Check(
        !OdstReadPhysicalMeleeResponse(fixtureOwner, 0, r, armed), "A4-08: a weapon this unit does not own is refused");
    secondaryBytes[0x155] = 1;
}

// Goldens: every place ODST's barrel code differs from the shared Reach/H4 barrel code,
// each pinned so a change to the shared code cannot drift ODST's behaviour.
static bool OdstAcquire(const weapon_muzzle::Receipt& muzzle, NativeShotTargetLease<0x28>& lease)
{
    return BarrelMuzzleAcquireTarget<OdstMuzzleShots>(muzzle, lease);
}
static uint64_t ShootWith(uint32_t weapon)
{
    return OdstMuzzleFireDetour(weapon, 0, reinterpret_cast<void*>(0x1234), -7, 0xFA);
}
// Reset() leaves the feature counters running; the goldens read them from zero.
static void ResetM2b(bool dual = false)
{
    Reset(dual);
    g_odstMuzzle.applied = 0;
    g_odstMuzzle.refused = 0;
    g_odstMuzzle.queries = 0;
    g_odstMuzzle.restoreRefused = 0;
}
static void M2bGoldens()
{
    // 1. The exact per-shot lines (four a second; ShotTests used this second's).
    Sleep(1100);
    ResetM2b();
    shotLogs = 0;
    (void)Shoot();
    Check(shotLogs == 1 &&
              !std::strcmp(shotLog, "ODST shot: barrel applied; origin 2.000 2.500 4.000, direction 0.000 1.000 0.000"),
        "M2b: exact ODST applied shot line");
    ResetM2b();
    g_odstMuzzle.enabled = false;
    expectMuzzle = false;
    shotLogs = 0;
    (void)Shoot();
    {
        float native = 0;
        std::memset(&native, 0x5A, 4);
        char expected[512];
        std::snprintf(expected, sizeof expected,
            "ODST shot: barrel refused; origin %.3f %.3f %.3f, direction %.3f %.3f %.3f", native, native, native,
            native, native, native);
        Check(shotLogs == 1 && !std::strcmp(shotLog, expected),
            "M2b: exact ODST refused shot line (native marker bytes)");
    }
    // 8. As in Reach and H4: only the local player's own refused shot is
    // counted and printed; an unreadable view unit is not the player's.
    ResetM2b();
    expectMuzzle = false;
    shotLogs = 0;
    (void)ShootWith(secondary);
    Check(g_odstMuzzle.refused == 0 && g_odstMuzzle.applied == 0 && shotLogs == 0,
        "D6 G1: another actor's refused ODST shot is not counted, no line");
    ResetM2b();
    expectMuzzle = false;
    g_odstMuzzleQueryContext.unit = 0;
    shotLogs = 0;
    (void)Shoot();
    Check(g_odstMuzzle.refused == 0 && shotLogs == 0, "D6 G1: context unit 0 not counted, no line");
    ResetM2b();
    expectMuzzle = false;
    g_odstMuzzleQueryContext.unit = UINT32_MAX;
    shotLogs = 0;
    (void)Shoot();
    Check(g_odstMuzzle.refused == 0 && shotLogs == 0, "D6 G1: context unit UINT32_MAX not counted, no line");
    ResetM2b();
    shotLogs = 0;
    (void)Shoot();
    Check(g_odstMuzzle.applied == 1 && shotLogs == 1, "M2b D6: positive control prints");
    // 2. The five-argument fire returns the native uint64; a faulting fire restores the lease once.
    ResetM2b();
    fireFault = true;
    Check(FaultingShot() && fireCalls == 1 && *reinterpret_cast<uint32_t*>(unitBytes + 0x230) == 0x44440007 &&
              g_odstMuzzle.restoreRefused == 0 && g_odstMuzzle.applied == 1 && !g_odstMuzzle.callbacks,
        "M2b D1: faulting fire restores the lease once");
    ResetM2b();
    g_odstMuzzle.fireOriginal = nullptr;
    Check(Shoot() == 0 && fireCalls == 0 && !g_odstMuzzle.callbacks && !g_odstMuzzleRequest.lease,
        "M2b D1: no native fire returns 0 and balances the scope");
    // The fire detour, not the shared scope, counts the callback, so
    // retire's quiescence wait never misses a thread between the two.
    ResetM2b();
    g_odstMuzzle.callbacks = 1;
    Check(BarrelMuzzleFireScope<OdstMuzzleShots>(fixturePrimary, 0, 0,
              []() -> uint64_t
              {
                  Check(g_odstMuzzle.callbacks == 1 && g_odstMuzzleRequest.lease,
                      "M2b: the fire scope adds no count of its own");
                  return 5;
              }) == 5 &&
              !g_odstMuzzle.callbacks && !g_odstMuzzleRequest.lease,
        "M2b: the fire scope releases the caller's count");
    // 3. A uint64 marker count above 0xFFFF is passed through whole and refused.
    ResetM2b();
    expectMuzzle = false;
    markerCount = 0x10001;
    (void)Shoot();
    Check(markerCalls == 1 && g_odstMuzzle.refused == 1 && g_odstMuzzle.applied == 0, "M2b D2: count 0x10001 is not 1");
    // 4. The view replaces origin and camera only, for a query shot from either query caller.
    {
        float direction[3]{9, 9, 9}, origin[3]{}, camera[3]{};
        for (uintptr_t at : {uintptr_t(0x160647), uintptr_t(0x160A5D), uintptr_t(0x160000)})
        {
            ResetM2b();
            g_odstMuzzleShot = {};
            g_odstMuzzleShot.active = g_odstMuzzleShot.query = true;
            g_odstMuzzleShot.unit = fixtureOwner;
            const float position[3]{5, 6, 7}, aim[3]{0, 0, 1};
            std::memcpy(g_odstMuzzleShot.position, position, 12);
            std::memcpy(g_odstMuzzleShot.direction, aim, 12);
            caller = imageBase + at;
            OdstMuzzleViewDetour(fixtureOwner, 0, direction, origin, camera);
            caller = 0;
            const bool query = at != 0x160000;
            Check(direction[0] == 9 && direction[1] == 9 && direction[2] == 9,
                "M2b D3: the view never writes the direction input");
            Check(query ? (origin[0] == 5 && origin[2] == 7 && camera[0] == 5 && camera[1] == 6 &&
                              g_odstMuzzleShot.viewApplied)
                        : (origin[0] == 10 && camera[0] == 20 && !g_odstMuzzleShot.viewApplied),
                "M2b D3: query callers replace origin and camera; others pass through");
        }
        ResetM2b();
        g_odstMuzzleShot = {};
        g_odstMuzzleShot.active = true;
        g_odstMuzzleShot.unit = fixtureOwner;
        g_odstMuzzleShot.position[0] = 5;
        caller = imageBase + 0x160647;
        OdstMuzzleViewDetour(fixtureOwner, 0, direction, origin, camera);
        caller = 0;
        Check(origin[0] == 10 && camera[0] == 20 && !g_odstMuzzleShot.viewApplied,
            "M2b D3: a non-query shot is not touched by the view");
        ResetM2b();
        g_odstMuzzleCapture = {};
        g_odstMuzzleCapture.active = true;
        caller = imageBase + 0x160A5D;
        OdstMuzzleViewDetour(fixtureOwner, 0, direction, origin, camera);
        Check(g_odstMuzzleCapture.count == 0, "M2b D3: the lead caller does not capture");
        caller = imageBase + 0x160647;
        OdstMuzzleViewDetour(fixtureOwner, 0, direction, origin, camera);
        caller = 0;
        Check(g_odstMuzzleCapture.count == 1 && g_odstMuzzleCapture.unit == fixtureOwner,
            "M2b D3: the first query caller captures its unit");
        g_odstMuzzleCapture = {};
    }
    // 5. The camera hook: only an active non-query shot of the same unit, from +161204.
    {
        auto run = [](bool query, uint32_t unit, uintptr_t at, float* position, float* direction)
        {
            ResetM2b();
            g_odstMuzzleShot = {};
            g_odstMuzzleShot.active = true;
            g_odstMuzzleShot.query = query;
            g_odstMuzzleShot.unit = fixtureOwner;
            const float p[3]{5, 6, 7}, d[3]{0, 0, 1};
            std::memcpy(g_odstMuzzleShot.position, p, 12);
            std::memcpy(g_odstMuzzleShot.direction, d, 12);
            caller = imageBase + at;
            const int32_t result = OdstMuzzleCameraDetour(unit, position, direction);
            caller = 0;
            return result;
        };
        float position[3]{}, direction[3]{};
        Check(run(false, fixtureOwner, 0x161204, position, direction) == 17 && position[0] == 5 && position[2] == 7 &&
                  direction[2] == 1 && direction[0] == 0,
            "M2b: the camera hook replaces position and direction for the barrel shot");
        Check(run(true, fixtureOwner, 0x161204, position, direction) == 17 && position[0] == 10 && direction[0] == 1,
            "M2b: camera hook passes a query shot through");
        Check(run(false, fixtureOwner ^ 0x10000, 0x161204, position, direction) == 17 && position[0] == 10 &&
                  direction[0] == 1,
            "M2b: camera hook passes another unit through");
        Check(run(false, fixtureOwner, 0x161200, position, direction) == 17 && position[0] == 10 && direction[0] == 1,
            "M2b: camera hook passes another caller through");
        ResetM2b();
        g_odstMuzzle.cameraOriginal = nullptr;
        caller = imageBase + 0x161204;
        Check(OdstMuzzleCameraDetour(fixtureOwner, position, direction) == 0 && !g_odstMuzzle.callbacks,
            "M2b: no native camera returns 0");
        caller = 0;
    }
    // 6. ODST's acquire checks both slots before and after the re-query, and a finite shot ray.
    ResetM2b(true);
    expectMuzzle = false;
    changeOtherSlot = true;
    (void)Shoot();
    Check(queryCalls == 1 && g_odstMuzzle.refused == 1 && g_odstMuzzle.queries == 0,
        "M2b D4: the other slot changing during the re-query is refused");
    ResetM2b();
    expectMuzzle = false;
    nonfiniteDirection = true;
    (void)Shoot();
    Check(queryCalls == 1 && g_odstMuzzle.refused == 1 && g_odstMuzzle.queries == 0,
        "M2b D4: a non-finite re-queried direction is refused");
    {
        ResetM2b();
        weapon_muzzle::Receipt muzzle{};
        muzzle.slot = 2;
        muzzle.weapon = fixturePrimary;
        NativeShotTargetLease<0x28> lease{};
        Check(!OdstAcquire(muzzle, lease) && queryCalls == 0 && !g_odstMuzzleShot.active && !lease.active,
            "M2b D4: slot 2 is refused before any query");
    }
    // 7. After an applied marker ODST's aim takes the barrel path; without the flag, its own call.
    {
        ResetM2b();
        g_odstMuzzleShot = {};
        g_odstMuzzleShot.active = true;
        g_odstMuzzleShot.unit = fixtureOwner;
        g_odstMuzzleShot.barrel = true;
        const float p[3]{2, 2.5f, 4}, d[3]{0, 1, 0};
        std::memcpy(g_odstMuzzleShot.position, p, 12);
        std::memcpy(g_odstMuzzleShot.direction, d, 12);
        float origin[3]{}, direction[3]{1, 0, 0}, offset[3]{};
        caller = imageBase + 0x3AEB2B;
        OdstMuzzleAimDetour(fixtureOwner, origin, direction, 0xFEDCBA9876543210ull, offset, 1, 1);
        Check(aimCalls == 1 && origin[1] == 2.5f && direction[1] == 1,
            "M2b D5: a barrel-marked shot takes the barrel aim path");
        g_odstMuzzleShot.barrel = false;
        expectMuzzle = false;
        origin[0] = origin[1] = origin[2] = 0;
        direction[0] = 1;
        direction[1] = direction[2] = 0;
        float expected[3]{};
        const float from[3]{0, 0, 0};
        const bool built = BuildIndependentWeaponDirection(from, g_odstMuzzleShot.position, g_odstMuzzleShot.direction,
            std::clamp(g_config.crosshair_distance_m, 2.f, 50.f) * Game_GetWorldScale(), expected);
        OdstMuzzleAimDetour(fixtureOwner, origin, direction, 0xFEDCBA9876543210ull, offset, 1, 1);
        caller = 0;
        Check(built && aimCalls == 2 && direction[0] == expected[0] && direction[1] == expected[1] &&
                  direction[2] == expected[2],
            "M2b D5: an unmarked shot keeps the native call and the independent-direction adjustment");
        ResetM2b();
        (void)Shoot();
        Check(g_odstMuzzle.applied == 1 && aimCalls == 2, "M2b D5: an applied marker reaches the barrel aim path");
    }
    // 9. The ownership tail: target storage +228 for the local unseated player only.
    ResetM2b();
    Check(OdstMuzzleTargetStorage(fixtureOwner) == unitBytes + 0x228, "M2b: ODST target storage is unit+228");
    ResetM2b();
    seated = true;
    Check(!OdstMuzzleTargetStorage(fixtureOwner), "M2b: ODST seated unit has no target storage");
    ResetM2b();
    localUnit = fixtureOwner ^ 0x10000;
    Check(!OdstMuzzleTargetStorage(fixtureOwner), "M2b: ODST foreign unit has no target storage");
    ResetM2b();
    *reinterpret_cast<uint32_t*>(unitBytes + 0x2B8) = fixtureTarget;
    Check(!OdstMuzzleTargetStorage(fixtureOwner), "M2b: ODST controlling parent has no target storage");
    ResetM2b();
}
int main()
{
    ShotTests();
    PublicationTests();
    Reset(true);
    RunLegacyPublicationFixtures<OdstPublicationFixture>(Check); // shared H3/ODST publication fixtures
    ReserveTests();
    MeleeResponseTests();
    Reset(true);
    g_config.gun_barrel_aim = false;
    g_config.independent_dual_aim = true;
    expectMuzzle = false;
    (void)Shoot();
    Check(fireCalls == 1 && queryCalls == 0, "H2/H3 dual option does not adapt ODST firing");
    LifecycleTests();
    M2bGoldens();
    std::printf("PASS: %u production ODST muzzle checks (native services stubbed)\n", checks);
}
