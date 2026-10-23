// Tests for the Halo 2 per-element HUD: labelled gameplay shader hashes -> elements, and the
// manual viewport offset (HUD pixels at 720 lines, +x right, +y up).
// Built as halomccvr_halo2_hud_element_tests and run by ctest; header-only
// src/common/titles/h2/halo2_hud_shader_logic.h. Pure logic: no game process.
#include "../src/common/titles/h2/halo2_hud_shader_logic.h"
#include <cmath>
#include <cstdio>
#include <limits>

static int g_failures = 0;
static void Check(bool ok, const char* what)
{
    if (!ok)
    {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}
static bool Near(float a, float b)
{
    return std::fabs(a - b) < 1e-4f;
}

int main()
{
    using namespace halo2_hud_shader;
    using hud_element::Element;
    Check(ElementForHash(0x417f1685ec6744aaULL) == hud_element::Shield &&
              ElementForHash(0x50e9b7e9476762fcULL) == hud_element::Shield,
        "shield and shield meter");
    Check(ElementForHash(0x5138655c43bc2879ULL) == hud_element::Weapon &&
              ElementForHash(0x740e59c9d34791b5ULL) == hud_element::Weapon &&
              ElementForHash(0xbdfb9aa0140f7f34ULL) == hud_element::Weapon,
        "ammo, weapon icons, round icon");
    Check(ElementForHash(0xc7f2063e1d1d87dfULL) == hud_element::Motion, "motion sensor");
    Check(ElementForHash(0xf972418445d6e438ULL) == hud_element::Messages, "pickup notification");
    Check(ElementForHash(kCrosshairHash) == -1, "crosshair never an element");
    int labelled = 0;
    for (uint64_t hash : kGameplayHudHashes)
    {
        Check(Classify(hash) == Role::GameplayHud, "every element hash is a gameplay HUD shader");
        labelled += ElementForHash(hash) >= 0;
    }
    Check(labelled == 7, "seven labelled shaders get elements; unlabelled MP/CUI stay whole-HUD");
    Check(ElementForHash(0x1234) == -1, "unknown hash");

    float dx = 0, dy = 0;
    Check(ElementViewportOffset(10, 20, 720, 300, dx, dy) && Near(dx, 10) && Near(dy, -20), "720-line viewport");
    Check(ElementViewportOffset(10, 20, 1440, 300, dx, dy) && Near(dx, 20) && Near(dy, -40), "scales with height");
    Check(ElementViewportOffset(1000, -1000, 720, 300, dx, dy) && Near(dx, 300) && Near(dy, 300), "clamped");
    Check(!ElementViewportOffset(0, 0, 720, 300, dx, dy) && dx == 0 && dy == 0, "zero offset: no change");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Check(!ElementViewportOffset(nan, 0, 720, 300, dx, dy) && !ElementViewportOffset(1, 1, 0, 300, dx, dy) &&
              !ElementViewportOffset(1, 1, nan, 300, dx, dy),
        "bad input refused");

    if (g_failures)
    {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("halo2_hud_element_tests: all passed\n");
    return 0;
}
