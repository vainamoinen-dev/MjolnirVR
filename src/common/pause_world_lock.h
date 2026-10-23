// Pause screen world lock: places the paused game screen once and keeps it there.
// Used by dll/core/vr.cpp and tests/game_menu_pointer_tests.cpp. Pure logic: no globals.
#pragma once
#include "math_types.h"
// Pause screen world lock: the paused game screen is placed once, where the head
// looked when the pause began (yaw only, at eye height), and keeps that pose
// until the pause ends, instead of following the gaze.
#include <cmath>

namespace pause_world_lock
{
    using Pose = mvr_math::Pose;
    // Why the quad is (not) anchored this frame, for the "pause quad:" mode log.
    enum class Mode
    {
        NotPaused,
        Anchored,
        SwitchOff,
        NoHeadPose
    };
    inline const char* ModeName(Mode m) noexcept
    {
        return m == Mode::Anchored     ? "anchored"
               : m == Mode::SwitchOff  ? "following head because the switch is off"
               : m == Mode::NoHeadPose ? "following head because no head pose to anchor yet"
                                       : "not paused";
    }
    // A pause blip (Reach load end, presentation flips) would drop the anchor, and
    // the next pause frame would anchor again wherever the head now looked, so the
    // pause screen stayed put sometimes and followed the gaze other times (found in
    // headset testing). The anchor survives an unpause shorter than this.
    inline constexpr unsigned long long kKeepAnchorMs = 1000;

    class Anchor
    {
    public:
        // Returns true with the quad pose (LOCAL space) while the lock holds. False = keep the
        // head-locked quad (not paused, switch off, or no head pose to capture yet).
        // nowMs 0 = no time source: the anchor drops on the first unpaused frame.
        bool Update(bool paused, bool enabled, bool headValid, const float headForward[3], const float headPosition[3],
            float distanceM, Pose& out, unsigned epoch = 0, unsigned long long nowMs = 0) noexcept
        {
            if (!enabled)
            {
                valid_ = false;
                mode_ = paused ? Mode::SwitchOff : Mode::NotPaused;
                return false;
            }
            if (!paused)
            {
                mode_ = Mode::NotPaused;
                if (!nowMs)
                    valid_ = false;
                return false;
            }
            // Expiry needs no unpaused call (the stock-screen path stops calling once
            // stereo resumes): a pause that starts kKeepAnchorMs or more after the
            // last paused frame takes a fresh anchor.
            if (nowMs)
            {
                if (valid_ && (nowMs < lastPausedMs_ || nowMs - lastPausedMs_ >= kKeepAnchorMs))
                    valid_ = false;
                lastPausedMs_ = nowMs;
            }
            if (valid_ && epoch != epoch_)
                valid_ = false; // recentred while paused: take a fresh anchor
            epoch_ = epoch;
            if (!valid_)
            {
                mode_ = Mode::NoHeadPose;
                if (!headValid)
                    return false;
                const float yaw = std::atan2(-headForward[0], -headForward[2]);
                if (!std::isfinite(yaw))
                    return false;
                sin_ = std::sin(yaw * 0.5f);
                cos_ = std::cos(yaw * 0.5f);
                for (int i = 0; i < 3; ++i)
                    pos_[i] = headPosition[i];
                valid_ = true;
                fresh_ = true;
            }
            mode_ = Mode::Anchored;
            // -Z rotated by the yaw about +Y
            const float yaw = 2.0f * std::atan2(sin_, cos_);
            out.orientation = {0, sin_, 0, cos_};
            out.position = {pos_[0] - std::sin(yaw) * distanceM, pos_[1], pos_[2] - std::cos(yaw) * distanceM};
            return true;
        }
        // True once after each capture (pause start or recapture), for one log line.
        bool TakeFresh() noexcept
        {
            const bool f = fresh_;
            fresh_ = false;
            return f;
        }
        Mode LastMode() const noexcept { return mode_; }

    private:
        Mode mode_{Mode::NotPaused};
        unsigned long long lastPausedMs_{};
        bool fresh_{};
        bool valid_{};
        unsigned epoch_{};
        float sin_{}, cos_{1}, pos_[3]{};
    };
} // namespace pause_world_lock
