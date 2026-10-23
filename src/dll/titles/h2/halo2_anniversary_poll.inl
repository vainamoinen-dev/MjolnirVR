// Halo 2 Anniversary stereo per-frame poll: arms, installs or removes the core and logs scope-pass status.
// Included once, by src/dll/titles/h2/halo2_anniversary_stereo.cpp; also compiled by
//   tests/halo2_anniversary_cleanup_tests.cpp. Not a standalone header:
//   it uses what the host defines above its include line, namely BeginRefused, CoreState,
//   InstallCore, NotRendered, RemoveCore, ScopePassStage, g_armed, g_armedLoggedGeneration,
//   g_coreState, g_generation, g_hostUiTarget, g_installed, g_levelLive,
//   g_manualRecoveryGeneration, g_moduleBase, g_rebuildTarget, and 11 more.
#pragma once
bool Halo2AnniversaryStereo_Poll(uintptr_t moduleBase, size_t moduleSize, uint32_t generation, bool activeAndRange,
    bool levelRunning, bool coldPassed, bool remasteredRendererLive, uintptr_t observerResultArray) noexcept
{
    static uint32_t reportedScopeFaults = 0;
    const auto scopeFaults = g_zoomFaults.load();
    if (scopeFaults != reportedScopeFaults)
    {
        reportedScopeFaults = scopeFaults;
        LOG("Halo 2 Anniversary zoom stock fallback: optional scope faults=%u generation=%u; stereo core retained",
            scopeFaults, g_zoomFaultGeneration.load());
    }
    {
        // Once a second while the scope pass is asked to render, log the last
        // stage it reached ("ended" + outcome "copied" = an image reached the glass).
        // Sampled only between attempts (g_scopePassSeq even and unchanged), so a
        // line never mixes a running attempt with the previous one's results.
        static uint32_t loggedAttempts = 0;
        static uint64_t loggedMs = 0;
        const uint32_t seq = g_scopePassSeq.load(std::memory_order_acquire);
        const uint32_t attempts = g_scopePassAttempts.load(std::memory_order_relaxed);
        const uint64_t nowMs = GetTickCount64();
        if (!(seq & 1) && attempts != loggedAttempts && nowMs - loggedMs >= 1000)
        {
            static const char* const kStage[]{"none", "camera-failed", "aspect-failed", "begin-refused",
                "not-rendered (projection mismatch or camera write refused)", "ended", "fault"};
            static const char* const kRefusal[]{
                "-", "no context/cache/source", "generation/serial", "title", "size/format"};
            static const char* const kOutcome[]{"-", "copied", "pass not completed", "source target never bound"};
            int refusal = 0, outcome = 0;
            VR_Halo2ScopeDiagnostics(refusal, outcome);
            const auto stage = static_cast<uint32_t>(g_scopePassStage.load(std::memory_order_relaxed));
            const float requested[2]{g_scopePassRequested[0].load(), g_scopePassRequested[1].load()};
            const float actual[2]{g_scopePassActual[0].load(), g_scopePassActual[1].load()};
            std::atomic_thread_fence(std::memory_order_acquire);
            const bool exact = g_scopePassSeq.load(std::memory_order_relaxed) == seq;
            // Fields this attempt never reached (begin refusal before begin; outcome
            // and tangents before the copy began) belong to an earlier one: show none.
            const bool begun = stage >= static_cast<uint32_t>(ScopePassStage::NotRendered);
            if (!begun)
                outcome = 0;
            if (stage < static_cast<uint32_t>(ScopePassStage::BeginRefused))
                refusal = 0;
            if (exact)
                LOG("H2A scope pass: last stage=%s attempts=%u (+%u) begin=%s outcome=%s requested tan %.4f/%.4f actual %.4f/%.4f faults=%u",
                    stage < 7 ? kStage[stage] : "?", attempts, attempts - loggedAttempts,
                    refusal >= 0 && refusal < 5 ? kRefusal[refusal] : "?",
                    outcome >= 0 && outcome < 4 ? kOutcome[outcome] : "?", begun ? requested[0] : 0,
                    begun ? requested[1] : 0, begun ? actual[0] : 0, begun ? actual[1] : 0, scopeFaults);
            if (exact)
            {
                loggedAttempts = attempts;
                loggedMs = nowMs;
            }
        }
    }
    uint32_t vrFailure = g_vrFailureGeneration.load(std::memory_order_acquire);
    if (vrFailure && generation && generation != vrFailure)
    {
        g_vrFailureGeneration.compare_exchange_strong(
            vrFailure, 0, std::memory_order_acq_rel, std::memory_order_acquire);
        vrFailure = g_vrFailureGeneration.load(std::memory_order_acquire);
    }

    const bool vrAvailable = !vrFailure || generation != vrFailure;
    if (PollManualVrRecovery(
            g_manualRecoveryGeneration, generation, TitleAdapter_GetActiveTitle() == GameTitle::Halo2 && vrAvailable,
            moduleBase && moduleSize == kHalo2RetailImageSize && activeAndRange,
            [] { return RemoveCore("manual VR recovery"); },
            [] { g_rejectedGeneration = 0; }) == ManualVrRecoveryPoll::Waiting)
        return false;
    const bool desired = moduleBase && generation && moduleSize == kHalo2RetailImageSize && activeAndRange &&
                         levelRunning && coldPassed && vrAvailable && observerResultArray != 0 &&
                         remasteredRendererLive;

    g_levelLive.store(levelRunning, std::memory_order_release);
    g_remasteredLive.store(remasteredRendererLive, std::memory_order_release);

    // A failed exit/partial-install cleanup must finish even if the same
    // retained module becomes eligible again before retirement completes.
    if (g_coreState == CoreState::CleanupRequired && !RemoveCore("pending cleanup"))
        return false;

    const uint32_t owned = g_generation.load(std::memory_order_acquire);
    const bool foreignModule = (g_sceneTarget || g_rebuildTarget || g_hostUiTarget) &&
                               (owned != generation || g_moduleBase.load(std::memory_order_acquire) != moduleBase);

    if (!desired || foreignModule)
    {
        if (g_installed.load(std::memory_order_acquire) || g_coreState != CoreState::StockFallback)
        {
            if (!RemoveCore(foreignModule ? "module generation changed" : "level or title no longer eligible"))
                return false;
        }
        if (TitleAdapter_GetActiveTitle() != GameTitle::Halo2 || generation != g_rejectedGeneration)
            g_rejectedGeneration = 0;
        return false;
    }

    if (g_coreState != CoreState::Installed)
    {
        if (!InstallCore(moduleBase, generation, observerResultArray))
            return false;
    }

    g_armed.store(true, std::memory_order_release);
    if (g_armedLoggedGeneration != generation)
    {
        g_armedLoggedGeneration = generation;
        LOG("Halo 2 Anniversary stereo armed: the remastered scene render runs "
            "once per eye from one game frame; a frame that cannot produce a "
            "complete pair renders stock exactly once and never blacks out");
    }
    Report();
    return true;
}
