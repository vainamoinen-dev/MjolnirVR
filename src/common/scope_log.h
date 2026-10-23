#pragma once
// Formats the scope log lines (scope camera origin, scope quad, release) as plain text.
// Used by src/dll/core/vr.cpp; tested by tests/scope_log_tests.cpp and tests/core_tests.cpp.
// Pure formatting into caller buffers: no globals, no file writes (the caller logs the text).
//
// Zoom test plan log lines, formatted here
// so the text the harness parses is unit-tested. Pure, no globals.
//  - "scope cam origin: title=<T> weapon=<id> cam=(x,y,z) lens=(x,y,z) muzzle=(x,y,z) fwd-of-muzzle=<m> head-dist=<m> cam-to-lens=<m>"
//     (every position and distance is in metres: world units / units-per-metre)
//  - "scope quad: eye-local offsets x/y/z size WxH angle=<deg> weapon=<id>"
//     then " shape=<circle|rounded-rect|oval|hex|none>" (the window applied to the quad)
//     per-eye parallax appends: " eye=L|R angle=<deg> shift=(u,v) eyebox=<k>"
//  - "scope released: reason=lower|far|near|pitch|lost|button|weapon held=<ms>"
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace scope_log
{
    // World positions in metres (title world units / unitsPerMeter). Missing
    // lens or muzzle print "(n/a)" and fwd-of-muzzle "n/a".
    // headFov*: Reach only (the shared-cull head FOV before/after the scope
    // widens it, degrees); NaN omits the field.
    inline int FormatCamOrigin(char* out, size_t size, int title, unsigned long long weapon, const float cam[3],
        const float* lens, const float* muzzle, float fwdOfMuzzleM, float headDistM, float headFovBeforeDeg = NAN,
        float headFovAfterDeg = NAN) noexcept
    {
        char lensText[64] = "(n/a)", muzzleText[64] = "(n/a)", fwdText[24] = "n/a", fovText[48] = "";
        if (std::isfinite(headFovBeforeDeg) && std::isfinite(headFovAfterDeg))
            std::snprintf(fovText, sizeof fovText, " head-vfov=%.1f->%.1f", headFovBeforeDeg, headFovAfterDeg);
        if (lens)
            std::snprintf(lensText, sizeof lensText, "(%.3f,%.3f,%.3f)", lens[0], lens[1], lens[2]);
        if (muzzle)
            std::snprintf(muzzleText, sizeof muzzleText, "(%.3f,%.3f,%.3f)", muzzle[0], muzzle[1], muzzle[2]);
        if (muzzle && std::isfinite(fwdOfMuzzleM))
            std::snprintf(fwdText, sizeof fwdText, "%.3f", fwdOfMuzzleM);
        char camLensText[32] = "";
        if (lens)
        {
            const float dx = cam[0] - lens[0], dy = cam[1] - lens[1], dz = cam[2] - lens[2];
            std::snprintf(camLensText, sizeof camLensText, " cam-to-lens=%.3f", std::sqrt(dx * dx + dy * dy + dz * dz));
        }
        return std::snprintf(out, size,
            "scope cam origin: title=%d weapon=%016llX cam=(%.3f,%.3f,%.3f) lens=%s muzzle=%s fwd-of-muzzle=%s head-dist=%.3f%s%s",
            title, weapon, cam[0], cam[1], cam[2], lensText, muzzleText, fwdText, headDistM, camLensText, fovText);
    }

    inline int FormatQuad(char* out, size_t size, const float offset[3], float widthM, float heightM, float angleDeg,
        unsigned long long weapon) noexcept
    {
        return std::snprintf(out, size,
            "scope quad: eye-local offsets %.3f/%.3f/%.3f size %.3fx%.3f angle=%.1f weapon=%016llX", offset[0],
            offset[1], offset[2], widthM, heightM, angleDeg, weapon);
    }

    // The scope quad line taken 1 s into a held raise (pose calibration) ends with " hold=1".
    inline int FormatQuadHold(char* out, size_t size, bool hold) noexcept
    {
        return std::snprintf(out, size, "%s", hold ? " hold=1" : "");
    }
    // 1 Hz while raised: the held pose the look gate measured.
    inline int FormatRaised(char* out, size_t size, float distanceM, float pitchDeg, char eye) noexcept
    {
        return std::snprintf(out, size, "scope glass raised: d=%.3f pitch=%+.1f eye=%c", distanceM, pitchDeg, eye);
    }

    inline int FormatQuadShape(char* out, size_t size, const char* shape) noexcept
    {
        return std::snprintf(out, size, " shape=%s", shape ? shape : "circle");
    }

    inline int FormatQuadEye(
        char* out, size_t size, char eye, float angleDeg, float shiftU, float shiftV, float k) noexcept
    {
        return std::snprintf(
            out, size, " eye=%c angle=%.2f shift=(%.4f,%.4f) eyebox=%.3f", eye, angleDeg, shiftU, shiftV, k);
    }

    enum class Release : uint8_t
    {
        None,
        Lower,
        Far,
        Near,
        Pitch,
        Lost,
        Button,
        Weapon
    };
    inline constexpr const char* ReleaseName(Release r) noexcept
    {
        return r == Release::Lower    ? "lower"
               : r == Release::Far    ? "far"
               : r == Release::Near   ? "near"
               : r == Release::Pitch  ? "pitch"
               : r == Release::Lost   ? "lost"
               : r == Release::Button ? "button"
               : r == Release::Weapon ? "weapon"
                                      : "none";
    }
    inline int FormatReleased(char* out, size_t size, Release reason, unsigned long long heldMs) noexcept
    {
        return std::snprintf(out, size, "scope released: reason=%s held=%llu", ReleaseName(reason), heldMs);
    }

    // Signed distance of the scope camera ahead of the muzzle along the barrel,
    // metres (negative = behind the muzzle, e.g. a camera at the head).
    inline float ForwardOfMuzzle(
        const float cam[3], const float muzzle[3], const float direction[3], float unitsPerMeter) noexcept
    {
        const float l =
            std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
        if (!(l > 1e-6f) || !std::isfinite(unitsPerMeter) || unitsPerMeter <= 0)
            return NAN;
        float d = 0;
        for (int i = 0; i < 3; ++i)
            d += (cam[i] - muzzle[i]) * direction[i] / l;
        return d / unitsPerMeter;
    }
} // namespace scope_log
