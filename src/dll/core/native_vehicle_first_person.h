// Optional first-person vehicle camera (CE, Halo 2, Halo 4), implemented in
// native_vehicle_first_person.cpp, plus the title-side seat readers it calls.
// Used by game.cpp, vr.cpp, menu.cpp and the CE/Halo 2 title files.
#pragma once
#include <cstdint>

enum class GameTitle : uint8_t;
struct NativeVehicleCameraOwner
{
    uint32_t generation{}, unit{UINT32_MAX}, parent{UINT32_MAX};
    int16_t seat{-1};
    uintptr_t object{};
    uint64_t vehicleIdentity{}; // stable authored identity; never a live object datum
    bool operator==(const NativeVehicleCameraOwner&) const = default;
};

// Optional CE/H2/H4 camera feature. Existing H3/ODST/Reach paths are independent.
void NativeVehicleFirstPerson_Poll();
bool NativeVehicleFirstPerson_TrimTarget(GameTitle title, uint64_t& identity, int& seat) noexcept;
bool Game_ReadVehicleCameraOwner(GameTitle title, NativeVehicleCameraOwner& owner) noexcept;
bool HaloCEControls_ReadVehicleCameraOwner(NativeVehicleCameraOwner& owner) noexcept;
// Halo CE occupied-vehicle frame (hull position/forward/up + seat) for seated
// anchors; false when on foot or the sample fails validation (stock).
namespace haloce_vehicle_frame
{
    struct Frame;
}
bool HaloCEControls_ReadVehicleFrame(haloce_vehicle_frame::Frame& frame) noexcept;
// Motion proof state (0 pending, 1 proven, 2 refuted), its window counts
// and the occupied hull's speed (m/s), for the "CE seat proof" log line.
void HaloCEControls_VehicleProofStatus(int& state, int& agree, int& disagree, float& speedMps) noexcept;
bool Halo2Observer6Dof_ReadVehicleCameraOwner(NativeVehicleCameraOwner& owner) noexcept;
