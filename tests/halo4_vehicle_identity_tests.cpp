// Tests for Halo 4 vehicle identity (src/dll/titles/h4/halo4_vehicle_identity.inl,
// halo4_render_model_identity.inl): the occupied vehicle and its render-model checksum.
// Built as halo4_vehicle_identity_tests and run by ctest. Native tag tables are fakes; no game process.
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <initializer_list>
std::atomic<bool> g_halo4VehicleIdentityProven{true};
uintptr_t g_halo4RenderModelTagIndexPointerSlot{}, g_halo4RenderModelGroupBaseTable{};
constexpr uintptr_t kHalo4RenderModelTagIndexPointerRva = 0x107C0B0, kHalo4RenderModelGroupBaseTableRva = 0x496A180;
constexpr int kHalo4FirstPersonBankTransforms = 255;
namespace sig
{
    uintptr_t Find(uintptr_t, size_t, const char*)
    {
        return 0;
    }
}
alignas(16) uint8_t fixtureParent[0x100]{}, tags[0x2000]{};
uint64_t fixtureTable[512]{}, pages[16]{};
uintptr_t tableBase = reinterpret_cast<uintptr_t>(fixtureTable);
unsigned checks = 0, checksumReads = 0;
bool mutate = false;
bool Contains(const void* source, size_t bytes, const void* start, size_t length)
{
    const auto p = reinterpret_cast<uintptr_t>(source), b = reinterpret_cast<uintptr_t>(start);
    return p >= b && p - b <= length && bytes <= length - (p - b);
}
int Halo4SafeRead(const void* source, void* destination, size_t bytes)
{
    if (!Contains(source, bytes, fixtureParent, sizeof(fixtureParent)) &&
        !Contains(source, bytes, tags, sizeof(tags)) && !Contains(source, bytes, fixtureTable, sizeof(fixtureTable)) &&
        !Contains(source, bytes, pages, sizeof(pages)) && !Contains(source, bytes, &tableBase, sizeof(tableBase)))
        return 0;
    if (mutate && source == tags + 0x808 && ++checksumReads == 2)
        tags[0x808] ^= 1;
    std::memcpy(destination, source, bytes);
    return 1;
}
#include "../src/dll/titles/h4/halo4_render_model_identity.inl"
#include "../src/dll/titles/h4/halo4_vehicle_identity.inl"
template <class T>
void Put(uint8_t* bytes, size_t offset, T value)
{
    std::memcpy(bytes + offset, &value, sizeof(value));
}
void Check(bool pass, const char* reason)
{
    ++checks;
    if (!pass)
    {
        std::printf("FAIL: %s\n", reason);
        std::exit(1);
    }
}
void SetMap(unsigned map)
{
    const uint16_t definition = static_cast<uint16_t>(map * 3 + 1), model = definition + 1, render = definition + 2;
    std::memset(fixtureTable, 0, sizeof(fixtureTable));
    fixtureTable[definition] = uint64_t{0x100 / 4} << 32;
    fixtureTable[model] = uint64_t{0x400 / 4} << 32;
    fixtureTable[render] = uint64_t{0x800 / 4} << 32;
    Put(fixtureParent, 8, definition);
    Put(tags, 0x170, uint32_t{0xABCD0000u | model});
    Put(tags, 0x40C, uint32_t{0xFEDC0000u | render});
    Put(tags, 0x808, uint32_t{0x12345678});
    Put(tags, 0x830, int32_t{17});
}
int main()
{
    g_halo4RenderModelTagIndexPointerSlot = reinterpret_cast<uintptr_t>(&tableBase);
    g_halo4RenderModelGroupBaseTable = reinterpret_cast<uintptr_t>(pages);
    pages[0] = reinterpret_cast<uintptr_t>(tags);
    const uint64_t expected = (uint64_t{17} << 32) | 0x12345678u;
    for (unsigned map = 0; map < 64; ++map)
    {
        SetMap(map);
        Check(Halo4ReadVehicleIdentity(fixtureParent) == expected,
            "map-local tag indices and salts do not change authored identity");
    }
    SetMap(0);
    g_halo4VehicleIdentityProven = false;
    Check(!Halo4ReadVehicleIdentity(fixtureParent), "missing native proof retains per-game fallback");
    g_halo4VehicleIdentityProven = true;
    Check(!Halo4ReadVehicleIdentity(nullptr), "missing parent refused");
    Put(tags, 0x170, UINT32_MAX);
    Check(!Halo4ReadVehicleIdentity(fixtureParent), "missing model reference refused");
    SetMap(0);
    Put(tags, 0x40C, UINT32_MAX);
    Check(!Halo4ReadVehicleIdentity(fixtureParent), "missing render model reference refused");
    SetMap(0);
    Put(tags, 0x808, uint32_t{0});
    Check(!Halo4ReadVehicleIdentity(fixtureParent), "zero checksum never persists a datum fallback");
    SetMap(0);
    Put(tags, 0x808, UINT32_MAX);
    Check(!Halo4ReadVehicleIdentity(fixtureParent), "sentinel checksum refused");
    SetMap(0);
    for (int count : {-1, 0, 256})
    {
        Put(tags, 0x830, count);
        Check(!Halo4ReadVehicleIdentity(fixtureParent), "invalid node count refused");
    }
    SetMap(0);
    pages[0] = UINTPTR_MAX - 3;
    Check(!Halo4ReadVehicleIdentity(fixtureParent), "packed tag address overflow refused");
    pages[0] = reinterpret_cast<uintptr_t>(tags);
    mutate = true;
    checksumReads = 0;
    Check(!Halo4ReadVehicleIdentity(fixtureParent), "model checksum changing during receipt rejected");
    mutate = false;
    SetMap(0);
    fixtureTable[1] = 0;
    Check(!Halo4ReadVehicleIdentity(fixtureParent), "unloaded definition refused");
    SetMap(0);
    tableBase = 0;
    Check(!Halo4ReadVehicleIdentity(fixtureParent), "unloaded tag table refused");
    std::printf("PASS: %u H4 persistent vehicle identity checks\n", checks);
}
