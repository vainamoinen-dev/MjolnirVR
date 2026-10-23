// Golden tests for the unmapped first-person rig log keys (src/common/fp_rig_coverage_logic.h):
// each new rig is logged once. Built as halomccvr_fp_rig_coverage_tests and run by ctest.
// Header-only logic: no game process.
#include "../src/common/fp_rig_coverage_logic.h"
#include <cstdio>

using namespace fp_rig_coverage;
namespace
{
    unsigned checks{}, failures{};
    void Check(bool ok, const char* message)
    {
        ++checks;
        if (!ok)
        {
            ++failures;
            std::printf("FAIL: %s\n", message);
        }
    }
}

int main()
{
    const uint64_t h4 = PackRig(GameTitle::Halo4, 92, 0x150D0001u);
    Check(RigTitle(h4) == GameTitle::Halo4 && RigNodeCount(h4) == 92 && RigId(h4) == 0x150D0001u, "rig key round trip");
    const int32_t a[3]{0, 1, 2}, b[3]{0, 2, 1};
    Check(BoneMapFingerprint(a, 3) != BoneMapFingerprint(b, 3) && BoneMapFingerprint(a, 3) == BoneMapFingerprint(a, 3),
        "bone-map fingerprint separates same-size rigs");
    UnmappedRigLog log;
    uint64_t key = 0;
    Check(!log.TakeNew(key), "nothing noted, nothing logged");
    log.Note(h4);
    Check(log.TakeNew(key) && key == h4, "a new rig logs once");
    log.Note(h4);
    Check(!log.TakeNew(key), "the same rig never logs twice");
    log.Note(PackRig(GameTitle::HaloReach, 47, BoneMapFingerprint(a, 3)));
    Check(log.TakeNew(key) && RigTitle(key) == GameTitle::HaloReach && RigNodeCount(key) == 47, "a second rig logs");
    log.Reset();
    log.Note(h4);
    Check(log.TakeNew(key), "reset (new map) logs it again");
    std::printf("fp rig coverage: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
