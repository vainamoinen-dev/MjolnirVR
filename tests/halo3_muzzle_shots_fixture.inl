// Test fixture included by halo3_independent_shots_tests.cpp: the Halo 3 muzzle marker detour,
// its gates and refusal reasons, the obstruction preflight and the shot diagnostic.
// Test-only: never in the DLL.
static uint64_t muzzleCount = 1;
static unsigned markerCalls{}, muzzleFires{};
static bool expectMuzzle = true, muzzleNativeFault = false, muzzleNested = false, muzzlePreflightFault = false;
// Goldens 3 and 6: a non-finite preflight result; what the preflight saw.
static bool muzzlePreflightNan = false, preflightOtherFlag = false;
static uint32_t preflightCallbacks = 0;
static uint64_t NativeMarkers(
    uint32_t object, uint32_t name, void* data, int16_t capacity, uint8_t original, uint8_t interpolated)
{
    ++markerCalls;
    Check((object == fixturePrimary || object == secondary) && name == 0xD4 && capacity == 64 && original == 0xAA &&
              interpolated == 0xBB,
        "all six native H3 marker arguments forwarded");
    if (muzzleNativeFault)
        RaiseException(0xE0424444, 0, 0, nullptr);
    std::memset(data, 0x5A, 0x70);
    return muzzleCount;
}
static void NativeMuzzleAim(
    uint32_t unit, float* position, float* direction, uint64_t velocity, float* offset, uint8_t project, uint8_t use)
{
    ++aimCalls;
    if (velocity != 0xFEDCBA9876543210ull)
    {
        Check(g_legacyCollisionOwnedQuery, "H3 obstruction preflight cannot schedule collision/melee work");
        if (muzzlePreflightFault)
            RaiseException(0xE0425555, 0, 0, nullptr);
        preflightOtherFlag = g_halo2CollisionOwnQuery;
        preflightCallbacks = g_halo3Muzzle.callbacks.load();
        Check(unit == owner && velocity && !offset && !project && !use, "private H3 obstruction preflight ABI");
        Check(position[0] == 2 && position[1] == 3 && position[2] == 4 && direction[1] == 1,
            "native H3 clamp sees authored muzzle");
        auto* output = reinterpret_cast<float*>(velocity);
        output[0] = 10;
        output[1] = 20;
        output[2] = 30;
        if (muzzlePreflightNan)
            position[2] = std::nanf("");
        position[1] = 2.5f;
        return;
    }
    if (expectMuzzle)
    {
        Check(unit == owner && !offset && !project && !use && position[0] == 2 && position[1] == 2.5f &&
                  position[2] == 4 && direction[1] == 1,
            "ordinary H3 helper retains clipped origin without camera projection");
    }
    else
    {
        Check(offset && project == 1 && use == 1, "refused H3 muzzle leaves ordinary helper arguments native");
        position[0] = position[1] = position[2] = 0;
        direction[0] = 0;
        direction[1] = 1;
        direction[2] = 0;
    }
}
static uint64_t NativeMuzzleFire(uint32_t weapon, int16_t barrel, void* data, int32_t index, uint8_t predicted)
{
    ++muzzleFires;
    Check(barrel == 0 && data == reinterpret_cast<void*>(0x1234) && index == -7 && predicted == 0xFA,
        "H3 five-argument firing ABI preserved");
    alignas(float) uint8_t marker[0x70]{};
    fixtureCaller = imageBase + 0x368651;
    Check(Halo3MuzzleMarkersDetour(weapon, 0xD4, marker, 64, 0xAA, 0xBB) == muzzleCount,
        "full native marker return preserved");
    if (expectMuzzle)
    {
        const float* forward = reinterpret_cast<const float*>(marker + 0x3C);
        const float* up = reinterpret_cast<const float*>(marker + 0x54);
        const float* point = reinterpret_cast<const float*>(marker + 0x60);
        Check(forward[0] == 0 && forward[1] == 1 && up[2] == 1 && point[0] == 2 && point[1] == 2.5f && point[2] == 4,
            "H3 firing marker uses authored barrel and native obstruction result");
        for (unsigned i = 0; i < 0x3C; ++i)
            Check(marker[i] == 0x5A, "native H3 local marker and scale untouched");
        for (unsigned i = 0x6C; i < 0x70; ++i)
            Check(marker[i] == 0x5A, "native H3 marker flags untouched");
        Check(g_halo3IndependentShot.barrel && *reinterpret_cast<uint32_t*>(unitBytes + 0x21C) == 0x22220005,
            "H3 native homing reads per-muzzle acquired target");
    }
    else
        for (auto value : marker)
            Check(value == 0x5A, "refused H3 marker retains every byte");
    float point[3]{}, direction[3]{}, offset[3]{};
    fixtureCaller = imageBase + 0x368B97;
    Halo3IndependentAimDetour(owner, point, direction, 0xFEDCBA9876543210ull, offset, 1, 1);
    fixtureCaller = imageBase + 0x13BC0D;
    Check(Halo3IndependentCameraDetour(owner, point, direction) == 17, "H3 native assist perspective preserved");
    if (expectMuzzle)
        Check(point[0] == 2 && point[1] == 2.5f && point[2] == 4 && direction[1] == 1,
            "later H3 native assist uses clipped muzzle and barrel direction");
    if (muzzleNested && weapon == fixturePrimary)
    {
        const auto previous = g_halo3MuzzleRequest;
        Check(Halo3IndependentFireDetour(secondary, 0, data, -7, 0xFA) == 0x123456789ABCDEF0ull,
            "nested H3 return preserved");
        Check(g_halo3MuzzleRequest.weapon == fixturePrimary && g_halo3MuzzleRequest.lease == previous.lease &&
                  g_halo3IndependentShot.slot == 0 && g_halo3IndependentShot.barrel,
            "nested H3 restores outer muzzle scope");
    }
    return 0x123456789ABCDEF0ull;
}
static void ResetMuzzle(bool dual = false)
{
    Reset();
    g_config.gun_barrel_aim = true;
    g_config.independent_dual_aim = dual;
    g_config.left_handed = false;
    g_halo3Muzzle.base = imageBase;
    g_halo3Muzzle.generation = 7;
    g_halo3Muzzle.original = NativeMarkers;
    g_halo3Muzzle.enabled = true;
    g_halo3Muzzle.faulted = false;
    g_halo3MuzzleRequest = {};
    g_halo3Dual.fireOriginal = NativeMuzzleFire;
    g_halo3Dual.aimOriginal = NativeMuzzleAim;
    markerCalls = muzzleFires = queryCalls = 0;
    muzzleCount = 1;
    expectMuzzle = true;
    muzzleNativeFault = muzzleNested = muzzlePreflightFault = false;
    for (uint8_t slot = 0; slot < 2; ++slot)
    {
        weapon_muzzle::Palette sample{};
        sample.barrels[0] = {GameTitle::Halo3, 7, owner, slot ? secondary : fixturePrimary, 1, 3, 11, GetTickCount64(),
            1000000000, slot, 0, false, {{2, 3, 4}, {0, 1, 0}, {0, 0, 1}}};
        Check(g_halo3Muzzles.Publish(GameTitle::Halo3, slot, sample), "H3 committed muzzle fixture");
    }
    if (!dual)
        liveWeapons[1] = UINT32_MAX;
}
static uint64_t ShootMuzzle()
{
    return Halo3IndependentFireDetour(fixturePrimary, 0, reinterpret_cast<void*>(0x1234), -7, 0xFA);
}
static bool FaultingMuzzle()
{
    __try
    {
        (void)ShootMuzzle();
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
    Check(ShootMuzzle() == 0x123456789ABCDEF0ull, "muzzle preserves H3 native return");
    Check(markerCalls == 1 && queryCalls == 1 && muzzleFires == 1 &&
              *reinterpret_cast<uint32_t*>(unitBytes + 0x21C) == 0x44440007,
        "single H3 weapon muzzle query and target restoration");
    ResetMuzzle(true);
    muzzleNested = true;
    (void)ShootMuzzle();
    Check(muzzleFires == 2 && queryCalls == 4 && *reinterpret_cast<uint32_t*>(unitBytes + 0x21C) == 0x44440007,
        "nested H3 dual and muzzle target leases restore in reverse order");
    for (int reason = 0; reason < 11; ++reason)
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
            g_halo3Muzzle.enabled = false;
            break;
        case 3:
            g_halo3Muzzle.faulted = true;
            break;
        case 4:
            g_halo3Muzzle.generation = 8;
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
        }
        (void)ShootMuzzle();
        Check(queryCalls == 0 && muzzleFires == 1, "unavailable H3 muzzle preserves native firing");
        exclusive_input::active = false;
    }
    ResetMuzzle();
    expectMuzzle = false;
    queryFault = true;
    (void)ShootMuzzle();
    Check(g_halo3Muzzle.faulted && !g_halo3Dual.faulted && muzzleFires == 1,
        "H3 muzzle acquisition failure stays isolated");
    ResetMuzzle();
    expectMuzzle = false;
    muzzlePreflightFault = true;
    (void)ShootMuzzle();
    Check(g_halo3Muzzle.faulted && !g_halo3Dual.faulted && !g_legacyCollisionOwnedQuery && queryCalls == 0 &&
              muzzleFires == 1,
        "H3 failed obstruction preflight restores scheduler scope and forwards native fire once");
    ResetMuzzle();
    muzzleNativeFault = true;
    Check(FaultingMuzzle() && markerCalls == 1 && muzzleFires == 1 && !g_halo3Muzzle.callbacks &&
              !g_halo3Dual.callbacks && !g_halo3MuzzleRequest.lease && !g_legacyCollisionOwnedQuery,
        "H3 native marker exception propagates once with balanced scopes");
    g_config.gun_barrel_aim = false;
    g_halo3Muzzle.enabled = false;
}

// Goldens 3-7, written on the older code before the move into shared code. The markers detour
// is driven directly, so every gate, count, capacity and lease state is
// reachable. A private native stub fills the buffer with 0x5A and returns
// goldenCount. Each refusal is identified by its exact reason string.
static uint64_t goldenCount = 1;
static unsigned goldenMarkerCalls{};
static NativeShotTargetLease<0x24> goldenLease{};
static alignas(float) uint8_t goldenBuffer[0x70]{};
static uint64_t GoldenMarkers(
    uint32_t, uint32_t name, void* data, int16_t capacity, uint8_t original, uint8_t interpolated)
{
    ++goldenMarkerCalls;
    Check(name == 0xD4 && original == 0xAA && interpolated == 0xBB, "golden H3 marker arguments forwarded");
    if (data && capacity > 0)
        std::memset(data, 0x5A, 0x70);
    return goldenCount;
}
static uint64_t GoldenH3Markers(
    uint32_t object, int16_t capacity, bool withMarkers, bool& untouched, uintptr_t at = 0x368651)
{
    std::memset(goldenBuffer, 0x5A, sizeof goldenBuffer);
    fixtureCaller = imageBase + at;
    const auto result =
        Halo3MuzzleMarkersDetour(object, 0xD4, withMarkers ? goldenBuffer : nullptr, capacity, 0xAA, 0xBB);
    untouched = true;
    for (auto value : goldenBuffer)
        untouched = untouched && value == 0x5A;
    return result;
}
static void ResetGolden()
{
    ResetMuzzle();
    g_halo3Muzzle.original = GoldenMarkers;
    goldenCount = 1;
    goldenMarkerCalls = 0;
    g_halo3Muzzle.applied = 0;
    g_halo3Muzzle.refused = 0;
    goldenLease = {};
    g_halo3MuzzleRequest = {fixturePrimary, 0, 0, &goldenLease};
    g_halo3IndependentShot = {true, false, true, owner, 1, {1, 2, 3}, {0, 0, 1}, false};
    g_halo3MuzzleRefusal = "none";
    aimCalls = 0;
    muzzlePreflightNan = false;
    diagnosticCalls = 0;
    diagnosticApplied = false;
    diagnosticWhy = nullptr;
    diagnosticMarkers = nullptr;
}
static bool GoldenShotScopeWhole()
{
    const auto& shot = g_halo3IndependentShot;
    return shot.active && !shot.query && shot.queryCameraApplied && shot.unit == owner && shot.slot == 1 &&
           shot.position[0] == 1 && shot.position[1] == 2 && shot.position[2] == 3 && shot.direction[0] == 0 &&
           shot.direction[1] == 0 && shot.direction[2] == 1 && !shot.barrel;
}
static void MuzzleGoldenTests()
{
    bool untouched = false;
    for (int gate = 0; gate < 33; ++gate)
    {
        ResetGolden();
        uint32_t object = fixturePrimary;
        int16_t capacity = 64;
        bool withMarkers = true, preflight = false;
        const char* why = nullptr;
        switch (gate)
        {
        case 0:
            g_halo3Muzzle.enabled = false;
            why = "feature-off-or-faulted";
            break;
        case 1:
            g_halo3Muzzle.faulted = true;
            why = "feature-off-or-faulted";
            break;
        case 2:
            g_config.gun_barrel_aim = false;
            why = "feature-off-or-faulted";
            break;
        case 3:
            g_halo3Dual.enabled = false;
            why = "feature-off-or-faulted";
            break;
        case 4:
            g_halo3Dual.faulted = true;
            why = "feature-off-or-faulted";
            break;
        case 5:
            exclusive_input::active = true;
            why = "exclusive-input";
            break;
        case 6:
            g_vrAim = false;
            why = "vr-aim-or-stereo-off";
            break;
        case 7:
            g_enabled = false;
            why = "vr-aim-or-stereo-off";
            break;
        case 8:
            stereo = false;
            why = "vr-aim-or-stereo-off";
            break;
        case 9:
            title = GameTitle::Halo2;
            why = "vr-aim-or-stereo-off";
            break;
        case 10:
            g_halo3Muzzle.generation = 8;
            why = "generation";
            break;
        case 11:
            goldenLease.active = true;
            why = "lease";
            break;
        case 12:
            g_halo3MuzzleRequest.barrel = -1;
            why = "barrel-or-object";
            break;
        case 13:
            g_halo3MuzzleRequest.barrel = 2;
            why = "barrel-or-object";
            break;
        case 14:
            object = secondary;
            why = "barrel-or-object";
            break;
        // H3 admits the firing weapon only, never its parent unit.
        case 15:
            object = owner;
            why = "barrel-or-object";
            break;
        case 16:
            goldenCount = 0;
            why = "marker-count";
            break;
        case 17:
            goldenCount = 2;
            why = "marker-count";
            break;
        case 18:
            goldenCount = 0x10001;
            why = "marker-count";
            break;
        // A uint64_t count is never truncated (its low 32 bits are 1).
        case 19:
            goldenCount = 0x100000001ull;
            why = "marker-count";
            break;
        case 20:
            capacity = 0;
            why = "marker-count";
            break;
        case 21:
            withMarkers = false;
            why = "marker-count";
            break;
        case 22:
            cinematic = true;
            why = "cinematic";
            break;
        case 23:
            validTracking = false;
            why = "weapons-or-tracking";
            break;
        case 24:
            owned = false;
            why = "weapons-or-tracking";
            break;
        // Golden 7: another actor's shot (a weapon the local unit does not hold).
        case 25:
            g_halo3MuzzleRequest.weapon = object = 0x99990009;
            why = "slot";
            break;
        case 26:
            fixtureTracking.referenceEpoch = 4;
            why = "muzzle-receipt-stale-or-missing";
            break;
        case 27:
            g_config.left_handed = true;
            why = "muzzle-receipt-stale-or-missing";
            break;
        case 28:
            g_halo3Dual.aimOriginal = nullptr;
            why = "aim-or-target-storage";
            break;
        case 29:
            seated = true;
            why = "aim-or-target-storage";
            break;
        case 30:
            muzzlePreflightNan = true;
            preflight = true;
            why = "non-finite";
            break;
        case 31:
            g_halo3Dual.queryOriginal = nullptr;
            preflight = true;
            why = "shot-lease";
            break;
        case 32:
            muzzlePreflightFault = true;
            preflight = true;
            why = "fault";
            break;
        }
        const auto result = GoldenH3Markers(object, capacity, withMarkers, untouched);
        // The native uint64_t count comes back unchanged.
        Check(result == goldenCount && untouched && goldenMarkerCalls == 1 && !std::strcmp(g_halo3MuzzleRefusal, why),
            "M3 golden 3/5: each H3 refusal leaves the native marker bytes and count, with its exact reason");
        Check(aimCalls == (preflight ? 1u : 0u) && g_halo3Muzzle.faulted.load() == (gate == 1 || gate == 32) &&
                  !g_legacyCollisionOwnedQuery,
            "M3 golden 3: only the late H3 refusals run the preflight; only an exception marks the fault");
        // Every refusal restores whole; only the player's own
        // shot counts (24: the player's weapons cannot be read; 25: another actor).
        const bool player = gate != 24 && gate != 25;
        Check(g_halo3Muzzle.refused == (player ? 1u : 0u) && !g_halo3Muzzle.applied && !g_halo3Muzzle.callbacks &&
                  GoldenShotScopeWhole() && g_halo3MuzzleRequest.lease == &goldenLease && !goldenLease.active,
            "D6 G1/G2: an H3 refusal restores the shot scope whole and counts only the player's own");
        // Golden 7: the diagnostic runs only for the player's own weapons, after
        // the bookkeeping, with the refusal reason, and markers only for a result.
        const void* markers = goldenCount >= 1 && capacity >= 1 && withMarkers ? goldenBuffer : nullptr;
        Check(diagnosticCalls == (player ? 1u : 0u) &&
                  (!player || (!diagnosticApplied && diagnosticWhy && !std::strcmp(diagnosticWhy, why) &&
                                  diagnosticMarkers == markers)),
            "M3 golden 7: H3 diagnostic for the player's own refused shot only, with reason and markers");
        exclusive_input::active = false;
        g_vrAim = true;
        g_enabled = true;
        g_halo3Dual.enabled = true;
        g_halo3Dual.faulted = false;
        g_halo3Dual.queryOriginal = NativeQuery;
        muzzlePreflightFault = false;
    }
    // The player test faults: not the player's, so neither counted nor
    // passed to the diagnostic; the restore still runs and nothing is faulted.
    // The first run counts the getter's calls; the second faults the last one.
    ResetGolden();
    g_halo3Dual.queryOriginal = nullptr;
    playerCalls = 0;
    (void)GoldenH3Markers(fixturePrimary, 64, true, untouched);
    const int calls = playerCalls;
    Check(calls >= 1 && g_halo3Muzzle.refused == 1, "D6 G3: the H3 transaction reads the player getter");
    // ResetGolden runs a real query, so the query original comes back first.
    g_halo3Dual.queryOriginal = NativeQuery;
    ResetGolden();
    g_halo3Dual.queryOriginal = nullptr;
    playerCalls = 0;
    playerFaultAt = calls;
    Check(GoldenH3Markers(fixturePrimary, 64, true, untouched) == 1 && untouched && !g_halo3Muzzle.refused &&
              !g_halo3Muzzle.applied && !g_halo3Muzzle.faulted.load() && !diagnosticCalls && !g_halo3Muzzle.callbacks &&
              GoldenShotScopeWhole() && !goldenLease.active,
        "D6 G3: a faulting H3 player test counts nothing, logs nothing and still restores");
    playerFaultAt = 0;
    g_halo3Dual.queryOriginal = NativeQuery;
    // No lease or another caller: no transaction, no reason, nothing counted.
    ResetGolden();
    g_halo3MuzzleRequest.lease = nullptr;
    Check(GoldenH3Markers(fixturePrimary, 64, true, untouched) == 1 && untouched && !g_halo3Muzzle.refused &&
              !g_halo3Muzzle.applied && !std::strcmp(g_halo3MuzzleRefusal, "none") && !diagnosticCalls &&
              GoldenShotScopeWhole(),
        "M3: no H3 muzzle lease, no transaction");
    ResetGolden();
    Check(GoldenH3Markers(fixturePrimary, 64, true, untouched, 0x368652) == 1 && untouched && !g_halo3Muzzle.refused &&
              !std::strcmp(g_halo3MuzzleRefusal, "none") && !diagnosticCalls,
        "M3: another H3 marker caller is never a transaction");
    // Golden 7 on the firing path: an applied player shot reports no reason and
    // its markers; a native result of 0 reports marker-count and no markers.
    ResetMuzzle();
    diagnosticCalls = 0;
    (void)ShootMuzzle();
    Check(diagnosticCalls == 1 && diagnosticApplied && diagnosticWhy && !*diagnosticWhy && diagnosticMarkers,
        "M3 golden 7: an applied H3 player shot reaches the diagnostic with no reason and its markers");
    ResetMuzzle();
    expectMuzzle = false;
    muzzleCount = 0;
    diagnosticCalls = 0;
    (void)ShootMuzzle();
    Check(
        diagnosticCalls == 1 && !diagnosticApplied && !std::strcmp(diagnosticWhy, "marker-count") && !diagnosticMarkers,
        "M3 golden 7: a zero native result reaches the diagnostic with no markers");
    // Golden 6: the preflight runs under H3's own flag, restored to its previous
    // value, with H2's flag and the callback count left alone.
    ResetMuzzle();
    const auto applied = g_halo3Muzzle.applied.load();
    g_legacyCollisionOwnedQuery = true;
    g_halo2CollisionOwnQuery = true;
    preflightOtherFlag = false;
    preflightCallbacks = 0;
    (void)ShootMuzzle();
    Check(g_halo3Muzzle.applied == applied + 1 && preflightOtherFlag && preflightCallbacks == 1 &&
              g_legacyCollisionOwnedQuery && g_halo2CollisionOwnQuery && !g_halo3Muzzle.callbacks,
        "M3 golden 6: H3 preflight sets only its own flag, restores it, inside one counted callback");
    g_legacyCollisionOwnedQuery = false;
    g_halo2CollisionOwnQuery = false;
    // The markers detour, not the shared
    // body, counts the callback. Called directly with the count at 1, the body
    // sees 1 inside (at the preflight) and leaves 1: it takes no count and
    // releases none. The detour's own __finally releases it.
    ResetGolden();
    g_halo3Muzzle.callbacks = 1;
    preflightCallbacks = 0;
    g_halo3Dual.queryOriginal = nullptr;
    std::memset(goldenBuffer, 0x5A, sizeof goldenBuffer);
    LegacyMuzzleAfterMarkers<Halo3MuzzleShots>(
        imageBase + 0x368651, fixturePrimary, uint64_t(1), goldenBuffer, int16_t(64));
    Check(preflightCallbacks == 1 && g_halo3Muzzle.callbacks == 1 && !std::strcmp(g_halo3MuzzleRefusal, "shot-lease") &&
              g_halo3Muzzle.refused == 1 && GoldenShotScopeWhole(),
        "M3: the shared H3 markers body adds no count and releases none");
    g_halo3Muzzle.callbacks = 0;
    g_halo3Dual.queryOriginal = NativeQuery;
    g_config.gun_barrel_aim = false;
    g_halo3Muzzle.enabled = false;
}
