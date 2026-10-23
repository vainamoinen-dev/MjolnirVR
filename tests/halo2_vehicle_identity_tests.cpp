// Tests for Halo 2 vehicle identity and seat roles (src/dll/titles/h2/halo2_vehicle_identity.inl,
// src/common/titles/h2/halo2_vehicle_roles.h): which vehicle the player rides and what each seat controls.
// Built as halo2_vehicle_identity_tests and run by ctest. The native definition chain is faked; no game process.
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include "../src/common/titles/h2/halo2_world_collision_logic.h"
#include "../src/common/titles/h2/halo2_vehicle_identity.h"
#include "../src/common/titles/h2/halo2_vehicle_roles.h"
#include "../src/dll/core/guard_count.h"
using Halo2GraphDefinitionGetFn = const void*(__fastcall*)(uint32_t);
std::atomic<bool> g_vehicleIdentityVerified{true};
std::atomic<uintptr_t> g_graphDefinitionGet{}, g_halo2TagDataBaseSlot{};
unsigned checks = 0;
uint32_t fixtureParentHandle = 0x12340007, fixtureModelTag = 0x4321000A, fixtureRenderTag = 0x5678000B;
uint16_t fixtureDefinition = 9;
alignas(8) uint8_t fixtureParent[0x220]{}, fixtureObject[0x40]{}, fixtureModel[8]{},
    fixtureRender[0x1C]{}, data[0x200]{};
const uint8_t* dataBase = data;
bool mutate = false;
const void* Halo2ObjectFromIndex(uint32_t handle)
{
    return handle == fixtureParentHandle ? fixtureParent : nullptr;
}
const void* __fastcall Get(uint32_t tag)
{
    if (tag == fixtureDefinition)
        return fixtureObject;
    if (tag == fixtureModelTag)
        return fixtureModel;
    if (tag == fixtureRenderTag)
    {
        if (mutate)
            fixtureParent[0]++;
        return fixtureRender;
    }
    return nullptr;
}
bool CountPatternMatches(uintptr_t, size_t, const char*, uintptr_t&, uint32_t&) noexcept
{
    return false;
}
#include "../src/dll/titles/h2/halo2_vehicle_identity.inl"
void Check(bool pass, const char* reason)
{
    ++checks;
    if (!pass)
    {
        std::printf("FAIL: %s\n", reason);
        std::exit(1);
    }
}
template <class T>
void Put(uint8_t* bytes, size_t at, T value)
{
    std::memcpy(bytes + at, &value, sizeof(value));
}
int main()
{
    {
        using namespace halo2_vehicle_roles;
        const auto car = SeatFor(kSeatDriverBit, "Warthog");
        Check(car.driver && car.role.steer == Steer::Car && !car.role.aircraft, "Warthog driver steers as a car");
        const auto gunner = SeatFor(kSeatGunnerBit, "Warthog");
        Check(!gunner.driver && gunner.gunner && gunner.role.steer == Steer::None, "Warthog gunner never steers");
        Check(SeatFor(kSeatDriverBit, "Ghost").role.steer == Steer::Bike &&
                  SeatFor(kSeatDriverBit, "Ghost").role.armedDriver,
            "Ghost driver: bike, armed");
        Check(SeatFor(kSeatDriverBit, "Mongoose").role.steer == Steer::Bike, "Mongoose: bike");
        Check(SeatFor(kSeatDriverBit, "Spectre").role.steer == Steer::Car, "Spectre: car");
        const auto banshee = SeatFor(kSeatDriverBit, "Banshee");
        Check(banshee.role.aircraft && banshee.role.steer == Steer::None, "Banshee: flight stick, no wheel");
        Check(SeatFor(kSeatDriverBit, "Scorpion").role.steer == Steer::None &&
                  !SeatFor(kSeatDriverBit, "Scorpion").role.aircraft,
            "tanks keep their own steering");
        Check(SeatFor(kSeatDriverBit, "Ghost / Seat").role.steer == Steer::None, "a vehicle part is never a vehicle");
        Check(SeatFor(kSeatDriverBit, nullptr).role.steer == Steer::None, "unknown vehicle: no role");
        Check(gunner.turret, "Warthog gunner seat is a turret seat");
        Check(!car.turret && !banshee.turret && !SeatFor(kSeatDriverBit | kSeatGunnerBit, "Ghost").turret,
            "drivers (armed or not) are never turret seats");
        Check(!SeatFor(kSeatDriverBit | kSeatGunnerBit, "Scorpion").turret, "the tank driver aims its own cannon");
        const auto ap = SeatFor(kSeatDriverBit | kSeatGunnerBit, "H Turret Ap");
        Check(ap.turret && ap.role.steer == Steer::None, "a stationary turret is a turret seat, never steered");
        Check(SeatFor(0, "C Turret Ap").turret && SeatFor(0, "Cov Guntower").turret, "stationary turrets by identity");
        Check(!SeatFor(0, "Warthog").turret, "a passenger seat is not a turret");
        {
            float y = 0, p = 0;
            BarrelReticle(0.3f, 0.1f, 1.0f, 0.2f, 1.0f, 0.2f, y, p);
            Check(std::fabs(y - 0.3f) < 1e-6f && std::fabs(p - 0.1f) < 1e-6f,
                "converged barrel: reticle is the hand ray");
            BarrelReticle(0.0f, 0.0f, 0.5f, 0.0f, 0.3f, -0.1f, y, p);
            Check(std::fabs(y - 0.2f) < 1e-6f && std::fabs(p + 0.1f) < 1e-6f,
                "barrel still right of the hand (world yaw smaller): reticle right, and low");
            BarrelReticle(0.0f, 0.0f, 3.1f, 0.0f, -3.1f, 0.0f, y, p);
            Check(std::fabs(y + 0.0832f) < 1e-3f, "yaw error wraps across +/-pi");
            BarrelReticle(0.0f, 1.5f, 0.0f, 0.0f, 0.0f, 0.5f, y, p);
            Check(p <= 1.55f, "pitch stays off the pole");
        }
        for (const char* name : {"Warthog", "Spectre", "Ghost", "Mongoose", "Banshee", "Scorpion", "C Turret Ap",
                 "H Turret Ap", "Cov Guntower"})
        {
            bool found = false;
            for (const auto& m : halo2_vehicle_identity::kModels)
                found |= !std::strcmp(m.name, name);
            Check(found, "every role name is a real identity name");
        }
    }
    g_graphDefinitionGet = reinterpret_cast<uintptr_t>(&Get);
    g_halo2TagDataBaseSlot = reinterpret_cast<uintptr_t>(&dataBase);
    const halo2_vehicle_identity::Model* warthog = nullptr;
    for (const auto& candidate : halo2_vehicle_identity::kModels)
        if (!std::strcmp(candidate.name, "Warthog"))
            warthog = &candidate;
    Check(warthog != nullptr, "official Warthog present");
    Put(fixtureParent, 0, fixtureDefinition);
    fixtureParent[0xAA] = 1;
    Put(fixtureParent, 0x118, static_cast<int16_t>(warthog->nodes * 0x34));
    Put(fixtureObject, 0x38, fixtureModelTag);
    Put(fixtureModel, 4, fixtureRenderTag);
    Put(fixtureRender, 0x14, int32_t{1});
    Put(fixtureRender, 0x18, int32_t{0x100});
    std::memcpy(data + 0x100, warthog->bounds, 24);
    Check(ReadHalo2VehicleIdentity(fixtureParentHandle) == warthog->identity,
        "production native chain identifies Warthog");
    for (unsigned map = 1; map < 16; ++map)
    {
        fixtureParentHandle += 0x10000;
        fixtureDefinition += 17;
        fixtureModelTag += 0x10020;
        fixtureRenderTag += 0x10030;
        Put(fixtureParent, 0, fixtureDefinition);
        Put(fixtureObject, 0x38, fixtureModelTag);
        Put(fixtureModel, 4, fixtureRenderTag);
        Check(ReadHalo2VehicleIdentity(fixtureParentHandle) == warthog->identity,
            "map-local handles do not alter persistent model identity");
    }
    Check(ReadHalo2VehicleIdentity(fixtureParentHandle - 0x10000) == 0, "stale parent salt refused");
    g_vehicleIdentityVerified = false;
    Check(ReadHalo2VehicleIdentity(fixtureParentHandle) == 0, "missing optional native proof stays unknown");
    g_vehicleIdentityVerified = true;
    fixtureParent[0xAA] = 0;
    Check(ReadHalo2VehicleIdentity(fixtureParentHandle) == 0, "nonvehicle object refused");
    fixtureParent[0xAA] = 1;
    Put(fixtureParent, 0x118, int16_t{53});
    Check(ReadHalo2VehicleIdentity(fixtureParentHandle) == 0, "partial native matrix stride refused");
    Put(fixtureParent, 0x118, static_cast<int16_t>(warthog->nodes * 0x34));
    Put(data, 0x100, 10000.f);
    Check(ReadHalo2VehicleIdentity(fixtureParentHandle) == 0, "unknown/custom model retains game fallback");
    Put(data, 0x100, std::numeric_limits<float>::quiet_NaN());
    Check(ReadHalo2VehicleIdentity(fixtureParentHandle) == 0, "nonfinite model bounds refused");
    std::memcpy(data + 0x100, warthog->bounds, 24);
    mutate = true;
    Check(ReadHalo2VehicleIdentity(fixtureParentHandle) == 0, "definition changing during lookup rejected");
    mutate = false;
    Put(fixtureParent, 0, fixtureDefinition);
    Put(fixtureRender, 0x14, int32_t{2});
    Check(ReadHalo2VehicleIdentity(fixtureParentHandle) == 0, "multiple compression records not guessed");
    unsigned unique = 0;
    for (const auto& candidate : halo2_vehicle_identity::kModels)
    {
        const auto* resolved = halo2_vehicle_identity::Find(candidate.bounds, candidate.nodes);
        if (resolved)
        {
            ++unique;
            Check(resolved->identity == candidate.identity, "each unambiguous official tuple retains its own key");
        }
    }
    Check(unique > 50, "catalog covers official vehicle/submodel collection");
    Check(!halo2_vehicle_identity::Name(0), "unknown identity has no invented name");
    std::printf("PASS: %u H2 persistent vehicle identity checks, %u unique official models\n", checks, unique);
}
