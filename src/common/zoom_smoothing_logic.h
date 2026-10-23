#pragma once
// Zoom smoothing: extra time-based aim smoothing while zoomed, and the level-horizon helper for the zoom camera.
// Used by scope_logic.cpp and src/dll/core/vr.cpp; tested by tests/visor_zoom_tests.cpp.
// Pure logic: no globals, no clock reads (callers pass the frame time).
//
// Extra aim smoothing while zoomed (prior art: Zoom1/Zoom2
// WeaponSmoothingAmount, 0.4 / 0.6; 0 = off, 2 = maximum), plus the level
// horizon helper for the zoom camera. Pure logic.
//
// The smoother is time based (alpha = 1 - exp(-dt / tau)), so calling it
// several times in one frame composes to the same result as calling it once:
// safe for the multi-call aim getter.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace zoom_smoothing
{
    // Smoothing amount for the current zoom: `low` at the lowest zoom, `high` at
    // the highest, interpolated on a log scale in between.
    inline float AmountFor(float zoom, float zoomMin, float zoomMax, float low, float high) noexcept
    {
        const auto fin = [](float v, float d) { return std::isfinite(v) ? std::clamp(v, 0.0f, 2.0f) : d; };
        const float lo = fin(low, 0.4f), hi = fin(high, 0.6f);
        if (!std::isfinite(zoom) || !std::isfinite(zoomMin) || !std::isfinite(zoomMax) || zoomMin <= 0 ||
            zoomMax <= zoomMin)
            return lo;
        const float z = std::clamp(zoom, zoomMin, zoomMax);
        const float t = std::log(z / zoomMin) / std::log(zoomMax / zoomMin);
        return lo + (hi - lo) * t;
    }
    // Amount 1.0 -> 100 ms time constant.
    inline constexpr float kSecondsPerAmount = 0.10f;

    class Smoother
    {
    public:
        void Reset() noexcept { valid_ = false; }
        // q: x,y,z,w orientation, filtered in place. Inactive or amount 0 passes
        // through and forgets, so opening the zoom never snaps from an old pose.
        void Apply(uint64_t nowMs, bool active, float amount, float q[4]) noexcept
        {
            if (!Finite(q))
            {
                Reset();
                return;
            }
            const float a = std::isfinite(amount) ? std::clamp(amount, 0.0f, 2.0f) : 0.0f;
            if (!active || a <= 0.0f || !valid_ || nowMs < last_ || nowMs - last_ > 250)
            {
                Store(q, nowMs);
                return;
            }
            const float dt = static_cast<float>(nowMs - last_) * 0.001f;
            const float alpha = 1.0f - std::exp(-dt / (a * kSecondsPerAmount));
            // Shortest-path nlerp from the filtered pose toward the new one.
            float d = prev_[0] * q[0] + prev_[1] * q[1] + prev_[2] * q[2] + prev_[3] * q[3];
            const float sign = d < 0 ? -1.0f : 1.0f;
            float out[4];
            for (int i = 0; i < 4; ++i)
                out[i] = prev_[i] + (sign * q[i] - prev_[i]) * alpha;
            const float n = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
            if (!std::isfinite(n) || n < 1e-6f)
            {
                Store(q, nowMs);
                return;
            }
            for (int i = 0; i < 4; ++i)
                q[i] = out[i] / n;
            Store(q, nowMs);
        }

    private:
        static bool Finite(const float q[4]) noexcept
        {
            return std::isfinite(q[0]) && std::isfinite(q[1]) && std::isfinite(q[2]) && std::isfinite(q[3]);
        }
        void Store(const float q[4], uint64_t now) noexcept
        {
            for (int i = 0; i < 4; ++i)
                prev_[i] = q[i];
            last_ = now;
            valid_ = true;
        }
        float prev_[4]{0, 0, 0, 1};
        uint64_t last_{};
        bool valid_{};
    };

    // Level horizon: an up vector perpendicular to `forward` in the plane of
    // forward and world up (Halo worlds are +Z up). False when looking almost
    // straight up or down, so the caller keeps the controller's roll.
    inline bool LevelUp(const float forward[3], const float worldUp[3], float up[3]) noexcept
    {
        const float along = forward[0] * worldUp[0] + forward[1] * worldUp[1] + forward[2] * worldUp[2];
        if (!std::isfinite(along) || std::fabs(along) > 0.98f)
            return false;
        for (int i = 0; i < 3; ++i)
            up[i] = worldUp[i] - forward[i] * along;
        const float n = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
        if (!std::isfinite(n) || n < 1e-4f)
            return false;
        for (int i = 0; i < 3; ++i)
            up[i] /= n;
        return true;
    }
} // namespace zoom_smoothing
