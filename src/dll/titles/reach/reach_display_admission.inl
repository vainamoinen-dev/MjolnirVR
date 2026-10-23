// Halo Reach display admission: decides when Present and the worker may start Reach's display resource checks.
// Selection admits resource verification only; exact native swapchain, device and epoch proofs still come first.
#pragma once
// Included once, by src/dll/titles/reach/reach_display_worker.cpp; also compiled by
//   tests/reach_display_admission_tests.cpp, which supplies its own versions. Not a standalone
//   header: it uses what the host defines above its include line (headers only).
// Shared by production Present/worker admission and its offline regression.
struct ReachDisplayAdmission
{
    TitleRuntimeAvailabilitySnapshot availability{};
    ReachModuleEpoch epoch{};
    GameTitle activeTitle = GameTitle::None;
    bool coherent = false;
    bool resident = false;
    bool selected = false;
};

bool SameReachAvailability(
    const TitleRuntimeAvailabilitySnapshot& left, const TitleRuntimeAvailabilitySnapshot& right) noexcept
{
    return left.stable && right.stable && left.availabilityMask == right.availabilityMask &&
           left.availabilitySetEpochMs == right.availabilitySetEpochMs && left.revision == right.revision &&
           left.moduleBases == right.moduleBases;
}

bool ReadReachDisplayAdmission(ReachDisplayAdmission& admission) noexcept
{
    admission = {};
    constexpr GameTitle title = GameTitle::HaloReach;
    constexpr size_t slot = TitleRuntimeSlotIndex(title);
    constexpr uint32_t bit = TitleRuntimeAvailabilityBit(title);
    static_assert(slot < kTitleRuntimeSlotCount);
    static_assert(bit != 0);

    const TitleRuntimeAvailabilitySnapshot before = TitleAdapter_GetAvailability();
    const GameTitle activeBefore = TitleAdapter_GetActiveTitle();
    const uint32_t generationBefore = TitleAdapter_GetGeneration(title);
    const TitleRuntimeAvailabilitySnapshot after = TitleAdapter_GetAvailability();
    const GameTitle activeAfter = TitleAdapter_GetActiveTitle();
    const uint32_t generationAfter = TitleAdapter_GetGeneration(title);
    if (!SameReachAvailability(before, after) || activeBefore != activeAfter || generationBefore != generationAfter)
    {
        return false;
    }

    admission.availability = after;
    admission.activeTitle = activeAfter;
    admission.coherent = true;
    admission.resident = (after.availabilityMask & bit) != 0 && after.moduleBases[slot] != 0 && generationAfter != 0;
    if (admission.resident)
    {
        admission.epoch = {after.moduleBases[slot], generationAfter};
    }
    // The adapter resolves the active game even when MCC retains other
    // title modules. Requiring a one-bit module mask deadlocked Reach's
    // display proof after CE. Selection admits resource verification only;
    // exact native swapchain/device/epoch proofs still precede ownership.
    admission.selected = admission.resident && activeAfter == title;
    return true;
}

bool ReachSameDisplayAdmission(const ReachDisplayAdmission& left, const ReachDisplayAdmission& right) noexcept
{
    return left.coherent && right.coherent && left.resident && right.resident && left.selected && right.selected &&
           left.activeTitle == right.activeTitle &&
           left.availability.availabilityMask == right.availability.availabilityMask &&
           left.availability.availabilitySetEpochMs == right.availability.availabilitySetEpochMs &&
           ReachSameModuleEpoch(left.epoch, right.epoch);
}
