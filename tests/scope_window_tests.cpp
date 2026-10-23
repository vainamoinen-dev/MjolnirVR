// Scope window shape extraction from a mask image (opaque vignette, transparent window), plus the
// glass aspect, shift headroom and lens-profile rules that decide when the scope picture is shown.
// Tests scope_window_logic.h (header-only); built as halomccvr_scope_window_tests. Masks are synthetic.
#include "../src/common/scope_window_logic.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static int g_failures = 0;
static void Check(bool ok, const char* what)
{
    if (!ok)
    {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

// Window = superellipse |x/a|^n + |y/b|^n < 1 centred in a w x h image; n=2 ellipse/circle, large n = rounded rectangle.
static std::vector<uint8_t> Image(int w, int h, float a, float b, float n)
{
    std::vector<uint8_t> px(static_cast<size_t>(w) * h, 255);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const float dx = std::fabs((x + 0.5f - w / 2.0f) / a), dy = std::fabs((y + 0.5f - h / 2.0f) / b);
            if (std::pow(dx, n) + std::pow(dy, n) < 1.0f)
                px[static_cast<size_t>(y) * w + x] = 0;
        }
    return px;
}

int main()
{
    {
        // Halo 3 and ODST share model identities; their masks do not.
        const uint64_t battleRifle = 0x12040E16;
        Check(scope_window::MaskCacheKey(GameTitle::Halo3, battleRifle) !=
                      scope_window::MaskCacheKey(GameTitle::Halo3ODST, battleRifle) &&
                  scope_window::MaskCacheKey(GameTitle::Halo3, 0) == 0,
            "A2-08: the scope mask cache key includes the title");
    }
    using namespace scope_window;
    Window w;
    auto circle = Image(256, 256, 100, 100, 2);
    Check(Extract(circle.data(), 256, 256, w) && w.circle, "a round window is reported as a circle (shader draws it)");
    Check(std::fabs(w.aspect - 1.0f) < 0.02f, "circle aspect ~1");
    auto sniper = Image(400, 250, 180, 112, 6);
    Check(Extract(sniper.data(), 400, 250, w) && !w.circle, "a rounded rectangle is a mask");
    Check(std::fabs(w.aspect - 1.6f) < 0.05f, "sniper window aspect ~1.6");
    {
        // The CE sniper art (1024x512, window 1.61:1) fills the texture; the quad gives the shape.
        auto ce = Image(1024, 512, 386, 240, 8);
        Window cw;
        Check(
            Extract(ce.data(), 1024, 512, cw) && std::fabs(cw.aspect - 1.61f) < 0.03f, "CE sniper art window is ~1.61");
        const float natural = cw.aspect;
        Check(CeWideGlass(GameTitle::HaloCE, 1024, 512, natural), "CE sniper art is a wide glass");
        Check(!CeWideGlass(GameTitle::HaloCE, 512, 512, 1.0f) && !CeWideGlass(GameTitle::Halo3, 1024, 512, natural) &&
                  !CeWideGlass(GameTitle::HaloCE, 1024, 512, 0.8f) && !CeWideGlass(GameTitle::HaloCE, 1024, 512, NAN),
            "only landscape non-square CE masks are wide glass");
        Check(Extract(ce.data(), 1024, 512, cw, 1.0f / natural) && std::fabs(cw.aspect - 1.0f) < 0.01f && !cw.circle,
            "the wide CE window fills the square texture (no letterbox bars)");
        int open = 0, openY = 0;
        for (int i = 0; i < kSize; ++i)
        {
            open += cw.mask[(kSize / 2) * kSize + i] > 128;
            openY += cw.mask[i * kSize + kSize / 2] > 128;
        }
        Check(open > kSize * 85 / 100 && openY > kSize * 85 / 100,
            "the wide CE window is open across the texture both ways");
        Check(GlassAspect(true, 1.61f) == 1.61f && GlassAspect(false, 2.0f) == 1.0f &&
                  GlassAspect(true, 9.0f) == 4.0f && GlassAspect(true, 3.0f) == 3.0f &&
                  GlassAspect(true, 0.2f) == 1.0f && GlassAspect(true, NAN) == 1.0f,
            "glass aspect: configured, clamped, square otherwise");
        // The vertical view is unchanged: a glass wider than the 1.387 capture renders wider by glass/capture.
        Check(GlassTangentScale(1.0f, 1.387f) == 1.0f && GlassTangentScale(1.387f, 1.387f) == 1.0f &&
                  std::fabs(GlassTangentScale(1.61f, 1.387f) - 1.61f / 1.387f) < 1e-5f &&
                  GlassTangentScale(2.0f, NAN) == 1.0f,
            "tangent scale only when the glass is wider than the capture");
        Check(!Extract(ce.data(), 1024, 512, cw, NAN), "a bad pixel aspect is refused");
        // A picture rendered for the square glass waits; its own aspect or unknown is shown.
        Check(!GlassImageMatches(1.0f, 3.0f) && GlassImageMatches(3.0f, 3.0f) && GlassImageMatches(0.0f, 3.0f) &&
                  GlassImageMatches(3.004f, 3.0f),
            "z44: glass shows only a picture rendered for its aspect");
        // The sniper's glass waits while the assault rifle's profile is still applied.
        Check(!LensProfileReady(0x7560F22650441950ull, 0x93663163761AA577ull) &&
                  LensProfileReady(0x7560F22650441950ull, 0x7560F22650441950ull) &&
                  LensProfileReady(0, 0x93663163761AA577ull),
            "z43: lens quad only under the held gun's own profile");
        // Headroom for the largest sideways shift on a wide glass; none for square glass.
        Check(std::fabs(GlassShiftMargin(3.0f, 1.387f, 0.25f) - (1.0f + 0.5f / 3.0f)) < 1e-5f &&
                  GlassShiftMargin(1.0f, 1.387f, 0.25f) == 1.0f && GlassShiftMargin(3.0f, NAN, 0.25f) == 1.0f &&
                  std::fabs(GlassShiftMargin(3.0f, 1.387f, NAN) - (1.0f + 0.5f / 3.0f)) < 1e-5f,
            "z42: wide glass renders 1 + 2 cap/aspect wider; square glass unchanged");
        for (const float da : {1.2f, 1.61f, 3.0f, 4.0f})
            for (const float cap : {0.25f, 0.5f, 1.0f})
            {
                // Every glass aspect and cfg cap keeps the largest shift inside the capture on both axes.
                const float sa = 1.387f, m = GlassShiftMargin(da, sa, cap);
                const float sx = (sa > da ? da / sa : 1.0f) / m, sy = (sa > da ? 1.0f : sa / da) / m;
                Check(0.5f + (0.5f + cap / da) * sx <= 1.0f + 1e-5f && 0.5f + (0.5f + cap) * sy <= 1.0f + 1e-5f,
                    "z44: headroom on both axes for every aspect and cap");
            }
        {
            // The cropped, shifted sample stays inside [0,1] at the largest shift (shader maths).
            const float da = 3.0f, sa = 1.387f, cap = 0.25f, m = GlassShiftMargin(da, sa, cap);
            const float sx = 1.0f / m, sy = (sa / da) / m, su = cap / da, sv = cap;
            Check(0.5f + (0.5f + su) * sx <= 1.0f + 1e-5f && 0.5f + (0.5f + sv) * sy <= 1.0f,
                "z42: the largest shift samples inside the capture, both axes");
        }
        Check(CeWideGlassFallback(GameTitle::HaloCE, "sniper rifle") &&
                  !CeWideGlassFallback(GameTitle::HaloCE, "pistol") &&
                  !CeWideGlassFallback(GameTitle::Halo3, "sniper rifle") &&
                  !CeWideGlassFallback(GameTitle::HaloCE, nullptr),
            "z39: the CE sniper is a wide glass without its PNG; nothing else is");
        // At the 3:1 default the vertical view is unchanged (glass/capture); the strip shows 3/1.387 of the capture's width.
        Check(
            std::fabs(GlassTangentScale(3.0f, 1.387f) - 3.0f / 1.387f) < 1e-5f && GlassTangentScale(4.0f, 1.0f) == 4.0f,
            "z39: a 3:1 glass renders 2.16x wider and taller, cropped back to z35's vertical");
    }
    const int mid = kSize / 2;
    Check(w.mask[mid * kSize + mid] > 240, "window is open in the middle");
    Check(w.mask[0] == 0 && w.mask[kSize * kSize - 1] == 0 && w.mask[(kSize - 1) * kSize] == 0, "corners are closed");
    int row = 0, col = 0;
    for (int i = 0; i < kSize; ++i)
    {
        row += w.mask[mid * kSize + i] > 127;
        col += w.mask[i * kSize + mid] > 127;
    }
    Check(row > col * 1.4 && row >= kSize - 2, "wide window fills the width and is letterboxed vertically");
    auto tall = Image(200, 320, 90, 150, 4);
    Check(Extract(tall.data(), 200, 320, w) && w.aspect < 0.7f, "a tall window letterboxes horizontally");
    int tallCol = 0, tallRow = 0;
    for (int i = 0; i < kSize; ++i)
    {
        tallRow += w.mask[mid * kSize + i] > 127;
        tallCol += w.mask[i * kSize + mid] > 127;
    }
    Check(tallCol > tallRow * 1.4, "tall window is taller than wide in the mask");
    auto oval = Image(300, 200, 140, 90, 2);
    Check(Extract(oval.data(), 300, 200, w) && !w.circle, "an oval is not a circle");
    // Soft edges: a half-transparent rim stays between 0 and 255.
    auto soft = Image(256, 256, 100, 100, 6);
    for (auto& p : soft)
        if (p == 255)
            p = 200; // vignette only 78% opaque
    Check(Extract(soft.data(), 256, 256, w), "vignette that is not fully opaque still bounds the window");
    Check(std::strcmp(GameDir(GameTitle::Halo3ODST), "odst") == 0 && GameDir(GameTitle::Halo4) == nullptr &&
              GameDir(GameTitle::None) == nullptr,
        "ODST maps to the odst folder; Halo 4 has no mask folder");
    char base[32];
    Check(FileBase("fp_sniper_rifle", base, sizeof(base)) && !std::strcmp(base, "sniper_rifle"),
        "fp_ prefix is stripped");
    Check(
        FileBase("sniper rifle", base, sizeof(base)) && !std::strcmp(base, "sniper_rifle"), "CE names use underscores");
    Check(!FileBase("fp_averyveryveryverylongweaponname", base, 8) && !FileBase(nullptr, base, 8) &&
              !FileBase("fp_", base, 8),
        "bad names refused");
    // Kit shape: rectangular glass only.
    Check(KitShapeAspect(0.489f) == 0.489f && KitShapeAspect(1.0f) == 0 && KitShapeAspect(0.95f) == 0 &&
              KitShapeAspect(0) == 0 && KitShapeAspect(-1) == 0 && KitShapeAspect(NAN) == 0,
        "kit shape: only rectangular glass leaves the circle");
    // Shape names.
    {
        Window named;
        named.circle = true;
        Check(!std::strcmp(ShapeName(named), "circle"), "shape: circle");
        named.circle = false;
        named.fill = 0.92f;
        named.aspect = 2.0f;
        Check(!std::strcmp(ShapeName(named), "rounded-rect"), "shape: rounded-rect");
        named.fill = 0.785f;
        Check(!std::strcmp(ShapeName(named), "oval"), "shape: oval");
        named.fill = 0.75f;
        named.aspect = 1.15f;
        Check(!std::strcmp(ShapeName(named), "hex"), "shape: hex");
    }
    // Refusals.
    std::vector<uint8_t> opaque(64 * 64, 255);
    Check(!Extract(opaque.data(), 64, 64, w), "opaque centre has no window");
    std::vector<uint8_t> clear(64 * 64, 0);
    Check(Extract(clear.data(), 64, 64, w) && w.aspect > 0.99f, "a fully clear image is one window (no crash)");
    Check(!Extract(nullptr, 64, 64, w) && !Extract(clear.data(), 4, 4, w) && !Extract(clear.data(), 9000, 64, w),
        "bad input refused");
    std::printf("Scope window: %d failures\n", g_failures);
    return g_failures ? 1 : 0;
}
