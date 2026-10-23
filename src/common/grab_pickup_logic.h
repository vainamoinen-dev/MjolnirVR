#pragma once
// Grab-pickup timing: centre the native pickup search on the reaching hand and press Use.
// Used by dll/core/game.cpp, dll/titles/reach/reach_grab_pickup.inl and tests/grab_pickup_tests.cpp.
//
// "Grab a gun off the floor" (Reach single player). The game keeps
// every decision: while the support hand reaches with a half grip, the native
// pickup search is centred on that hand instead of the body, so the game picks
// the weapon nearest the hand under its own rules (radius, can-pickup checks,
// player_disable_weapon_pickup). When the game's own chosen target is a weapon
// under the hand, a full grip presses the game's Use / pick-up input. The press
// is held only while that chosen target stays the SAME weapon: if the game's
// pick changes (a vehicle, turret, switch, another weapon, nothing) the press
// is released at once. Nothing here calls the engine; it is pure timing.
#include <cmath>
#include <cstdint>

namespace grab_pickup
{
    inline constexpr uint32_t kNoTarget = 0xFFFFFFFFu;

    // Game simulation type gate (docs/GRAB-G1-PLAN.md 7.1). 1 = local; 3 =
    // synchronous host, which MCC also uses for a solo campaign, allowed only
    // while no other machine is in the session. otherMachines < 0 = unknown.
    inline bool SimAllowsGrab(int sim, int otherMachines) noexcept
    {
        return sim == 1 || (sim == 3 && otherMachines == 0);
    }
    // Reach 0x58E70's own test: this machine's index (-1 = not in a session)
    // and the session's machine mask. -1 when the index is unusable.
    inline int MachinesOtherThan(int32_t index, uint32_t mask) noexcept
    {
        if (index < 0 || index > 31)
            return -1;
        uint32_t others = mask & ~(1u << index);
        int count = 0;
        for (; others; others &= others - 1)
            ++count;
        return count;
    }

    struct Settings
    {
        float reachGrip = 0.30f;   // half grip: the search follows the hand
        float grabGrip = 0.85f;    // full grip: press pick up (rising edge)
        float releaseGrip = 0.60f; // below this the next full grip is a new grab
        float minDropM = 0.35f;    // hand at least this far below the head (reaching down/out)
        uint64_t pressMs = 700;    // how long the native pick-up input is held
        uint64_t cooldownMs = 1000;
        uint64_t stableMs = 175;  // the same chosen weapon this long before a press may start
        uint64_t verdictMs = 400; // after the pick moves mid-press: time for the held weapon to change
    };

    struct Input
    {
        bool enabled = false;   // switch on, title proven, single-player gate passed
        bool handValid = false; // support hand tracked and its world point published
        bool handBusy = false;  // the support hand is already holding something (two-hand grip, wheel...)
        float grip = 0.0f;      // support grip 0..1
        float handDropM = 0.0f; // head height minus hand height, metres
        // The game's final pickup target from a hand-steered search, when it is a
        // weapon (its object handle); kNoTarget otherwise (none, vehicle, device...).
        uint32_t target = kNoTarget;
        // The weapon in the player's hand (object handle); kNoTarget when unknown or unarmed.
        uint32_t held = kNoTarget;
        uint64_t nowMs = 0;
    };

    struct Output
    {
        bool steerSearch = false; // centre the native pickup search on the hand
        bool pressPickup = false; // hold the native Use / pick-up input now
        bool haptic = false;      // one tick: a weapon prompt just appeared under the hand
        bool fired = false;       // a grab pickup started this update ("Action fired")
        bool taken = false;       // the held weapon changed after the press: the grab worked
        bool aborted = false;     // the pick changed mid-press and the held weapon did not
    };

    class Grab
    {
    public:
        Output Update(const Input& in, const Settings& s = {}) noexcept
        {
            Output out{};
            const bool reaching =
                in.enabled && in.handValid && !in.handBusy && in.grip >= s.reachGrip && in.handDropM >= s.minDropM;
            if (in.grip < s.releaseGrip)
                armed_ = true;
            // A press, or the short wait after it, ends as "taken" when the
            // weapon in the hand changes (A4-18: the game's pick moves as soon
            // as the weapon is taken, so a moved pick alone is not an abort).
            // An empty hand at the press (a grip-held drop) counts once a
            // weapon is in it.
            if ((pressUntil_ || verdictUntil_) && (heldAtFire_ != kNoTarget || in.held != kNoTarget) &&
                in.held != heldAtFire_)
            {
                pressUntil_ = verdictUntil_ = 0;
                heldAtFire_ = kNoTarget;
                out.taken = true;
            }
            if (pressUntil_ && in.nowMs >= pressUntil_)
                pressUntil_ = 0;
            if (verdictUntil_ && in.nowMs >= verdictUntil_)
            {
                verdictUntil_ = 0;
                out.aborted = true;
            }
            out.steerSearch = reaching || pressUntil_ != 0; // keep the target until the press ends
            // Only the weapon the press was started on may receive it: release
            // at once, and wait for the held weapon before calling it an abort.
            if (pressUntil_ && in.target != held_)
            {
                pressUntil_ = 0;
                verdictUntil_ = in.nowMs + s.verdictMs;
            }
            const bool prompt = reaching && in.target != kNoTarget;
            const bool newPick = prompt && (!lastPrompt_ || in.target != lastTarget_);
            if (!prompt || newPick)
                pickSince_ = in.nowMs; // a flicker restarts the wait
            out.haptic = newPick;
            lastPrompt_ = prompt;
            lastTarget_ = prompt ? in.target : kNoTarget;
            if (prompt && armed_ && in.grip >= s.grabGrip && !pressUntil_ && in.nowMs >= pickSince_ + s.stableMs &&
                (!lastFire_ || in.nowMs >= lastFire_ + s.cooldownMs))
            {
                armed_ = false;
                lastFire_ = in.nowMs ? in.nowMs : 1;
                pressUntil_ = in.nowMs + s.pressMs;
                verdictUntil_ = 0;
                held_ = in.target;
                heldAtFire_ = in.held;
                out.fired = true;
                out.steerSearch = true;
            }
            if (!in.enabled || !in.handValid)
                pressUntil_ = 0; // never hold a press past a gate
            out.pressPickup = pressUntil_ != 0 && in.nowMs < pressUntil_;
            return out;
        }
        void Reset() noexcept { *this = Grab{}; }

    private:
        bool armed_ = true, lastPrompt_ = false;
        uint32_t lastTarget_ = kNoTarget, held_ = kNoTarget, heldAtFire_ = kNoTarget;
        uint64_t pressUntil_ = 0, lastFire_ = 0, pickSince_ = 0, verdictUntil_ = 0;
    };

    // Nearest-weapon line (docs/GRAB-G1-PLAN.md section 2): the offset from the
    // hand to the weapon in the player's body frame, from the head's yaw in
    // tracking space (OpenXR: +x right, +y up, -z forward; quaternion x,y,z,w).
    inline bool BodyOffset(const float trackingDelta[3], const float headQ[4], float out[3]) noexcept
    {
        const float x = headQ[0], y = headQ[1], z = headQ[2], w = headQ[3];
        const float fx = -2 * (x * z + w * y), fz = -(1 - 2 * (x * x + y * y));
        const float length = std::sqrt(fx * fx + fz * fz);
        if (!std::isfinite(length) || length < 1e-4f)
            return false;
        const float forward[2]{fx / length, fz / length}, right[2]{-forward[1], forward[0]};
        out[0] = trackingDelta[0] * right[0] + trackingDelta[2] * right[1];
        out[1] = trackingDelta[1];
        out[2] = trackingDelta[0] * forward[0] + trackingDelta[2] * forward[1];
        return std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]);
    }
    // 0.25 m buckets (Reviewer condition 5: the line logs on a change of id or bucket).
    inline int Bucket(float metres) noexcept
    {
        return std::isfinite(metres) ? static_cast<int>(std::lround(metres * 4.0f)) : 0x7FFFFFFF;
    }
    class NearestWeaponLog
    {
    public:
        // true when the line should print: a new weapon (or none), or any axis in a new bucket.
        bool Changed(uint32_t weapon, const float offset[3]) noexcept
        {
            int buckets[3]{};
            if (weapon != kNoTarget)
                for (int i = 0; i < 3; ++i)
                    buckets[i] = Bucket(offset[i]);
            const bool changed = !seen_ || weapon != weapon_ || buckets[0] != buckets_[0] ||
                                 buckets[1] != buckets_[1] || buckets[2] != buckets_[2];
            seen_ = true;
            weapon_ = weapon;
            for (int i = 0; i < 3; ++i)
                buckets_[i] = buckets[i];
            return changed;
        }

    private:
        bool seen_ = false;
        uint32_t weapon_ = kNoTarget;
        int buckets_[3]{};
    };
} // namespace grab_pickup
