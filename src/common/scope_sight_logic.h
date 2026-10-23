#pragma once
// Look-through scope activation: decides when raising a gun to the eye turns the zoom image on and off.
// Used by src/dll/core/vr.cpp; tested by tests/scope_sight_tests.cpp and tests/visor_zoom_tests.cpp.
// Pure logic: no globals, no OpenXR (the caller passes tracked poses in).
//
// Look-through scope activation (tuned in headset testing): the zoom image turns
// on when the gun is raised LEVEL (barrel pitch within +-15 deg of the world
// horizontal - the main trigger) with the scope 0.15-0.30 m (6-12 in) from the
// sighting eye; distance alone keeps a level gun at the hip out. No yaw or
// head-direction test. It turns off below 0.12 m, beyond 0.35 m or past
// +-25 deg pitch (hysteresis, no flicker at the edge). Pure logic, no globals.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace scope_sight
{
    struct Settings
    {
        float minDistance{0.15f}; // eye-to-lens, metres
        float maxDistance{0.30f};
        float pitchDeg{15.0f}; // barrel pitch from level, either way
    };
    // Release thresholds: 0.12 m / 0.35 m / 25 deg at the defaults.
    inline constexpr float kExitNearM = 0.03f, kExitFarM = 0.05f, kExitPitchDeg = 10.0f;
    inline constexpr uint64_t kDwellMs = 80, kGapMs = 200;
    inline constexpr float kEyeOffsetM = 0.032f; // half a typical IPD

    // Sighting eye: 0 = weapon side, 1 = left, 2 = right. Returns +1 for the
    // headset's right eye, -1 for the left.
    inline constexpr float EyeSide(int choice, bool leftHanded) noexcept
    {
        return choice == 1 ? -1.0f : choice == 2 ? 1.0f : (leftHanded ? -1.0f : 1.0f);
    }

    inline bool Finite3(const float v[3]) noexcept
    {
        return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
    }

    // Eye-to-lens distance, and the barrel's pitch from level (tracking space,
    // +Y up), in degrees: positive up, negative down.
    inline bool Measure(
        const float eye[3], const float lens[3], const float forward[3], float& distance, float& pitchDeg) noexcept
    {
        if (!Finite3(eye) || !Finite3(lens) || !Finite3(forward))
            return false;
        const float v[3]{lens[0] - eye[0], lens[1] - eye[1], lens[2] - eye[2]};
        distance = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        const float f = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
        if (!std::isfinite(distance) || !std::isfinite(f) || f < 1e-4f)
            return false;
        pitchDeg = std::asin(std::clamp(forward[1] / f, -1.0f, 1.0f)) * 57.2957795f;
        return std::isfinite(pitchDeg);
    }

    class Detector
    {
    public:
        void Reset() noexcept { *this = Detector{}; }
        bool Sighting() const noexcept { return sighting_; }
        // Why the last engagement ended (scope_log::Release values; 0 = none yet).
        int LastRelease() const noexcept { return lastRelease_; }
        // Last measured eye-to-lens (m) and barrel pitch (deg); NaN when the
        // last frame was untracked, a gap or non-finite (for edge logging).
        float LastDistance() const noexcept { return lastDistance_; }
        float LastPitchDeg() const noexcept { return lastPitch_; }
        // valid: both poses tracked this frame. Returns the sighting state.
        bool Update(uint64_t now, bool valid, const float eye[3], const float lens[3], const float forward[3],
            const Settings& s) noexcept
        {
            float distance{}, pitch{};
            if (!now || !valid || (last_ && (now < last_ || now - last_ > kGapMs)) ||
                !Measure(eye, lens, forward, distance, pitch))
            {
                const bool was = sighting_;
                Reset();
                last_ = now;
                if (was)
                    lastRelease_ = 5; // lost (tracking / gap)
                return false;
            }
            last_ = now;
            lastDistance_ = distance;
            lastPitch_ = pitch;
            const float nearM = std::isfinite(s.minDistance) ? std::clamp(s.minDistance, 0.05f, 0.40f) : 0.15f;
            const float farM = std::isfinite(s.maxDistance) ? std::clamp(s.maxDistance, nearM + 0.02f, 0.50f) : 0.30f;
            const float level = std::isfinite(s.pitchDeg) ? std::clamp(s.pitchDeg, 3.0f, 45.0f) : 15.0f;
            const float absPitch = std::fabs(pitch);
            if (sighting_)
            {
                if (distance < nearM - kExitNearM || distance > farM + kExitFarM || absPitch > level + kExitPitchDeg)
                {
                    sighting_ = false;
                    since_ = 0;
                    // Release reason: 1 lower, 2 far, 3 near, 4 pitch (scope_log::Release order).
                    lastRelease_ = distance > farM + kExitFarM     ? 2
                                   : distance < nearM - kExitNearM ? 3
                                   : pitch < 0                     ? 1
                                                                   : 4;
                }
                return sighting_;
            }
            if (distance >= nearM && distance <= farM && absPitch <= level)
            {
                if (!since_)
                    since_ = now;
                if (now - since_ >= kDwellMs)
                    sighting_ = true;
            }
            else
                since_ = 0;
            return sighting_;
        }

    private:
        uint64_t last_{}, since_{};
        float lastDistance_{std::numeric_limits<float>::quiet_NaN()};
        float lastPitch_{std::numeric_limits<float>::quiet_NaN()};
        bool sighting_{};
        int lastRelease_{};
    };

    // Activation mode: 0 = button only (R3), 1 = look only, 2 = both.
    inline constexpr bool Active(int mode, bool buttonLatched, bool sighting) noexcept
    {
        return (mode != 1 && buttonLatched) || (mode != 0 && sighting);
    }

    // Two states. "live": the scope pass renders onto the glass (a held glass gun
    // always; otherwise only while engaged). "engaged": the gun is zoomed in use;
    // stick magnification, aim smoothing, Reach cull widening, pacing and the
    // engage/release logs follow this, never "live". A held glass gun is engaged
    // only while raised to the eye (R3 ignored); other guns use Active().
    struct GlassState
    {
        bool live;
        bool engaged;
    };
    inline constexpr GlassState Resolve(
        bool enabled, bool glassHeld, int mode, bool buttonLatched, bool sighting) noexcept
    {
        const bool engaged = enabled && (glassHeld ? sighting : Active(mode, buttonLatched, sighting));
        return {engaged || (enabled && glassHeld), engaged};
    }
} // namespace scope_sight
