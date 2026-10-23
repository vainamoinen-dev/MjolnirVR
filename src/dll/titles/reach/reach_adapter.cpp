// Halo Reach title adapter: reports the stage, the pinned retail evidence and whether runtime hooks are allowed.
// Declared in reach_adapter.h. Pure answers: no hooks, no native reads, no logging.
#include "reach_adapter.h"

namespace
{
    constexpr ReachEvidenceIdentity kReachRetailEvidence = {
        L"haloreach.dll",
        "738DD2D24EA3AEA12E1EE9AA4A61094BF116027D42004C35A19E5048608B0894",
        0x68A0EFE1u,
        0x04EDA000u,
        "2023.07.17.176677.1-QFE1",
    };
}

ReachAdapterStage ReachAdapter_GetStage()
{
    return ReachAdapterStage::ControllerInputOnly;
}

const ReachEvidenceIdentity& ReachAdapter_GetEvidenceIdentity()
{
    return kReachRetailEvidence;
}

bool ReachAdapter_HookProofComplete(const ReachHookProof& proof)
{
    return proof.retailIdentity && proof.loadedImageMatchCount == 1 && proof.executableRange && proof.abi &&
           proof.callers && proof.dataFlow && proof.hrekSemantics && proof.consumedLayoutFields;
}

bool ReachAdapter_RuntimeHooksPermitted()
{
    // Controller transport is shared XInput policy, not a Reach engine hook.
    // The separately identified render candidate may compile for static gate
    // testing, but camera, render, aim, movement, HUD, IK, haptics, and
    // lifecycle hooks stay forbidden in this milestone.
    return false;
}
