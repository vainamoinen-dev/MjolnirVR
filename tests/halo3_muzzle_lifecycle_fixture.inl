// Test fixture included by halo3_independent_shots_tests.cpp: it includes the production
// src/dll/titles/h3/halo3_muzzle_lifecycle.inl and checks install, busy retire and cleanup lines
// against fake MinHook and binding scans. Test-only: never in the DLL.
static bool muzzleHookExists = false, muzzleHookEnabled = false, muzzleIngress = false;
static int muzzleLifecycleFailure = 0, muzzleScan = 0;
static uintptr_t muzzleFixtureBase = 0;
static char muzzleLog[256]{};
static std::atomic<bool> g_halo3MuzzleBindingsReady{false};
namespace MuzzleSig
{
    static uintptr_t Find(uintptr_t base, size_t, const char*)
    {
        const uint32_t rvas[]{0x343D74, 0x2C24AC, 0x2C250F};
        const int index = muzzleScan++ / 2;
        Check(index < 3, "bounded H3 marker and FP ownership proof inventory");
        if (muzzleLifecycleFailure == 1)
            return 0;
        if (base == muzzleFixtureBase)
            return base + rvas[index];
        return muzzleLifecycleFailure == 2 ? base + 0x100 : 0;
    }
}
static MH_STATUS MuzzleCreate(void* target, void* detour, void** original)
{
    Check(target == reinterpret_cast<void*>(muzzleFixtureBase + 0x343D74) &&
              detour == reinterpret_cast<void*>(&Halo3MuzzleMarkersDetour),
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
    Check(!muzzleHookEnabled && !muzzleIngress && !g_halo3Muzzle.callbacks,
        "marker trampoline only freed after complete drain");
    if (muzzleLifecycleFailure == 6)
        return MH_ERROR_MEMORY_PROTECT;
    muzzleHookExists = false;
    return MH_OK;
}
static bool MuzzleQuiescence(
    const void* const* functions, const void* const* originals, size_t count, const std::atomic<uint32_t>& callbacks)
{
    Check(count == 1 && functions[0] == reinterpret_cast<void*>(&Halo3MuzzleMarkersDetour) &&
              originals[0] == reinterpret_cast<void*>(0x9000),
        "marker retirement protects exact detour and trampoline");
    return !callbacks.load() && !muzzleIngress;
}
#define sig MuzzleSig
#define MCCVR_DisableHookForRetirement MuzzleDisable
#define WaitForNativeDetourQuiescence MuzzleQuiescence
#define MH_CreateHook MuzzleCreate
#define MCCVR_CreateHookChecked MuzzleCreate
#define MH_EnableHook MuzzleEnable
#define MH_RemoveHook MuzzleRemove
#define MCCVR_RemoveHookChecked MuzzleRemove
#define InstallHalo3Muzzle FixtureInstallMuzzle
#define RemoveHalo3Muzzle FixtureRemoveMuzzle
#define LOG(...) (++logged, (void)std::snprintf(muzzleLog, sizeof muzzleLog, __VA_ARGS__))
#include "../src/dll/titles/h3/halo3_muzzle_lifecycle.inl"
#undef sig
#undef MCCVR_DisableHookForRetirement
#undef WaitForNativeDetourQuiescence
#undef MH_CreateHook
#undef MCCVR_CreateHookChecked
#undef MH_EnableHook
#undef MH_RemoveHook
#undef MCCVR_RemoveHookChecked
#undef InstallHalo3Muzzle
#undef RemoveHalo3Muzzle
#undef LOG
static void MuzzleLifecycleTests()
{
    constexpr size_t size = 0x900000;
    auto* bytes = static_cast<uint8_t*>(VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    Check(bytes != nullptr, "bounded native muzzle image fixture");
    muzzleFixtureBase = reinterpret_cast<uintptr_t>(bytes);
    for (const auto [call, target] : {std::pair{0x36864C, 0x343D74}})
    {
        bytes[call] = 0xE8;
        const int32_t delta = target - call - 5;
        std::memcpy(bytes + call + 1, &delta, 4);
    }
    g_halo3Dual.enabled = true;
    g_halo3Dual.generation = 7;
    g_halo3Muzzle.targets[0] = nullptr;
    g_halo3Muzzle.original = nullptr;
    for (int failure = 0; failure <= 8; ++failure)
    {
        muzzleLifecycleFailure = failure <= 4 ? failure : 0;
        muzzleScan = 0;
        const bool installed = FixtureInstallMuzzle(muzzleFixtureBase, size);
        Check(installed == g_halo3MuzzleBindingsReady.load() && installed == (failure == 0 || failure > 4),
            "optional marker install admission and creation failures");
        Check(g_halo3Dual.enabled.load(), "marker failure never disables independent firing");
        if (failure >= 5)
        {
            muzzleLifecycleFailure = failure;
            if (failure == 7)
                g_halo3Muzzle.callbacks = 1;
            if (failure == 8)
                muzzleIngress = true;
            Check(!FixtureRemoveMuzzle() && g_halo3Muzzle.targets[0] && g_halo3Muzzle.original && muzzleHookExists,
                "failed marker retirement retains target and native original");
        }
        muzzleLifecycleFailure = 0;
        muzzleIngress = false;
        g_halo3Muzzle.callbacks = 0;
        Check(FixtureRemoveMuzzle() && !g_halo3MuzzleBindingsReady.load() && !g_halo3Muzzle.targets[0] &&
                  !g_halo3Muzzle.original && !muzzleHookExists,
            "marker cleanup succeeds on retry");
    }
    // Goldens 1 and 8 (written on the older code): exact lines, the held set, busy retire,
    // and both of H3's publication flags withdrawn by remove.
    constexpr const char* kPrecondition = "Halo 3 muzzle StockFallback: firing unavailable or cleanup pending";
    g_config.gun_barrel_aim = true;
    muzzleScan = 0;
    logged = 0;
    Check(FixtureInstallMuzzle(muzzleFixtureBase, size) && logged == 1 &&
              !std::strcmp(muzzleLog,
                  "Halo 3 muzzle Installed: optional committed FP marker, native targeting/obstruction; option=1"),
        "M3 golden 1: exact H3 Installed line");
    Check(g_halo3Muzzle.enabled.load() && g_halo3MuzzleBindingsReady.load() &&
              g_halo3Muzzle.base == muzzleFixtureBase && g_halo3Muzzle.generation == 7 && !g_halo3Muzzle.faulted.load(),
        "M3 golden 8: H3 install stamps, enables and publishes");
    muzzleScan = 0;
    logged = 0;
    Check(!FixtureInstallMuzzle(muzzleFixtureBase, size) && logged == 1 && !muzzleScan &&
              !std::strcmp(muzzleLog, kPrecondition),
        "M3 golden 8: a held H3 set refuses a second install with its precondition line");
    g_halo3Muzzle.callbacks = 1;
    logged = 0;
    Check(!FixtureRemoveMuzzle() && logged >= 1 &&
              !std::strcmp(muzzleLog, "Halo 3 muzzle CleanupRequired: callbacks or ingress busy") && muzzleHookExists &&
              !g_halo3Muzzle.enabled.load() && !g_halo3MuzzleBindingsReady.load(),
        "M3 golden 1/8: exact H3 CleanupRequired busy line, both flags withdrawn");
    g_halo3Muzzle.callbacks = 0;
    Check(FixtureRemoveMuzzle() && !muzzleHookExists && !g_halo3Muzzle.enabled.load() &&
              !g_halo3MuzzleBindingsReady.load(),
        "M3 golden 8: busy H3 retire succeeds on retry, both flags false");
    // A refused disable names the MinHook status; the hook is kept and
    // the retry succeeds.
    muzzleScan = 0;
    Check(FixtureInstallMuzzle(muzzleFixtureBase, size), "D6: H3 reinstalled for the disable case");
    {
        char expected[128];
        std::snprintf(expected, sizeof expected, "Halo 3 muzzle CleanupRequired: disable failed (status %d)",
            int(MH_ERROR_MEMORY_PROTECT));
        muzzleLifecycleFailure = 5;
        logged = 0;
        Check(!FixtureRemoveMuzzle() && logged == 1 && !std::strcmp(muzzleLog, expected) && muzzleHookExists,
            "D6 G5: exact H3 disable-failed line with the status, hook kept");
        muzzleLifecycleFailure = 0;
        Check(FixtureRemoveMuzzle() && !muzzleHookExists, "D6 G5: H3 disable retry succeeds");
    }
    g_halo3Dual.enabled = false;
    muzzleScan = 0;
    logged = 0;
    Check(!FixtureInstallMuzzle(muzzleFixtureBase, size) && logged == 1 && !muzzleScan &&
              !std::strcmp(muzzleLog, kPrecondition),
        "M3 golden 1: H3 precondition line with the dual runtime off");
    g_halo3Dual.enabled = true;
    muzzleScan = 0;
    logged = 0;
    Check(!FixtureInstallMuzzle(muzzleFixtureBase, 0x368651) && logged == 1 && !muzzleScan &&
              !std::strcmp(muzzleLog, kPrecondition),
        "M3 golden 1: H3 precondition line for a short image");
    muzzleLifecycleFailure = 1;
    muzzleScan = 0;
    logged = 0;
    Check(!FixtureInstallMuzzle(muzzleFixtureBase, size) && logged == 1 && !muzzleHookExists &&
              !std::strcmp(muzzleLog, "Halo 3 muzzle StockFallback: missing/ambiguous binding +343D74"),
        "M3 golden 1: exact H3 missing-binding line");
    muzzleLifecycleFailure = 0;
    g_config.gun_barrel_aim = false;
    muzzleScan = 0;
    bytes[0x36864C] = 0x90;
    Check(!FixtureInstallMuzzle(muzzleFixtureBase, size) && g_halo3Dual.enabled.load(),
        "changed native edge refuses only muzzle feature");
    Check(VirtualFree(bytes, 0, MEM_RELEASE) != 0, "muzzle fixture allocation released");
}
