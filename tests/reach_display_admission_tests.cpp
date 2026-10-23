// Tests Reach display admission (reach_display_admission.inl, compiled in directly): Reach publishes
// display resources only when it is the selected title with a matching module and generation.
// Built as halomccvr_reach_display_admission_tests. Pure logic over fake runtime state.
#include <cstdio>
#include "../src/common/title_runtime_state.h"
#include "../src/common/titles/reach/reach_render_logic.h"

namespace
{
    TitleRuntimeAvailabilitySnapshot fixtureBefore{}, fixtureAfter{};
    GameTitle fixtureActiveBefore = GameTitle::HaloReach, fixtureActiveAfter = GameTitle::HaloReach;
    uint32_t fixtureGenerationBefore = 3, fixtureGenerationAfter = 3;
    unsigned reads = 0, activeReads = 0, generationReads = 0, failures = 0, checks = 0;
    TitleRuntimeAvailabilitySnapshot TitleAdapter_GetAvailability()
    {
        return reads++ ? fixtureAfter : fixtureBefore;
    }
    GameTitle TitleAdapter_GetActiveTitle()
    {
        return activeReads++ ? fixtureActiveAfter : fixtureActiveBefore;
    }
    uint32_t TitleAdapter_GetGeneration(GameTitle)
    {
        return generationReads++ ? fixtureGenerationAfter : fixtureGenerationBefore;
    }

#include "../src/dll/titles/reach/reach_display_admission.inl"

    void Check(bool value, const char* name)
    {
        ++checks;
        if (!value)
        {
            ++failures;
            std::printf("FAIL: %s\n", name);
        }
    }
    void Reset(uint32_t mask = TitleRuntimeAvailabilityBit(GameTitle::HaloReach))
    {
        fixtureBefore = {};
        fixtureBefore.stable = true;
        fixtureBefore.availabilityMask = mask;
        fixtureBefore.availabilitySetEpochMs = 100;
        fixtureBefore.revision = 4;
        for (size_t i = 0; i < kTitleRuntimeSlotCount; ++i)
            if (mask & (1u << i))
                fixtureBefore.moduleBases[i] = 0x10000000 + i * 0x10000000;
        fixtureAfter = fixtureBefore;
        fixtureActiveBefore = fixtureActiveAfter = GameTitle::HaloReach;
        fixtureGenerationBefore = fixtureGenerationAfter = 3;
        reads = activeReads = generationReads = 0;
    }
    ReachDisplayAdmission Read()
    {
        reads = activeReads = generationReads = 0;
        ReachDisplayAdmission result{};
        ReadReachDisplayAdmission(result);
        return result;
    }
}

int main()
{
    constexpr uint32_t reach = TitleRuntimeAvailabilityBit(GameTitle::HaloReach);
    // Every resident-title combination includes CE retained after either graphics mode.
    for (uint32_t mask = 0; mask <= kTitleRuntimeAvailabilityMask; ++mask)
    {
        Reset(mask);
        auto result = Read();
        Check(result.selected == bool(mask & reach), "selected Reach supports every resident-module combination");
        for (size_t i = 0; i < kTitleRuntimeSlotCount; ++i)
        {
            const GameTitle title = TitleRuntimeSlotTitle(i);
            if (title == GameTitle::HaloReach)
                continue;
            fixtureActiveBefore = fixtureActiveAfter = title;
            Check(!Read().selected, "another selected title cannot publish Reach resources");
        }
        fixtureActiveBefore = fixtureActiveAfter = GameTitle::Unknown;
        Check(!Read().selected, "ambiguous selection stays blocked");
        fixtureActiveBefore = fixtureActiveAfter = GameTitle::None;
        Check(!Read().selected, "shell stays blocked");
    }
    for (int change = 0; change < 8; ++change)
    {
        Reset(kTitleRuntimeAvailabilityMask);
        switch (change)
        {
        case 0:
            fixtureAfter.stable = false;
            break;
        case 1:
            ++fixtureAfter.revision;
            break;
        case 2:
            ++fixtureAfter.availabilitySetEpochMs;
            break;
        case 3:
            fixtureAfter.availabilityMask ^= 1;
            break;
        case 4:
            ++fixtureAfter.moduleBases[0];
            break;
        case 5:
            fixtureActiveAfter = GameTitle::HaloCE;
            break;
        case 6:
            ++fixtureGenerationAfter;
            break;
        case 7:
            fixtureBefore.stable = false;
            break;
        }
        Check(!Read().coherent, "concurrent title/module/generation change rejects admission");
    }
    Reset();
    fixtureGenerationBefore = fixtureGenerationAfter = 0;
    Check(!Read().selected, "zero generation blocked");
    Reset();
    fixtureBefore.moduleBases[2] = fixtureAfter.moduleBases[2] = 0;
    Check(!Read().selected, "missing Reach module blocked");
    Reset(kTitleRuntimeAvailabilityMask);
    const auto first = Read();
    Check(ReachSameDisplayAdmission(first, Read()), "stable current resource admission");
    ++fixtureBefore.availabilitySetEpochMs;
    fixtureAfter = fixtureBefore;
    Check(!ReachSameDisplayAdmission(first, Read()), "old availability epoch cannot publish");
    Reset(kTitleRuntimeAvailabilityMask);
    ++fixtureGenerationBefore;
    fixtureGenerationAfter = fixtureGenerationBefore;
    Check(!ReachSameDisplayAdmission(first, Read()), "old generation cannot publish");
    Reset(kTitleRuntimeAvailabilityMask);
    ++fixtureBefore.moduleBases[2];
    fixtureAfter = fixtureBefore;
    Check(!ReachSameDisplayAdmission(first, Read()), "reloaded module cannot publish");
    std::printf("Reach production display admission: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
