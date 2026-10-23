// haloce_bridge.h: the Halo CE runtime functions that core (game.cpp) calls, in namespace haloce_bridge.
// Defined in src/dll/titles/ce/haloce_runtime.cpp. Included by game.cpp and the CE runtime. Core reaches
// the CE runtime only through what is declared here (and the Game_* title API in game.h); it must not
// grow core logic.
#pragma once
#include "../../core/game_internal.h"

namespace haloce_bridge
{
    using namespace game_core;
    bool AutoVrTickHaloCE();
    void TickHaloCE(const TitleTickContext& c);
} // namespace haloce_bridge
