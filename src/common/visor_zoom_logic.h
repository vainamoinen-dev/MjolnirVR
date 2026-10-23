#pragma once
// Visor zoom: the head-locked zoom window for guns without a real optic, its edges, reticle and magnification.
// Used by config.cpp and src/dll/core (menu.cpp, vr.cpp); tested by tests/visor_zoom_tests.cpp.
// Pure logic: no globals, no D3D, no OpenXR (the frame itself is drawn by the shader).
//
// Two zoom tiers (set from headset testing). Weapons with a real optic keep the
// gun-mounted look-through lens; weapons without one (for example the Halo CE
// pistol) get a "visor zoom": a head-locked window in the centre of view that
// shows the same zoomed render along the gun's aim, inside a procedural
// cyborg-HUD frame (border, corner brackets, reticle, tick ladders and a
// 7-segment magnification readout). The frame is drawn from SDF shapes in the
// shader; this header holds the parts that can be tested on the CPU.
//
// Pure logic: no globals, no D3D, no OpenXR.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace visor_zoom
{
    // What the weapon itself offers. Fed by weapon_profile::ZoomKindFor once the
    // profile rows carry it; Unknown until then.
    enum class Kind : uint8_t
    {
        Unknown,
        Optic,
        Visor
    };
    enum class Tier : uint8_t
    {
        Lens,
        Visor
    };

    // visor_zoom_mode: 0 = off (always the lens), 1 = by weapon (default),
    // 2 = every weapon (testing / preference).
    inline constexpr Tier Choose(Kind kind, int mode) noexcept
    {
        return mode == 2 ? Tier::Visor : (mode == 1 && kind == Kind::Visor ? Tier::Visor : Tier::Lens);
    }

    // Head-locked window layout in view space (metres; -z is forward).
    struct Layout
    {
        float x{}, y{}, z{}, width{}, height{}, yaw{};
    }; // yaw: radians about +Y, faces the eye
    inline constexpr float kAspect = 4.0f / 3.0f;
    // On the right-eye axis (half a typical IPD) at the default 0.40 m.
    inline constexpr float kDefaultOffsetX = 0.032f;
    inline Layout WindowLayout(float width, float distance, float offsetX, float offsetY) noexcept
    {
        const auto fin = [](float v, float lo, float hi, float d)
        { return std::isfinite(v) ? std::clamp(v, lo, hi) : d; };
        Layout l{};
        l.width = fin(width, 0.10f, 0.80f, 0.20f);
        l.height = l.width / kAspect;
        l.z = -fin(distance, 0.40f, 2.00f, 0.40f);
        l.x = fin(offsetX, -0.60f, 0.60f, kDefaultOffsetX);
        l.y = fin(offsetY, -0.40f, 0.40f, -0.02f);
        l.yaw = std::atan2(-l.x, -l.z); // turn the window toward the eye
        return l;
    }

    // ---- Window size, four edges and distance (set from headset testing) ----
    // The window is four signed edges in the window plane (metres from the
    // eye-forward axis; +x right, +y up), grown or shrunk by `scale` about its own
    // centre, at `distance`. The zoomed picture is anchored: its centre (the aim)
    // is drawn at the anchor point, and the edges only mask it, so moving an edge
    // shows or hides picture and never moves or rescales it. The zoom render
    // covers the smallest 4:3 "virtual window" centred on the anchor that
    // contains the edges, so every 4:3 rule above (ScopeZoomForWindow,
    // AimInWindow, CeVisorCrop) runs unchanged on the virtual width. With the
    // anchor at the centre of a 4:3 window (every migrated file), the virtual
    // window is the window itself.
    struct Edges
    {
        float left{}, right{}, top{}, bottom{};
    };
    // The farthest any old window reached: offset limit plus half the widest window
    // (0.60 + 0.40 across, 0.40 + 0.30 up), so every old window migrates exactly.
    inline constexpr float kEdgeLimitX = 1.00f, kEdgeLimitY = 0.70f;
    inline constexpr float kAnchorLimitX = 0.60f, kAnchorLimitY = 0.40f; // the old offset ranges
    inline constexpr float kEdgeMinGap = 0.02f;                          // the narrowest window side
    inline constexpr float kDistanceMin = 0.25f, kDistanceMax = 2.00f;
    inline constexpr float kScaleMin = 0.5f, kScaleMax = 3.0f;
    inline float FiniteOr(float v, float fallback) noexcept
    {
        return std::isfinite(v) ? v : fallback;
    }
    // Each edge inside its range, and each pair at least kEdgeMinGap apart
    // (a crossed pair is spread about its middle).
    inline Edges OrderedEdges(Edges e) noexcept
    {
        const auto pair = [](float& low, float& high, float limit)
        {
            low = std::clamp(low, -limit, limit);
            high = std::clamp(high, -limit, limit);
            if (high < low + kEdgeMinGap)
            {
                const float middle =
                    std::clamp((low + high) * 0.5f, -limit + kEdgeMinGap * 0.5f, limit - kEdgeMinGap * 0.5f);
                low = middle - kEdgeMinGap * 0.5f;
                high = middle + kEdgeMinGap * 0.5f;
            }
        };
        pair(e.left, e.right, kEdgeLimitX);
        pair(e.bottom, e.top, kEdgeLimitY);
        return e;
    }
    inline Edges ScaledEdges(Edges e, float scale) noexcept
    {
        const float s = std::clamp(FiniteOr(scale, 1.0f), kScaleMin, kScaleMax);
        const float cx = (e.left + e.right) * 0.5f, cy = (e.top + e.bottom) * 0.5f;
        return {cx + (e.left - cx) * s, cx + (e.right - cx) * s, cy + (e.top - cy) * s, cy + (e.bottom - cy) * s};
    }
    // The reticle's size in metres does not follow the edges: a 20th of
    // the unscaled default window's height (0.133 m wide, 4:3), whatever the edges.
    inline constexpr float kReticleWindowHeightM = 0.133f / kAspect;
    // The shader's reticle divisor: the virtual window's height over that height
    // (1.5, the old size multiplier, on the migrated default window).
    inline float ReticleDivisor(float virtualWidth) noexcept
    {
        return std::clamp(virtualWidth / kAspect / kReticleWindowHeightM, 0.5f, 10.0f);
    }
    struct EdgeWindow
    {
        Layout quad;                      // the visible window
        float virtualWidth{0.20f};        // the 4:3 virtual window the zoom render covers (height = width / kAspect)
        float u0{0}, v0{0}, u1{1}, v1{1}; // the visible part of the virtual window's image (v down)
        float planeX{}, planeY{};         // the window's centre in its own plane (edge units, before the yaw)
    };
    inline EdgeWindow WindowFromEdges(Edges edges, float scale, float distance, float anchorX, float anchorY) noexcept
    {
        const Edges e = ScaledEdges(OrderedEdges(edges), scale);
        const float ax = std::clamp(FiniteOr(anchorX, kDefaultOffsetX), -kAnchorLimitX, kAnchorLimitX);
        const float ay = std::clamp(FiniteOr(anchorY, -0.02f), -kAnchorLimitY, kAnchorLimitY);
        EdgeWindow w{};
        w.quad.width = e.right - e.left;
        w.quad.height = e.top - e.bottom;
        // The plane turns toward the eye through the anchor, so moving an edge
        // never re-angles it: the anchored picture stays put. The centre sits on
        // that plane, offset from the anchor along it.
        // On a migrated window the anchor is the centre, so this is WindowLayout's.
        const float d = std::clamp(FiniteOr(distance, 0.40f), kDistanceMin, kDistanceMax);
        w.planeX = (e.left + e.right) * 0.5f;
        w.planeY = (e.top + e.bottom) * 0.5f;
        w.quad.yaw = std::atan2(-ax, d);
        const float along = w.planeX - ax; // rotated about +Y by the yaw: (cos, 0, -sin)
        w.quad.x = ax + along * std::cos(w.quad.yaw);
        w.quad.y = w.planeY;
        w.quad.z = -d - along * std::sin(w.quad.yaw);
        const float halfX = std::max(ax - e.left, e.right - ax), halfY = std::max(ay - e.bottom, e.top - ay);
        w.virtualWidth = std::max(halfX * 2.0f, halfY * 2.0f * kAspect);
        const float virtualHeight = w.virtualWidth / kAspect;
        const float left = ax - w.virtualWidth * 0.5f, top = ay + virtualHeight * 0.5f;
        w.u0 = (e.left - left) / w.virtualWidth;
        w.u1 = (e.right - left) / w.virtualWidth;
        w.v0 = (top - e.top) / virtualHeight;
        w.v1 = (top - e.bottom) / virtualHeight;
        return w;
    }
    // v25 migration: the window today's keys draw (VisorWindowWidthM, then
    // WindowLayout's clamps), as edges at scale 1, with the anchor at its centre.
    struct MigratedWindow
    {
        Edges edges;
        float anchorX{}, anchorY{};
    };
    inline MigratedWindow EdgesFromOldWindow(float width, float scale, float offsetX, float offsetY) noexcept
    {
        const float w =
            std::clamp(FiniteOr(width, 0.133f) * std::clamp(FiniteOr(scale, 1.5f), 0.5f, 3.0f), 0.10f, 0.80f);
        const Layout old = WindowLayout(w, 0.40f, offsetX, offsetY);
        MigratedWindow m{};
        m.edges = {
            old.x - old.width * 0.5f, old.x + old.width * 0.5f, old.y + old.height * 0.5f, old.y - old.height * 0.5f};
        m.anchorX = old.x;
        m.anchorY = old.y;
        return m;
    }

    // Magnification readout "NN.N": tens (or blank), ones, tenths. Zoom is clamped
    // to 1.0-99.9 and rounded to one decimal.
    struct Readout
    {
        int tens{-1}, ones{}, tenths{};
    }; // tens -1 = blank
    inline Readout Digits(float zoom) noexcept
    {
        const float z = std::isfinite(zoom) ? std::clamp(zoom, 1.0f, 99.9f) : 1.0f;
        const int tenthsTotal = static_cast<int>(std::lround(z * 10.0f));
        Readout r{};
        r.tenths = tenthsTotal % 10;
        r.ones = (tenthsTotal / 10) % 10;
        const int tens = tenthsTotal / 100;
        r.tens = tens > 0 ? tens : -1;
        return r;
    }

    // 7-segment masks, bit order a b c d e f g (top, upper-right, lower-right,
    // bottom, lower-left, upper-left, middle). Mirrored in the visor shader.
    inline constexpr uint8_t kSegments[10]{0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    inline constexpr uint8_t SegmentsFor(int digit) noexcept
    {
        return digit >= 0 && digit <= 9 ? kSegments[digit] : 0;
    }

    // Edge softness (F1, 0..1): the share of the window/lens radius over which
    // the image fades out. The visor feathers fully; the lens gently (half).
    inline constexpr float kEdgeSoftnessDefault = 0.40f;
    inline float VisorFeather(float softness) noexcept
    {
        const float s = std::isfinite(softness) ? std::clamp(softness, 0.0f, 1.0f) : kEdgeSoftnessDefault;
        return 0.02f + 0.48f * s;
    } // 0 -> 2 % (just antialiased), default -> ~21 %, 1 -> 50 %
    inline float LensFeather(float softness) noexcept
    {
        return 0.5f * VisorFeather(softness) - 0.01f;
    }

    // "Raise to eye" for the visor: the same sighting detector as the lens, aimed
    // at a virtual sight point ahead of the grip, with relaxed defaults.
    inline constexpr float kVirtualSightAheadM = 0.25f;
    // Raise-to-eye for a visor-tier gun measures this virtual point only when the gun
    // has no anchored glass; an anchored glass (CE sniper in Anniversary) is measured
    // directly, as a lens gun's is.
    inline constexpr bool RaiseUsesVirtualSight(bool visorTier, bool lensAnchored) noexcept
    {
        return visorTier && !lensAnchored;
    }
    inline constexpr float kRaiseDistanceDefault = 0.30f, kRaiseAngleDefault = 20.0f;
    // ---- What the window shows (headset testing: a naive FOV looked zoomed OUT) ----
    // The window subtends a fixed angle (width at distance); a true Nx zoom must
    // show 1/N of that angle. The scope render's FOV comes from
    // ComputeScopeProjectionTangents (70 deg horizontal at 1x, vertical from the
    // source aspect), and the visor shader shows the centre of that image at the
    // window's 4:3 aspect. ScopeZoomForWindow returns the zoom to hand that
    // function so the window's own horizontal half-tangent becomes
    // (width/2/distance)/zoom - i.e. the window magnifies exactly `zoom` times.
    inline constexpr float kScopeBaseHorizontalTangent = 0.70020754f; // tan(35 deg), scope_logic.cpp
    inline float WindowHalfTangent(float width, float distance) noexcept
    {
        if (!std::isfinite(width) || !std::isfinite(distance) || width <= 0.0f || distance <= 0.0f)
            return 0.2f;
        return width * 0.5f / distance;
    }
    // Weapon profile zoom kind -> visor tier input (None and unknown weapons keep the lens).
    enum class ProfileZoom : uint8_t
    {
        None,
        Optic,
        Visor
    };
    inline constexpr Kind KindOf(ProfileZoom z) noexcept
    {
        return z == ProfileZoom::Visor ? Kind::Visor : z == ProfileZoom::Optic ? Kind::Optic : Kind::Unknown;
    }

    // CE has no second render: its visor is a crop of the eye image, which gets
    // soft past ~2x. ce_visor_zoom_max caps the magnification it shows (and reads
    // out); other titles are never capped. cap <= 1 or non-finite = no cap.
    inline float CappedZoom(float zoom, float cap) noexcept
    {
        const float z = std::isfinite(zoom) && zoom >= 1.0f ? zoom : 1.0f;
        return std::isfinite(cap) && cap > 1.0f ? std::min(z, cap) : z;
    }

    // One rule for an unknown zoom everywhere (crop, scope render, reticle): 1x.
    inline float SanitizeZoom(float zoom) noexcept
    {
        return std::isfinite(zoom) && zoom >= 1.0f ? zoom : 1.0f;
    }
    inline float ScopeZoomForWindow(float zoom, float sourceAspect, float width, float distance) noexcept
    {
        zoom = SanitizeZoom(zoom);
        if (!std::isfinite(sourceAspect) || sourceAspect < 0.5f || sourceAspect > 4.0f)
            sourceAspect = kAspect;
        const float windowH = WindowHalfTangent(width, distance) / zoom; // wanted horizontal half-tangent
        const float m = std::min(sourceAspect, 1.0f);
        // Scope image: V = (base/z)/m vertical, H = V*sa horizontal. The shader
        // keeps full height when the source is wider than 4:3 (window H = V*4/3),
        // otherwise full width (window H = H).
        const float zoomE = sourceAspect > kAspect ? kScopeBaseHorizontalTangent * kAspect / (windowH * m)
                                                   : kScopeBaseHorizontalTangent * sourceAspect / (windowH * m);
        return std::isfinite(zoomE) && zoomE >= 1.0f ? zoomE : 1.0f;
    }

    // Where the gun's aim point sits in the window, in the shader's units (window
    // height 1, centre 0, +x right, +y down). The window shows the head's view at
    // half-tangent windowH = WindowHalfTangent/zoom horizontally. `tangentX/Y` are
    // the aim point's tangents in the head frame (+x right, +y up). Returns false
    // when the point is outside the window (the reticle is hidden).
    inline bool AimInWindow(
        float tangentX, float tangentY, float zoom, float width, float distance, float& px, float& py) noexcept
    {
        zoom = SanitizeZoom(zoom);
        const float windowH = WindowHalfTangent(width, distance) / zoom, windowV = windowH / kAspect;
        if (!std::isfinite(tangentX) || !std::isfinite(tangentY))
            return false;
        px = tangentX / windowH * (kAspect * 0.5f);
        py = -tangentY / windowV * 0.5f;
        return std::fabs(px) <= kAspect * 0.5f && std::fabs(py) <= 0.5f;
    }

    // CE visor: a 4:3 crop of the eye image's centre. `coverX` is the eye's
    // horizontal half-tangent, texW x texH its size. The crop keeps
    // (WindowHalfTangent/zoom)/coverX of the eye's width, rounded to even pixels
    // (the old 32-px steps and 128-px floor capped real magnification near 2x).
    // achievedZoom is what the window really magnifies, for the reticle.
    struct CeCrop
    {
        uint32_t width{}, height{};
        float achievedZoom{1.0f};
    };
    inline constexpr uint32_t kCeCropMinPixels = 16;
    inline CeCrop CeVisorCrop(
        float zoom, float coverX, float width, float distance, uint32_t texW, uint32_t texH) noexcept
    {
        CeCrop c{};
        if (!std::isfinite(coverX) || !(coverX > 0.0f) || texW < kCeCropMinPixels || texH < kCeCropMinPixels)
            return c;
        const float wh = WindowHalfTangent(width, distance);
        const float crop = std::max(1.0f, coverX / (wh / SanitizeZoom(zoom)));
        const uint32_t maxW = texW & ~1u;
        uint32_t cropW = static_cast<uint32_t>(static_cast<float>(texW) / crop + 0.5f) & ~1u;
        cropW = std::clamp(cropW, kCeCropMinPixels, maxW);
        uint32_t cropH = (cropW * 3u / 4u) & ~1u;
        if (cropH > texH)
        {
            cropH = texH & ~1u;
            cropW = (cropH * 4u / 3u) & ~1u;
        }
        c.width = cropW;
        c.height = cropH;
        const float shown = coverX * static_cast<float>(cropW) / static_cast<float>(texW);
        const float achieved = shown > 0.0f ? wh / shown : 1.0f;
        c.achievedZoom = std::isfinite(achieved) && achieved > 0.0f ? achieved : 1.0f;
        return c;
    }

    // CE Anniversary agreed deviation: for the first boostFrames
    // samples after zoom-on, steal one frame in boostEvery so the zoom picture fills
    // in fast, then the normal one in normalEvery. boostFrames <= 0: no change.
    // boostEvery is kept in 2..normalEvery (never every frame, never slower).
    struct ZoomBoost
    {
        int remaining = 0;
        bool wasOn = false;
        // Returns the cadence for this sample; started is true on the zoom-on sample.
        unsigned Every(
            bool zoomOn, unsigned normalEvery, int boostFrames, int boostEvery, bool* started = nullptr) noexcept
        {
            if (started)
                *started = false;
            if (!zoomOn)
            {
                wasOn = false;
                remaining = 0;
                return normalEvery;
            }
            if (!wasOn)
            {
                wasOn = true;
                remaining = boostFrames > 0 ? boostFrames : 0;
                if (started)
                    *started = remaining > 0;
            }
            if (remaining <= 0)
                return normalEvery;
            --remaining;
            const int lo = 2, hi = normalEvery < 2u ? 2 : static_cast<int>(normalEvery);
            return static_cast<unsigned>(boostEvery < lo ? lo : boostEvery > hi ? hi : boostEvery);
        }
    };
} // namespace visor_zoom
