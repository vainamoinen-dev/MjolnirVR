// ODST physical crouch: finds the native crouch witnesses and lowers the camera by the player's real crouch.
// Shares the camera step with Halo 3 (native_physical_crouch.inl).
#pragma once
// Included once, by src/dll/titles/odst/odst_camera_core.cpp; also compiled by
//   tests/physical_crouch_apply_golden_tests.cpp, which supplies its own versions. Not a
//   standalone header: it uses what the host defines above its include line, namely
//   OdstApplyPhysicalCrouchCamera, OdstLoadedTagDefinition, OdstPreparePhysicalCrouchCamera,
//   g_odstPlayerUnitGetter, g_odstRuntimeGeneration.
#include "../../core/native_physical_crouch.inl"
std::atomic<uint32_t> g_odstCrouchCameraGeneration{};

void OdstPreparePhysicalCrouchCamera(uintptr_t base, size_t size)
{
    g_odstCrouchCameraGeneration.store(0, std::memory_order_release);
    unsigned admitted = 0;
    for (const auto& witness : kCrouchAdditionalWitnesses)
    {
        if (witness.title != GameTitle::Halo3ODST)
            continue;
        const auto hit = sig::Find(base, size, witness.pattern);
        if (!hit || hit - base != witness.rva || sig::Find(hit + 1, base + size - hit - 1, witness.pattern))
        {
            LOG("ODST physical crouch camera: own-title height witness missing/ambiguous; correction stays stock, camera core unaffected");
            return;
        }
        ++admitted;
    }
    if (admitted != 5)
        return;
    g_odstCrouchCameraGeneration.store(g_odstRuntimeGeneration.load(), std::memory_order_release);
    LOG("ODST physical crouch camera: own-title height witnesses verified; read-only local-biped correction, bounded below native camera");
}

const uint8_t* OdstPhysicalCrouchUnit(uint32_t owner)
{
    if (!OdstContactObject(owner, true))
        return nullptr;
    const auto* table = *reinterpret_cast<const uint8_t* const*>(OdstContactTls() + kOdstTlsObjectTableOffset);
    const auto* entries = *reinterpret_cast<const uint8_t* const*>(table + 0x48);
    return *reinterpret_cast<const uint8_t* const*>(entries + (owner & 0xffff) * 0x18 + 0x10);
}

struct OdstPhysicalCrouchTraits
{
    static constexpr GameTitle kTitle = GameTitle::Halo3ODST;
    static uint32_t Generation() { return g_odstRuntimeGeneration.load(std::memory_order_acquire); }
    static uint32_t CameraGeneration() { return g_odstCrouchCameraGeneration.load(std::memory_order_acquire); }
    static bool HasPlayerUnit() { return g_odstPlayerUnitGetter != nullptr; }
    static uint32_t PlayerUnit() { return static_cast<uint32_t>(g_odstPlayerUnitGetter(0)); }
    static const uint8_t* Unit(uint32_t owner) { return OdstPhysicalCrouchUnit(owner); }
    static const uint8_t* Definition(uint32_t tag) { return OdstLoadedTagDefinition(tag); }
    static bool Reduction(const uint8_t* unit, const uint8_t* definition, float& reduction)
    {
        return physical_crouch_native::Odst(unit, definition, reduction);
    }
};

void OdstApplyPhysicalCrouchCamera(float* position, float physicalDown, bool playerControlled)
{
    ApplyNativePhysicalCrouch<OdstPhysicalCrouchTraits>(position, physicalDown, playerControlled);
}
