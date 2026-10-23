// Halo 4 first-person effect hide bridge, called from halo4_restoration.asm: while enabled, it moves each local
// first-person effect's matrix to a finite far distance so the effect is not seen in VR.
// A torn effect record leaves that effect stock; the camera and OpenXR session are not touched here.
#pragma once
// Included once, by src/dll/titles/h4/halo4_camera_core.cpp; also compiled by
//   tests/muzzle_suppression_tests.cpp, which supplies its own versions. Not a standalone
//   header: it uses what the host defines above its include line, namely Halo4EffectHideBridge,
//   g_halo4EffectsEnabled, g_halo4EffectsHidden.
// Production local first-person effect bridge, shared with the fixture.
extern "C" void __fastcall Halo4EffectHideBridge(void* descriptor, void* matrix)
{
    if (!g_halo4EffectsEnabled || !descriptor || !matrix)
        return;
    __try
    {
        const uint8_t flags = *(static_cast<const uint8_t*>(descriptor) + 0x4A);
        if (!Halo4EffectDescriptorIsLocalFirstPerson(flags))
            return;
        constexpr uint32_t kFiniteFar = 0x461C4000u;
        auto* const destination = static_cast<uint8_t*>(matrix);
        std::memcpy(destination + 0x28, &kFiniteFar, sizeof(kFiniteFar));
        std::memcpy(destination + 0x2C, &kFiniteFar, sizeof(kFiniteFar));
        std::memcpy(destination + 0x30, &kFiniteFar, sizeof(kFiniteFar));
        InterlockedIncrement64(&g_halo4EffectsHidden);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("hiding a Halo 4 first-person effect faulted");
        // A torn optional effect record leaves that effect stock. The camera
        // and OpenXR session are owned by a separate transaction.
    }
}
