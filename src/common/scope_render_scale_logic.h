#pragma once
// Adaptive scope pass resolution: drops the scope render to a quarter of its pixels while frames miss budget.
// Used by src/dll/core/vr.cpp once per presented frame; tested by tests/scope_render_scale_tests.cpp.
// Pure logic: no globals, no clock reads (the caller passes times in), no rendering calls.
//
// While the scope pass renders and the
// recent frames miss the headset's frame budget, the pass renders into the top-left
// half x half of its target (a quarter of the pixels); back to full size once the
// frames recover. Real-life rule (a scope does not flicker): every change is held,
// and a recovery that misses again soon doubles the next hold. The recovery window
// lies inside the hold: full size returns when the hold ends if its last ~3 s of
// scoped frames ran clean (10 s after the drop, not 10 + 3), later once they do.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace scope_render_scale
{
    constexpr float kLowScale = 0.5f;
    constexpr int kWindowFrames = 45;   // ~0.5 s at 90 Hz
    constexpr int kDropMisses = 3;      // misses in the window that drop to kLowScale (~7% frames lost)
    constexpr int kRecoverFrames = 270; // the latest ~3 s of scoped frames (counted during the hold) ...
    constexpr int kRecoverMisses = 1;   // ... with at most this many misses recovers to 1 once the hold ends
    constexpr float kMissFactor = 1.2f; // a frame interval over 1.2 x budget is a missed frame
    constexpr uint64_t kHoldMs = 10000; // the first low hold
    constexpr uint64_t kMaxHoldMs = 120000;
    constexpr uint64_t kRelapseMs = 30000; // a drop this soon after a recovery doubles the hold
    constexpr uint64_t kScopedGapMs = 150; // no scope pass for this long: the window starts again

    // Frame budget from the headset rate (0 or nonsense = 90 Hz).
    inline float BudgetMs(float refreshHz) noexcept
    {
        return std::isfinite(refreshHz) && refreshHz >= 30.0f && refreshHz <= 240.0f ? 1000.0f / refreshHz
                                                                                     : 1000.0f / 90.0f;
    }

    struct Change
    {
        bool changed = false;
        float from = 1.0f, to = 1.0f;
        int frames = 0, misses = 0;        // the window that decided it
        float p50 = 0, p95 = 0, maxMs = 0; // its frame intervals
        uint64_t holdMs = 0;               // after a drop: the earliest recovery (ms after the drop)
        uint64_t lowMs = 0;                // after a recovery: how long it was at kLowScale
    };

    class Controller
    {
    public:
        float Scale() const noexcept { return scale_; }

        // Back to full size with no history (title change, feature off).
        void Reset() noexcept
        {
            scale_ = 1.0f;
            count_ = 0;
            next_ = 0;
            holdMs_ = kHoldMs;
            changedMs_ = 0;
            recoveredMs_ = 0;
        }

        // One presented frame. scoped: a scope pass ran within kScopedGapMs (only those frames count).
        Change Update(float intervalMs, bool scoped, float budgetMs, uint64_t nowMs) noexcept
        {
            Change c{};
            if (!scoped || !std::isfinite(intervalMs) || intervalMs <= 0.0f)
            {
                if (!scoped)
                    count_ = 0, next_ = 0;
                return c;
            }
            ring_[next_] = intervalMs;
            next_ = (next_ + 1) % kRecoverFrames;
            count_ = std::min(count_ + 1, kRecoverFrames);
            const float missMs = budgetMs * kMissFactor;
            if (scale_ >= 1.0f)
            {
                if (count_ < kWindowFrames)
                    return c;
                const int misses = Misses(kWindowFrames, missMs);
                if (misses < kDropMisses)
                    return c;
                if (recoveredMs_ && nowMs - recoveredMs_ < kRelapseMs)
                    holdMs_ = std::min(holdMs_ * 2, kMaxHoldMs);
                else
                    holdMs_ = kHoldMs;
                Fill(c, kWindowFrames, misses);
                scale_ = kLowScale;
                changedMs_ = nowMs;
                count_ = 0;
                next_ = 0;
            }
            else
            {
                if (nowMs - changedMs_ < holdMs_ || count_ < kRecoverFrames)
                    return c;
                const int misses = Misses(kRecoverFrames, missMs);
                if (misses > kRecoverMisses)
                    return c;
                Fill(c, kRecoverFrames, misses);
                c.lowMs = nowMs - changedMs_;
                scale_ = 1.0f;
                changedMs_ = recoveredMs_ = nowMs;
                count_ = 0;
                next_ = 0;
            }
            c.changed = true;
            c.to = scale_;
            c.from = scale_ < 1.0f ? 1.0f : kLowScale;
            c.holdMs = scale_ < 1.0f ? holdMs_ : 0;
            return c;
        }

    private:
        // The latest n intervals (n <= count_), newest first.
        template <class F>
        void Latest(int n, F f) const noexcept
        {
            for (int i = 1; i <= n; ++i)
                f(ring_[(next_ - i + kRecoverFrames) % kRecoverFrames]);
        }
        int Misses(int n, float missMs) const noexcept
        {
            int m = 0;
            Latest(n, [&](float v) { m += v > missMs; });
            return m;
        }
        void Fill(Change& c, int n, int misses) const noexcept
        {
            float v[kRecoverFrames];
            int k = 0;
            Latest(n, [&](float x) { v[k++] = x; });
            std::sort(v, v + k);
            c.frames = k;
            c.misses = misses;
            c.p50 = v[k / 2];
            c.p95 = v[std::min(k - 1, static_cast<int>(std::ceil(0.95f * k)) - 1)];
            c.maxMs = v[k - 1];
        }

        float ring_[kRecoverFrames]{};
        int count_ = 0, next_ = 0;
        float scale_ = 1.0f;
        uint64_t holdMs_ = kHoldMs, changedMs_ = 0, recoveredMs_ = 0;
    };
} // namespace scope_render_scale
