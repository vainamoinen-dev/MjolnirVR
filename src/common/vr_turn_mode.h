// VR turn mode: whether smooth turn applies right now (saved on-foot preference or the vehicle override).
// Used by src/dll/core/game.cpp and the CE controls logic; tested by tests/vehicle_turn_tests.cpp.
// Pure function; it never writes the saved preference.
#pragma once

// Effective mode only: never overwrite the user's saved on-foot preference.
inline bool UseSmoothVrTurn(bool savedSmooth, bool vehicleSmooth, bool seated) noexcept
{
    return savedSmooth || (vehicleSmooth && seated);
}
