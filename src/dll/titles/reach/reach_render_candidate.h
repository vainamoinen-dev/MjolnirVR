// Halo Reach render candidate: the worker's loaded-image proof, private resource preparation and render-action choice.
// Used by src/dll/core/game.cpp, src/dll/core/vr.cpp and tests/core_tests.cpp. No Reach detour is called from here.
#pragma once

#include "../../../common/titles/reach/reach_render_logic.h"

// Hard-off foundation. The title worker may publish exact loaded-image proof
// and prepare private copy-compatible resources from a bounded Present
// observation, but the title
// registry, lifecycle publisher, MinHook installer, camera mutator, copy path,
// and VR capture path do not call a Reach detour in this candidate.
bool ReachRenderCandidate_Compiled() noexcept;
bool ReachRenderCandidate_RuntimeHooksEnabled() noexcept;

// The 50 ms title worker is the only caller. Signature scanning, file hashing,
// and PE parsing never run from Present or any game render callback.
void ReachRenderCandidate_ColdPoll(
    uintptr_t moduleBase, size_t moduleSize, uint32_t generation, bool soleReachTitle) noexcept;
ReachPreflightToken ReachRenderCandidate_GetPreflight(const ReachModuleEpoch& epoch) noexcept;
bool ReachRenderCandidate_IsPreflightCurrent(const ReachPreflightToken& token) noexcept;

// Explicit manual recovery only, on the title worker after camera cleanup.
// Reopens an exact failed attempt; passing proofs are never invalidated or
// rescanned against live hooks. The normal gated cold poll performs the retry.
bool ReachRenderCandidate_RetryFailed(const ReachModuleEpoch& epoch) noexcept;

ReachRenderAction ReachRenderCandidate_SelectAction(const ReachPreflightToken& preflight,
    const ReachRenderOwnerGate& owner, const ReachRenderOwnerToken& token, const ReachDirectCopyGate& directCopyGate,
    const ReachInnerRenderInput& input) noexcept;
