#pragma once
// Shared look-test feed for titles whose native look tests read the player's
// look deltas from an input builder (Halo 2, Halo 4).
// Each title keeps its own hooks, signature proofs, game-word read (under its
// own SEH), builder ABI and log text; this owns the common steps:
//   - which look bits to feed this builder tick (polled tests x counted head
//     steps x the game's own word),
//   - feeding them into the idle look axes for one native builder call and
//     restoring the real deltas afterwards,
//   - the periodic "head look fed ..." report.
// Callers supply VR_GetHeadPose / VR_IsPausePresentation / GetTickCount64.
#include <windows.h>
#include <atomic>
#include <cstdint>

#include "../../common/head_look_test.h"
#include "vr.h"

namespace look_test_feed
{
    // readWord(uint64_t&) reads the title's look-test game word; false means
    // unavailable (or faulted), and nothing is fed this tick.
    template <class ReadWord>
    uint64_t BitsToFeed(head_look_test::Anchor& anchor, const head_look_test::PollTracker& tracker,
        const std::atomic<bool>& installed, const std::atomic<bool>& enabled, int bitShift, ReadWord readWord) noexcept
    {
        const uint64_t now = GetTickCount64();
        const head_look_test::PollSnapshot polls = head_look_test::Snapshot(tracker, now);
        bool anyPoll = false;
        for (bool polled : polls.polled)
            anyPoll |= polled;
        float q[4]{}, p[3]{};
        if (!anyPoll || !installed.load(std::memory_order_acquire) || !enabled.load(std::memory_order_relaxed) ||
            VR_IsPausePresentation() || !VR_GetHeadPose(q, p))
        {
            anchor = {};
            return 0;
        }
        float yaw = 0.0f, pitch = 0.0f;
        head_look_test::YawPitchFromQuat(q, yaw, pitch);
        const uint64_t counted = head_look_test::Step(anchor, yaw, pitch);
        if (!counted)
            return 0;
        uint64_t word = 0;
        if (!readWord(word))
            return 0;
        return head_look_test::FeedBits(polls, counted, word >> bitShift);
    }

    // One native builder call with `feed` (non-zero) written into the idle look
    // axes. The builders may clamp or abs/negate the deltas they read; every such
    // transform maps 0 to 0, so feeding idle axes only keeps the restore exact.
    template <class CallOriginal>
    void FeedIdleAxes(float* yaw, float* pitch, uint64_t feed, std::atomic<uint64_t>& fedBits,
        std::atomic<uint64_t>& fedTicks, CallOriginal callOriginal)
    {
        const float savedYaw = *yaw, savedPitch = *pitch;
        const uint64_t idle = head_look_test::OnlyIdleAxes(feed, savedYaw, savedPitch);
        if (!idle)
        {
            callOriginal();
            return;
        }
        const float step = head_look_test::kStepRadians;
        if (idle & head_look_test::kLeftBit)
            *yaw = step;
        else if (idle & head_look_test::kRightBit)
            *yaw = -step;
        if (idle & head_look_test::kUpBit)
            *pitch = step;
        else if (idle & head_look_test::kDownBit)
            *pitch = -step;
        __try
        {
            callOriginal();
        }
        __finally
        {
            *yaw = savedYaw;
            *pitch = savedPitch;
        }
        fedBits.fetch_or(idle, std::memory_order_relaxed);
        fedTicks.fetch_add(1, std::memory_order_relaxed);
    }

    // At most every 2 s, and only after something was fed: log(letters, ticks)
    // with the title's own literal line.
    template <class Log>
    void Report(
        uint64_t& lastReportMs, std::atomic<uint64_t>& fedBits, std::atomic<uint64_t>& fedTicks, Log log) noexcept
    {
        const uint64_t now = GetTickCount64();
        if (now - lastReportMs < 2000)
            return;
        lastReportMs = now;
        const uint64_t ticks = fedTicks.exchange(0, std::memory_order_relaxed);
        const uint64_t fed = fedBits.exchange(0, std::memory_order_relaxed);
        if (!ticks)
            return;
        char letters[5];
        head_look_test::Letters(fed, letters);
        log(static_cast<const char*>(letters), static_cast<unsigned long long>(ticks));
    }
} // namespace look_test_feed
