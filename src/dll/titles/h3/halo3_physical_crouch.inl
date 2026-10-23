// Halo 3 physical crouch: finds the native crouch witnesses and lowers the camera by the player's real crouch.
// No hook, engine function call, native state write or scan on the camera thread.
#pragma once
// Included once, by src/dll/titles/h3/halo3_camera_install.cpp; also compiled by
//   tests/physical_crouch_apply_golden_tests.cpp, which supplies its own versions. Not a
//   standalone header: it uses what the host defines above its include line, namely
//   Halo3ApplyPhysicalCrouchCamera, Halo3LoadedTagDefinition, Halo3PreparePhysicalCrouchCamera,
//   g_halo3PlayerUnitGetter, g_halo3RuntimeGeneration.
#include "halo3_physical_crouch_reader.inl"
#include "../../core/native_physical_crouch.inl"

std::atomic<uint32_t> g_halo3CrouchCameraGeneration{};

void Halo3PreparePhysicalCrouchCamera(uintptr_t base, size_t size)
{
    g_halo3CrouchCameraGeneration.store(0, std::memory_order_release);
    // Each witness is independently unique in the pinned module; no hook,
    // engine function call, native state write, or scan on the camera thread.
    static constexpr const char* witnesses[] = {
        "41 83 CB FF 44 39 5E 10 0F 85 B0 00 00 00 8B 86 10 01 00 00 41 8D 5B 02 C1 E8 02 84 C3 75 37 44 38 B6 96 00 00 00 75 2E",
        "41 8B CE E8 C5 20 00 00 45 33 C0 84 C0 75 08 F3 0F 10 BF 84 03 00 00",
        "F3 0F 5C F7 F3 41 0F 59 BC 24 EC 02 00 00 F3 41 0F 59 B4 24 E8 02 00 00 F3 0F 58 F7 F3 0F 59 B7 8C 00 00 00",
        "32 C0 80 B9 DE 04 00 00 06 75 16 8B 89 E4 04 00 00 BA 01 00 00 00 C1 E9 04 84 CA 0F B6 C0 0F 45 C2 C3",
        "48 0F BF 87 62 01 00 00 8B 0C 38 83 C1 F9 83 F9 01 77 04 48 8D 77 68"};
    for (const auto* pattern : witnesses)
    {
        const auto hit = sig::Find(base, size, pattern);
        if (!hit || sig::Find(hit + 1, base + size - hit - 1, pattern))
        {
            LOG("H3 physical crouch camera: native height witness missing/ambiguous; correction stays stock, camera core unaffected");
            return;
        }
    }
    g_halo3CrouchCameraGeneration.store(g_halo3RuntimeGeneration.load(), std::memory_order_release);
    LOG("H3 physical crouch camera: own-title height witnesses verified; read-only local-biped correction, bounded below native camera");
}

struct Halo3PhysicalCrouchTraits
{
    static constexpr GameTitle kTitle = GameTitle::Halo3;
    static uint32_t Generation() { return g_halo3RuntimeGeneration.load(std::memory_order_acquire); }
    static uint32_t CameraGeneration() { return g_halo3CrouchCameraGeneration.load(std::memory_order_acquire); }
    static bool HasPlayerUnit() { return g_halo3PlayerUnitGetter != nullptr; }
    static uint32_t PlayerUnit() { return static_cast<uint32_t>(g_halo3PlayerUnitGetter(0)); }
    static const uint8_t* Unit(uint32_t owner) { return Halo3MeleeSelectionObject(owner, 0); }
    static const uint8_t* Definition(uint32_t tag) { return Halo3LoadedTagDefinition(tag); }
    static bool Reduction(const uint8_t* unit, const uint8_t* definition, float& reduction)
    {
        return Halo3PhysicalCrouchReduction(unit, definition, reduction);
    }
};

void Halo3ApplyPhysicalCrouchCamera(float* position, float physicalDown, bool playerControlled)
{
    ApplyNativePhysicalCrouch<Halo3PhysicalCrouchTraits>(position, physicalDown, playerControlled);
}
