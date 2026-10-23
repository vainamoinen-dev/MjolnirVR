// Halo 2 Classic first-person particle gate (a detour of the native particle renderer).
// Included once, by src/dll/titles/h2/halo2_observer_6dof.cpp; also compiled by
//   tests/muzzle_suppression_tests.cpp, which supplies its own versions. Not a standalone
//   header: it uses what the host defines above its include line, namely
//   Halo2ParticleRendererFn, g_armed, g_levelLive, g_moduleBase, g_particleActiveCallbacks,
//   g_particleHitPending, g_particleOriginal, g_particleReadFaults, g_particleSuppressed,
//   g_teardownRequested.
#pragma once
// Production Classic-only particle gate; included by the H2 adapter and fixture.
__declspec(noinline) void __fastcall Halo2ParticleRendererDetour(
    uint32_t arg0, uint32_t currentUserFirstPerson, uint32_t arg2, uint32_t arg3)
{
    g_particleActiveCallbacks.fetch_add(1, std::memory_order_acq_rel);
    __try
    {
        const auto original =
            reinterpret_cast<Halo2ParticleRendererFn>(g_particleOriginal.load(std::memory_order_acquire));
        bool suppress = false;
        if (original && g_config.hide_muzzle_flash[5] && g_armed.load(std::memory_order_acquire) &&
            g_levelLive.load(std::memory_order_acquire) && !g_teardownRequested.load(std::memory_order_acquire))
        {
            const uintptr_t base = g_moduleBase.load(std::memory_order_acquire);
            if (base)
            {
                uint8_t classicDisabled = 1;
                bool readable = false;
                __try
                {
                    classicDisabled =
                        *reinterpret_cast<const volatile uint8_t*>(base + kHalo2ClassicRenderDisabledByteRva);
                    readable = true;
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    GUARD_COUNT("reading Halo 2's particle render flag faulted");
                    g_particleReadFaults.fetch_add(1, std::memory_order_relaxed);
                }
                suppress = readable && Halo2ShouldSuppressClassicFirstPersonParticle(
                                           classicDisabled, static_cast<uint8_t>(currentUserFirstPerson));
            }
        }

        if (suppress)
        {
            g_particleSuppressed.fetch_add(1, std::memory_order_relaxed);
            g_particleHitPending.store(true, std::memory_order_release);
        }
        else if (original)
        {
            original(arg0, currentUserFirstPerson, arg2, arg3);
        }
    }
    __finally
    {
        g_particleActiveCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
}
