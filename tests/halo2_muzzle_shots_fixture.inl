// Test fixture included by halo2_independent_shots_tests.cpp: the Halo 2 muzzle marker detour,
// its gates, the obstruction preflight and the shot lease, driven with a private native stub.
// Test-only: never in the DLL.
static uint16_t muzzleCount = 1;
static unsigned markerCalls{}, muzzleFires{};
static bool expectMuzzle = true, muzzleNativeFault = false, muzzleNested = false, muzzlePreflightFault = false;
static uint32_t markerObject = fixturePrimary;
static bool parentMarker = false;
static uint32_t overrideMarker = UINT32_MAX;
// Golden 6: what the preflight saw (the other title's flag, the callback count).
static bool preflightOtherFlag = false;
static uint32_t preflightCallbacks = 0;
static float ExpectedMuzzleX()
{
    return g_halo2MuzzleRequest.weapon == fixtureSecondary ? -2.0f : 2.0f;
}
static uint16_t NativeMarkers(uint32_t object, uint32_t name, void* data, int16_t capacity)
{
    ++markerCalls;
    Check(object == markerObject && name == 0xF0000DB && capacity == 64, "native marker ABI preserved");
    if (muzzleNativeFault)
        RaiseException(0xE0424444, 0, 0, nullptr);
    std::memset(data, 0x5A, 0x70);
    return muzzleCount;
}
static void NativeMuzzleAim(uint32_t unit, float* position, float* direction, uint64_t velocity, float* offset,
    uint8_t project, uint8_t use, uint8_t collision)
{
    ++aimCalls;
    if (velocity != 0xFEDCBA9876543210ull)
    {
        Check(g_halo2CollisionOwnQuery, "H2 obstruction preflight cannot schedule collision/melee work");
        if (muzzlePreflightFault)
            RaiseException(0xE0425555, 0, 0, nullptr);
        preflightOtherFlag = g_legacyCollisionOwnedQuery;
        preflightCallbacks = g_halo2Muzzle.callbacks.load();
        Check(unit == owner && velocity && !offset && !project && !use && collision == 1,
            "marker preflight uses private native velocity storage and obstruction clamp");
        Check(position[0] == ExpectedMuzzleX() && position[1] == 3 && position[2] == 4 && direction[1] == 1,
            "preflight sees visible marker before any native origin restoration");
        auto* output = reinterpret_cast<float*>(velocity);
        output[0] = 10;
        output[1] = 20;
        output[2] = 30;
        position[1] = 2.5f;
        return;
    }
    if (expectMuzzle)
    {
        Check(unit == owner && velocity && !offset && !project && !use && collision == 1,
            "muzzle passes native velocity and collision while disabling camera relocation");
        Check(position[0] == ExpectedMuzzleX() && position[1] == 2.5f && position[2] == 4 && direction[0] == 0 &&
                  direction[1] == 1,
            "native clamp starts at actual authored muzzle and direction");
        position[1] = 2.5f; // Native obstruction result must survive every later override.
    }
    else
    {
        Check(offset && project == 1 && use == 1 && collision == 1,
            "refused muzzle preserves all native helper arguments");
        position[0] = 0;
        position[1] = 0;
        position[2] = 1;
        direction[0] = 1;
        direction[1] = 0;
        direction[2] = 0;
    }
}
static void NativeMuzzleFire(uint32_t weapon, int16_t barrel, int32_t projectile, uint8_t predicted)
{
    ++muzzleFires;
    Check(barrel == 0 && projectile == -7 && predicted == 0xFA, "muzzle preserves complete native fire ABI");
    alignas(float) uint8_t nativeMarker[0x70]{};
    caller = g_halo2Muzzle.base + 0x8E4BAF;
    markerObject = overrideMarker != UINT32_MAX ? overrideMarker : parentMarker ? owner : weapon;
    Check(Halo2MuzzleMarkersDetour(markerObject, 0xF0000DB, nativeMarker, 64) == muzzleCount,
        "native marker count unchanged");
    if (expectMuzzle)
    {
        const float* forward = reinterpret_cast<const float*>(nativeMarker + 0x3C);
        const float* up = reinterpret_cast<const float*>(nativeMarker + 0x54);
        const float* position = reinterpret_cast<const float*>(nativeMarker + 0x60);
        Check(forward[0] == 0 && forward[1] == 1 && up[2] == 1 && position[0] == ExpectedMuzzleX() &&
                  position[1] == 2.5f && position[2] == 4,
            "native firing buffer receives visible muzzle position and orientation");
        for (unsigned i = 0; i < 0x3C; ++i)
            Check(nativeMarker[i] == 0x5A, "local marker and world scale retained");
        for (unsigned i = 0x6C; i < 0x70; ++i)
            Check(nativeMarker[i] == 0x5A, "native marker flags retained");
        Check(g_halo2IndependentShot.barrel && *reinterpret_cast<uint32_t*>(unitBytes + 0x1D4) == 0x22220005,
            "direct native homing sees muzzle acquisition target");
    }
    else
        for (auto value : nativeMarker)
            Check(value == 0x5A, "refused marker buffer remains native");
    float origin[3]{}, direction[3]{}, offset[3]{};
    caller = g_halo2Dual.base + 0x8E4FCD;
    Halo2IndependentAimDetour(owner, origin, direction, 0xFEDCBA9876543210ull, offset, 1, 1, 1);
    if (expectMuzzle)
        Check(origin[1] == 2.5f && direction[1] == 1, "native wall clipping survives early helper");
    caller = g_halo2Dual.base + 0x7597BD;
    float cameraPoint[3]{}, cameraForward[3]{};
    Check(Halo2IndependentCameraDetour(owner, cameraPoint, cameraForward) == 0x12345678,
        "native assist camera return unchanged");
    if (expectMuzzle)
        Check(cameraPoint[0] == ExpectedMuzzleX() && cameraPoint[1] == 2.5f && cameraPoint[2] == 4 &&
                  cameraForward[1] == 1,
            "downstream native assist uses clipped muzzle and exact barrel direction");
    if (muzzleNested && weapon == fixturePrimary)
    {
        const auto outer = g_halo2MuzzleRequest;
        const auto shot = g_halo2IndependentShot;
        Halo2IndependentFireDetour(fixtureSecondary, 0, -7, 0xFA);
        Check(g_halo2MuzzleRequest.lease == outer.lease && g_halo2MuzzleRequest.weapon == fixturePrimary &&
                  g_halo2IndependentShot.slot == shot.slot && g_halo2IndependentShot.barrel,
            "nested muzzle shot restores outer lease and weapon role");
    }
}
static void ResetMuzzle(bool dual = false)
{
    Reset();
    g_config.gun_barrel_aim = true;
    g_config.independent_dual_aim = dual;
    g_config.left_handed = false;
    g_halo2Muzzle.base = g_halo2Dual.base;
    g_halo2Muzzle.generation = 7;
    g_halo2Muzzle.original = NativeMarkers;
    g_halo2Muzzle.enabled = true;
    g_halo2Muzzle.faulted = false;
    g_halo2MuzzleRequest = {};
    g_halo2Dual.fireOriginal = NativeMuzzleFire;
    g_halo2Dual.aimOriginal = NativeMuzzleAim;
    markerCalls = muzzleFires = 0;
    muzzleCount = 1;
    expectMuzzle = true;
    muzzleNativeFault = muzzleNested = muzzlePreflightFault = false;
    parentMarker = false;
    overrideMarker = UINT32_MAX;
    for (uint8_t slot = 0; slot < 2; ++slot)
    {
        weapon_muzzle::Palette sample{};
        sample.barrels[0] = {GameTitle::Halo2, 7, owner, slot ? fixtureSecondary : fixturePrimary, 1, 3, 11,
            GetTickCount64(), 1000000000, slot, 0, false, {{slot ? -2.0f : 2.0f, 3, 4}, {0, 1, 0}, {0, 0, 1}}};
        Check(g_halo2Muzzles.Publish(GameTitle::Halo2, slot, sample), "fixture committed muzzle publication");
    }
    RecordHalo2IndependentQuery();
    if (!dual)
        fixtureUsers[kHalo2FirstPersonWeaponDataOffset + kHalo2FirstPersonWeaponSlotStride] = 0;
}
static void ShootMuzzle(uint32_t weapon = fixturePrimary)
{
    Halo2IndependentFireDetour(weapon, 0, -7, 0xFA);
}
static bool FaultingMuzzle()
{
    __try
    {
        ShootMuzzle();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return true;
    }
    return false;
}
static void MuzzleTests()
{
    ResetMuzzle();
    ShootMuzzle();
    Check(markerCalls == 1 && queryCalls == 1 && muzzleFires == 1 &&
              *reinterpret_cast<uint32_t*>(unitBytes + 0x1D4) == 0x44440007,
        "single weapon muzzle query and LIFO cleanup");
    // T-2 shot evidence on the committed-muzzle branch: the mod's barrel ray
    // was the engine's input, the firing context carries a real barrel index,
    // and the replaced camera-projection/unit-aim arguments are reported as
    // false rather than as whatever the caller passed.
    Check(shotEvents == 1 && lastShot.weapon == fixturePrimary && lastShot.slot == 0 && lastShot.barrel == 0 &&
              lastShot.substituted && !lastShot.predicted && !lastShot.firesFromCamera && !lastShot.unitAim,
        "committed muzzle shot publishes the substituted barrel ray with its real barrel");
    Check(lastShot.origin[0] == 2 && lastShot.origin[1] == 2.5f && lastShot.origin[2] == 4 &&
              lastShot.direction[0] == 0 && lastShot.direction[1] == 1,
        "committed muzzle shot carries the final clipped muzzle ray");
    Check(!g_halo2IndependentShot.active && !g_halo2MuzzleRequest.lease && !g_halo2Muzzle.callbacks &&
              !g_halo2Dual.callbacks && !g_halo2CollisionOwnQuery,
        "muzzle scopes and native callbacks balanced");
    ResetMuzzle(true);
    muzzleNested = true;
    ShootMuzzle();
    Check(muzzleFires == 2 && queryCalls == 4 && *reinterpret_cast<uint32_t*>(unitBytes + 0x1D4) == 0x44440007,
        "dual and muzzle leases restore in reverse order across nested guns");
    ResetMuzzle(true);
    parentMarker = true;
    ShootMuzzle(fixturePrimary);
    ShootMuzzle(fixtureSecondary);
    Check(muzzleFires == 2 && markerCalls == 2 && queryCalls == 4,
        "native parent markers retain separate primary and secondary visible origins");
    for (uint32_t foreign : {owner + 0x10000u, fixtureSecondary, 0x11110004u})
    {
        ResetMuzzle();
        overrideMarker = foreign;
        expectMuzzle = false;
        ShootMuzzle();
        Check(queryCalls == 0 && muzzleFires == 1, "foreign or other-weapon marker does not gain local ownership");
    }
    for (int reason = 0; reason < 9; ++reason)
    {
        ResetMuzzle();
        expectMuzzle = false;
        switch (reason)
        {
        case 0:
            muzzleCount = 0;
            break;
        case 1:
            muzzleCount = 2;
            break;
        case 2:
            g_halo2Muzzle.enabled = false;
            break;
        case 3:
            g_halo2Muzzle.faulted = true;
            break;
        case 4:
            g_halo2Muzzle.generation = 8;
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
        }
        ShootMuzzle();
        Check(queryCalls == 0 && muzzleFires == 1, "unavailable muzzle forwards native shot once without query");
    }
    ResetMuzzle();
    expectMuzzle = false;
    queryFault = true;
    ShootMuzzle();
    Check(g_halo2Muzzle.faulted && !g_halo2Dual.faulted && muzzleFires == 1,
        "muzzle query exception isolates failure from independent firing");
    ResetMuzzle();
    expectMuzzle = false;
    muzzlePreflightFault = true;
    ShootMuzzle();
    Check(g_halo2Muzzle.faulted && !g_halo2Dual.faulted && !g_halo2CollisionOwnQuery && queryCalls == 0 &&
              muzzleFires == 1,
        "H2 failed obstruction preflight restores scheduler scope and forwards native fire once");
    ResetMuzzle();
    muzzleNativeFault = true;
    Check(FaultingMuzzle() && markerCalls == 1 && muzzleFires == 1 && !g_halo2Muzzle.callbacks &&
              !g_halo2Dual.callbacks && !g_halo2MuzzleRequest.lease,
        "native marker exception propagates once after cleanup");
    g_config.gun_barrel_aim = false;
    g_halo2Muzzle.enabled = false;
}

// Goldens 2, 4-7, written on the older code before the move into shared code. The markers
// detour is driven directly, so every gate, count, capacity and lease state is
// reachable. A private native stub fills the buffer with 0x5A and returns
// goldenCount. Tracking is invalid, so a request that passes the whole gate is
// refused by the tail's first test, which reads tracking once: a gate refusal
// is one with no tracking read and no preflight.
static uint16_t goldenCount = 1;
static unsigned goldenMarkerCalls{};
static NativeShotTargetLease<0x24> goldenLease{};
static alignas(float) uint8_t goldenBuffer[0x70]{};
static uint16_t GoldenMarkers(uint32_t, uint32_t name, void* data, int16_t capacity)
{
    ++goldenMarkerCalls;
    Check(name == 0xF0000DB, "golden marker name forwarded");
    if (data && capacity > 0)
        std::memset(data, 0x5A, 0x70);
    return goldenCount;
}
static uint16_t GoldenH2Markers(
    uint32_t object, int16_t capacity, bool withMarkers, bool& untouched, uintptr_t at = 0x8E4BAF)
{
    std::memset(goldenBuffer, 0x5A, sizeof goldenBuffer);
    caller = g_halo2Muzzle.base + at;
    const auto result = Halo2MuzzleMarkersDetour(object, 0xF0000DB, withMarkers ? goldenBuffer : nullptr, capacity);
    untouched = true;
    for (auto value : goldenBuffer)
        untouched = untouched && value == 0x5A;
    return result;
}
static void ResetGolden()
{
    ResetMuzzle();
    g_halo2Muzzle.original = GoldenMarkers;
    goldenCount = 1;
    goldenMarkerCalls = 0;
    g_halo2Muzzle.applied = 0;
    g_halo2Muzzle.refused = 0;
    goldenLease = {};
    g_halo2MuzzleRequest = {fixturePrimary, 0, &goldenLease};
    g_halo2IndependentShot = {true, false, false, owner, 1, carriers[1], 7, false};
    trackingValid = false;
    trackingReads = 0;
    aimCalls = 0;
}
static bool GoldenShotScopeWhole()
{
    const auto& shot = g_halo2IndependentShot;
    return shot.active && !shot.query && !shot.locationApplied && shot.unit == owner && shot.slot == 1 &&
           shot.distance == 7 && !shot.barrel && !std::memcmp(&shot.carrier, &carriers[1], sizeof carriers[1]);
}
static void MuzzleGoldenTests()
{
    bool untouched = false;
    ResetGolden();
    Check(GoldenH2Markers(fixturePrimary, 64, true, untouched) == 1 && untouched && trackingReads == 1 && !aimCalls &&
              g_halo2Muzzle.refused == 1 && !g_halo2Muzzle.applied && !g_halo2Muzzle.callbacks &&
              GoldenShotScopeWhole(),
        "M3 golden 2: the H2 gate admits the base request and the tail refuses at tracking");
    // H2EK 4A0F50 lets the native owner be the firing weapon's parent unit.
    ResetGolden();
    Check(GoldenH2Markers(owner, 64, true, untouched) == 1 && trackingReads == 1 && g_halo2Muzzle.refused == 1,
        "M3 golden 4: H2 admits a marker on the firing weapon's parent unit");
    for (int gate = 0; gate < 21; ++gate)
    {
        ResetGolden();
        uint32_t object = fixturePrimary;
        int16_t capacity = 64;
        bool withMarkers = true;
        switch (gate)
        {
        case 0:
            g_halo2Muzzle.enabled = false;
            break;
        case 1:
            g_halo2Muzzle.faulted = true;
            break;
        case 2:
            g_config.gun_barrel_aim = false;
            break;
        case 3:
            g_halo2Dual.enabled = false;
            break;
        case 4:
            g_halo2Dual.faulted = true;
            break;
        case 5:
            exclusive_input::active = true;
            break;
        case 6:
            controller = false;
            break;
        case 7:
            palette = false;
            break;
        case 8:
            direct = false;
            break;
        case 9:
            g_halo2Muzzle.generation = 8;
            break;
        case 10:
            goldenLease.active = true;
            break;
        case 11:
            g_halo2MuzzleRequest.barrel = -1;
            break;
        case 12:
            g_halo2MuzzleRequest.barrel = 2;
            break;
        case 13:
            goldenCount = 0;
            break;
        case 14:
            goldenCount = 2;
            break;
        case 15:
            goldenCount = 0xFFFF;
            break;
        case 16:
            capacity = 0;
            break;
        case 17:
            withMarkers = false;
            break;
        case 18:
            object = fixtureSecondary;
            break;
        case 19:
            object = owner + 0x10000;
            break;
        case 20:
            object = 0x11110004;
            break;
        }
        // The native uint16_t count comes back unchanged, refused or not.
        Check(GoldenH2Markers(object, capacity, withMarkers, untouched) == goldenCount && untouched && !trackingReads &&
                  !aimCalls && goldenMarkerCalls == 1,
            "M3 golden 2/5: each H2 gate refuses alone, before the tail, leaving the native marker bytes and count");
        Check(g_halo2Muzzle.refused == 1 && !g_halo2Muzzle.applied && !g_halo2Muzzle.callbacks &&
                  GoldenShotScopeWhole() && g_halo2MuzzleRequest.lease == &goldenLease && !goldenLease.active,
            "M3 golden 2/7: an H2 gate refusal counts once, balances and restores the shot scope whole");
        exclusive_input::active = false;
        g_halo2Dual.enabled = true;
        g_halo2Dual.faulted = false;
    }
    // No lease or another caller: no transaction, nothing counted.
    ResetGolden();
    g_halo2MuzzleRequest.lease = nullptr;
    Check(GoldenH2Markers(fixturePrimary, 64, true, untouched) == 1 && untouched && !trackingReads &&
              !g_halo2Muzzle.refused && !g_halo2Muzzle.applied && GoldenShotScopeWhole(),
        "M3: no H2 muzzle lease, no transaction");
    ResetGolden();
    Check(GoldenH2Markers(fixturePrimary, 64, true, untouched, 0x8E4BB0) == 1 && untouched && !trackingReads &&
              !g_halo2Muzzle.refused && !g_halo2Muzzle.applied,
        "M3: another H2 marker caller is never a transaction");
    // Golden 7: another actor's shot (a weapon the local unit
    // does not hold) passes the gate and is refused by the tail (slot). It is
    // restored whole but not counted: refused counts the player's own shots.
    ResetGolden();
    trackingValid = true;
    g_halo2MuzzleRequest.weapon = 0x99990009;
    Check(GoldenH2Markers(0x99990009, 64, true, untouched) == 1 && untouched && trackingReads == 1 && !aimCalls &&
              !g_halo2Muzzle.refused && !g_halo2Muzzle.applied && GoldenShotScopeWhole() && !goldenLease.active,
        "D6 G1: another actor's refused H2 shot is restored whole and not counted");
    // The player test faults: not the player's, so not counted; the
    // restore still runs and the feature is not faulted.
    ResetGolden();
    ownedUnitFault = true;
    Check(GoldenH2Markers(fixturePrimary, 64, true, untouched) == 1 && untouched && trackingReads == 1 &&
              !g_halo2Muzzle.refused && !g_halo2Muzzle.applied && !g_halo2Muzzle.faulted.load() &&
              !g_halo2Muzzle.callbacks && GoldenShotScopeWhole() && !goldenLease.active,
        "D6 G3: a faulting H2 player test counts nothing and still restores");
    ownedUnitFault = false;
    // Golden 6: the preflight runs under H2's own flag, restored to its previous
    // value, with the other title's flag and the callback count left alone.
    ResetMuzzle();
    const auto applied = g_halo2Muzzle.applied.load();
    g_halo2CollisionOwnQuery = true;
    g_legacyCollisionOwnedQuery = true;
    preflightOtherFlag = false;
    preflightCallbacks = 0;
    ShootMuzzle();
    Check(g_halo2Muzzle.applied == applied + 1 && preflightOtherFlag && preflightCallbacks == 1 &&
              g_halo2CollisionOwnQuery && g_legacyCollisionOwnedQuery && !g_halo2Muzzle.callbacks,
        "M3 golden 6: H2 preflight sets only its own flag, restores it, inside one counted callback");
    g_halo2CollisionOwnQuery = false;
    g_legacyCollisionOwnedQuery = false;
    // The markers detour, not the shared
    // body, counts the callback. Called directly with the count at 1, the body
    // sees 1 inside (at the preflight) and leaves 1: it takes no count and
    // releases none. The detour's own __finally releases it. With no native
    // query the acquire refuses, after the preflight.
    ResetGolden();
    trackingValid = true;
    g_aimAssistOriginal = 0;
    g_halo2Muzzle.callbacks = 1;
    preflightCallbacks = 0;
    std::memset(goldenBuffer, 0x5A, sizeof goldenBuffer);
    LegacyMuzzleAfterMarkers<Halo2MuzzleShots>(
        g_halo2Muzzle.base + 0x8E4BAF, fixturePrimary, uint16_t(1), goldenBuffer, int16_t(64));
    Check(preflightCallbacks == 1 && aimCalls == 1 && g_halo2Muzzle.callbacks == 1 && g_halo2Muzzle.refused == 1 &&
              !g_halo2Muzzle.applied && GoldenShotScopeWhole(),
        "M3: the shared H2 markers body adds no count and releases none");
    g_halo2Muzzle.callbacks = 0;
    g_aimAssistOriginal = reinterpret_cast<uintptr_t>(&NativeQuery);
    g_config.gun_barrel_aim = false;
    g_halo2Muzzle.enabled = false;
    trackingValid = true;
}
