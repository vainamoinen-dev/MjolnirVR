// Tests for the Halo 4 helmet visor frame at its own size: viewport scale = visor / HUD per axis
// (src/common/titles/h4/halo4_helmet_shader_logic.h). Built as halomccvr_halo4_visor_tests and run by ctest.
// Header-only logic: no game process.
#include "../src/common/titles/h4/halo4_helmet_shader_logic.h"
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
    using halo4_helmet_shader::VisorViewportScale;
    float sx = 0, sy = 0;
    Check(!VisorViewportScale(0.43f, 1.22f, 0.0f, sx, sy) && sx == 1 && sy == 1, "0 = same as HUD: unchanged");
    Check(
        VisorViewportScale(0.43f, 1.0f, 0.86f, sx, sy) && Near(sx, 2.0f) && Near(sy, 2.0f), "visor twice the HUD size");
    Check(!VisorViewportScale(0.5f, 1.22f, 0.5f, sx, sy) && sx == 1 && sy == 1, "same size: no change");
    // Aspect applies to both, with the HUD's horizontal clamp.
    Check(VisorViewportScale(0.43f, 1.22f, 0.8f, sx, sy) && Near(sy, 0.8f / 0.43f) &&
              Near(sx, (0.8f * 1.22f > 1.0f ? 1.0f : 0.8f * 1.22f) / (0.43f * 1.22f)),
        "aspect and clamp per axis");
    Check(VisorViewportScale(0.43f, 1.0f, 5.0f, sx, sy) && Near(sy, 1.0f / 0.43f), "visor size clamped to 1.0");
    Check(VisorViewportScale(0.43f, 1.0f, 0.1f, sx, sy) && Near(sy, 0.30f / 0.43f), "visor size clamped to 0.30");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Check(!VisorViewportScale(nan, 1.0f, 0.8f, sx, sy) && !VisorViewportScale(0.43f, nan, 0.8f, sx, sy) &&
              !VisorViewportScale(0.43f, 1.0f, nan, sx, sy) && !VisorViewportScale(0.0f, 1.0f, 0.8f, sx, sy),
        "bad input leaves the draw alone");

    using halo4_helmet_shader::EffectiveVisorSize;
    Check(EffectiveVisorSize(true, 0.0f, 0.43f) == 0.0f && EffectiveVisorSize(true, 0.8f, 0.43f) == 0.0f,
        "follow HUD: unchanged draw");
    Check(Near(EffectiveVisorSize(false, 0.0f, 0.43f), 0.688f), "auto = 1.6x HUD");
    Check(Near(EffectiveVisorSize(false, 0.0f, 0.9f), 1.0f) && Near(EffectiveVisorSize(false, 0.0f, 0.1f), 0.30f),
        "auto clamps");
    Check(Near(EffectiveVisorSize(false, 0.55f, 0.43f), 0.55f), "explicit wins");
    Check(EffectiveVisorSize(false, 0.0f, 0.0f) == 0.0f, "no HUD size: leave alone");
    // the owner's cfg: hud 0.43, aspect 1.22, size 0 -> auto enlarges viewport and scissor by 1.6 on both axes.
    Check(VisorViewportScale(0.43f, 1.22f, EffectiveVisorSize(false, 0.0f, 0.43f), sx, sy) && Near(sx, 1.6f) &&
              Near(sy, 1.6f),
        "his config: frame enlarged 1.6x about the centre");

    if (g_failures)
    {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("halo4_visor_tests: all passed\n");
    return 0;
}
