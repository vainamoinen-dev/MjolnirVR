// Out-of-line code for title_runtime_state.h: ownership resolution and the lock-free per-title publication store.
// Compiled into the mod DLL and the unit-test target; callers include title_runtime_state.h.
// No globals, no locks, no logging, no operating-system calls.
#include "title_runtime_state.h"

#include <algorithm>

static_assert(std::atomic<uint32_t>::is_always_lock_free);
static_assert(std::atomic<uint64_t>::is_always_lock_free);

namespace
{
    constexpr uint32_t kSnapshotReadAttempts = 8;
    constexpr uint32_t kPublicationWriteAttempts = 16;

    uint32_t NextGeneration(uint32_t generation)
    {
        ++generation;
        return generation ? generation : 1;
    }

    bool TryBeginPublication(std::atomic<uint32_t>& sequence, uint32_t& writeSequence)
    {
        for (uint32_t attempt = 0; attempt < kPublicationWriteAttempts; ++attempt)
        {
            uint32_t expected = sequence.load(std::memory_order_acquire);
            if (expected & 1u)
                continue;
            writeSequence = expected + 1u;
            if (sequence.compare_exchange_weak(
                    expected, writeSequence, std::memory_order_acq_rel, std::memory_order_acquire))
            {
                return true;
            }
        }
        return false;
    }

    void EndPublication(std::atomic<uint32_t>& sequence, uint32_t writeSequence)
    {
        sequence.store(writeSequence + 1u, std::memory_order_release);
    }

    uint64_t PackStampedValue(uint32_t generation, uint32_t value)
    {
        return (uint64_t{generation} << 32) | value;
    }

    uint32_t StampedGeneration(uint64_t stamped)
    {
        return static_cast<uint32_t>(stamped >> 32);
    }

    uint32_t StampedValue(uint64_t stamped)
    {
        return static_cast<uint32_t>(stamped);
    }

    bool IsRuntimeModeValue(uint32_t value)
    {
        return value <= static_cast<uint32_t>(RuntimeMode::Unsupported);
    }
}

TitleRuntimeSnapshot ResolveTitleRuntime(const TitleRuntimeResolveInput& input) noexcept
{
    TitleRuntimeSnapshot resolved{};
    TitleRuntimeSnapshot candidateSnapshot{};

    for (const TitleRuntimeCandidate& candidate : input.titles)
    {
        const uint32_t availabilityBit = TitleRuntimeAvailabilityBit(candidate.title);
        if (!availabilityBit || (input.availabilityMask & availabilityBit) == 0 || candidate.generation == 0 ||
            !candidate.installed || candidate.teardownRequested || candidate.heartbeatFreshForMs == 0 ||
            candidate.heartbeatMs <= candidate.generationStartMs ||
            candidate.heartbeatMs <= input.availabilitySetEpochMs || input.nowMs < candidate.heartbeatMs ||
            input.nowMs - candidate.heartbeatMs >= candidate.heartbeatFreshForMs)
        {
            continue;
        }

        ++resolved.qualifyingOwnerCount;
        if (resolved.qualifyingOwnerCount != 1)
            continue;

        candidateSnapshot.owner = candidate.title;
        candidateSnapshot.generation = candidate.generation;
        candidateSnapshot.installed = candidate.installed;
        candidateSnapshot.armed = candidate.armed;
        candidateSnapshot.teardownRequested = candidate.teardownRequested;
        candidateSnapshot.mode = candidate.mode;
        candidateSnapshot.heartbeatMs = candidate.heartbeatMs;
        candidateSnapshot.enabledCapabilities = candidate.enabledCapabilities & kTitleRuntimeKnownCapabilities;
    }

    if (resolved.qualifyingOwnerCount == 1)
    {
        candidateSnapshot.qualifyingOwnerCount = 1;
        return candidateSnapshot;
    }

    // Zero or multiple candidates always fail open to stock behavior.
    resolved.owner = GameTitle::None;
    resolved.enabledCapabilities = TitleCapability_None;
    return resolved;
}

bool TitleRuntimeOwnershipMayBePending(
    const TitleRuntimeResolveInput& input, GameTitle retainedOwner, uint64_t graceMs) noexcept
{
    const uint32_t retainedBit = TitleRuntimeAvailabilityBit(retainedOwner);
    const uint32_t availableTitles = input.availabilityMask & kTitleRuntimeAvailabilityMask;
    if (!retainedBit || (availableTitles & retainedBit) == 0 || (availableTitles & (availableTitles - 1u)) == 0 ||
        input.nowMs < input.availabilitySetEpochMs || input.nowMs - input.availabilitySetEpochMs >= graceMs)
    {
        return false;
    }

    const TitleRuntimeCandidate* retained = nullptr;
    for (const TitleRuntimeCandidate& candidate : input.titles)
    {
        if (candidate.title != retainedOwner)
            continue;
        if (retained)
            return false;
        retained = &candidate;
    }
    if (!retained || retained->generation == 0 || retained->generationStartMs > input.nowMs || !retained->installed ||
        retained->teardownRequested)
    {
        return false;
    }

    return ResolveTitleRuntime(input).qualifyingOwnerCount == 0;
}

uint32_t TitleRuntimeMaskUnarmedCapabilities(
    const TitleRuntimeSnapshot& snapshot, uint32_t capabilitiesRequiringArm) noexcept
{
    return snapshot.armed ? snapshot.enabledCapabilities & kTitleRuntimeKnownCapabilities
                          : snapshot.enabledCapabilities & kTitleRuntimeKnownCapabilities & ~capabilitiesRequiringArm;
}

bool TitleRuntimeState::PublishModuleSet(const TitleRuntimeModuleSet& modules, uint64_t observedAtMs) noexcept
{
    if (modules.availabilityMask & ~kTitleRuntimeAvailabilityMask)
        return false;

    for (size_t index = 0; index < kTitleRuntimeSlotCount; ++index)
    {
        const bool available = (modules.availabilityMask & (uint32_t{1} << index)) != 0;
        if (available != (modules.moduleBases[index] != 0))
            return false;
    }

    uint32_t writeSequence = 0;
    if (!TryBeginPublication(m_availabilitySequence, writeSequence))
        return false;

    const uint32_t previousMask = m_availabilityMask.load(std::memory_order_relaxed);
    const uint64_t previousEpoch = m_availabilitySetEpochMs.load(std::memory_order_relaxed);
    bool moduleSetChanged = previousMask != modules.availabilityMask;
    for (size_t index = 0; index < kTitleRuntimeSlotCount; ++index)
    {
        if (m_slots[index].moduleBase.load(std::memory_order_relaxed) != modules.moduleBases[index])
        {
            moduleSetChanged = true;
            break;
        }
    }
    const uint64_t nextAvailabilityEpoch = moduleSetChanged ? std::max(previousEpoch, observedAtMs) : previousEpoch;

    for (size_t index = 0; index < kTitleRuntimeSlotCount; ++index)
    {
        Slot& slot = m_slots[index];
        const bool wasAvailable = (previousMask & (uint32_t{1} << index)) != 0;
        const bool isAvailable = (modules.availabilityMask & (uint32_t{1} << index)) != 0;
        const uintptr_t previousBase = slot.moduleBase.load(std::memory_order_relaxed);
        const uintptr_t nextBase = modules.moduleBases[index];
        if (wasAvailable == isAvailable && previousBase == nextBase)
            continue;

        const uint64_t previousStart = slot.generationStartMs.load(std::memory_order_relaxed);
        uint64_t nextStart = std::max(previousStart, observedAtMs);
        if (moduleSetChanged)
            nextStart = std::max(nextStart, nextAvailabilityEpoch);

        slot.moduleBase.store(nextBase, std::memory_order_relaxed);
        slot.generationStartMs.store(nextStart, std::memory_order_relaxed);
        const uint32_t previousGeneration = slot.generation.load(std::memory_order_relaxed);
        slot.generation.store(NextGeneration(previousGeneration), std::memory_order_release);
    }

    if (moduleSetChanged)
    {
        m_availabilitySetEpochMs.store(nextAvailabilityEpoch, std::memory_order_relaxed);
        m_availabilityMask.store(modules.availabilityMask, std::memory_order_release);
    }

    EndPublication(m_availabilitySequence, writeSequence);
    return true;
}

uint32_t TitleRuntimeState::Generation(GameTitle title) const noexcept
{
    const size_t index = TitleRuntimeSlotIndex(title);
    if (index >= kTitleRuntimeSlotCount)
        return 0;
    return m_slots[index].generation.load(std::memory_order_acquire);
}

TitleRuntimeAvailabilitySnapshot TitleRuntimeState::LoadAvailability() const noexcept
{
    TitleRuntimeAvailabilitySnapshot snapshot{};
    for (uint32_t attempt = 0; attempt < kSnapshotReadAttempts; ++attempt)
    {
        const uint32_t before = m_availabilitySequence.load(std::memory_order_acquire);
        if (before & 1u)
            continue;

        snapshot.availabilityMask = m_availabilityMask.load(std::memory_order_acquire);
        snapshot.availabilitySetEpochMs = m_availabilitySetEpochMs.load(std::memory_order_acquire);
        for (size_t index = 0; index < kTitleRuntimeSlotCount; ++index)
        {
            snapshot.moduleBases[index] = m_slots[index].moduleBase.load(std::memory_order_acquire);
        }

        const uint32_t after = m_availabilitySequence.load(std::memory_order_acquire);
        if (before == after && !(after & 1u))
        {
            snapshot.stable = true;
            snapshot.revision = after;
            return snapshot;
        }
    }

    return {};
}

bool TitleRuntimeState::LoadCandidate(
    GameTitle title, uint64_t heartbeatFreshForMs, TitleRuntimeCandidate& candidate) const noexcept
{
    candidate = {};
    const size_t index = TitleRuntimeSlotIndex(title);
    if (index >= kTitleRuntimeSlotCount)
        return false;

    const Slot& slot = m_slots[index];
    for (uint32_t attempt = 0; attempt < kSnapshotReadAttempts; ++attempt)
    {
        const uint32_t generationBefore = slot.generation.load(std::memory_order_acquire);
        if (!generationBefore)
            return false;

        TitleRuntimeCandidate next{};
        next.title = title;
        next.generation = generationBefore;
        next.generationStartMs = slot.generationStartMs.load(std::memory_order_acquire);
        next.heartbeatFreshForMs = heartbeatFreshForMs;

        bool lifecycleStable = false;
        uint32_t lifecycleRevision = 0;
        for (uint32_t lifecycleAttempt = 0; lifecycleAttempt < kSnapshotReadAttempts; ++lifecycleAttempt)
        {
            const uint32_t before = slot.lifecycleSequence.load(std::memory_order_acquire);
            if (before & 1u)
                continue;

            const uint32_t publicationGeneration = slot.lifecycleGeneration.load(std::memory_order_acquire);
            const bool installed = slot.installed.load(std::memory_order_relaxed) != 0;
            const bool armed = slot.armed.load(std::memory_order_relaxed) != 0;
            const bool teardownRequested = slot.teardownRequested.load(std::memory_order_relaxed) != 0;
            const uint32_t enabledCapabilities = slot.enabledCapabilities.load(std::memory_order_relaxed);

            const uint32_t after = slot.lifecycleSequence.load(std::memory_order_acquire);
            if (before != after || (after & 1u))
                continue;

            if (publicationGeneration == generationBefore)
            {
                next.installed = installed;
                next.armed = armed;
                next.teardownRequested = teardownRequested;
                next.enabledCapabilities = enabledCapabilities;
            }
            lifecycleRevision = after;
            lifecycleStable = true;
            break;
        }
        if (!lifecycleStable)
            continue;

        const uint64_t stampedMode = slot.stampedMode.load(std::memory_order_acquire);
        if (StampedGeneration(stampedMode) == generationBefore && IsRuntimeModeValue(StampedValue(stampedMode)))
        {
            next.mode = static_cast<RuntimeMode>(StampedValue(stampedMode));
        }

        bool heartbeatStable = false;
        uint32_t heartbeatRevision = 0;
        for (uint32_t heartbeatAttempt = 0; heartbeatAttempt < kSnapshotReadAttempts; ++heartbeatAttempt)
        {
            const uint32_t before = slot.heartbeatSequence.load(std::memory_order_acquire);
            if (before & 1u)
                continue;

            const uint32_t publicationGeneration = slot.heartbeatGeneration.load(std::memory_order_acquire);
            const uint64_t heartbeatMs = slot.heartbeatMs.load(std::memory_order_relaxed);
            const uint32_t after = slot.heartbeatSequence.load(std::memory_order_acquire);
            if (before != after || (after & 1u))
                continue;

            if (publicationGeneration == generationBefore)
                next.heartbeatMs = heartbeatMs;
            heartbeatRevision = after;
            heartbeatStable = true;
            break;
        }
        if (!heartbeatStable)
            continue;

        const uint32_t generationAfter = slot.generation.load(std::memory_order_acquire);
        if (generationBefore == generationAfter &&
            slot.lifecycleSequence.load(std::memory_order_acquire) == lifecycleRevision &&
            slot.heartbeatSequence.load(std::memory_order_acquire) == heartbeatRevision)
        {
            candidate = next;
            return true;
        }
    }

    return false;
}

bool TitleRuntimeState::PublishLifecycle(
    GameTitle title, uint32_t generation, const TitleRuntimeLifecycle& lifecycle) noexcept
{
    const size_t index = TitleRuntimeSlotIndex(title);
    if (index >= kTitleRuntimeSlotCount || generation == 0)
        return false;

    Slot& slot = m_slots[index];
    if (slot.generation.load(std::memory_order_acquire) != generation)
        return false;

    uint32_t writeSequence = 0;
    if (!TryBeginPublication(slot.lifecycleSequence, writeSequence))
        return false;
    if (slot.generation.load(std::memory_order_acquire) != generation)
    {
        EndPublication(slot.lifecycleSequence, writeSequence);
        return false;
    }

    slot.installed.store(lifecycle.installed ? 1 : 0, std::memory_order_relaxed);
    slot.armed.store(lifecycle.armed ? 1 : 0, std::memory_order_relaxed);
    slot.teardownRequested.store(lifecycle.teardownRequested ? 1 : 0, std::memory_order_relaxed);
    slot.enabledCapabilities.store(
        lifecycle.enabledCapabilities & kTitleRuntimeKnownCapabilities, std::memory_order_relaxed);
    if (!lifecycle.installed || lifecycle.teardownRequested)
        slot.stampedMode.store(0, std::memory_order_release);
    slot.lifecycleGeneration.store(generation, std::memory_order_release);
    EndPublication(slot.lifecycleSequence, writeSequence);

    return slot.generation.load(std::memory_order_acquire) == generation;
}

bool TitleRuntimeState::PublishMode(GameTitle title, uint32_t generation, RuntimeMode mode) noexcept
{
    const size_t index = TitleRuntimeSlotIndex(title);
    const uint32_t modeValue = static_cast<uint32_t>(mode);
    if (index >= kTitleRuntimeSlotCount || generation == 0 || !IsRuntimeModeValue(modeValue))
    {
        return false;
    }

    Slot& slot = m_slots[index];
    if (slot.generation.load(std::memory_order_acquire) != generation)
        return false;

    // Serialize the admission check with lifecycle publication so a teardown
    // cannot begin between observing the lifecycle and publishing its mode.
    uint32_t writeSequence = 0;
    if (!TryBeginPublication(slot.lifecycleSequence, writeSequence))
        return false;
    const bool lifecycleAllowsMode = slot.generation.load(std::memory_order_acquire) == generation &&
                                     slot.lifecycleGeneration.load(std::memory_order_acquire) == generation &&
                                     slot.installed.load(std::memory_order_relaxed) != 0 &&
                                     slot.teardownRequested.load(std::memory_order_relaxed) == 0;
    if (!lifecycleAllowsMode)
    {
        EndPublication(slot.lifecycleSequence, writeSequence);
        return false;
    }
    slot.stampedMode.store(PackStampedValue(generation, modeValue), std::memory_order_release);
    EndPublication(slot.lifecycleSequence, writeSequence);
    return slot.generation.load(std::memory_order_acquire) == generation;
}

bool TitleRuntimeState::PublishHeartbeat(GameTitle title, uint32_t generation, uint64_t heartbeatMs) noexcept
{
    const size_t index = TitleRuntimeSlotIndex(title);
    if (index >= kTitleRuntimeSlotCount || generation == 0)
        return false;

    Slot& slot = m_slots[index];
    if (slot.generation.load(std::memory_order_acquire) != generation ||
        heartbeatMs <= slot.generationStartMs.load(std::memory_order_acquire) ||
        heartbeatMs <= m_availabilitySetEpochMs.load(std::memory_order_acquire))
    {
        return false;
    }

    uint32_t writeSequence = 0;
    if (!TryBeginPublication(slot.heartbeatSequence, writeSequence))
        return false;
    if (slot.generation.load(std::memory_order_acquire) != generation ||
        heartbeatMs <= slot.generationStartMs.load(std::memory_order_acquire) ||
        heartbeatMs <= m_availabilitySetEpochMs.load(std::memory_order_acquire))
    {
        EndPublication(slot.heartbeatSequence, writeSequence);
        return false;
    }

    const uint32_t previousGeneration = slot.heartbeatGeneration.load(std::memory_order_acquire);
    const uint64_t previousHeartbeat = slot.heartbeatMs.load(std::memory_order_relaxed);
    if (previousGeneration == generation && heartbeatMs <= previousHeartbeat)
    {
        EndPublication(slot.heartbeatSequence, writeSequence);
        return false;
    }

    slot.heartbeatMs.store(heartbeatMs, std::memory_order_relaxed);
    slot.heartbeatGeneration.store(generation, std::memory_order_release);
    EndPublication(slot.heartbeatSequence, writeSequence);

    return slot.generation.load(std::memory_order_acquire) == generation;
}

bool TitleRuntimeState::ClearHeartbeat(GameTitle title, uint32_t generation) noexcept
{
    const size_t index = TitleRuntimeSlotIndex(title);
    if (index >= kTitleRuntimeSlotCount || generation == 0)
        return false;

    Slot& slot = m_slots[index];
    if (slot.generation.load(std::memory_order_acquire) != generation)
        return false;

    uint32_t writeSequence = 0;
    if (!TryBeginPublication(slot.heartbeatSequence, writeSequence))
        return false;
    if (slot.generation.load(std::memory_order_acquire) != generation)
    {
        EndPublication(slot.heartbeatSequence, writeSequence);
        return false;
    }

    slot.heartbeatMs.store(0, std::memory_order_relaxed);
    slot.heartbeatGeneration.store(generation, std::memory_order_release);
    EndPublication(slot.heartbeatSequence, writeSequence);
    return slot.generation.load(std::memory_order_acquire) == generation;
}

TitleRuntimeSnapshot TitleRuntimeState::Resolve(
    uint64_t nowMs, const TitleRuntimeHeartbeatPolicy& policy) const noexcept
{
    for (uint32_t attempt = 0; attempt < kSnapshotReadAttempts; ++attempt)
    {
        const TitleRuntimeAvailabilitySnapshot availability = LoadAvailability();
        if (!availability.stable)
            return {};

        TitleRuntimeResolveInput input{};
        input.availabilityMask = availability.availabilityMask;
        input.availabilitySetEpochMs = availability.availabilitySetEpochMs;
        input.nowMs = nowMs;

        bool candidatesStable = true;
        for (size_t index = 0; index < kTitleRuntimeSlotCount; ++index)
        {
            const GameTitle title = TitleRuntimeSlotTitle(index);
            if (!LoadCandidate(title, policy.freshForMs[index], input.titles[index]))
            {
                // A never-loaded title has generation zero and contributes no
                // candidate. A loaded title that changed during the read makes
                // the complete snapshot unstable and must be retried.
                if (Generation(title) != 0)
                {
                    candidatesStable = false;
                    break;
                }
                input.titles[index].title = title;
                input.titles[index].heartbeatFreshForMs = policy.freshForMs[index];
            }
        }
        if (!candidatesStable)
            continue;

        const TitleRuntimeAvailabilitySnapshot after = LoadAvailability();
        if (!after.stable || after.revision != availability.revision ||
            after.availabilityMask != availability.availabilityMask ||
            after.availabilitySetEpochMs != availability.availabilitySetEpochMs)
        {
            continue;
        }

        return ResolveTitleRuntime(input);
    }

    return {};
}
