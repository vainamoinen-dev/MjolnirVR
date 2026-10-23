// Halo 2 dual-wield aim: with two owned weapons, each one fires along its own controller ray.
// Included once, by src/dll/titles/h2/halo2_observer_6dof.cpp. Not a standalone header: it
//   uses what the host defines above its include line, namely Halo2ObjectDatumAccessorFn,
//   Halo2WeaponAimHelperFn, g_objectDatumAccessor.
#pragma once
// H2EK 49C960(weapon, barrel, projectile, predicted) -> 47DC20(unit, ...).
// Retail 8E4940 -> 8F0F70; verified separately from the dormant firing-only direction hook.
// With two verified owned weapons, each role overrides its own native ray.
// A single equipped weapon retains the existing native aiming path.
using Halo2DualFireFn = void(__fastcall*)(uint32_t, int16_t, int32_t, uint8_t);
using Halo2DualCameraFn = int32_t(__fastcall*)(uint32_t, float*, float*);
using Halo2DualLocationFn = const void*(__fastcall*)(uint32_t);
struct Halo2DualRuntime
{
    uintptr_t base = 0;
    void* fireTarget = nullptr;
    void* aimTarget = nullptr;
    Halo2DualFireFn fireOriginal = nullptr;
    Halo2WeaponAimHelperFn aimOriginal = nullptr;
    void* cameraTarget = nullptr;
    void* locationTarget = nullptr;
    Halo2DualCameraFn cameraOriginal = nullptr;
    Halo2DualLocationFn locationOriginal = nullptr;
    uint32_t generation = 0;
    uint64_t installedAtMs = 0;
    std::atomic<bool> enabled{false}, faulted{false};
    std::atomic<uint32_t> callbacks{0};
    std::atomic<uint64_t> primaryRays{0}, secondaryRays{0}, refused{0};
    std::atomic<uint64_t> nativeQueries{0}, targetRestoresRefused{0};
} g_halo2Dual;

// This independently verifies the complete salt and parent-unit identity.
// H2EK object_get + type mask4 and retail 8E4940 prove weapon type2,
// object parent flag+130 and parent full handle+158. No copied H3 fields.
bool Halo2DualWeaponOwned(uint32_t weapon, uint32_t unit)
{
    if (weapon == UINT32_MAX || !(weapon >> 16) || unit == UINT32_MAX)
        return false;
    const auto* table = *reinterpret_cast<const uint8_t* const*>(g_halo2Dual.base + kHalo2ObjectsDataArrayPointerRva);
    if (!table || !table[0x29] || *reinterpret_cast<const int32_t*>(table + 0x24) != 0xC)
        return false;
    const int32_t capacity = *reinterpret_cast<const int32_t*>(table + 0x20);
    const uintptr_t offset = *reinterpret_cast<const uintptr_t*>(table + 0x48);
    if (capacity <= 0 || capacity > 0x2800 || (weapon & 0xFFFF) >= uint32_t(capacity) || !offset ||
        offset > UINTPTR_MAX - reinterpret_cast<uintptr_t>(table) - size_t(capacity) * 0xC)
        return false;
    const auto* entry = table + offset + (weapon & 0xFFFF) * 0xC;
    if (*reinterpret_cast<const uint16_t*>(entry) != uint16_t(weapon >> 16) || entry[3] != 2)
        return false;
    const auto accessor =
        reinterpret_cast<Halo2ObjectDatumAccessorFn>(g_objectDatumAccessor.load(std::memory_order_acquire));
    const auto* object = accessor ? static_cast<const uint8_t*>(accessor(entry)) : nullptr;
    return object && (object[0x130] & 1u) && *reinterpret_cast<const uint32_t*>(object + 0x158) == unit;
}

#include "halo2_independent_shots.inl"

bool RemoveHalo2DualAim();

#include "halo2_muzzle_lifecycle.inl"
#include "halo2_dual_lifecycle.inl"
