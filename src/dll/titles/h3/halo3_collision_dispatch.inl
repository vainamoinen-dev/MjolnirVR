// Halo 3 world-collision scheduler detour body: runs Halo's own collision query, then the mod's
// optional contact work. A fault in Halo's original query is never turned into a miss.
#pragma once
// Included once, by src/dll/core/game.cpp; also compiled by
//   tests/halo3_collision_dispatch_tests.cpp, which supplies its own versions. Not a standalone
//   header: it uses what the host defines above its include line, namely
//   Halo3RedirectContactVector, LegacyCollisionTestVectorFn, LegacyWorldCollisionFeature,
//   LegacyWorldCollisionTick, g_halo3WorldCollision, g_legacyCollisionOwnedQuery.
uint8_t LegacyCollisionSchedulerDetourBody(LegacyWorldCollisionFeature& feature, uint64_t flags, int32_t mode,
    const float* start, const float* desired, int32_t ignoredObjectA, int32_t ignoredObjectB, int32_t ignoredObjectC,
    void* result)
{
    LegacyCollisionTestVectorFn original = reinterpret_cast<LegacyCollisionTestVectorFn>(feature.original);
    if (!original)
        return 0;
    const uint8_t nativeResult =
        original(flags, mode, start, desired, ignoredObjectA, ignoredObjectB, ignoredObjectC, result);
    if (!g_legacyCollisionOwnedQuery)
    {
        feature.engineCalls.fetch_add(1, std::memory_order_relaxed);
        // Only our extra contact work is optional. A fault in Halo's
        // original query above must reach its caller, not become a miss
        // with a partially populated collision result.
        __try
        {
            LegacyWorldCollisionTick(feature);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("the Halo 3 world collision tick faulted");
            feature.failures.fetch_add(1, std::memory_order_relaxed);
            feature.installed.store(false, std::memory_order_release);
        }
    }
    return nativeResult;
}

__declspec(noinline) uint8_t __fastcall Halo3CollisionResolveDetour(uint64_t flags, int32_t mode, const float* start,
    const float* desired, int32_t ignoredObjectA, int32_t ignoredObjectB, int32_t ignoredObjectC, void* result)
{
    g_halo3WorldCollision.callbacks.fetch_add(1, std::memory_order_acq_rel);
    uint8_t nativeResult = 0;
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    __try
    {
        if (!Halo3RedirectContactVector(
                caller, flags, mode, ignoredObjectA, ignoredObjectB, ignoredObjectC, result, nativeResult))
            nativeResult = LegacyCollisionSchedulerDetourBody(g_halo3WorldCollision, flags, mode, start, desired,
                ignoredObjectA, ignoredObjectB, ignoredObjectC, result);
    }
    __finally
    {
        g_halo3WorldCollision.callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return nativeResult;
}
