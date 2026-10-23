// halo2_bridge.h: the Halo 2 runtime functions that core (game.cpp) calls, in namespace halo2_bridge.
// Defined in src/dll/titles/h2/halo2_runtime.cpp (until the move: in game.cpp). Included by game.cpp and
// the Halo 2 runtime. Core reaches the Halo 2 runtime only through what is declared here (and the
// Game_* title API in game.h); it must not grow core logic.
#pragma once
#include "../../core/game_internal.h"

namespace halo2_bridge
{
    using namespace game_core;
    bool AutoVrTickHalo2Stereo6Dof();
    void AutoVrHalo2ClaimGate();
    bool ComputeHalo2ControllerAimStick(const float controllerOrientation[4], const float controllerPosition[3],
        const float /*headOrientation*/[4], const float /*headPosition*/[3], float& outRx, float& outRy);
    bool Halo2TurretSeatNow();
    void TickHalo2Observation(const TitleTickContext& c);
    void TickHalo2Stereo(const TitleTickContext& c);
} // namespace halo2_bridge
