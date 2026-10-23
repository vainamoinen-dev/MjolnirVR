// Halo 4 title adapter: reports the current stage, the pinned retail evidence and whether runtime hooks are allowed.
// Declared in halo4_adapter.h. Pure answers: no hooks, no native reads, no logging.
#include "halo4_adapter.h"

#include "../../../common/titles/h4/halo4_render_logic.h"

namespace
{
    constexpr Halo4EvidenceIdentity kHalo4RetailEvidence = {
        L"halo4.dll",
        kHalo4RetailModuleSha256[0],
        kHalo4RetailModuleSha256[1],
        kHalo4RetailPeTimestamp,
        static_cast<uint32_t>(kHalo4RetailImageSize),
        kHalo4KitBuildTag,
    };
}

Halo4AdapterStage Halo4Adapter_GetStage()
{
    // Everything the cold-observation stage establishes, plus the per-eye camera core.
    return Halo4AdapterStage::ControllerInputAndStereoCamera;
}

const Halo4EvidenceIdentity& Halo4Adapter_GetEvidenceIdentity()
{
    return kHalo4RetailEvidence;
}

bool Halo4Adapter_HookProofComplete(const Halo4HookProof& proof)
{
    return proof.retailIdentity && proof.loadedImageMatchCount == 1 && proof.executableRange && proof.abi &&
           proof.callers && proof.dataFlow && proof.h4ekSemantics && proof.consumedLayoutFields;
}

bool Halo4Adapter_RuntimeHooksPermitted()
{
    // This permits ONLY the camera core's two hooks, and only after
    // its own all-or-nothing install proof passes: the cold observation
    // must have PASSED for this exact module generation, all four pinned
    // camera anchors must be unique at their pinned RVAs, and the per-window
    // loop's own two call instructions must target the two functions being
    // hooked. Aim, movement, HUD, IK and haptics remain unhooked - they have
    // no Halo 4 evidence and are not published as capabilities.
    return true;
}
