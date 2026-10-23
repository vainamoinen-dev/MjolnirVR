#pragma once
// Included once, by src/dll/titles/ce/haloce_first_person.cpp. Not a standalone header: it
//   uses what the host defines above its include line, namely LocalOnFootShooter, ModernRayFn.
// CE muzzle shots: the native aim-query, marker and fire hooks for gun-barrel aim.
void CaptureMuzzleQuery(int32_t input, int16_t zoom, bool direct)
{
    HaloCELocalPlayerState state{};
    RenderContext context{};
    if (muzzleInstalled.load() && !muzzleFaulted.load() && HaloCEControls_GetLocalPlayerState(state) &&
        input == state.inputUser && LocalOnFootShooter(state.unit) && HaloCE_GetGameplayContext(context) &&
        context.tracking.controllers.gunBarrelAim)
        muzzleQueryContext = {generation.load(), state.unit, input, zoom, direct, GetTickCount64()};
}
__declspec(noinline) void __fastcall MuzzleQueryHook(int32_t input, int16_t zoom, Vec3* control, void* target)
{
    callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        if (!muzzleQueryHook.original)
            __leave;
        reinterpret_cast<MuzzleQueryFn>(muzzleQueryHook.original)(input, zoom, control, target);
        CaptureMuzzleQuery(input, zoom, false);
    }
    __finally
    {
        callbacks.fetch_sub(1, std::memory_order_release);
    }
}
__declspec(noinline) void __fastcall MuzzleDirectQueryHook(int32_t input, int16_t zoom, void* target)
{
    callbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        if (!muzzleDirectQueryHook.original)
            __leave;
        reinterpret_cast<MuzzleDirectQueryFn>(muzzleDirectQueryHook.original)(input, zoom, target);
        CaptureMuzzleQuery(input, zoom, true);
    }
    __finally
    {
        callbacks.fetch_sub(1, std::memory_order_release);
    }
}
bool ApplyMuzzleMarker(uint32_t object, int16_t count, void* markers, int16_t capacity)
{
    const auto request = muzzleRequest;
    const auto query = muzzleQueryContext;
    const uint64_t now = GetTickCount64();
    if (!request.lease || request.lease->active || request.replicated || object != request.weapon || count != 1 ||
        capacity < 1 || !markers || query.generation != generation.load() || !query.at ||
        query.at < muzzleInstalledAt || now < query.at || now - query.at > 100 || !modernRayHook.original ||
        !targetInstalled.load() || targetRetiring.load())
        return false;
    weapon_muzzle::Receipt receipt{};
    if (!ReadMuzzle(query.unit, request.weapon, request.barrel, receipt))
        return false;
    void* storage = MuzzleTargetStorage(query.unit);
    if (!storage)
        return false;
    auto& shot = muzzleShot;
    shot = {};
    shot.active = true;
    shot.unit = query.unit;
    std::memcpy(&shot.position, receipt.ray.position, 12);
    std::memcpy(&shot.direction, receipt.ray.direction, 12);
    Vec3 velocity{};
    // HCEEK 8CD510 / retail B00880: native obstruction and velocity output,
    // with native camera projection and weapon offset disabled for this ray.
    reinterpret_cast<ModernRayFn>(modernRayHook.original)(
        query.unit, &shot.position, &shot.direction, &velocity, nullptr, false, false);
    if (!Finite(shot.position) || !Finite(shot.direction))
        return false;
    alignas(8) unsigned char target[0x10]{};
    Vec3 control{};
    shot.query = true;
    __try
    {
        if (query.direct)
        {
            if (!muzzleDirectQueryHook.original)
                __leave;
            reinterpret_cast<MuzzleDirectQueryFn>(muzzleDirectQueryHook.original)(query.input, query.zoom, target);
        }
        else
        {
            if (!muzzleQueryHook.original)
                __leave;
            reinterpret_cast<MuzzleQueryFn>(muzzleQueryHook.original)(query.input, query.zoom, &control, target);
        }
    }
    __finally
    {
        shot.query = false;
    }
    if (!shot.queryApplied || !MuzzleOwner(query.unit, request.weapon) || query.generation != generation.load() ||
        MuzzleTargetStorage(query.unit) != storage || !std::isfinite(*reinterpret_cast<float*>(target)))
        return false;
    // Native query constructs its complete16-byte result, including no-target.
    if (!request.lease->Apply(query.generation, query.unit, storage, target))
        return false;
    // Own CE0x6C record: node/pad0, local4, world38; no later-title tail flags.
    weapon_muzzle::WriteMarkerWorldFrame(markers, &shot.direction.x, receipt.ray.up, &shot.position.x);
    return true;
}
// Scope-camera diagnostic (headset testing found the CE zoom camera misplaced): per local player shot (hip and
// scoped), whether the barrel was applied and, while scoped, the angle between the
// scope camera actually built (<=250 ms old; receipt or aim) and the direction the
// shot leaves (marker world frame +0x3C, origin +0x60), split into yaw/pitch, and how
// far the shot origin sits off the camera axis. At most 4 lines a second. Game thread.
void CeScopedShotDiagnostic(uint32_t object, bool shotApplied, int16_t count, const void* markers) noexcept
{
    static ULONGLONG shotSecond = 0;
    static unsigned shotLines = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - shotSecond >= 1000)
    {
        shotSecond = now;
        shotLines = 0;
    }
    // Only the local player's weapon: AI fire takes leases too and must not
    // spend the budget or be compared with the player's scope camera.
    HaloCELocalPlayerState local{};
    if (count != 1 || !markers || !HaloCEControls_GetLocalPlayerState(local) || local.weapon != object)
        return;
    if (++shotLines > 4)
        return;
    float shotDir[3]{}, shotPos[3]{};
    std::memcpy(shotDir, static_cast<const uint8_t*>(markers) + 0x3C, 12);
    std::memcpy(shotPos, static_cast<const uint8_t*>(markers) + 0x60, 12);
    char frame[256];
    Game_FormatShotFrame(GameTitle::HaloCE, frame, sizeof frame);
    const bool scoped = VR_IsScopeActive();
    float cam[3]{}, fwd[3]{}, scale = 0;
    bool receipt = false;
    uint64_t builtMs = 0;
    const bool camera = scoped && VR_CeScopeCamera(cam, fwd, receipt, scale, builtMs) && now >= builtMs &&
                        now - builtMs <= 250 && std::isfinite(scale) && scale > 0;
    if (!camera)
    {
        LOG("CE shot: barrel %s; scoped=%d; camera=%s; origin %.3f %.3f %.3f, direction %.3f %.3f %.3f%s",
            shotApplied ? "applied" : "refused", scoped ? 1 : 0, scoped ? "not built" : "-", shotPos[0], shotPos[1],
            shotPos[2], shotDir[0], shotDir[1], shotDir[2], frame);
        return;
    }
    float yaw = 0, pitch = 0, total = 0;
    DirectionDeltaDegrees(fwd, shotDir, yaw, pitch, total);
    const float lf = std::sqrt(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
    float along = 0, off = 0;
    if (lf > 1e-5f)
    {
        float d[3];
        for (int i = 0; i < 3; ++i)
            d[i] = shotPos[i] - cam[i];
        along = (d[0] * fwd[0] + d[1] * fwd[1] + d[2] * fwd[2]) / lf;
        float o2 = 0;
        for (int i = 0; i < 3; ++i)
        {
            const float c = d[i] - fwd[i] / lf * along;
            o2 += c * c;
        }
        off = std::sqrt(o2) / scale;
        along /= scale;
    }
    LOG("CE shot: barrel %s; scoped=1; camera=%s (%llu ms old); camera vs shot direction %.2f deg (yaw %+.2f pitch %+.2f); "
        "shot origin %.3f m off the camera axis, %+.3f m along it; origin %.3f %.3f %.3f, direction %.3f %.3f %.3f%s",
        shotApplied ? "applied" : "refused", receipt ? "receipt" : "aim",
        static_cast<unsigned long long>(now - builtMs), total, yaw, pitch, off, along, shotPos[0], shotPos[1],
        shotPos[2], shotDir[0], shotDir[1], shotDir[2], frame);
}
int16_t MuzzleMarkersBody(uint32_t object, const char* name, void* markers, int16_t capacity, uintptr_t caller)
{
    if (!muzzleMarkersHook.original)
        return 0;
    const int16_t result =
        reinterpret_cast<MuzzleMarkersFn>(muzzleMarkersHook.original)(object, name, markers, capacity);
    if (caller != moduleBase + 0xB7A574 || !muzzleRequest.lease)
        return result;
    const auto previous = muzzleShot;
    bool shotApplied = false;
    __try
    {
        shotApplied = ApplyMuzzleMarker(object, result, markers, capacity);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("applying the CE muzzle marker faulted");
        muzzleFaulted = true;
    }
    if (shotApplied)
        ++muzzleApplied;
    else
    {
        RestoreMuzzleTarget(*muzzleRequest.lease);
        muzzleShot = previous;
        ++muzzleRefused;
    }
    __try
    {
        CeScopedShotDiagnostic(object, shotApplied, result, markers);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("the CE shot diagnostic faulted");
    }
    return result;
}
__declspec(noinline) int16_t __fastcall MuzzleMarkersHook(
    uint32_t object, const char* name, void* markers, int16_t capacity)
{
    callbacks.fetch_add(1, std::memory_order_acq_rel);
    int16_t result = 0;
    __try
    {
        result = MuzzleMarkersBody(object, name, markers, capacity, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    }
    __finally
    {
        callbacks.fetch_sub(1, std::memory_order_release);
    }
    return result;
}
__declspec(noinline) void __fastcall MuzzleFireHook(
    uint32_t weapon, int16_t barrel, uint32_t simulation, int16_t mode, const void* replicated, uint8_t predicted)
{
    callbacks.fetch_add(1, std::memory_order_acq_rel);
    const auto previousShot = muzzleShot;
    const auto previousRequest = muzzleRequest;
    NativeShotTargetLease<0x10> lease{};
    muzzleShot = {};
    muzzleRequest = {weapon, barrel, replicated != nullptr, &lease};
    __try
    {
        if (muzzleFireHook.original)
            reinterpret_cast<MuzzleFireFn>(muzzleFireHook.original)(
                weapon, barrel, simulation, mode, replicated, predicted);
    }
    __finally
    {
        RestoreMuzzleTarget(lease);
        muzzleShot = previousShot;
        muzzleRequest = previousRequest;
        callbacks.fetch_sub(1, std::memory_order_release);
    }
}
