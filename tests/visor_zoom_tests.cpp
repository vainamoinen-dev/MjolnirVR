// Tests the visor zoom tier: its layout and config, raise-to-eye, zoomed aim smoothing, and how it
// hands over to the scope sight (visor_zoom_logic.h, scope_sight_logic.h, zoom_smoothing_logic.h).
// Compiled with scope_logic.cpp; built as halomccvr_visor_zoom_tests. No game process or headset needed.
#include "../src/common/visor_zoom_logic.h"
#include "../src/common/scope_sight_logic.h"
#include "../src/common/zoom_smoothing_logic.h"
#include "../src/common/scope_logic.h"
#include <cstdio>
#include <limits>

using namespace visor_zoom;
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
    {
        // The window magnifies exactly `zoom` times: what it shows horizontally
        // (after the shader's 4:3 centre crop) is its own half-angle / zoom.
        const float width = 0.30f, distance = 0.75f, wh = WindowHalfTangent(width, distance);
        bool exact = true;
        for (float sa : {0.9f, 1.0f, 1.25f, 16.0f / 9.0f})
            for (float zoom : {1.5f, 2.0f, 4.0f, 8.0f})
            {
                const ScopeProjectionTangents t =
                    ComputeScopeProjectionTangents(ScopeZoomForWindow(zoom, sa, width, distance), sa);
                const float shown = sa > kAspect ? t.vertical * kAspect : t.horizontal;
                exact = exact && std::fabs(shown - wh / zoom) < 1e-4f;
            }
        Check(exact, "visor window shows its own angle divided by the zoom (not the 70-degree scope / zoom)");
        float px = 0, py = 0;
        Check(AimInWindow(0, 0, 2, width, distance, px, py) && px == 0 && py == 0,
            "aim straight ahead sits at the window centre");
        Check(AimInWindow(wh / 4, 0, 2, width, distance, px, py) && std::fabs(px - kAspect * 0.25f) < 1e-5f,
            "aim half-way to the zoomed edge sits half-way to the window edge");
        Check(AimInWindow(0, wh / kAspect / 4, 2, width, distance, px, py) && std::fabs(py + 0.25f) < 1e-5f,
            "aim up moves the reticle up (shader y down)");
        Check(!AimInWindow(wh, 0, 2, width, distance, px, py), "aim outside the zoomed view hides the reticle");
    }
    {
        // CE crop: the window really magnifies the nominal zoom (old 128-px
        // floor capped it near 2x while the reticle was placed for 8x).
        const float width = 0.30f, distance = 0.75f, coverX = 1.2f;
        bool close = true, achievedExact = true;
        for (uint32_t texW : {1440u, 2064u, 2448u})
            for (float zoom : {1.5f, 2.0f, 4.0f, 6.0f, 8.0f})
            {
                const CeCrop c = CeVisorCrop(zoom, coverX, width, distance, texW, texW);
                close = close && c.width && c.height && std::fabs(c.achievedZoom / zoom - 1.0f) < 0.05f;
                const float shown = coverX * static_cast<float>(c.width) / static_cast<float>(texW);
                achievedExact =
                    achievedExact && std::fabs(c.achievedZoom - WindowHalfTangent(width, distance) / shown) < 1e-4f;
            }
        Check(close, "CE crop achieves the nominal zoom within 5% at 1.5-8x");
        Check(achievedExact, "CE achieved zoom is exactly what the crop shows");
        const CeCrop c8 = CeVisorCrop(8.0f, coverX, width, distance, 2064, 2096);
        Check(c8.width % 2 == 0 && c8.height % 2 == 0 && c8.height == (c8.width * 3 / 4 & ~1u),
            "CE crop is even-sized 4:3");
        const float nan = std::numeric_limits<float>::quiet_NaN();
        Check(CeVisorCrop(nan, coverX, width, distance, 2064, 2096).width ==
                  CeVisorCrop(1.0f, coverX, width, distance, 2064, 2096).width,
            "CE unknown zoom is 1x (same as reticle)");
        float ax = 0, ay = 0, bx = 0, by = 0;
        Check(AimInWindow(0.01f, 0, nan, width, distance, ax, ay) &&
                  AimInWindow(0.01f, 0, 1.0f, width, distance, bx, by) && ax == bx &&
                  ScopeZoomForWindow(nan, 1.0f, width, distance) == ScopeZoomForWindow(1.0f, 1.0f, width, distance),
            "reticle and scope render also treat unknown zoom as 1x");
        Check(CeVisorCrop(99.0f, coverX, width, distance, 2064, 2096).width == kCeCropMinPixels,
            "CE crop floor is 16 px");
        Check(CeVisorCrop(2.0f, 0.0f, width, distance, 2064, 2096).width == 0, "CE crop refuses a bad eye cover");
    }
    {
        // Weapon-profile kinds reach the tier choice for every title.
        Check(Choose(KindOf(ProfileZoom::Visor), 1) == Tier::Visor &&
                  Choose(KindOf(ProfileZoom::Optic), 1) == Tier::Lens &&
                  Choose(KindOf(ProfileZoom::None), 1) == Tier::Lens &&
                  Choose(KindOf(ProfileZoom::None), 2) == Tier::Visor,
            "visor weapons open the visor window, optic and unknown ones the lens, every-weapon mode all visor");
        // CE cap: crop, reticle and readout agree on the capped zoom.
        Check(CappedZoom(8.0f, 2.0f) == 2.0f && CappedZoom(1.5f, 2.0f) == 1.5f && CappedZoom(8.0f, 99.0f) == 8.0f &&
                  CappedZoom(std::numeric_limits<float>::quiet_NaN(), 2.0f) == 1.0f && CappedZoom(8.0f, 0.0f) == 8.0f,
            "CE zoom cap clamps, never raises, and no cap leaves the zoom alone");
        const CeCrop capped = CeVisorCrop(CappedZoom(8.0f, 2.0f), 1.2f, 0.30f, 0.75f, 2064, 2096);
        Check(
            std::fabs(capped.achievedZoom - 2.0f) < 0.1f, "a capped CE crop achieves the cap, which the reticle uses");
    }
    Check(Choose(Kind::Visor, 1) == Tier::Visor && Choose(Kind::Optic, 1) == Tier::Lens &&
              Choose(Kind::Unknown, 1) == Tier::Lens,
        "by weapon: only sightless weapons get the visor; unknown keeps the lens");
    Check(Choose(Kind::Visor, 0) == Tier::Lens && Choose(Kind::Optic, 2) == Tier::Visor,
        "mode off always uses the lens; mode all uses the visor everywhere");

    const Layout l = WindowLayout(0.1995f, 0.40f, kDefaultOffsetX, -0.02f);
    const float degrees = std::atan2(l.x, -l.z) * 57.2958f;
    const float wideDeg = 2.0f * std::atan(l.width * 0.5f / -l.z) * 57.2958f;
    Check(std::fabs(l.height - l.width * 0.75f) < 1e-6f && l.z == -0.40f && l.y == -0.02f && degrees > 4.0f &&
              degrees < 5.0f && wideDeg > 27.0f && wideDeg < 29.0f,
        "window is 4:3, on the right-eye axis (~4.6 deg), about 28 deg wide at 0.40 m");
    Check(l.yaw < 0 && std::fabs(l.yaw + degrees / 57.2958f) < 1e-4f, "the window turns to face the eye");
    const Layout centred = WindowLayout(0.20f, 0.40f, 0.0f, 0.0f);
    Check(centred.yaw == 0.0f, "a centred window faces straight back");
    const Layout bad = WindowLayout(std::numeric_limits<float>::quiet_NaN(), 99.0f, -9.0f, 9.0f);
    Check(bad.width == 0.20f && bad.z == -2.0f && bad.x == -0.60f && bad.y == 0.40f, "layout inputs are clamped");

    Readout r = Digits(6.0f);
    Check(r.tens == -1 && r.ones == 6 && r.tenths == 0, "6.0x");
    r = Digits(12.34f);
    Check(r.tens == 1 && r.ones == 2 && r.tenths == 3, "12.3x");
    r = Digits(24.0f);
    Check(r.tens == 2 && r.ones == 4 && r.tenths == 0, "24.0x");
    r = Digits(1000.0f);
    Check(r.tens == 9 && r.ones == 9 && r.tenths == 9, "readout clamps at 99.9x");
    r = Digits(std::numeric_limits<float>::quiet_NaN());
    Check(r.tens == -1 && r.ones == 1 && r.tenths == 0, "a bad zoom reads 1.0x");
    Check(SegmentsFor(8) == 0x7F && SegmentsFor(1) == 0x06 && SegmentsFor(-1) == 0 && SegmentsFor(10) == 0,
        "7-segment table");

    // Raise to eye: the visor tier's wider pitch (visor_raise_angle_deg)
    // accepts a gun tipped 18 deg up that the lens tier (15 deg) rejects.
    const float forward[3]{0, 0.309f, -0.951f}; // 18 deg up
    const float sight[3]{0.03f, 1.60f, -0.45f}; // virtual sight point
    const float eye[3]{0.03f, 1.60f, -0.20f};   // 0.25 m behind it
    scope_sight::Settings relaxed{0.15f, kRaiseDistanceDefault, kRaiseAngleDefault}, lens{};
    scope_sight::Detector a, b;
    uint64_t now = 1000;
    for (int i = 0; i < 6; ++i)
    {
        a.Update(now += 20, true, eye, sight, forward, relaxed);
        b.Update(now, true, eye, sight, forward, lens);
    }
    Check(a.Sighting() && !b.Sighting(), "raise-to-eye uses the visor tier's wider pitch");

    Check(std::fabs(VisorFeather(kEdgeSoftnessDefault) - 0.212f) < 1e-3f && VisorFeather(0) == 0.02f &&
              VisorFeather(1) == 0.50f && LensFeather(kEdgeSoftnessDefault) < VisorFeather(kEdgeSoftnessDefault) &&
              VisorFeather(std::numeric_limits<float>::quiet_NaN()) == VisorFeather(kEdgeSoftnessDefault),
        "edge softness: the visor feathers ~21 % of its radius by default, the lens gently");
    // Zoomed aim smoothing: by zoom level, time based, off when not zoomed.
    {
        using namespace zoom_smoothing;
        Check(std::fabs(AmountFor(6, 6, 24, 0.4f, 0.6f) - 0.4f) < 1e-5f &&
                  std::fabs(AmountFor(24, 6, 24, 0.4f, 0.6f) - 0.6f) < 1e-5f &&
                  std::fabs(AmountFor(12, 6, 24, 0.4f, 0.6f) - 0.5f) < 1e-5f,
            "smoothing grows from the low amount at 6x to the high amount at 24x (log scale)");
        Smoother s;
        float q[4]{0, 0, 0, 1};
        s.Apply(1000, true, 0.4f, q);
        const float h = 0.3826834f;
        float turned[4]{0, h, 0, 0.9238795f}; // 45 degree yaw
        float aInner[4]{turned[0], turned[1], turned[2], turned[3]};
        s.Apply(1020, true, 0.4f, aInner);
        Check(aInner[1] > 0 && aInner[1] < h * 0.6f, "a sudden turn is eased while zoomed");
        Smoother once, twice;
        float bInner[4]{0, 0, 0, 1}, c[4]{0, 0, 0, 1};
        once.Apply(1000, true, 0.6f, bInner);
        twice.Apply(1000, true, 0.6f, c);
        float b1[4]{turned[0], turned[1], turned[2], turned[3]};
        once.Apply(1040, true, 0.6f, b1);
        float c1[4]{turned[0], turned[1], turned[2], turned[3]};
        twice.Apply(1020, true, 0.6f, c1);
        float c2[4]{turned[0], turned[1], turned[2], turned[3]};
        twice.Apply(1040, true, 0.6f, c2);
        Check(std::fabs(b1[1] - c2[1]) < 0.02f, "several calls in a frame compose like one (multi-call aim getter)");
        Smoother off;
        float d[4]{0, 0, 0, 1};
        off.Apply(1000, false, 0.6f, d);
        float e[4]{turned[0], turned[1], turned[2], turned[3]};
        off.Apply(1020, false, 0.6f, e);
        Check(e[1] == turned[1], "not zoomed: aim passes through untouched");
        Smoother zero;
        float z0[4]{0, 0, 0, 1};
        zero.Apply(1000, true, 0.0f, z0);
        float z1[4]{turned[0], turned[1], turned[2], turned[3]};
        zero.Apply(1020, true, 0.0f, z1);
        Check(z1[1] == turned[1], "amount 0 is off");
        const float fwd[3]{0.7071f, 0, 0.7071f}, upZ[3]{0, 0, 1};
        float up[3];
        Check(LevelUp(fwd, upZ, up) && std::fabs(up[1]) < 1e-5f && up[2] > 0.7f,
            "level horizon: up stays in the vertical plane of the aim");
        const float straightUp[3]{0, 0, 1};
        Check(!LevelUp(straightUp, upZ, up), "looking straight up keeps the gun's own roll");
    }
    {
        // CE Anniversary zoom boost schedule.
        ZoomBoost boost;
        bool started = false;
        Check(boost.Every(false, 3, 90, 2) == 3, "boost: zoom off keeps the normal cadence");
        Check(boost.Every(true, 3, 4, 2, &started) == 2 && started, "boost: zoom-on starts the boost");
        Check(boost.Every(true, 3, 4, 2, &started) == 2 && !started && boost.Every(true, 3, 4, 2) == 2 &&
                  boost.Every(true, 3, 4, 2) == 2,
            "boost: N samples at 1 in M");
        Check(boost.Every(true, 3, 4, 2) == 3, "boost: then back to 1 in K");
        Check(boost.Every(false, 3, 4, 2) == 3 && boost.Every(true, 3, 4, 2, &started) == 2 && started,
            "boost: restarts on the next zoom-on");
        ZoomBoost off;
        Check(off.Every(true, 3, 0, 2, &started) == 3 && !started && off.Every(true, 3, 0, 2) == 3,
            "boost_frames = 0: no change");
        ZoomBoost clamp;
        Check(clamp.Every(true, 3, 5, 1) == 2 && clamp.Every(true, 3, 5, 9) == 3, "boost every stays in 2..normal");
    }
    Check(RaiseUsesVirtualSight(true, false) && !RaiseUsesVirtualSight(true, true) &&
              !RaiseUsesVirtualSight(false, false),
        "visor raise: virtual point only without an anchored glass (CE sniper in Anniversary uses its glass)");
    {
        // Layout: four edges, size about the centre, distance, picture anchored.
        const auto near = [](float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; };
        const auto sameLayout = [&](const Layout& a, const Layout& b)
        {
            return near(a.x, b.x, 1e-6f) && near(a.y, b.y, 1e-6f) && near(a.z, b.z, 1e-6f) &&
                   near(a.width, b.width, 1e-6f) && near(a.height, b.height, 1e-6f) && near(a.yaw, b.yaw, 1e-6f);
        };
        // Old windows: the default (the owner's, v16), a large offset one, and the old limits.
        struct Old
        {
            float width, scale, offsetX, offsetY, distance;
        };
        bool migrated = true, crop = true, sizes = true, reticle = true, rects = true;
        for (const Old old : {Old{0.133f, 1.5f, 0.032f, -0.02f, 0.40f}, Old{0.30f, 2.0f, 0.27f, -0.02f, 0.75f},
                 Old{0.80f, 3.0f, 0.60f, -0.40f, 2.00f}, Old{0.10f, 0.5f, -0.60f, 0.40f, 0.40f}})
        {
            const float before =
                std::clamp(old.width * std::clamp(old.scale, 0.5f, 3.0f), 0.10f, 0.80f); // VisorWindowWidthM
            const Layout quad = WindowLayout(before, old.distance, old.offsetX, old.offsetY);
            const MigratedWindow m = EdgesFromOldWindow(old.width, old.scale, old.offsetX, old.offsetY);
            const EdgeWindow nowInner = WindowFromEdges(m.edges, 1.0f, old.distance, m.anchorX, m.anchorY);
            migrated = migrated && sameLayout(nowInner.quad, quad);
            sizes = sizes && near(nowInner.virtualWidth, before, 1e-6f);
            rects = rects && near(nowInner.u0, 0, 1e-5f) && near(nowInner.v0, 0, 1e-5f) &&
                    near(nowInner.u1, 1, 1e-5f) && near(nowInner.v1, 1, 1e-5f);
            reticle = reticle && near(ReticleDivisor(nowInner.virtualWidth),
                                     std::clamp(before / kAspect / kReticleWindowHeightM, 0.5f, 10.0f), 1e-5f);
            // The CE eye-image crop is the same pixels for every zoom.
            for (float zoom = 1.0f; zoom <= 8.0f; zoom += 0.05f)
            {
                const CeCrop aInner = CeVisorCrop(zoom, 1.25f, before, old.distance, 2912, 2100);
                const CeCrop bInner = CeVisorCrop(zoom, 1.25f, nowInner.virtualWidth, old.distance, 2912, 2100);
                crop = crop && aInner.width == bInner.width && aInner.height == bInner.height;
            }
        }
        Check(migrated,
            "F1 C v25: the migrated edges reproduce WindowLayout's quad (default, the owner's, and the old limits)");
        Check(sizes && rects,
            "F1 C v25: a migrated window is its own virtual window (same width; the whole image shows)");
        Check(crop, "F1 C v25: the CE crop on a migrated window is identical at every zoom");
        const EdgeWindow def =
            WindowFromEdges({-0.06775f, 0.13175f, 0.0548125f, -0.0948125f}, 1.0f, 0.40f, 0.032f, -0.02f);
        Check(near(ReticleDivisor(def.virtualWidth), 1.5f, 1e-4f),
            "F1 C: the default window keeps the old reticle divisor (1.5)");
        Check(reticle, "F1 C: the reticle keeps its size in metres on a migrated window");
        // Moving one edge moves only that side; the anchor's magnification stays.
        const Edges base{-0.06775f, 0.13175f, 0.0548125f, -0.0948125f};
        Edges wider = base;
        wider.left = -0.20f;
        const EdgeWindow moved = WindowFromEdges(wider, 1.0f, 0.40f, 0.032f, -0.02f);
        Check(near(moved.planeX - moved.quad.width * 0.5f, -0.20f, 1e-6f) &&
                  near(moved.planeX + moved.quad.width * 0.5f, def.planeX + def.quad.width * 0.5f, 1e-6f) &&
                  near(moved.quad.height, def.quad.height, 1e-6f) && near(moved.quad.y, def.quad.y, 1e-6f),
            "F1 C: moving the left edge moves only the left side");
        // The anchor's view-space point and the plane's angle do not move with an
        // edge: the anchor is the quad centre minus the in-plane offset, turned by the yaw.
        const auto anchorPoint = [](const EdgeWindow& w, float ax)
        {
            const float along = ax - w.planeX;
            return Layout{w.quad.x + along * std::cos(w.quad.yaw), w.quad.y, w.quad.z - along * std::sin(w.quad.yaw), 0,
                0, w.quad.yaw};
        };
        const Layout anchorBefore = anchorPoint(def, 0.032f), anchorAfter = anchorPoint(moved, 0.032f);
        Check(near(anchorAfter.x, anchorBefore.x, 1e-6f) && near(anchorAfter.z, anchorBefore.z, 1e-6f) &&
                  near(anchorAfter.x, 0.032f, 1e-6f) && near(anchorAfter.z, -0.40f, 1e-6f) &&
                  near(moved.quad.yaw, def.quad.yaw, 1e-7f) && near(moved.quad.yaw, std::atan2(-0.032f, 0.40f), 1e-7f),
            "F1 C: moving an edge leaves the anchor's view-space point and the window's angle unchanged");
        // The render covers the virtual window (centred on the anchor) at 1/zoom
        // of its angle, and the quad shows the strip of it between the edges, at
        // the same metres per image unit: the anchor and the scale do not move.
        Check(
            near((moved.u1 - moved.u0) * moved.virtualWidth, moved.quad.width, 1e-5f) &&
                near((moved.v1 - moved.v0) * moved.virtualWidth / kAspect, moved.quad.height, 1e-5f) &&
                near((0.5f * (moved.u0 + moved.u1) - 0.5f) * moved.virtualWidth, moved.planeX - 0.032f, 1e-5f) &&
                near((0.5f - 0.5f * (moved.v0 + moved.v1)) * moved.virtualWidth / kAspect, moved.quad.y + 0.02f, 1e-5f),
            "F1 C: an edge change shows more picture, still anchored, at the same metres per image unit");
        // Size grows the window about its own centre.
        const EdgeWindow twice = WindowFromEdges(base, 2.0f, 0.40f, 0.032f, -0.02f);
        Check(near(twice.quad.width, 2.0f * def.quad.width, 1e-6f) &&
                  near(twice.quad.height, 2.0f * def.quad.height, 1e-6f) && near(twice.planeX, def.planeX, 1e-6f) &&
                  near(twice.quad.y, def.quad.y, 1e-6f),
            "F1 C: size grows the window about its centre");
        // Distance is clamped to [0.25, 2.00].
        Check(near(WindowFromEdges(base, 1.0f, 0.10f, 0.032f, -0.02f).quad.z, -0.25f, 1e-6f) &&
                  near(WindowFromEdges(base, 1.0f, 0.25f, 0.032f, -0.02f).quad.z, -0.25f, 1e-6f) &&
                  near(WindowFromEdges(base, 1.0f, 3.00f, 0.032f, -0.02f).quad.z, -2.00f, 1e-6f),
            "F1 C: distance is clamped to 0.25-2.00 m");
        // Crossed edges are spread to the minimum gap; a non-4:3 window is allowed.
        const Edges crossed = OrderedEdges({0.10f, 0.05f, -0.10f, 0.10f});
        Check(near(crossed.right - crossed.left, kEdgeMinGap, 1e-6f) &&
                  near(crossed.top - crossed.bottom, kEdgeMinGap, 1e-6f),
            "F1 C: crossed edges become the narrowest window");
        const EdgeWindow strip = WindowFromEdges({-0.20f, 0.26f, 0.02f, -0.06f}, 1.0f, 0.40f, 0.032f, -0.02f);
        Check(near(strip.quad.width / strip.quad.height, 0.46f / 0.08f, 1e-4f) && strip.v1 - strip.v0 < 0.5f,
            "F1 C: the 4:3 aspect is gone (a wide strip shows a band of the picture)");
        // The zoom render covers every visible part of the window (no edge falls
        // outside the image) and is the smallest 4:3 window on the anchor that
        // does: a side moved off the anchor, a wide strip and a tall strip.
        const EdgeWindow tall = WindowFromEdges({0.0f, 0.06f, 0.15f, -0.15f}, 1.0f, 0.40f, 0.032f, -0.02f);
        bool covered = true;
        for (const EdgeWindow* w : {&moved, &strip, &tall})
        {
            const float slack = std::min(std::min(w->u0, 1.0f - w->u1), std::min(w->v0, 1.0f - w->v1));
            covered = covered && slack > -1e-6f && slack < 1e-6f;
        }
        Check(covered && near(tall.virtualWidth, 0.34f * kAspect, 1e-5f),
            "F1 C: the zoom render covers the whole window and no more (sides moved off the anchor, wide and tall strips)");
    }
    std::printf("Visor zoom: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
