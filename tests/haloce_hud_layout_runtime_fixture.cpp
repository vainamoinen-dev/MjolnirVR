// Initialize only native-hook admission for the production runtime test.
// All numeric raster observation/replay/restoration remains production code.
// Linked into halomccvr_ce_runtime_tests (with haloce_runtime_tests.cpp); includes haloce_hud_layout.cpp.
#include "hook_checked_forwarders.h" // DLL hook sites call MinHook through the checked names
#include "../src/dll/titles/ce/haloce_hud_layout.cpp"

void ConfigureCeHudLayoutRuntimeFixture(uint32_t gen, bool enabledForTest, bool installedForTest)
{
    installed = enabledForTest && installedForTest;
    active = enabledForTest;
    retiring = false;
    generation = gen;
    observationAvailable = enabledForTest;
}
void RunCeHudGameplayRuntimeFixture(uintptr_t base, void(__fastcall* draw)())
{
    moduleBase = base;
    original = draw;
    MainBody();
}
