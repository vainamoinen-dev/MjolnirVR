#pragma once
// First-person driving: per-class steering mode, the grab wheel and the driver-seat trigger controls.
// Used by src/dll/core/game.cpp and the Halo 2 6DOF adapter; tested by tests/vehicle_wheel_tests.cpp.
// Pure logic: no globals, no game memory, no OpenXR.
//
// First-person driving: a per-class steering choice and a Vice City VR
// style grab wheel.
//
// - Steering per vehicle class (car / bike): Point (hand points the way, the
//   pre-existing default), Wheel (grab a virtual wheel), Stick (thumbstick).
//   Wheel and Stick make that driver seat follow the hull (the wheel and stick
//   need the view to turn with the car; the point servo needs it not to).
// - Wheel: grab at >= 0.65 grip, release at <= 0.30, re-squeeze inside the
//   grab zone (arriving with the grip already squeezed never grabs). Two
//   hands use the line between them, one hand the line from the wheel centre
//   (ignored within 6 cm of it). Jumps over 45 deg in one frame are ignored.
//   80 deg lock, 3 % deadzone. Letting go returns steering to the stick.
// - Wheel v2 (Settings::rotation; in headset testing v1 steered by
//   "which side of the screen my hand is"): after the grab the wheel angle
//   comes only from each holding hand's ROTATION about the wheel axis (the
//   body-forward axis frozen at the grab), measured from that hand's own
//   orientation at the grab. Hand position, head position and where the
//   head looks no longer steer. Two hands average their twists.
// - Vehicle context (driver seat in Wheel/Stick mode): right trigger throttle,
//   left trigger brake/reverse for unarmed vehicles, and holding a grip
//   outside the wheel zone exits through the game's own Use action.
//
// Pure logic: no globals, no game memory, no OpenXR.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vehicle_wheel
{
    enum class SteerClass : uint8_t
    {
        None,
        Car,
        Bike
    };
    enum SteerMode : int
    {
        Point = 0,
        Wheel = 1,
        Stick = 2
    };

    inline constexpr int ModeFor(SteerClass c, int carMode, int bikeMode) noexcept
    {
        const int m = c == SteerClass::Car ? carMode : c == SteerClass::Bike ? bikeMode : Point;
        return m == Wheel || m == Stick ? m : Point;
    }
    // The view follows the hull when the global switch says so, or for a driver
    // seat whose class is set to Wheel or Stick.
    // Only Wheel mode uses the grab wheel; Point keeps the old double-click
    // toggle exactly, and Stick has no wheel.
    inline constexpr bool UsesGrabWheel(int mode) noexcept
    {
        return mode == Wheel;
    }
    inline constexpr bool EffectiveFollow(bool viewFollow, bool driverSeat, SteerClass c, int mode) noexcept
    {
        return viewFollow || (driverSeat && c != SteerClass::None && (mode == Wheel || mode == Stick));
    }

    struct Settings
    {
        float radius{0.18f};   // virtual wheel radius, m
        float forward{0.40f};  // wheel centre ahead of the head
        float down{0.35f};     // wheel centre below the head
        float grabZone{0.23f}; // hand-to-rim distance that can grab
        float lockDeg{80.0f};
        float deadzone{0.03f};
        float exitHoldMs{800.0f};
        bool gripExit{true};    // Wheel/Stick context: a held grip out at the side exits
        bool allowGrab{true};   // Wheel mode only; Stick never grabs
        float exitSideM{0.15f}; // exit grip must be this far outside the wheel, to the side (hip/door)
        bool rotation{false};   // wheel v2: angle from hand rotation about the wheel axis
    };

    // Twist (radians) of the rotation from q0 to q1 about the unit `axis`
    // (world frame), right-hand rule: positive = counter-clockwise when looking
    // back along the axis. Swing-twist decomposition; NaN-safe (returns 0).
    inline float TwistAbout(const float q0[4], const float q1[4], const float axis[3]) noexcept
    {
        // d = q1 * conj(q0)
        const float a[4]{-q0[0], -q0[1], -q0[2], q0[3]};
        const float d[4]{q1[3] * a[0] + q1[0] * a[3] + q1[1] * a[2] - q1[2] * a[1],
            q1[3] * a[1] - q1[0] * a[2] + q1[1] * a[3] + q1[2] * a[0],
            q1[3] * a[2] + q1[0] * a[1] - q1[1] * a[0] + q1[2] * a[3],
            q1[3] * a[3] - q1[0] * a[0] - q1[1] * a[1] - q1[2] * a[2]};
        const float p = d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2];
        const float t = 2.0f * std::atan2(p, d[3]);
        if (!std::isfinite(t))
            return 0.0f;
        constexpr float pi = 3.14159265f;
        return t > pi ? t - 2 * pi : t < -pi ? t + 2 * pi : t;
    }
    inline constexpr float kGrabGrip = 0.65f, kReleaseGrip = 0.30f;
    inline constexpr float kCentreIgnoreM = 0.06f, kJumpDeg = 45.0f;

    struct Hand
    {
        float pos[3]{};
        float q[4]{0, 0, 0, 1};
        float grip{};
        bool valid{};
    };
    struct Input
    {
        uint64_t now{};
        bool available{}; // driver seat, wheel allowed, gameplay
        float head[3]{};
        float bodyForward[3]{}; // horizontal unit facing (room space, -z forward convention not assumed)
        Hand left{}, right{};
    };
    struct Output
    {
        float steer{};                // -1..1, + = right
        bool driving{};               // the wheel, not the stick, steers
        bool grabbed{}, released{};   // edges, for ticks and logs
        bool grabLeft{}, grabRight{}; // edge per hand (which hand to tick)
        bool consumeLeft{}, consumeRight{};
        bool exitRequested{}; // grip held outside the wheel long enough
        float centre[3]{};
    };

    inline float Wrap(float a) noexcept
    {
        constexpr float pi = 3.14159265f;
        while (a > pi)
            a -= 2 * pi;
        while (a < -pi)
            a += 2 * pi;
        return a;
    }

    class GrabWheel
    {
    public:
        void Reset() noexcept { *this = GrabWheel{}; }
        bool Holding() const noexcept { return held_[0] || held_[1]; }
        Output Update(const Input& in, const Settings& c) noexcept
        {
            Output out{};
            const Hand* hands[2]{&in.left, &in.right};
            bool valid[2], pressed[2], releasedGrip[2];
            for (int h = 0; h < 2; ++h)
            {
                valid[h] = hands[h]->valid && std::isfinite(hands[h]->grip) && Finite3(hands[h]->pos);
                pressed[h] = valid[h] && hands[h]->grip >= kGrabGrip;
                releasedGrip[h] = !valid[h] || hands[h]->grip <= kReleaseGrip;
            }
            if (!in.available || !Finite3(in.head) || !Finite3(in.bodyForward))
            {
                const bool wasHolding = Holding();
                Reset();
                // Keep a squeezed grip hidden until it is let go.
                for (int h = 0; h < 2; ++h)
                    wasPressed_[h] = pressed[h];
                out.released = wasHolding;
                return out;
            }
            // Wheel frame: centre ahead of and below the head on the body heading.
            const float* f = in.bodyForward;
            const float right[3]{-f[2], 0, f[0]};
            const float up[3]{0, 1, 0};
            const float radius = Clamp(c.radius, 0.10f, 0.30f, 0.18f);
            for (int i = 0; i < 3; ++i)
                out.centre[i] = in.head[i] + f[i] * Clamp(c.forward, 0.15f, 0.70f, 0.40f) -
                                up[i] * Clamp(c.down, 0.10f, 0.70f, 0.35f);
            const float zone = Clamp(c.grabZone, 0.05f, 0.40f, 0.23f);
            bool inZone[2];
            float planar[2][2]{};
            for (int h = 0; h < 2; ++h)
            {
                float v[3];
                for (int i = 0; i < 3; ++i)
                    v[i] = hands[h]->pos[i] - out.centre[i];
                planar[h][0] = Dot(v, right);
                planar[h][1] = Dot(v, up);
                const float depth = Dot(v, f);
                const float ring = std::sqrt(planar[h][0] * planar[h][0] + planar[h][1] * planar[h][1]) - radius;
                inZone[h] = valid[h] && std::sqrt(ring * ring + depth * depth) <= zone;
            }
            const int before = held_[0] + held_[1];
            for (int h = 0; h < 2; ++h)
            {
                const bool rising = pressed[h] && !wasPressed_[h];
                if (held_[h] && releasedGrip[h])
                {
                    held_[h] = false;
                    out.released = true;
                }
                else if (c.allowGrab && !held_[h] && rising && inZone[h])
                {
                    held_[h] = true;
                    out.grabbed = true;
                    for (int i = 0; i < 4; ++i)
                        ref_[h][i] = hands[h]->q[i];
                    if (!held_[1 - h])
                        for (int i = 0; i < 3; ++i)
                            axis_[i] = f[i]; // frozen at the first grab
                    (h ? out.grabRight : out.grabLeft) = true;
                    exitSince_[h] = 0;
                }
                // Exit: a grip squeezed fresh outside the wheel and held there.
                // Exit only from out at the side (hip/door), well clear of the
                // wheel, so a missed grab just past the rim cannot exit in combat.
                const bool atSide = std::fabs(planar[h][0]) >= radius + zone + Clamp(c.exitSideM, 0.0f, 0.50f, 0.15f);
                if (c.gripExit && !held_[h] && pressed[h] && !inZone[h] && atSide && (rising || exitSince_[h]))
                {
                    if (!exitSince_[h])
                        exitSince_[h] = in.now ? in.now : 1;
                    if (!exitFired_[h] &&
                        in.now - exitSince_[h] >= static_cast<uint64_t>(Clamp(c.exitHoldMs, 200.0f, 2000.0f, 800.0f)))
                    {
                        exitFired_[h] = true;
                        out.exitRequested = true;
                    }
                }
                if (!pressed[h] || atSide == false)
                {
                    if (!pressed[h])
                        exitFired_[h] = false;
                    exitSince_[h] = 0;
                }
                wasPressed_[h] = pressed[h] ? true : (releasedGrip[h] ? false : wasPressed_[h]);
            }
            const int count = held_[0] + held_[1];
            float measured = 0.0f;
            bool measuredOk = false;
            if (c.rotation && count > 0)
            {
                // v2: steering = hand rotation about the wheel axis. The driver
                // looks along +forward, so a positive (right-hand rule) twist
                // about it is clockwise as the driver sees it; the wheel angle
                // here is counter-clockwise positive, hence the minus.
                float sum = 0.0f;
                bool ok = true;
                for (int h = 0; h < 2; ++h)
                    if (held_[h])
                    {
                        if (!FiniteQ(hands[h]->q))
                        {
                            ok = false;
                            break;
                        }
                        sum += -TwistAbout(ref_[h], hands[h]->q, axis_);
                    }
                if (ok)
                {
                    measured = sum / count;
                    measuredOk = true;
                }
            }
            else if (count == 2)
            {
                const float dx = planar[1][0] - planar[0][0], dy = planar[1][1] - planar[0][1];
                if (dx * dx + dy * dy > 0.0004f)
                {
                    measured = std::atan2(dy, dx);
                    measuredOk = true;
                }
            }
            else if (count == 1)
            {
                const int h = held_[1] ? 1 : 0;
                const float px = planar[h][0], py = planar[h][1];
                if (px * px + py * py >= kCentreIgnoreM * kCentreIgnoreM)
                {
                    // Counter-clockwise (as the driver sees it) is positive; the
                    // right hand rests at 3 o'clock, the left at 9 o'clock.
                    const float rest = h ? 0.0f : 3.14159265f;
                    measured = Wrap(std::atan2(py, px) - rest);
                    measuredOk = true;
                }
            }
            if (count == 0)
            {
                angle_ = 0.0f;
                haveMeasure_ = false;
            }
            else if (measuredOk)
            {
                if (count != before || !haveMeasure_)
                {
                    // A new grip arrangement keeps the wheel where it is: no snap.
                    offset_ = Wrap(measured - angle_);
                    lastMeasured_ = measured;
                    haveMeasure_ = true;
                }
                else if (std::fabs(Wrap(measured - lastMeasured_)) * 57.2957795f <= kJumpDeg)
                {
                    lastMeasured_ = measured;
                    const float lock = Clamp(c.lockDeg, 20.0f, 180.0f, 80.0f) / 57.2957795f;
                    angle_ = std::clamp(Wrap(measured - offset_), -lock, lock);
                }
                // else: a tracking glitch; keep the last wheel angle.
            }
            out.driving = count > 0;
            if (out.driving)
            {
                const float lock = Clamp(c.lockDeg, 20.0f, 180.0f, 80.0f) / 57.2957795f;
                const float s = -angle_ / lock; // counter-clockwise = left
                const float dz = Clamp(c.deadzone, 0.0f, 0.30f, 0.03f);
                const float mag = std::fabs(s) <= dz ? 0.0f : (std::fabs(s) - dz) / (1.0f - dz);
                out.steer = std::clamp(s < 0 ? -mag : mag, -1.0f, 1.0f);
            }
            out.consumeLeft = held_[0] || (pressed[0] && exitSince_[0]) || (c.allowGrab && pressed[0] && inZone[0]);
            out.consumeRight = held_[1] || (pressed[1] && exitSince_[1]) || (c.allowGrab && pressed[1] && inZone[1]);
            return out;
        }

    private:
        static bool Finite3(const float v[3]) noexcept
        {
            return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
        }
        static bool FiniteQ(const float q[4]) noexcept
        {
            return Finite3(q) && std::isfinite(q[3]) && q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3] > 0.25f;
        }
        static float Dot(const float a[3], const float b[3]) noexcept
        {
            return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        }
        static float Clamp(float v, float lo, float hi, float d) noexcept
        {
            return std::isfinite(v) ? std::clamp(v, lo, hi) : d;
        }
        bool held_[2]{}, wasPressed_[2]{}, exitFired_[2]{};
        uint64_t exitSince_[2]{};
        float angle_{}, offset_{}, lastMeasured_{};
        float ref_[2][4]{{0, 0, 0, 1}, {0, 0, 0, 1}}, axis_[3]{0, 0, -1};
        bool haveMeasure_{};
    };

    // Wheel feel. Turn rate (default): the wheel angle is the
    // turn stick, so a held angle keeps the look heading turning. Direct angle
    // (experimental): the wheel angle sets where the look points relative to the
    // hull, `steer * fullLockDeg` off the nose, and a servo drives the turn stick
    // to hold it. A centred wheel tracks the hull and drives straight; a held
    // angle is a steady turn, as on a real car. Halo still steers toward the look,
    // so nothing new is written to game memory. The servo deadband and gain are
    // the Reach hull servo's (R-V16 hand steer: 0.052 rad, 3 per rad).
    enum WheelFeel : int
    {
        TurnRate = 0,
        DirectAngle = 1
    };
    inline constexpr float kDirectDeadbandRad = 0.052f, kDirectGain = 3.0f;

    // Yaw is Halo's: radians, counter-clockwise (left) positive. steer is the
    // wheel output (+ = right). Returns the turn stick (+ = turn right).
    inline float DirectAngleStick(float steer, float hullYaw, float aimYaw, float fullLockDeg) noexcept
    {
        if (!std::isfinite(steer) || !std::isfinite(hullYaw) || !std::isfinite(aimYaw))
            return 0.0f;
        const float full = (std::isfinite(fullLockDeg) ? std::clamp(fullLockDeg, 15.0f, 90.0f) : 45.0f) / 57.2957795f;
        const float target = hullYaw - std::clamp(steer, -1.0f, 1.0f) * full;
        const float err = Wrap(target - aimYaw); // + = the look must swing left
        const float mag = std::fabs(err);
        if (mag <= kDirectDeadbandRad)
            return 0.0f;
        const float push = std::min((mag - kDirectDeadbandRad) * kDirectGain, 1.0f);
        return err > 0 ? -push : push;
    }

    // Trigger throttle for the vehicle context: right trigger forward, left
    // trigger brake/reverse, as a left-stick Y value (-1..1).
    inline float TriggerThrottle(float rightTrigger, float leftTrigger) noexcept
    {
        const auto t = [](float v) { return std::isfinite(v) ? std::clamp((v - 0.05f) / 0.95f, 0.0f, 1.0f) : 0.0f; };
        return std::clamp(t(rightTrigger) - t(leftTrigger), -1.0f, 1.0f);
    }
} // namespace vehicle_wheel
