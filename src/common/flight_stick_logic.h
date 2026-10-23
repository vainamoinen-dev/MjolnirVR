#pragma once
// Virtual flight stick for aircraft driver seats: grip a stick, tilt it to fly.
// Used by dll/core/game.cpp and tests/flight_stick_tests.cpp.
//
// Flight stick (VTOL VR style):
// in an aircraft driver seat, squeeze the weapon-hand grip at a virtual stick
// to fly by stick deflection instead of by pointing.
// - The hand's orientation at the grab is the stick's neutral (no lurch).
// - The stick shaft leans forward by `tilt` (VTOL VR players asked for a
//   20-30 deg forward-tilted neutral posture), so sideways tilt is measured
//   about the leaning shaft, as on a real tilted grip.
// - Forward/back tilt = pitch (flight convention: push forward, nose down),
//   sideways tilt = yaw (Halo aircraft have no roll axis), wrist twist adds a
//   little yaw. Full deflection at `fullDeg`, 5 % deadzone.
// - Output is a right-stick RATE, the way Halo flies aircraft. The trigger is
//   untouched: the weapon-hand trigger still fires the aircraft's guns while
//   the stick is held, and the off-hand trigger keeps its native role.
//
// Pure logic, OpenXR room space (Y up, -Z forward). No globals.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace flight_stick
{
    struct Settings
    {
        float forward{0.30f}; // stick grip ahead of the head
        float down{0.45f};    // and below it
        float zone{0.20f};
        float tiltDeg{25.0f}; // shaft lean forward
        float fullDeg{30.0f}; // deflection for full stick
        float deadzone{0.05f};
    };
    inline constexpr float kGrabGrip = 0.65f, kReleaseGrip = 0.30f, kTwistShare = 0.5f;

    struct Input
    {
        uint64_t now{};
        bool available{}; // aircraft driver seat, gameplay
        float head[3]{};
        float bodyForward[3]{}; // horizontal unit
        float hand[3]{};
        float handQ[4]{0, 0, 0, 1};
        float grip{};
        bool handValid{};
    };
    struct Output
    {
        bool flying{};    // the stick owns the right stick this frame
        float rx{}, ry{}; // yaw rate (+ right), pitch rate (+ nose up)
        bool grabbed{}, released{}, consumeGrip{};
    };

    class Stick
    {
    public:
        void Reset() noexcept { *this = Stick{}; }
        bool Holding() const noexcept { return held_; }
        Output Update(const Input& in, const Settings& c) noexcept
        {
            Output out{};
            const bool valid = in.handValid && std::isfinite(in.grip) && Finite3(in.hand) && FiniteQ(in.handQ);
            const bool pressed = valid && in.grip >= kGrabGrip;
            const bool letGo = !valid || in.grip <= kReleaseGrip;
            if (!in.available || !Finite3(in.head) || !Finite3(in.bodyForward))
            {
                out.released = held_;
                Reset();
                wasPressed_ = pressed;
                return out;
            }
            const float* f = in.bodyForward;
            const float up[3]{0, 1, 0};
            const float right[3]{-f[2], 0, f[0]};
            float grip[3];
            for (int i = 0; i < 3; ++i)
                grip[i] = in.head[i] + f[i] * Clamp(c.forward, 0.10f, 0.70f, 0.30f) -
                          up[i] * Clamp(c.down, 0.10f, 0.80f, 0.45f);
            const bool inZone = valid && Distance(in.hand, grip) <= Clamp(c.zone, 0.05f, 0.40f, 0.20f);
            const bool rising = pressed && !wasPressed_;
            if (held_ && letGo)
            {
                held_ = false;
                out.released = true;
            }
            else if (!held_ && rising && inZone)
            {
                held_ = true;
                out.grabbed = true;
                for (int i = 0; i < 4; ++i)
                    neutral_[i] = in.handQ[i];
            }
            wasPressed_ = pressed ? true : (letGo ? false : wasPressed_);
            out.consumeGrip = held_ || (pressed && inZone);
            if (!held_ || !valid)
                return out;
            // World-frame rotation from the neutral to the current hand pose.
            float inv[4]{-neutral_[0], -neutral_[1], -neutral_[2], neutral_[3]};
            float d[4];
            Mul(in.handQ, inv, d);
            if (d[3] < 0)
                for (float& v : d)
                    v = -v;
            const float s = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            float rot[3]{0, 0, 0};
            if (s > 1e-6f)
            {
                const float angle = 2.0f * std::atan2(s, d[3]);
                for (int i = 0; i < 3; ++i)
                    rot[i] = d[i] / s * angle;
            }
            // Axes of a stick whose shaft leans forward by tilt.
            const float t = Clamp(c.tiltDeg, 0.0f, 45.0f, 25.0f) / 57.2957795f;
            float shaft[3], lateralAxis[3];
            for (int i = 0; i < 3; ++i)
            {
                shaft[i] = up[i] * std::cos(t) + f[i] * std::sin(t);
                lateralAxis[i] = f[i] * std::cos(t) - up[i] * std::sin(t);
            }
            const float full = Clamp(c.fullDeg, 10.0f, 60.0f, 30.0f) / 57.2957795f;
            const float pushForward = -Dot(rot, right);  // top of the stick tips forward
            const float lateral = Dot(rot, lateralAxis); // top of the stick tips right
            const float twistLeft = Dot(rot, shaft);     // wrist turned left
            const float pitchCmd = -pushForward / full;  // push forward: nose down
            const float yawCmd = (lateral - kTwistShare * twistLeft) / full;
            out.ry = Shape(pitchCmd, c.deadzone);
            out.rx = Shape(yawCmd, c.deadzone);
            out.flying = true;
            return out;
        }

    private:
        static float Shape(float v, float dz) noexcept
        {
            const float d = std::isfinite(dz) ? std::clamp(dz, 0.0f, 0.3f) : 0.05f;
            const float a = std::fabs(std::isfinite(v) ? v : 0.0f);
            const float m = a <= d ? 0.0f : std::min((a - d) / (1.0f - d), 1.0f);
            return v < 0 ? -m : m;
        }
        static void Mul(const float a[4], const float b[4], float o[4]) noexcept
        {
            o[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
            o[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
            o[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
            o[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
        }
        static bool Finite3(const float v[3]) noexcept
        {
            return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
        }
        static bool FiniteQ(const float q[4]) noexcept { return Finite3(q) && std::isfinite(q[3]); }
        static float Dot(const float a[3], const float b[3]) noexcept
        {
            return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        }
        static float Distance(const float a[3], const float b[3]) noexcept
        {
            const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
            return std::sqrt(x * x + y * y + z * z);
        }
        static float Clamp(float v, float lo, float hi, float d) noexcept
        {
            return std::isfinite(v) ? std::clamp(v, lo, hi) : d;
        }
        float neutral_[4]{0, 0, 0, 1};
        bool held_{}, wasPressed_{};
    };
} // namespace flight_stick
