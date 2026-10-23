#pragma once
// Scope glass window shapes: the window mask cut from a gun's scope art, and the size and shape of the lens quad.
// Used by src/dll/core/vr.cpp and glass_reprojection_logic.h; tested by tests/scope_window_tests.cpp.
// Pure logic: no globals, no file or GPU access (the caller loads the PNG and uploads the mask).
//
// Gun-specific scope window shape from the game's own full-screen scope art (the user's local copy of the mask PNG,
// never shipped in the repo). The mask is opaque vignette with a transparent window; the window is the transparent region
// connected to the centre. Extract() crops it to its bounding box and letterboxes it, at its own aspect, into a
// kSize x kSize A8 mask (255 = window) that the lens quad's square uv can sample directly.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include "runtime_types.h"

namespace scope_window
{
    // The scope window/mask cache key. Halo 3 and ODST share model
    // identities (fp_battle_rifle, fp_needler, fp_flak_cannon), so the title is part of it.
    inline uint64_t MaskCacheKey(GameTitle title, uint64_t identity) noexcept
    {
        return identity ? identity ^ (static_cast<uint64_t>(title) * 0x9E3779B97F4A7C15ull) : 0;
    }
    inline constexpr int kSize = 128;
    inline constexpr int kMaxSide = 4096;
    inline constexpr uint8_t kWindowAlphaBelow = 90; // 35% alpha

    struct Window
    {
        uint8_t mask[kSize * kSize]{};
        float aspect = 1.0f; // window width / height
        bool circle = false; // round enough that the analytic circle is as good (no mask needed)
        float fill = 0.0f;   // window area / its bounding box
    };

    // The scope quad log line's "shape=" name for a local mask: an ellipse fills pi/4 of its box, a
    // rounded rectangle more, a hexagon (3/4) or lobed shape less.
    inline const char* ShapeName(const Window& w) noexcept
    {
        if (w.circle)
            return "circle";
        if (w.fill > 0.81f)
            return "rounded-rect";
        if (w.fill >= 0.765f)
            return std::fabs(w.aspect - 1.0f) < 0.03f ? "circle" : "oval";
        return "hex";
    }

    // Headset testing showed the CE sniper glass as a square that was too thin; it needs to be
    // a longer rectangle. So a landscape
    // non-square CE mask (the 1024x512 sniper art, window 1.61:1) gets a non-square lens
    // quad: height = the square side as before, width = height x the configured aspect.
    // Its mask is extracted to fill the square texture (Extract at 1/natural), so the quad
    // alone sets the drawn window shape and no letterbox bars exist. True for those masks;
    // false for square CE masks (authored circles), other titles and portrait windows.
    inline bool CeWideGlass(GameTitle title, int w, int h, float naturalAspect) noexcept
    {
        return title == GameTitle::HaloCE && w > 0 && h > 0 && w != h && std::isfinite(naturalAspect) &&
               naturalAspect >= 1.0f && naturalAspect <= 10.0f;
    }
    // Headset testing: the CE sniper glass should be about 3x as wide as it is tall. Default 3.0, range 1.0-4.0.
    inline constexpr float kGlassAspectMax = 4.0f;
    // The lens quad's width/height for a wide CE glass (configured aspect; 1 = square).
    inline float GlassAspect(bool wide, float configured) noexcept
    {
        return wide && std::isfinite(configured) ? std::clamp(configured, 1.0f, kGlassAspectMax) : 1.0f;
    }
    // The CE sniper is a wide glass with or without its local mask PNG. With no PNG (and no
    // kit glass measurement: its kit row's glassAspect is 0) it gets a rounded rectangle that fills
    // the whole wide quad instead of a circle on a square quad.
    inline bool CeWideGlassFallback(GameTitle title, const char* model) noexcept
    {
        return title == GameTitle::HaloCE && model && !std::strcmp(model, "sniper rifle");
    }
    // The scope camera's vertical tangent factor for a glass of glassAspect fed from a
    // capture of sourceAspect: the picture is cropped at the glass aspect, so a glass wider
    // than the capture needs a wider render to keep the vertical view (and so the magnification).
    inline float GlassTangentScale(float glassAspect, float sourceAspect) noexcept
    {
        if (!std::isfinite(glassAspect) || !std::isfinite(sourceAspect) || sourceAspect < 0.5f || sourceAspect > 4.0f)
            return 1.0f;
        return std::max(1.0f, glassAspect / sourceAspect);
    }

    // Real-life rule: a real scope never shows a smeared or black side band.
    // A wide glass crops the full width of its capture, so any sideways parallax shift
    // read past the edge (in testing, the right ~22% clamped to streaks). The camera
    // renders this much wider (both axes, on top of GlassTangentScale) and the upload
    // crops back by it, so the view is unchanged and the largest shift (maxShift glass
    // heights = maxShift / aspect in width-uv) stays inside the image. 1 for square glass.
    inline float GlassShiftMargin(float glassAspect, float sourceAspect, float maxShift) noexcept
    {
        if (!std::isfinite(glassAspect) || glassAspect <= 1.0f || !std::isfinite(sourceAspect) || sourceAspect < 0.5f ||
            sourceAspect > 4.0f)
            return 1.0f;
        // The cfg's whole 0-1 cap range, and headroom on both axes (the vertical shift is
        // cap glass heights; a glass narrower than its capture crops less vertically).
        const float cap = std::isfinite(maxShift) ? std::clamp(maxShift, 0.0f, 1.0f) : 0.25f;
        const float cropX = std::min(1.0f, glassAspect / sourceAspect);
        const float cropY = std::min(1.0f, sourceAspect / glassAspect);
        return std::max({1.0f, cropX * (1.0f + 2.0f * cap / glassAspect), cropY * (1.0f + 2.0f * cap)});
    }

    // On switching back to the CE sniper, one "scope quad" was once submitted at
    // 0.477 x 0.159 m - the previous gun's global 0.159 width under the sniper's
    // 3:1 glass - 11-23 ms before the sniper's own lens row (0.040) was applied.
    // The lens quad is drawn only once the held gun's placement and width
    // are the applied profile: same identity, or no identity known (unchanged).
    inline bool LensProfileReady(uint64_t heldIdentity, uint64_t appliedIdentity) noexcept
    {
        return heldIdentity == 0 || heldIdentity == appliedIdentity;
    }

    // Real-life rule: a scope never jumps magnification. A CE glass image rendered
    // for another glass aspect (the first sample after a gun swap) is not shown; the glass
    // waits for an image rendered for its own aspect. 0 = unknown/not a glass render (shown).
    inline bool GlassImageMatches(float imageAspect, float glassAspect) noexcept
    {
        return !(std::isfinite(imageAspect) && imageAspect > 0.0f) || !std::isfinite(glassAspect) ||
               std::fabs(imageAspect - glassAspect) <= 0.01f;
    }

    // alpha: w*h bytes, row-major, 0 = transparent. False when the centre is not transparent, the window is tiny, or sizes are absurd.
    // pixelAspect: the drawn width/height of one texel (1/natural fills the texture: CeWideGlass).
    inline bool Extract(const uint8_t* alpha, int w, int h, Window& out, float pixelAspect = 1.0f)
    {
        if (!std::isfinite(pixelAspect) || pixelAspect < 0.1f || pixelAspect > 10.0f)
            return false;
        if (!alpha || w < 8 || h < 8 || w > kMaxSide || h > kMaxSide)
            return false;
        const int cx = w / 2, cy = h / 2;
        if (alpha[cy * w + cx] >= kWindowAlphaBelow)
            return false;
        std::vector<uint8_t> inside(static_cast<size_t>(w) * h, 0);
        std::vector<int> stack;
        stack.push_back(cy * w + cx);
        inside[cy * w + cx] = 1;
        int minX = cx, maxX = cx, minY = cy, maxY = cy;
        long long count = 0;
        while (!stack.empty())
        {
            const int i = stack.back();
            stack.pop_back();
            const int x = i % w, y = i / w;
            ++count;
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
            const int next[4] = {
                x > 0 ? i - 1 : -1, x + 1 < w ? i + 1 : -1, y > 0 ? i - w : -1, y + 1 < h ? i + w : -1};
            for (int n : next)
                if (n >= 0 && !inside[n] && alpha[n] < kWindowAlphaBelow)
                {
                    inside[n] = 1;
                    stack.push_back(n);
                }
        }
        const int bw = maxX - minX + 1, bh = maxY - minY + 1;
        if (bw < 8 || bh < 8)
            return false;
        const float aspect = static_cast<float>(bw) / static_cast<float>(bh) * pixelAspect;
        const float fill = static_cast<float>(count) / (static_cast<float>(bw) * static_cast<float>(bh));
        const int tw = aspect >= 1.0f ? kSize : std::max(1, static_cast<int>(std::lround(kSize * aspect)));
        const int th = aspect >= 1.0f ? std::max(1, static_cast<int>(std::lround(kSize / aspect))) : kSize;
        const int ox = (kSize - tw) / 2, oy = (kSize - th) / 2;
        Window result;
        for (int y = 0; y < th; ++y)
            for (int x = 0; x < tw; ++x)
            {
                // Area-average the source block this texel covers (window pixels only, weighted by how clear they are).
                const int x0 = minX + x * bw / tw, x1 = std::max(x0 + 1, minX + (x + 1) * bw / tw);
                const int y0 = minY + y * bh / th, y1 = std::max(y0 + 1, minY + (y + 1) * bh / th);
                long long sum = 0, n = 0;
                for (int sy = y0; sy < y1 && sy < h; ++sy)
                    for (int sx = x0; sx < x1 && sx < w; ++sx, ++n)
                        if (inside[static_cast<size_t>(sy) * w + sx])
                            sum += 255 - alpha[static_cast<size_t>(sy) * w + sx];
                result.mask[(oy + y) * kSize + ox + x] = n ? static_cast<uint8_t>(sum / n) : 0;
            }
        result.aspect = aspect;
        result.fill = fill;
        // A round window (fill ~ pi/4 of its box, square) is drawn by the shader's own feathered circle instead.
        result.circle = std::fabs(aspect - 1.0f) < 0.03f && std::fabs(fill - 0.7854f) < 0.03f;
        out = result;
        return true;
    }

    // Local asset layout: <mod dir>/assets/scope-masks/<game>/<weapon>.png (the user's own extracted game art; not in the repo).
    inline const char* GameDir(GameTitle title) noexcept
    {
        switch (title)
        {
        case GameTitle::Halo2:
            return "halo2";
        case GameTitle::Halo3:
            return "halo3";
        case GameTitle::Halo3ODST:
            return "odst";
        case GameTitle::HaloReach:
            return "reach";
        case GameTitle::HaloCE:
            return "haloce";
        default:
            return nullptr; // Halo 4's windows are coarse polyart: always the circle
        }
    }
    // Catalog model name ("fp_sniper_rifle", "sniper rifle") -> file base name ("sniper_rifle"). False when it does not fit.
    inline bool FileBase(const char* model, char* out, size_t capacity) noexcept
    {
        if (!model || !out || capacity < 2)
            return false;
        if (!std::strncmp(model, "fp_", 3))
            model += 3;
        size_t n = 0;
        for (; model[n]; ++n)
        {
            if (n + 1 >= capacity)
                return false;
            const char c = model[n];
            out[n] = c == ' ' ? '_' : c;
        }
        out[n] = 0;
        return n > 0;
    }

    // A gun with no local mask PNG still gets its own glass shape
    // from the kit: a rounded rectangle at the glass's short/long aspect, long side
    // across, letterboxed in the square quad like a mask. Near-square glass (and
    // unmeasured rows) stay the shader's circle: returns 0 then.
    inline constexpr float kKitShapeMaxAspect = 0.9f;
    inline float KitShapeAspect(float glassAspect) noexcept
    {
        return glassAspect > 0.05f && glassAspect < kKitShapeMaxAspect ? glassAspect : 0.0f;
    }
} // namespace scope_window
