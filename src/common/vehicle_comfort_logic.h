#pragma once
// Vehicle comfort effects: turn vignette, stable-horizon follow and a vehicle-fixed dash line.
// Used by src/dll/core (menu.cpp, vr.cpp); tested by tests/vehicle_comfort_tests.cpp.
// Pure logic: no globals, no D3D, no OpenXR.
//
// Vehicle comfort:
// - a turn vignette scaled by the vehicle's yaw rate,
// - a stable horizon (yaw-only follow) option, applied at the follow passes,
// - a faint vehicle-fixed "dash line" reference.
//
// Borrowed from the earlier CE VR mod's first-person vehicle camera (its motion-control pull request): the hull pose
// only changes on 30 Hz simulation ticks, so a rate is measured per NEW
// observed pose over the real time since the previous one (never per render
// frame, which would flicker between ticks), and comfort effects wait out a
// short activation delay after entering a seat (their camera does the same),
// so the entry turn never flashes the vignette.
//
// Pure logic: no globals, no D3D, no OpenXR.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vehicle_comfort
{
    inline constexpr uint64_t kActivationDelayMs = 1000; // after a new following seat
    inline constexpr uint64_t kStaleMs = 150;            // no new pose: the turn is over
    inline constexpr float kStartDegPerSec = 10.0f;      // ~10 deg/s felt best (research)
    inline constexpr float kSpanDegPerSec = 90.0f;       // full vignette at start + span
    inline constexpr float kAttackSeconds = 0.15f, kReleaseSeconds = 0.40f;

    // Yaw rate of the vehicle the view is following.
    class YawRate
    {
    public:
        void Reset() noexcept { *this = YawRate{}; }
        // following: a seat whose view follows the hull this frame.
        // yawTotal: accumulated followed hull yaw, radians (any wrap).
        // Returns smoothed |deg/s|, 0 while not following or during the entry delay.
        float Update(uint64_t now, bool following, float yawTotal) noexcept
        {
            if (!following || !now || !std::isfinite(yawTotal))
            {
                Reset();
                return 0.0f;
            }
            if (!since_)
            {
                since_ = now;
                observed_ = yawTotal;
                observedAt_ = now;
                return 0.0f;
            }
            if (yawTotal != observed_)
            {
                const float dt = static_cast<float>(now - observedAt_) * 0.001f;
                if (dt > 0.0f && dt < 0.5f)
                    raw_ = std::fabs(yawTotal - observed_) * 57.2957795f / dt;
                observed_ = yawTotal;
                observedAt_ = now;
            }
            else if (now - observedAt_ > kStaleMs)
                raw_ = 0.0f;
            if (!std::isfinite(raw_))
                raw_ = 0.0f;
            return now - since_ < kActivationDelayMs ? 0.0f : raw_;
        }

    private:
        uint64_t since_{}, observedAt_{};
        float observed_{}, raw_{};
    };

    // Vignette strength target for a yaw rate, scaled by the user's strength.
    inline float VignetteTarget(float degPerSec, float strength) noexcept
    {
        const float s = std::isfinite(strength) ? std::clamp(strength, 0.0f, 1.0f) : 0.0f;
        const float r = std::isfinite(degPerSec) ? degPerSec : 0.0f;
        return s * std::clamp((r - kStartDegPerSec) / kSpanDegPerSec, 0.0f, 1.0f);
    }

    // Quick in, slower out, so it never pulses with the 30 Hz ticks.
    class Envelope
    {
    public:
        void Reset() noexcept
        {
            value_ = 0.0f;
            last_ = 0;
        }
        float Update(uint64_t now, float target) noexcept
        {
            const float t = std::isfinite(target) ? std::clamp(target, 0.0f, 1.0f) : 0.0f;
            if (!last_ || now < last_ || now - last_ > 250)
            {
                last_ = now;
                value_ = t < value_ ? value_ : 0.0f;
            }
            const float dt = static_cast<float>(now - last_) * 0.001f;
            last_ = now;
            const float tau = t > value_ ? kAttackSeconds : kReleaseSeconds;
            value_ += (t - value_) * (1.0f - std::exp(-dt / tau));
            if (value_ < 1e-3f && t == 0.0f)
                value_ = 0.0f;
            return value_;
        }

    private:
        float value_{};
        uint64_t last_{};
    };

    // Where the vehicle-fixed dash line sits: captured once per following seat in
    // room space (which is the vehicle frame while the view follows the hull).
    struct DashPose
    {
        float position[3]{};
        float yaw{};
        bool valid{};
    };
    inline constexpr float kDashAheadM = 0.80f, kDashBelowM = 0.45f, kDashWidthM = 0.90f, kDashHeightM = 0.06f;
    inline DashPose CaptureDash(const float head[3], float headYaw) noexcept
    {
        DashPose d{};
        if (!std::isfinite(head[0]) || !std::isfinite(head[1]) || !std::isfinite(head[2]) || !std::isfinite(headYaw))
            return d;
        // OpenXR room space: forward = (-sin yaw, 0, -cos yaw).
        d.position[0] = head[0] - std::sin(headYaw) * kDashAheadM;
        d.position[1] = head[1] - kDashBelowM;
        d.position[2] = head[2] - std::cos(headYaw) * kDashAheadM;
        d.yaw = headYaw;
        d.valid = true;
        return d;
    }
    // Cabin volume (Squadrons): fade to black as the head leaves the seat.
    // 0 inside `thresholdM` of the seated eye, full black 0.10 m beyond it.
    inline constexpr float kCabinFadeSpanM = 0.10f;
    inline float CabinFadeAlpha(const float head[3], const float seatEye[3], float thresholdM) noexcept
    {
        if (!std::isfinite(thresholdM) || thresholdM <= 0.0f)
            return 0.0f;
        float d2 = 0.0f;
        for (int i = 0; i < 3; ++i)
        {
            const float d = head[i] - seatEye[i];
            if (!std::isfinite(d))
                return 0.0f;
            d2 += d * d;
        }
        return std::clamp((std::sqrt(d2) - thresholdM) / kCabinFadeSpanM, 0.0f, 1.0f);
    }
    // The seated eye the cabin fade measures from. Captured on seat entry; it is
    // re-captured when a position recenter lands (the player sat down, or hit the
    // recenter chord) and, as a safety, when the view has been mostly black for
    // kCabinRebaseMs (a real lean is short; a chair is not), so the fade can never
    // strand the player in black.
    inline constexpr uint64_t kCabinRebaseMs = 1500;
    class CabinFade
    {
    public:
        void Reset() noexcept
        {
            m_valid = false;
            m_blackSince = 0;
            m_epoch = 0;
        }
        void Capture(const float head[3], uint32_t recenterEpoch) noexcept
        {
            for (int i = 0; i < 3; ++i)
                m_eye[i] = head[i];
            m_valid = std::isfinite(head[0]) && std::isfinite(head[1]) && std::isfinite(head[2]);
            m_blackSince = 0;
            m_epoch = recenterEpoch;
        }
        bool Valid() const noexcept { return m_valid; }
        float Update(uint64_t nowMs, const float head[3], float thresholdM, uint32_t recenterEpoch) noexcept
        {
            if (!m_valid)
                return 0.0f;
            if (recenterEpoch != m_epoch)
            {
                Capture(head, recenterEpoch);
                return 0.0f;
            }
            const float a = CabinFadeAlpha(head, m_eye, thresholdM);
            if (a < 0.5f)
            {
                m_blackSince = 0;
                return a;
            }
            if (!m_blackSince)
                m_blackSince = nowMs ? nowMs : 1;
            else if (nowMs >= m_blackSince && nowMs - m_blackSince >= kCabinRebaseMs)
            {
                Capture(head, m_epoch);
                return 0.0f;
            }
            return a;
        }

    private:
        float m_eye[3]{};
        bool m_valid = false;
        uint64_t m_blackSince = 0;
        uint32_t m_epoch = 0;
    };
    // On-foot smooth-turn rate for the turn vignette (the stick turns the view at
    // `degPerSec` past the 0.15 dead zone the turn code uses). Snap turn: none.
    inline float SmoothTurnRate(bool smooth, float stickX, float degPerSec) noexcept
    {
        if (!smooth || !std::isfinite(stickX) || !std::isfinite(degPerSec))
            return 0.0f;
        const float x = std::fabs(stickX);
        return x > 0.15f ? std::min(x, 1.0f) * std::max(degPerSec, 0.0f) : 0.0f;
    }
    // Snap increments offered in F1 (Squadrons: 30/60/90, default 30).
    inline constexpr float kSnapTurnOptions[4]{30.0f, 45.0f, 60.0f, 90.0f};
} // namespace vehicle_comfort
