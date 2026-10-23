#pragma once
// Turret handles: a virtual two-grip handle bar in gunner seats that aims the turret.
// Used by src/dll/core/game.cpp; tested by tests/turret_handles_tests.cpp.
// Pure logic: no globals, no game memory, no OpenXR.
//
// Grab the handle bar (Vice City grab rules, as the wheel) and the gun
// follows the handles.
// - Lever (default): a real spade-grip mount. The pivot sits ahead of the
//   handles, so pushing the handles left swings the barrel right and lifting
//   them points it down.
// - Point: the gun aims where the handles point (pivot behind the handles).
// Borrowed from the earlier CE VR mod's motion-controlled vehicle aim (its motion-control pull request): on grab the
// aim eases from where the controller pointed to the handle aim with a
// smoothstep over 1 s (ModelAimTransitionSeconds), and the result never turns
// faster than 360 deg/s (MaximumTurnSpeedDegreesPerSecond). The engine still
// owns each turret's traverse limits and turn rate.
//
// Pure logic, OpenXR room space (Y up, -Z forward). No globals.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace turret_handles
{
    struct Settings
    {
        float forward{0.40f}; // handle bar centre ahead of the head
        float down{0.30f};    // and below it
        float span{0.24f};    // distance between the two handles
        float zone{0.18f};    // grab zone around each handle
        float pivot{0.35f};   // lever arm: pivot distance from the handles
        bool lever{true};     // true: spade-grip lever; false: aim where the handles point
    };
    inline constexpr float kGrabGrip = 0.65f, kReleaseGrip = 0.30f;
    inline constexpr float kBlendSeconds = 1.0f, kMaxDegPerSec = 360.0f;

    struct Hand
    {
        float pos[3]{};
        float grip{};
        bool valid{};
    };
    struct Input
    {
        uint64_t now{};
        bool available{}; // gunner/turret seat, gameplay
        float head[3]{};
        float bodyForward[3]{}; // horizontal unit
        Hand left{}, right{};
        float controller[4]{0, 0, 0, 1}; // current controller aim (x,y,z,w)
    };
    struct Output
    {
        bool aiming{}; // the handles own the aim this frame
        float orientation[4]{0, 0, 0, 1};
        float origin[3]{}; // aim ray origin while held: the handle-bar centre
        bool grabbed{}, released{}, grabLeft{}, grabRight{};
        bool consumeLeft{}, consumeRight{};
    };

    // Orientation (x,y,z,w) whose -Z axis points along dir (no roll).
    inline bool LookRotation(const float dir[3], float q[4]) noexcept
    {
        const float n = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        if (!std::isfinite(n) || n < 1e-5f)
            return false;
        const float x = dir[0] / n, y = dir[1] / n, z = dir[2] / n;
        const float yaw = std::atan2(-x, -z);
        const float pitch = std::asin(std::clamp(y, -1.0f, 1.0f));
        const float cy = std::cos(yaw * 0.5f), sy = std::sin(yaw * 0.5f);
        const float cp = std::cos(pitch * 0.5f), sp = std::sin(pitch * 0.5f);
        // q = yaw(about +Y) * pitch(about +X)
        q[0] = cy * sp;
        q[1] = sy * cp;
        q[2] = -sy * sp;
        q[3] = cy * cp;
        return true;
    }

    inline float AngleBetween(const float a[4], const float b[4]) noexcept
    {
        const float d = std::fabs(a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]);
        return 2.0f * std::acos(std::clamp(d, 0.0f, 1.0f));
    }
    // Shortest-path slerp toward `to` by fraction t.
    inline void Slerp(const float from[4], const float to[4], float t, float out[4]) noexcept
    {
        float d = from[0] * to[0] + from[1] * to[1] + from[2] * to[2] + from[3] * to[3];
        float b[4]{to[0], to[1], to[2], to[3]};
        if (d < 0)
        {
            d = -d;
            for (float& v : b)
                v = -v;
        }
        if (d > 0.9995f)
        {
            for (int i = 0; i < 4; ++i)
                out[i] = from[i] + (b[i] - from[i]) * t;
        }
        else
        {
            const float theta = std::acos(d), s = std::sin(theta);
            const float wa = std::sin((1 - t) * theta) / s, wb = std::sin(t * theta) / s;
            for (int i = 0; i < 4; ++i)
                out[i] = from[i] * wa + b[i] * wb;
        }
        const float n = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
        if (n > 1e-6f)
            for (int i = 0; i < 4; ++i)
                out[i] /= n;
    }

    // The seat axis the handles are centred on. Captured once when the player takes
    // a turret seat (the view is recentred at entry, so that facing is the gun's
    // axis) and held until they leave, so turning the head to look around never
    // swings the "hands centred" aim off the seat axis.
    class SeatAxis
    {
    public:
        void Reset() noexcept { *this = SeatAxis{}; }
        // seated: in a turret seat; head: the head's forward (any length). Writes the horizontal
        // unit axis to out. False (and out untouched) when not seated or no usable facing yet.
        bool Update(bool seated, const float head[3], float out[3]) noexcept
        {
            if (!seated)
            {
                Reset();
                return false;
            }
            if (!have_)
            {
                const float n = std::sqrt(head[0] * head[0] + head[2] * head[2]);
                if (!std::isfinite(n) || n < 1e-4f)
                    return false;
                axis_[0] = head[0] / n;
                axis_[1] = 0;
                axis_[2] = head[2] / n;
                have_ = true;
            }
            for (int i = 0; i < 3; ++i)
                out[i] = axis_[i];
            return true;
        }

    private:
        float axis_[3]{};
        bool have_{};
    };

    class Handles
    {
    public:
        void Reset() noexcept { *this = Handles{}; }
        bool Holding() const noexcept { return held_[0] || held_[1]; }
        Output Update(const Input& in, const Settings& c) noexcept
        {
            Output out{};
            const Hand* hands[2]{&in.left, &in.right};
            bool valid[2], pressed[2], letGo[2];
            for (int h = 0; h < 2; ++h)
            {
                valid[h] = hands[h]->valid && std::isfinite(hands[h]->grip) && Finite3(hands[h]->pos);
                pressed[h] = valid[h] && hands[h]->grip >= kGrabGrip;
                letGo[h] = !valid[h] || hands[h]->grip <= kReleaseGrip;
            }
            if (!in.available || !Finite3(in.head) || !Finite3(in.bodyForward))
            {
                out.released = Holding();
                const bool armed0 = !pressed[0], armed1 = !pressed[1];
                Reset();
                wasPressed_[0] = !armed0;
                wasPressed_[1] = !armed1;
                return out;
            }
            const float* f = in.bodyForward;
            const float right[3]{-f[2], 0, f[0]};
            const float span = Clamp(c.span, 0.10f, 0.50f, 0.24f);
            float centre[3], rest[2][3];
            for (int i = 0; i < 3; ++i)
            {
                centre[i] = in.head[i] + f[i] * Clamp(c.forward, 0.15f, 0.70f, 0.40f) -
                            (i == 1 ? Clamp(c.down, 0.05f, 0.60f, 0.30f) : 0.0f);
                rest[0][i] = centre[i] - right[i] * span * 0.5f;
                rest[1][i] = centre[i] + right[i] * span * 0.5f;
            }
            const float zone = Clamp(c.zone, 0.05f, 0.40f, 0.18f);
            for (int h = 0; h < 2; ++h)
            {
                const bool inZone = valid[h] && Distance(hands[h]->pos, rest[h]) <= zone;
                const bool rising = pressed[h] && !wasPressed_[h];
                if (held_[h] && letGo[h])
                {
                    held_[h] = false;
                    out.released = true;
                }
                else if (!held_[h] && rising && inZone)
                {
                    held_[h] = true;
                    out.grabbed = true;
                    (h ? out.grabRight : out.grabLeft) = true;
                }
                wasPressed_[h] = pressed[h] ? true : (letGo[h] ? false : wasPressed_[h]);
                (h ? out.consumeRight : out.consumeLeft) = held_[h] || (pressed[h] && inZone);
            }
            const int count = held_[0] + held_[1];
            if (count == 0)
            {
                blendSince_ = 0;
                haveOut_ = false;
                return out;
            }
            for (int i = 0; i < 3; ++i)
                out.origin[i] = centre[i];
            // Handles midpoint, with a lone hand standing in for its pair.
            float mid[3];
            for (int i = 0; i < 3; ++i)
            {
                if (count == 2)
                    mid[i] = (hands[0]->pos[i] + hands[1]->pos[i]) * 0.5f;
                else
                {
                    const int h = held_[1] ? 1 : 0;
                    mid[i] = hands[h]->pos[i] + (centre[i] - rest[h][i]);
                }
            }
            const float arm = Clamp(c.pivot, 0.10f, 1.00f, 0.35f);
            float dir[3];
            for (int i = 0; i < 3; ++i)
            {
                if (c.lever)
                    dir[i] = (centre[i] + f[i] * arm) - mid[i]; // pivot ahead: a real lever
                else
                    dir[i] = mid[i] - (centre[i] - f[i] * arm); // pivot behind: point the handles
            }
            float target[4];
            if (!LookRotation(dir, target))
                return out;
            if (!blendSince_)
            {
                blendSince_ = in.now ? in.now : 1;
                for (int i = 0; i < 4; ++i)
                    start_[i] = in.controller[i];
                if (!Finite4(start_))
                    for (int i = 0; i < 4; ++i)
                        start_[i] = target[i];
                for (int i = 0; i < 4; ++i)
                    last_[i] = start_[i];
                lastAt_ = in.now;
                haveOut_ = true;
            }
            // PR153: smoothstep from the controller's aim to the handles over 1 s.
            const float t = std::clamp(static_cast<float>(in.now - blendSince_) * 0.001f / kBlendSeconds, 0.0f, 1.0f);
            const float s = t * t * (3.0f - 2.0f * t);
            float goal[4];
            Slerp(start_, target, s, goal);
            // PR153: never faster than 360 deg/s.
            const float dt = std::clamp(static_cast<float>(in.now - lastAt_) * 0.001f, 0.0f, 0.05f);
            const float maxStep = kMaxDegPerSec / 57.2957795f * dt;
            const float angle = AngleBetween(last_, goal);
            if (angle > maxStep && angle > 1e-6f)
                Slerp(last_, goal, maxStep / angle, out.orientation);
            else
                for (int i = 0; i < 4; ++i)
                    out.orientation[i] = goal[i];
            for (int i = 0; i < 4; ++i)
                last_[i] = out.orientation[i];
            lastAt_ = in.now;
            out.aiming = true;
            return out;
        }

    private:
        static bool Finite3(const float v[3]) noexcept
        {
            return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
        }
        static bool Finite4(const float v[4]) noexcept { return Finite3(v) && std::isfinite(v[3]); }
        static float Distance(const float a[3], const float b[3]) noexcept
        {
            const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
            return std::sqrt(x * x + y * y + z * z);
        }
        static float Clamp(float v, float lo, float hi, float d) noexcept
        {
            return std::isfinite(v) ? std::clamp(v, lo, hi) : d;
        }
        bool held_[2]{}, wasPressed_[2]{}, haveOut_{};
        uint64_t blendSince_{}, lastAt_{};
        float start_[4]{0, 0, 0, 1}, last_[4]{0, 0, 0, 1};
    };
} // namespace turret_handles
