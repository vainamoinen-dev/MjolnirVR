// Shared runtime enums: GameTitle, RuntimeMode, CinematicControlState, VrHand and the TitleCapability bits.
// Included across src/common, src/dll and tests wherever a title, mode or hand is named.
// Types only: no functions, no state.
#pragma once

#include <cstdint>

enum class GameTitle : uint8_t
{
    None = 0,
    Halo3,
    Halo3ODST,
    HaloReach,
    Halo4,
    HaloCE,
    Halo2,
    Unknown,
};

enum class RuntimeMode : uint8_t
{
    Shell = 0,
    Loading,
    Gameplay,
    Paused,
    Cutscene,
    Vehicle,
    Turret,
    Dead,
    Unsupported,
};

// A title adapter's proof of who owns the active camera. Shared presentation
// code may enter the cutscene theatre only for AuthoredLocked. Unknown is a
// deliberate fail-open state: keep today's immersive view when proof is absent
// or stale instead of guessing from camera motion, black bars, or title names.
enum class CinematicControlState : uint8_t
{
    Unknown = 0,
    PlayerControlled,
    AuthoredLocked,
};

enum class VrHand : uint8_t
{
    Left = 0,
    Right,
};

enum TitleCapability : uint32_t
{
    TitleCapability_None = 0,
    TitleCapability_Stereo = 1u << 0,
    TitleCapability_ControllerAim = 1u << 1,
    TitleCapability_Hud = 1u << 2,
    TitleCapability_ArmIk = 1u << 3,
    TitleCapability_RuntimeModes = 1u << 4,
    TitleCapability_RoomScale = 1u << 5,
    TitleCapability_ControllerInput = 1u << 6,
    TitleCapability_Haptics = 1u << 7,
    TitleCapability_CutsceneTheater = 1u << 8,
};
