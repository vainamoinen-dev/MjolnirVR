// Golden: a point riding on the weapon keeps its offset in the weapon's frame.
// Tests weapon_frame_transfer.h (header-only); built as halomccvr_weapon_frame_transfer_tests.
// Pure logic: no game process.
#include "../src/common/weapon_frame_transfer.h"
#include <cmath>
#include <cstdio>

static int g_failures = 0;
static void Check(bool ok, const char* what)
{
    if (!ok)
    {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}
static bool Near(const float* a, float x, float y, float z)
{
    return std::fabs(a[0] - x) < 1e-4f && std::fabs(a[1] - y) < 1e-4f && std::fabs(a[2] - z) < 1e-4f;
}

int main()
{
    using namespace weapon_frame_transfer;
    const float I[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    // Yaw +90 deg about +Z: local +X (barrel) -> world +Y.
    const float Yaw90[9] = {0, -1, 0, 1, 0, 0, 0, 0, 1};
    const float stockT[3] = {10, 20, 30};
    const float muzzle[3] = {10.5f, 20, 30.1f}; // 0.5 ahead of the grip, 0.1 up
    float out[3];
    TransferPoint(I, stockT, I, stockT, muzzle, out);
    Check(Near(out, 10.5f, 20, 30.1f), "unchanged weapon: point unchanged");
    const float movedT[3] = {12, 18, 29};
    TransferPoint(I, stockT, I, movedT, muzzle, out);
    Check(Near(out, 12.5f, 18, 29.1f), "translated weapon, same angle: same offset");
    TransferPoint(I, stockT, Yaw90, movedT, muzzle, out);
    Check(Near(out, 12, 18.5f, 29.1f), "gun turned 90 deg from the view: the muzzle turns with it (was 12.5,18,29.1)");
    TransferPoint(Yaw90, stockT, Yaw90, movedT, muzzle, out);
    Check(Near(out, 12.5f, 18, 29.1f), "both frames turned alike: offset preserved");
    if (g_failures)
    {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("weapon_frame_transfer_tests: all passed\n");
    return 0;
}
