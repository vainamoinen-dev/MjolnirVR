#pragma once
// Per-eye parallax shift and eyebox shadow for the scope glass.
// Used by src/dll/core/vr.cpp when it draws the lens quad; tested by tests/scope_parallax_tests.cpp.
// Pure maths: no globals, no rendering calls.
//
// Eye position is in the glass's own frame (metres): x right, y up, z along
// the quad normal toward the eye; glassWidth is the quad width (metres).
#include <cmath>

namespace scope_parallax
{
    struct Settings
    {
        bool parallax = true;
        float depth = 0.5f; // virtual image depth behind the glass, in glass widths
        bool eyebox = true;
        float tolerance = 0.03f; // m off the lens axis with the full picture
        float fade = 0.05f;      // m over which it fades to black
        float otherEye = 0.2f;   // floor of the non-aiming eye's eyebox disc
        // The glass is always live: parallax may dim and shift the
        // picture but never black it out.
        float maxShift = 0.25f; // picture shift cap, glass widths (uv)
        float floor = 0.35f;    // brightness never below this, anywhere on the glass
    };

    // sample = uv + shift (uv units, v down). Shadow disc centre in uv; k = disc
    // radius in half-glass units (1 = full glass, 0 = black). valid = false: stock image.
    struct Eye
    {
        float shiftU = 0, shiftV = 0;
        float shadowU = 0.5f, shadowV = 0.5f;
        float k = 1.0f;
        float floor = 1.0f; // brightness floor outside the clear disc (1 = no shadow)
        float offAxisM = 0; // |o|
        float angleDeg = 0; // eye off the lens axis, seen from the glass
        bool valid = false;
    };

    inline constexpr float kMinEyeDistanceM = 0.01f;
    inline constexpr float kReticleLag = 0.1f; // reticle at mix(shifted, unshifted, 0.1) (Godot parallax_factor)

    inline float Smoothstep(float a, float b, float x) noexcept
    {
        if (!(b > a))
            return x < a ? 0.0f : 1.0f;
        float t = (x - a) / (b - a);
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        return t * t * (3 - 2 * t);
    }

    // glassHeight: a non-square glass's height (the wide CE glass); 0 = square.
    inline Eye Compute(const float eyeLocal[3], float glassWidth, const Settings& s, float glassHeight = 0.0f) noexcept
    {
        Eye e{};
        if (!eyeLocal || !std::isfinite(eyeLocal[0]) || !std::isfinite(eyeLocal[1]) || !std::isfinite(eyeLocal[2]) ||
            !std::isfinite(glassWidth) || glassWidth <= 0 || eyeLocal[2] <= kMinEyeDistanceM)
            return e;
        const float ox = eyeLocal[0], oy = eyeLocal[1], d = eyeLocal[2];
        e.valid = true;
        e.offAxisM = std::sqrt(ox * ox + oy * oy);
        e.angleDeg = std::atan2(e.offAxisM, d) * 57.2957795f;
        if (s.parallax && std::isfinite(s.depth))
        {
            // The picture sits depth glass-sides behind the glass: an eye at +x
            // looks through the centre toward -x on that plane (v grows downward).
            // A wide glass measures depth and the cap in its height (the
            // square's side), and u in its own width, so the picture moves the same
            // distance sideways as up (a real image behind a real lens).
            const bool wide = std::isfinite(glassHeight) && glassHeight > 0 && glassHeight < glassWidth;
            e.shiftU = -s.depth * ox / d;
            e.shiftV = s.depth * oy / d;
            const float cap = std::isfinite(s.maxShift) && s.maxShift >= 0 ? s.maxShift : 0.25f;
            const float len = std::sqrt(e.shiftU * e.shiftU + e.shiftV * e.shiftV);
            if (len > cap && len > 0)
            {
                e.shiftU *= cap / len;
                e.shiftV *= cap / len;
            }
            if (wide)
                e.shiftU *= glassHeight / glassWidth;
        }
        if (s.eyebox)
        {
            e.k = 1.0f - Smoothstep(s.tolerance, s.tolerance + s.fade, e.offAxisM);
            e.floor = std::isfinite(s.floor) ? (s.floor < 0 ? 0.0f : s.floor > 1 ? 1.0f : s.floor) : 0.35f;
            auto clampUv = [](float v) { return v < -0.5f ? -0.5f : v > 1.5f ? 1.5f : v; };
            e.shadowU = clampUv(0.5f + ox / glassWidth);
            e.shadowV = clampUv(0.5f - oy / (std::isfinite(glassHeight) && glassHeight > 0 ? glassHeight : glassWidth));
        }
        return e;
    }

    // The eye farther from the lens axis is the non-aiming eye: its glass is
    // floored at otherEye so the player can open it up (default about the k a
    // 6 cm offset gives anyway).
    inline void ApplyOtherEye(Eye eyes[2], const Settings& s) noexcept
    {
        if (!s.eyebox || !eyes[0].valid || !eyes[1].valid || !std::isfinite(s.otherEye))
            return;
        Eye& other = eyes[0].offAxisM >= eyes[1].offAxisM ? eyes[0] : eyes[1];
        const float floor = s.otherEye < 0 ? 0 : s.otherEye > 1 ? 1 : s.otherEye;
        if (other.k < floor)
            other.k = floor;
    }
} // namespace scope_parallax
