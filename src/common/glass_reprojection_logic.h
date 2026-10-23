#pragma once
// Scope glass helpers: rotational reprojection of a held gun-camera picture, held-image
// lifetime, scaled-pass clamps and the borrow cadence. Used by dll/core/vr.cpp and
// tests/glass_reprojection_tests.cpp. Pure logic: no globals, no D3D, no logging.
//
// Glass rotational reprojection. The CE Anniversary gun camera refreshes the
// glass only on borrowed frames (1 in 6 raised); between them the held picture is shifted
// by the gun's rotation since it was rendered, so the glass keeps tracking the aim (the
// glass may refresh slower; it must not step). Rotation only: translation and animation
// inside the picture still update at the borrow rate.
#include "scope_window_logic.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace glass_reprojection
{
    struct V3
    {
        float x{}, y{}, z{};
    };
    inline float Dot(V3 a, V3 b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }
    inline V3 Cross(V3 a, V3 b)
    {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }
    inline bool Finite(V3 v)
    {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }
    inline V3 Normalized(V3 v)
    {
        const float l = std::sqrt(Dot(v, v));
        return l > 1e-6f ? V3{v.x / l, v.y / l, v.z / l} : V3{};
    }

    struct Shift
    {
        float du{}, dv{}, degrees{};
    };

    // Whether the glass still has a picture after a pair that shows the held
    // gun-camera image. The scope refresh scheduler clears the image flag on every raised
    // frame (1 in N at the hip); if only a newly borrowed image set it again, the lens quad
    // would show 1 raised frame in 6 and be blank in between. The held image stays valid
    // between borrows (it is reprojected by the gun's rotation), as CE Classic's is.
    inline bool HeldImagePresent(bool newImageCopied, bool hadImage, bool heldImageIsCurrent) noexcept
    {
        return newImageCopied || (hadImage && heldImageIsCurrent);
    }

    // The scope image after a native pass. A pass that never drew
    // (refused before its render: a Reach stage, a camera or projection check) leaves the
    // last good image on the glass; a scope does not blink because one frame was late. A
    // pass that drew into the cache but captured nothing leaves a half-drawn image: drop it.
    inline bool ImageAfterPass(bool hadImage, bool drewIntoCache, bool captured) noexcept
    {
        return captured || (hadImage && !drewIntoCache);
    }

    // ...but a pass that keeps refusing must not freeze the glass on an old picture: a held
    // image older than this is dropped. CE Anniversary is exempt: its picture is a borrowed
    // frame by design (none while the head turns), and Compute() reprojects it in between.
    inline constexpr uint64_t kHeldImageMaxMs = 500;
    inline bool HeldImageFresh(bool borrowedFrames, uint64_t ageMs) noexcept
    {
        return borrowedFrames || ageMs <= kHeldImageMaxMs;
    }

    // CPU mirror of the glass shader's scaled-pass clamp. A pass
    // that rendered at fraction s fills lround(s * size) texels per axis (the pass
    // rounds its rect with lround); the sample stays between the first and last
    // filled texel CENTRES, half a texel in at both ends (not a whole texel in at the
    // high end). Returns the UV bounds for one axis.
    struct SubrectBounds
    {
        float low, high;
    };
    inline SubrectBounds SubrectClamp(float fraction, unsigned size) noexcept
    {
        const float s = static_cast<float>(size ? size : 1u);
        const float filled = std::floor(fraction * s + 0.5f); // lround for positive values, as the shader
        return {0.5f / s, (filled - 0.5f) / s};
    }

    // A resource whose creation failed is retried after a pause
    // instead of being off for the rest of the process (one runtime hiccup is not a verdict).
    struct RetryLatch
    {
        static constexpr uint64_t kRetryMs = 5000;
        uint64_t failedAtMs = 0;
        bool Blocked(uint64_t nowMs) const noexcept
        {
            return failedAtMs && nowMs >= failedAtMs && nowMs - failedAtMs < kRetryMs;
        }
        void Fail(uint64_t nowMs) noexcept { failedAtMs = nowMs ? nowMs : 1; }
    };

    // The texture-uv shift that puts the current aim `now` at the glass centre, for a picture
    // rendered along `forward` with `up` (game world: x forward, y left, z up, so right =
    // forward x up) and half-angle tangents tanH (horizontal) / tanV. u grows to the camera's
    // right, v downward. False: no usable rotation (non-finite, degenerate, or over 30 deg).
    inline bool Compute(V3 forward, V3 up, V3 now, float tanH, float tanV, Shift& out)
    {
        out = {};
        if (!Finite(forward) || !Finite(up) || !Finite(now) || !std::isfinite(tanH) || !std::isfinite(tanV) ||
            tanH <= 1e-4f || tanV <= 1e-4f)
            return false;
        const V3 f = Normalized(forward), n = Normalized(now);
        V3 u{up.x - f.x * Dot(up, f), up.y - f.y * Dot(up, f), up.z - f.z * Dot(up, f)};
        u = Normalized(u);
        if (!Dot(f, f) || !Dot(n, n) || !Dot(u, u))
            return false;
        const float along = Dot(n, f);
        if (along < 0.866f)
            return false; // > 30 deg: a different picture, not a shift
        const V3 r = Cross(f, u);
        out.du = Dot(n, r) / along / (2.0f * tanH);
        out.dv = -Dot(n, u) / along / (2.0f * tanV);
        out.degrees = std::acos(std::min(1.0f, along)) * 57.29578f;
        return std::isfinite(out.du) && std::isfinite(out.dv);
    }

    // How far (texture uv, each side) the glass crop can move before it leaves the picture:
    // the shader's crop scale (picture at the glass aspect, GlassShiftMargin headroom).
    struct Room
    {
        float u{}, v{};
    };
    inline Room CropRoom(float glassAspect, float sourceAspect, float maxShift)
    {
        const float sa = std::isfinite(sourceAspect) && sourceAspect > 0 ? sourceAspect : 1.0f;
        const float da = std::isfinite(glassAspect) && glassAspect > 0 ? glassAspect : 1.0f;
        float su = sa > da ? da / sa : 1.0f, sv = sa > da ? 1.0f : sa / da;
        if (da > 1.0f)
        {
            const float m = scope_window::GlassShiftMargin(da, sa, maxShift);
            su /= m;
            sv /= m;
        }
        return {std::max(0.0f, 0.5f - 0.5f * su), std::max(0.0f, 0.5f - 0.5f * sv)};
    }

    // The reprojection headroom a CE Anniversary glass image is rendered with:
    // 30 deg/s of aim (tracking or settling onto a target) over 80 ms (a 1-in-6 image is up
    // to ~67 ms old at 90 Hz, plus render latency) = 2.4 deg each side.
    constexpr float kRoomDegrees = 2.4f;

    // How often to borrow a gun-camera frame, from the gun's angular rate. The
    // shown picture is up to `every` frames old plus about two frames of render-to-display
    // latency, so at `rate` it is shifted (every + 2) * period * rate. Keep that inside a
    // share of kRoomDegrees (the rest absorbs a skipped borrow and rate peaks): fast aim
    // borrows down to 1 in 2, slow aim keeps `base` (the setting: 1 in 6 raised, 12 hip).
    // A fixed 1 in 6 at 30 deg/s reached the picture edge in automated tests (shift max
    // 2.6 deg raised, 4.46 at the hip's 1 in 12).
    constexpr float kBorrowRoomShare = 0.6f;
    constexpr float kBorrowLatencyFrames = 2.0f;
    inline unsigned BorrowEvery(float rateDegPerS, float framePeriodS, unsigned base) noexcept
    {
        base = std::max(base, 2u);
        if (!std::isfinite(rateDegPerS) || rateDegPerS <= 0.0f || !std::isfinite(framePeriodS) || framePeriodS <= 0.0f)
            return base;
        const float frames = kBorrowRoomShare * kRoomDegrees / (rateDegPerS * framePeriodS) - kBorrowLatencyFrames;
        if (!std::isfinite(frames) || frames >= static_cast<float>(base))
            return base;
        return frames <= 2.0f ? 2u : static_cast<unsigned>(frames);
    }

    // The gun's angular rate held at its recent peak for holdMs, so a sweep keeps the fast
    // cadence through its slow middle samples and only relaxes once the gun settles.
    struct RateHold
    {
        float held{};
        uint64_t atMs{};
        float Update(float rate, uint64_t nowMs, uint64_t holdMs = 250) noexcept
        {
            if (!std::isfinite(rate) || rate < 0.0f)
                rate = 0.0f;
            if (rate >= held || nowMs < atMs || nowMs - atMs > holdMs)
            {
                held = rate;
                atMs = nowMs;
            }
            return held;
        }
    };

    // The gun's angular rate from its lens-receipt forward: measured between
    // actual changes of the receipt over the real time between them, so a receipt that moves
    // only on game ticks (not every frame) reads its true rate, not a multiple of it. Frames
    // without a change report 0 (RateHold keeps the peak). Seconds come from the caller's clock.
    struct GunRateMeter
    {
        V3 last{};
        double lastChangeS{};
        bool have{};
        float Update(V3 forward, double nowS) noexcept
        {
            const float lengths = std::sqrt(Dot(forward, forward) * Dot(last, last));
            if (!Finite(forward) || !std::isfinite(nowS))
            {
                have = false;
                return 0.0f;
            }
            if (!have || !(lengths > 1e-6f))
            {
                last = forward;
                lastChangeS = nowS;
                have = true;
                return 0.0f;
            }
            const float c = std::clamp(Dot(forward, last) / lengths, -1.0f, 1.0f);
            const float degrees = std::acos(c) * 57.2957795f;
            if (degrees < 1e-3f)
                return 0.0f; // unchanged: not a sample
            const double dt = nowS - lastChangeS;
            last = forward;
            lastChangeS = nowS;
            return dt > 1e-4 && dt < 0.25 ? static_cast<float>(degrees / dt) : 0.0f;
        }
        void Reset() noexcept { have = false; }
    };

    // The GlassShiftMargin cap (glass heights) that leaves at least roomTan (tangent) of
    // horizontal headroom beside the visible crop, for a picture whose unmargined horizontal
    // half-angle tangent is tanH0. Never below `cap` (the parallax setting), at most 1 (the
    // shader's clamp); unchanged for a glass that is not wide (no margin applies).
    inline float CapForRoom(float glassAspect, float sourceAspect, float cap, float tanH0, float roomTan)
    {
        if (!std::isfinite(glassAspect) || glassAspect <= 1.0f || !std::isfinite(sourceAspect) ||
            sourceAspect <= 0.0f || !std::isfinite(tanH0) || tanH0 <= 1e-4f || !std::isfinite(roomTan) ||
            roomTan <= 0.0f || !std::isfinite(cap))
            return cap;
        // The image spans tanH0 x margin; the crop shows tanH0 x cropX; margin = cropX(1 + 2c/da).
        const float cropX = std::min(1.0f, glassAspect / sourceAspect);
        const float margin = cropX + roomTan / tanH0;
        return std::clamp(std::max(cap, (margin / cropX - 1.0f) * glassAspect * 0.5f), 0.0f, 1.0f);
    }

    // The shift the shader applies (inside the room); clamped reports a picture edge reached.
    inline Shift Clamp(Shift s, Room room, bool& clamped)
    {
        const float du = std::clamp(s.du, -room.u, room.u), dv = std::clamp(s.dv, -room.v, room.v);
        clamped = du != s.du || dv != s.dv;
        s.du = du;
        s.dv = dv;
        return s;
    }
} // namespace glass_reprojection
