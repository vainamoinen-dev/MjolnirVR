// Halo 2 title adapter: returns the fixed adapter stage (same-frame stereo with 6DoF),
// the retail module identity, and the hook and engine-write permissions.
// Called through halo2_adapter.h by the core title adapter and game.cpp.
// Constant answers only: no Windows calls, no mutable globals, no logging.
#include "halo2_adapter.h"

#include "../../../common/titles/h2/halo2_render_logic.h"

namespace
{
    constexpr Halo2EvidenceIdentity kHalo2RetailEvidence = {
        L"halo2.dll",
        kHalo2RetailModuleSha256[0],
        kHalo2RetailModuleSha256[1],
        kHalo2RetailPeTimestamp,
        static_cast<uint32_t>(kHalo2RetailImageSize),
        kHalo2KitBuildTag,
        kHalo2KitTagTestSha256,
    };
}

Halo2AdapterStage Halo2Adapter_GetStage()
{
    return Halo2AdapterStage::SameFrameStereoSixDof;
}

const Halo2EvidenceIdentity& Halo2Adapter_GetEvidenceIdentity()
{
    return kHalo2RetailEvidence;
}

bool Halo2Adapter_RuntimeHooksPermitted()
{
    return true;
}

bool Halo2Adapter_EngineWritesPermitted()
{
    // Same-frame stereo permits only scoped camera pose writes; the dormant
    // temporal stage permitted only position. Generic draw distance remains
    // hard-denied in either case.
    return true;
}
