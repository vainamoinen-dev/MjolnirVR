// Test fixture included by halo2_independent_shots_tests.cpp: it includes the production
// src/dll/titles/h2/halo2_muzzle_lifecycle.inl and checks install, busy retire and cleanup lines
// against fake MinHook and binding scans. Test-only: never in the DLL.
static bool muzzleHookExists = false, muzzleHookEnabled = false, muzzleIngress = false;
static int muzzleLifecycleFailure = 0, muzzleScan = 0;
static uintptr_t muzzleFixtureBase = 0;
static char muzzleLog[256]{};
static bool MuzzleCountPatternMatches(uintptr_t base, size_t, const char*, uintptr_t& match, uint32_t& count)
{
    const uint32_t rvas[]{0x8D6570, 0x8D11A0, 0x8E8990, 0x8E89E7};
    Check(muzzleScan < 4, "only proven muzzle/marker-owner bindings scanned");
    match = base + rvas[muzzleScan++];
    count = muzzleLifecycleFailure == 1 ? 0 : muzzleLifecycleFailure == 2 ? 2 : 1;
    return true;
}
static MH_STATUS MuzzleCreate(void* target, void* detour, void** original)
{
    Check(target == reinterpret_cast<void*>(muzzleFixtureBase + 0x8D6570) &&
              detour == reinterpret_cast<void*>(&Halo2MuzzleMarkersDetour),
        "only exact marker wrapper hooked");
    if (muzzleLifecycleFailure == 3)
        return MH_ERROR_MEMORY_ALLOC;
    muzzleHookExists = true;
    *original = reinterpret_cast<void*>(0x9000);
    return MH_OK;
}
static MH_STATUS MuzzleEnable(void*)
{
    if (muzzleLifecycleFailure == 4)
        return MH_ERROR_MEMORY_PROTECT;
    muzzleHookEnabled = true;
    return MH_OK;
}
static MH_STATUS MuzzleDisable(void*)
{
    if (muzzleLifecycleFailure == 5)
        return MH_ERROR_MEMORY_PROTECT;
    muzzleHookEnabled = false;
    return MH_OK;
}
static MH_STATUS MuzzleRemove(void*)
{
    Check(!muzzleHookEnabled && !muzzleIngress && !g_halo2Muzzle.callbacks,
        "marker trampoline only freed after complete drain");
    if (muzzleLifecycleFailure == 6)
        return MH_ERROR_MEMORY_PROTECT;
    muzzleHookExists = false;
    return MH_OK;
}
static bool MuzzleQuiescence(
    const void* const* functions, const void* const* originals, size_t count, const std::atomic<uint32_t>& callbacks)
{
    Check(count == 1 && functions[0] == reinterpret_cast<void*>(&Halo2MuzzleMarkersDetour) &&
              originals[0] == reinterpret_cast<void*>(0x9000),
        "marker retirement protects exact detour and trampoline");
    return !callbacks.load() && !muzzleIngress;
}
#define CountPatternMatches MuzzleCountPatternMatches
#define MCCVR_DisableHookForRetirement MuzzleDisable
#define WaitForNativeDetourQuiescence MuzzleQuiescence
#define MH_CreateHook MuzzleCreate
#define MCCVR_CreateHookChecked MuzzleCreate
#define MH_EnableHook MuzzleEnable
#define MH_RemoveHook MuzzleRemove
#define MCCVR_RemoveHookChecked MuzzleRemove
#define InstallHalo2Muzzle FixtureInstallMuzzle
#define RemoveHalo2Muzzle FixtureRemoveMuzzle
#define LOG(...) (++logged, (void)std::snprintf(muzzleLog, sizeof muzzleLog, __VA_ARGS__))
#include "../src/dll/titles/h2/halo2_muzzle_lifecycle.inl"
#undef CountPatternMatches
#undef MCCVR_DisableHookForRetirement
#undef WaitForNativeDetourQuiescence
#undef MH_CreateHook
#undef MCCVR_CreateHookChecked
#undef MH_EnableHook
#undef MH_RemoveHook
#undef MCCVR_RemoveHookChecked
#undef InstallHalo2Muzzle
#undef RemoveHalo2Muzzle
#undef LOG
static void MuzzleLifecycleTests()
{
    constexpr size_t size = 0x900000;
    auto* bytes = static_cast<uint8_t*>(VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    Check(bytes != nullptr, "bounded native muzzle image fixture");
    muzzleFixtureBase = reinterpret_cast<uintptr_t>(bytes);
    for (const auto [call, target] :
        {std::pair{0x8E4B94, 0x8E8990}, std::pair{0x8E4BAA, 0x8D6570}, std::pair{0x8D657E, 0x8D11A0}})
    {
        bytes[call] = 0xE8;
        const int32_t delta = target - call - 5;
        std::memcpy(bytes + call + 1, &delta, 4);
    }
    g_halo2Dual.enabled = true;
    g_halo2Dual.generation = 7;
    g_halo2Muzzle.targets[0] = nullptr;
    g_halo2Muzzle.original = nullptr;
    for (int failure = 0; failure <= 8; ++failure)
    {
        muzzleLifecycleFailure = failure <= 4 ? failure : 0;
        muzzleScan = 0;
        const bool installed = FixtureInstallMuzzle(muzzleFixtureBase, size);
        Check(installed == (failure == 0 || failure > 4), "optional marker install admission and creation failures");
        Check(g_halo2Dual.enabled.load(), "marker failure never disables independent firing");
        if (failure >= 5)
        {
            muzzleLifecycleFailure = failure;
            if (failure == 7)
                g_halo2Muzzle.callbacks = 1;
            if (failure == 8)
                muzzleIngress = true;
            Check(!FixtureRemoveMuzzle() && g_halo2Muzzle.targets[0] && g_halo2Muzzle.original && muzzleHookExists,
                "failed marker retirement retains target and native original");
        }
        muzzleLifecycleFailure = 0;
        muzzleIngress = false;
        g_halo2Muzzle.callbacks = 0;
        Check(FixtureRemoveMuzzle() && !g_halo2Muzzle.targets[0] && !g_halo2Muzzle.original && !muzzleHookExists,
            "marker cleanup succeeds on retry");
    }
    // Goldens 1 and 8 (written on the older code): exact lines, the held set, busy retire.
    constexpr const char* kPrecondition =
        "Halo 2 muzzle StockFallback: firing transaction unavailable or cleanup pending";
    g_config.gun_barrel_aim = true;
    muzzleScan = 0;
    logged = 0;
    Check(FixtureInstallMuzzle(muzzleFixtureBase, size) && logged == 1 &&
              !std::strcmp(muzzleLog,
                  "Halo 2 muzzle Installed: optional committed FP marker, native targeting/obstruction; option=1"),
        "M3 golden 1: exact H2 Installed line");
    Check(g_halo2Muzzle.enabled.load() && g_halo2Muzzle.base == muzzleFixtureBase && g_halo2Muzzle.generation == 7 &&
              !g_halo2Muzzle.faulted.load(),
        "M3 golden 8: H2 install stamps base and generation, then enables");
    muzzleScan = 0;
    logged = 0;
    Check(!FixtureInstallMuzzle(muzzleFixtureBase, size) && logged == 1 && !muzzleScan &&
              !std::strcmp(muzzleLog, kPrecondition),
        "M3 golden 8: a held H2 set refuses a second install with its precondition line");
    g_halo2Muzzle.callbacks = 1;
    logged = 0;
    Check(!FixtureRemoveMuzzle() && logged >= 1 &&
              !std::strcmp(muzzleLog, "Halo 2 muzzle CleanupRequired: callbacks or ingress busy") && muzzleHookExists &&
              !g_halo2Muzzle.enabled.load(),
        "M3 golden 1: exact H2 CleanupRequired busy line, enabled withdrawn");
    g_halo2Muzzle.callbacks = 0;
    Check(FixtureRemoveMuzzle() && !muzzleHookExists && !g_halo2Muzzle.enabled.load(),
        "M3 golden 8: busy H2 retire succeeds on retry");
    // A refused disable names the MinHook status; the hook is kept and
    // the retry succeeds.
    muzzleScan = 0;
    Check(FixtureInstallMuzzle(muzzleFixtureBase, size), "D6: H2 reinstalled for the disable case");
    {
        char expected[128];
        std::snprintf(expected, sizeof expected, "Halo 2 muzzle CleanupRequired: disable failed (status %d)",
            int(MH_ERROR_MEMORY_PROTECT));
        muzzleLifecycleFailure = 5;
        logged = 0;
        Check(!FixtureRemoveMuzzle() && logged == 1 && !std::strcmp(muzzleLog, expected) && muzzleHookExists,
            "D6 G5: exact H2 disable-failed line with the status, hook kept");
        muzzleLifecycleFailure = 0;
        Check(FixtureRemoveMuzzle() && !muzzleHookExists, "D6 G5: H2 disable retry succeeds");
    }
    g_halo2Dual.enabled = false;
    muzzleScan = 0;
    logged = 0;
    Check(!FixtureInstallMuzzle(muzzleFixtureBase, size) && logged == 1 && !muzzleScan &&
              !std::strcmp(muzzleLog, kPrecondition),
        "M3 golden 1: H2 precondition line with the dual runtime off");
    g_halo2Dual.enabled = true;
    muzzleScan = 0;
    logged = 0;
    Check(!FixtureInstallMuzzle(muzzleFixtureBase, 0x8E8A10) && logged == 1 && !muzzleScan &&
              !std::strcmp(muzzleLog, kPrecondition),
        "M3 golden 1: H2 precondition line for a short image");
    muzzleLifecycleFailure = 1;
    muzzleScan = 0;
    logged = 0;
    Check(!FixtureInstallMuzzle(muzzleFixtureBase, size) && logged == 1 && !muzzleHookExists &&
              !std::strcmp(muzzleLog, "Halo 2 muzzle StockFallback: missing/ambiguous marker binding +8D6570"),
        "M3 golden 1: exact H2 missing-binding line");
    muzzleLifecycleFailure = 0;
    g_config.gun_barrel_aim = false;
    muzzleScan = 0;
    bytes[0x8E4BAA] = 0x90;
    Check(!FixtureInstallMuzzle(muzzleFixtureBase, size) && g_halo2Dual.enabled.load(),
        "changed native edge refuses only muzzle feature");
    Check(VirtualFree(bytes, 0, MEM_RELEASE) != 0, "muzzle fixture allocation released");
}
