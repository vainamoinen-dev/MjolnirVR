// Co-op drop probe, testable half: the per-frame Present timing record and its once-a-second
// summary. Used by dll/core/d3d11_hook.cpp, dll/core/title_adapter.cpp and tests/core_tests.cpp.
// Pure logic: no engine, D3D or OpenXR access.
#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime_types.h"

// --- Co-op drop probe -------------------------------------------------------
// MCC campaign co-op runs a synchronised simulation across both machines, and
// this mod submits the whole VR frame INSIDE MCC's own Present, on MCC's own
// render thread. If that hold makes one machine chronically late, the shared
// session can collapse -- and a co-op collapse takes BOTH players to the menu,
// which is exactly the reported symptom.
//
// This header is the pure, testable half: the per-frame record and the
// reduction that turns a ring of them into one line per second. Nothing here
// touches the engine, D3D or OpenXR, so the maths is unit-tested directly.

// One frame of evidence. "hold" is the time the MOD adds inside Present -- the
// whole hook minus the game's own DXGI call -- because that, not the total, is
// the delay we impose on the thread MCC paces its simulation from.
struct CoopProbeSample
{
    uint32_t tickMs = 0;     // GetTickCount64() truncated; only deltas are used
    float holdMs = 0.0f;     // time the MOD added inside Present
    float dxgiMs = 0.0f;     // the game's own Present call
    float intervalMs = 0.0f; // wall time since the previous frame's hook entry
};

// One second of frames, reduced. Deliberately average / maximum / over-threshold
// counts and no percentile: a percentile needs a sort, and a sort of thousands
// of samples is not something to run while the game's render thread is live.
struct CoopProbeBucket
{
    uint32_t frames = 0;
    float holdAvgMs = 0.0f;
    float holdMaxMs = 0.0f;
    uint32_t holdOver4Ms = 0;
    uint32_t holdOver8Ms = 0;
    float dxgiAvgMs = 0.0f;
    float dxgiMaxMs = 0.0f;
    float intervalAvgMs = 0.0f;
    float intervalMaxMs = 0.0f;
};

// Reduce `count` samples into one-second buckets relative to `dumpTickMs`.
//
// IMPORTANT: buckets are indexed by AGE -- out[0] is the second immediately
// before the dump, out[1] the second before that, and so on. The caller walks
// the result backwards to print oldest-first. Samples older than `maxBuckets`
// seconds are dropped. Returns the number of bucket slots that hold frames
// (i.e. highest populated age + 1), so a caller can skip trailing empties.
//
// uint32 tick arithmetic is wrap-safe: the subtraction below is modular, so a
// capture spanning a GetTickCount64 truncation boundary still ages correctly.
inline size_t CoopProbeSummarise(
    const CoopProbeSample* samples, size_t count, uint32_t dumpTickMs, CoopProbeBucket* out, size_t maxBuckets)
{
    if (!samples || !out || !maxBuckets)
        return 0;

    for (size_t i = 0; i < maxBuckets; ++i)
        out[i] = CoopProbeBucket{};

    size_t populated = 0;
    for (size_t i = 0; i < count; ++i)
    {
        const CoopProbeSample& s = samples[i];
        const uint32_t age = (dumpTickMs - s.tickMs) / 1000u;
        if (age >= maxBuckets)
            continue;

        CoopProbeBucket& b = out[age];
        // The average fields carry running sums until the normalise pass below.
        b.frames++;
        b.holdAvgMs += s.holdMs;
        b.dxgiAvgMs += s.dxgiMs;
        b.intervalAvgMs += s.intervalMs;
        if (s.holdMs > b.holdMaxMs)
            b.holdMaxMs = s.holdMs;
        if (s.dxgiMs > b.dxgiMaxMs)
            b.dxgiMaxMs = s.dxgiMs;
        if (s.intervalMs > b.intervalMaxMs)
            b.intervalMaxMs = s.intervalMs;
        if (s.holdMs > 4.0f)
            b.holdOver4Ms++;
        if (s.holdMs > 8.0f)
            b.holdOver8Ms++;
        if (age + 1 > populated)
            populated = age + 1;
    }

    for (size_t i = 0; i < maxBuckets; ++i)
    {
        CoopProbeBucket& b = out[i];
        if (!b.frames)
            continue;
        const float n = static_cast<float>(b.frames);
        b.holdAvgMs /= n;
        b.dxgiAvgMs /= n;
        b.intervalAvgMs /= n;
    }
    return populated;
}

// The teardown edge worth capturing. A co-op kick always shows up as a level
// mode falling to Loading; Shell and Unsupported are already out of a level, so
// arriving at Loading from them is ordinary menu traffic and carries no run-up.
inline constexpr bool CoopProbeIsInLevelMode(RuntimeMode mode)
{
    switch (mode)
    {
    case RuntimeMode::Gameplay:
    case RuntimeMode::Paused:
    case RuntimeMode::Cutscene:
    case RuntimeMode::Vehicle:
    case RuntimeMode::Turret:
    case RuntimeMode::Dead:
        return true;
    default:
        return false;
    }
}
