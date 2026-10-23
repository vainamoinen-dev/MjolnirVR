// Frame pacing decisions for the OpenXR frame loop: real display-period changes and when the
// frame-wait worker may run. Used by dll/core/vr.cpp and tests/core_tests.cpp.
// Pure logic: no OpenXR calls, no globals.
#pragma once

#include <cstdint>

// OpenXR may vary predictedDisplayPeriod by a few nanoseconds without changing
// cadence. Treat only a relative change larger than five percent as a pacing
// transition. This is deliberately expressed as a ratio: no headset refresh
// rate is assumed.
inline constexpr bool IsMaterialFramePeriodTransition(uint64_t previousPeriodNs, uint64_t currentPeriodNs)
{
    if (!previousPeriodNs || !currentPeriodNs)
        return false;

    const uint64_t smaller = previousPeriodNs < currentPeriodNs ? previousPeriodNs : currentPeriodNs;
    const uint64_t larger = previousPeriodNs < currentPeriodNs ? currentPeriodNs : previousPeriodNs;
    return larger - smaller > smaller / 20;
}

// The render thread may release a claimed Wait(N) packet immediately before
// Begin(N). OpenXR then requires the worker's subsequent Wait(N+1) to block
// until Begin(N), keeping runtime pacing on the dedicated wait thread. Never
// release for a state that was not claimed from that worker.
inline constexpr bool ShouldReleaseFrameWaitWorkerBeforeBegin(bool waitThreadActive, bool workerPacketClaimed)
{
    return waitThreadActive && workerPacketClaimed;
}

enum class FrameWaitPermit
{
    Park,
    StartNextWait,
    Fault,
};

// Auto-reset events are only wakeups. Exact sequence ownership decides whether
// the worker may issue another xrWaitFrame, so a timeout or stale event credit
// can never create a second/concurrent wait.
inline constexpr FrameWaitPermit ClassifyFrameWaitPermit(uint64_t publishedSequence, uint64_t releasedSequence)
{
    if (!publishedSequence || releasedSequence > publishedSequence)
        return FrameWaitPermit::Fault;
    if (releasedSequence == publishedSequence)
        return FrameWaitPermit::StartNextWait;
    return FrameWaitPermit::Park;
}

// Once Wait(N) is released, the only legal worker dispatch observed before
// Begin(N) is Wait(N+1). This narrows the scheduler race to the call boundary;
// only the runtime can observe actual API entry.
inline constexpr bool IsExpectedNextFrameWaitDispatch(uint64_t claimedSequence, uint64_t dispatchSequence)
{
    return claimedSequence != 0 && claimedSequence != UINT64_MAX && dispatchSequence == claimedSequence + 1;
}
