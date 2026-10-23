// Halo CE per-frame render context: the native camera plus the one XR sample it is paired with.
// Included by most CE logic headers and by src/dll/titles/ce (haloce_contact.h, haloce_controls.h,
// haloce_stereo_core.h). Plain data: no functions, no globals.
#pragma once
#include "haloce_render_logic.h"

namespace halo_ce
{
    // One native center camera and one immutable XR sample/reference. The caller
    // supplies the stock camera before first-person or per-eye changes; consumers
    // must never reconstruct this from separately sampled global hand/head poses.
    struct RenderContext
    {
        Tracking tracking;
        Reference reference;
        Camera camera;
        float unitsPerMeter{};
        bool positional{};
        uint64_t referenceRevision{}, rendererEpoch{};
    };
}
