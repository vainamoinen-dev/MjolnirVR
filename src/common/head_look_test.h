#pragma once
// Script look tests driven by head look (Halo CE and ODST).
// Used by dll/core/game.cpp, dll/core/look_test_feed.h, the CE, Halo 2 and Halo 4 look-test
// code and tests/odst_drop_pod_tests.cpp. No globals, no game memory, no logging.
//
// Both engines' player_action_test_look_relative_up/down/left/right read
// action-test bits 7/8/9/10 of an accumulator the script's
// player_action_test_reset clears. The engine sets a bit on any tick whose
// FACING delta has that sign: pitch > 0 up, < 0 down, yaw > 0 left, < 0 right.
// There is no threshold and no dwell (ODST builder +0x114FD0, CE +0xA9A4D6).
//
// In VR the head turns the VIEW, not the facing, so head look alone never
// sets them. That leaves the ODST pod's "look around the pod" step and CE's
// opening "look around" tutorial stuck. Method from the earlier CE VR mod (its CE
// UpdatePitchYaw hook feeds head-minus-facing as the native look delta): the
// title's builder hook hands the builder - and only the builder - a look
// delta in the direction the head just turned, then restores the real input,
// so the game sets its own bits and facing/view/aim never change. Each
// kStepRadians of head travel one way from the axis' last count is one look.
#include <atomic>
#include <cmath>
#include <cstdint>

namespace head_look_test
{
    inline constexpr float kStepRadians = 0.0873f; // 5 degrees
    inline constexpr uint64_t kUpBit = 1ull << 7;
    inline constexpr uint64_t kDownBit = 1ull << 8;
    inline constexpr uint64_t kLeftBit = 1ull << 9;
    inline constexpr uint64_t kRightBit = 1ull << 10;
    inline constexpr uint64_t kAllBits = kUpBit | kDownBit | kLeftBit | kRightBit;
    struct Anchor
    {
        bool armed = false;
        float yaw = 0.0f;   // head yaw at the last count, + = looking right
        float pitch = 0.0f; // head pitch at the last count, + = looking up
    };

    // Head yaw (+ right) and pitch (+ up) from an OpenXR head orientation
    // (x, y, z, w; -Z forward, +Y up).
    inline void YawPitchFromQuat(const float q[4], float& yaw, float& pitch) noexcept
    {
        const float x = q[0], y = q[1], z = q[2], w = q[3];
        const float fx = -2.0f * (w * y + x * z);
        const float fy = 2.0f * (w * x - y * z);
        const float fz = -(1.0f - 2.0f * (x * x + y * y));
        yaw = std::atan2(fx, -fz);
        pitch = std::asin(fy < -1.0f ? -1.0f : fy > 1.0f ? 1.0f : fy);
    }

    // Feeds one head sample; returns the look-test bits it completes (0 or more).
    inline uint64_t Step(Anchor& anchor, float headYaw, float headPitch) noexcept
    {
        if (!(headYaw == headYaw) || !(headPitch == headPitch))
        {
            anchor = {};
            return 0;
        }
        if (!anchor.armed)
        {
            anchor = {true, headYaw, headPitch};
            return 0;
        }
        constexpr float kPi = 3.14159265f;
        float yawTravel = headYaw - anchor.yaw;
        while (yawTravel > kPi)
            yawTravel -= 2.0f * kPi;
        while (yawTravel < -kPi)
            yawTravel += 2.0f * kPi;
        uint64_t bits = 0;
        if (yawTravel >= kStepRadians)
        {
            bits |= kRightBit;
            anchor.yaw = headYaw;
        }
        else if (yawTravel <= -kStepRadians)
        {
            bits |= kLeftBit;
            anchor.yaw = headYaw;
        }
        const float pitchTravel = headPitch - anchor.pitch;
        if (pitchTravel >= kStepRadians)
        {
            bits |= kUpBit;
            anchor.pitch = headPitch;
        }
        else if (pitchTravel <= -kStepRadians)
        {
            bits |= kDownBit;
            anchor.pitch = headPitch;
        }
        return bits;
    }

    // Which look tests a script is polling right now, from hooks on the title's
    // look-test evaluates/readers. We feed only what is being asked:
    //  - all_directions polled: every counted direction (the look-around step);
    //  - single directions polled: just those;
    //  - BOTH directions of one axis polled (an up-vs-down invert check) and not
    //    all_directions: that axis is exclusive - never add a direction whose
    //    opposite the game word already holds (it keeps bits until the reset).
    //  - nothing polled: nothing is fed, whatever the head does.
    enum Poll : int
    {
        PollUp = 0,
        PollDown,
        PollLeft,
        PollRight,
        PollAll,
        PollCount
    };
    inline constexpr uint64_t kPollWindowMs = 2000;

    struct PollTracker
    {
        std::atomic<uint64_t> lastMs[PollCount]{};
        void Note(Poll poll, uint64_t nowMs) noexcept
        {
            lastMs[poll].store(nowMs ? nowMs : 1, std::memory_order_relaxed);
        }
        bool Recent(Poll poll, uint64_t nowMs) const noexcept
        {
            const uint64_t at = lastMs[poll].load(std::memory_order_relaxed);
            return at && nowMs >= at && nowMs - at <= kPollWindowMs;
        }
        void Clear() noexcept
        {
            for (auto& t : lastMs)
                t.store(0, std::memory_order_relaxed);
        }
    };

    struct PollSnapshot
    {
        bool polled[PollCount] = {};
    };
    inline PollSnapshot Snapshot(const PollTracker& tracker, uint64_t nowMs) noexcept
    {
        PollSnapshot s{};
        for (int i = 0; i < PollCount; ++i)
            s.polled[i] = tracker.Recent(static_cast<Poll>(i), nowMs);
        return s;
    }

    // The look bits to OR into the game word this tick.
    constexpr uint64_t FeedBits(const PollSnapshot& polls, uint64_t held, uint64_t gameBits) noexcept
    {
        uint64_t wanted = polls.polled[PollAll] ? kAllBits : 0;
        for (int i = 0; i < 4; ++i)
            if (polls.polled[i])
                wanted |= kUpBit << i;
        uint64_t bits = held & wanted;
        if (!polls.polled[PollAll])
        {
            if (polls.polled[PollUp] && polls.polled[PollDown])
            {
                if (gameBits & kUpBit)
                    bits &= ~kDownBit;
                if (gameBits & kDownBit)
                    bits &= ~kUpBit;
            }
            if (polls.polled[PollLeft] && polls.polled[PollRight])
            {
                if (gameBits & kLeftBit)
                    bits &= ~kRightBit;
                if (gameBits & kRightBit)
                    bits &= ~kLeftBit;
            }
        }
        return bits;
    }

    // The builders may transform the deltas they read (ODST/H2 clamp them against
    // look-inhibit flags; H2 also takes abs/negates pitch), and our restore would
    // undo that. Every such transform maps 0 to 0, so an axis is fed only while
    // its real delta is exactly 0 - restoring 0 is then exact. When the player is
    // really moving that axis, the stick/mouse sets the game's own bit anyway.
    constexpr uint64_t OnlyIdleAxes(uint64_t feed, float realYaw, float realPitch) noexcept
    {
        uint64_t bits = feed;
        if (realYaw != 0.0f)
            bits &= ~(kLeftBit | kRightBit);
        if (realPitch != 0.0f)
            bits &= ~(kUpBit | kDownBit);
        return bits;
    }

    // "UDLR" letters for the bits set (diagnostics); "-" when none.
    inline void Letters(uint64_t bits, char out[5]) noexcept
    {
        const char names[] = "UDLR";
        int n = 0;
        for (int i = 0; i < 4; ++i)
            if (bits & (kUpBit << i))
                out[n++] = names[i];
        if (!n)
            out[n++] = '-';
        out[n] = 0;
    }
} // namespace head_look_test
