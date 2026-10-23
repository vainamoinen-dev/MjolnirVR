// Halo Reach action-test builder guard; see reach_action_test_guard_logic.h.
// Ticked by the camera-core worker in src/dll/core/game.cpp. Never pins, scans or writes while a level is loading.
//
// Lifetime follows the camera core's module rule (game.cpp): no pin,
// scan or write while the level is loading, so the guard installs only once a
// Reach level runs and then holds its own module pin for as long as MinHook
// owns the entry. It therefore protects every later load in that Reach session
// (restart, revert, next mission), not the very first one.
// Internal compatibility fix: config key reach_action_test_guard only (no F1).
#include "reach_action_test_guard.h"
#include "../../core/hook_install_check.h" // MinHook create/remove go through the install-time detour check
#include "../../core/hook_quiescence.h"
#include "../../core/title_adapter.h"
#include "../../../common/config.h"
#include "../../../common/gated_hook.h"
#include "../../../common/log.h"
#include "../../../common/minhook_lifecycle.h"
#include "../../../common/titles/reach/reach_action_test_guard_logic.h"
#include "../../../common/title_registry.h"
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include "../../core/guard_count.h"

uint64_t Input_LastPadDeliveryMs() noexcept;
uint32_t Input_LastPadButtons() noexcept;

namespace
{
    using namespace reach_action_test_guard;
    using BuilderFn = void(__fastcall*)(int, void*, void*);

    void* g_target = nullptr;
    BuilderFn g_original = nullptr;
    bool g_enabled = false;
    std::atomic<bool> g_live{false}; // read by the XInput shim (neutral pad)
    HMODULE g_pin = nullptr;
    const uint32_t* g_tlsIndex = nullptr;
    uint32_t g_generation = 0;
    bool g_attempted = false;
    std::atomic<uint32_t> g_callbacks{0};
    std::atomic<uint64_t> g_skipped{0};
    uint64_t g_skippedLogged = 0;
    bool g_firstSkipLogged = false;

    void* ReadActionTestState() noexcept
    {
        auto** slots = reinterpret_cast<unsigned char**>(__readgsqword(0x58));
        if (!slots || !g_tlsIndex)
            return nullptr;
        unsigned char* block = slots[*g_tlsIndex];
        return block ? *reinterpret_cast<void**>(block + kTlsMember) : nullptr;
    }

    __declspec(noinline) void __fastcall BuilderDetour(int user, void* actions, void* input)
    {
        g_callbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            const BuilderFn original = g_original;
            void* state = nullptr;
            __try
            {
                state = ReadActionTestState();
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                GUARD_COUNT("reading Reach's action-test state faulted");
                state = nullptr;
            }
            // Only our own TLS read is guarded; a fault inside the builder itself
            // goes to the game exactly as stock.
            const bool called =
                original && CallIfStateExists([state] { return state; }, [&] { original(user, actions, input); });
            if (!called)
                g_skipped.fetch_add(1, std::memory_order_relaxed);
        }
        __finally
        {
            g_callbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    bool Retire() noexcept
    {
        if (!g_target)
            return true;
        if (!HookDisableStatusIsSafe(MCCVR_DisableHookForRetirement(g_target)))
            return false;
        g_enabled = false;
        g_live.store(false, std::memory_order_release);
        const void* functions[]{reinterpret_cast<const void*>(&BuilderDetour)};
        const void* trampolines[]{reinterpret_cast<const void*>(g_original)};
        if (!WaitForNativeDetourQuiescence(functions, trampolines, 1, g_callbacks))
            return false;
        const MH_STATUS removed = MCCVR_RemoveHookChecked(g_target);
        if (removed != MH_OK && removed != MH_ERROR_NOT_CREATED)
            return false;
        g_target = nullptr;
        g_original = nullptr;
        g_tlsIndex = nullptr;
        if (g_pin)
        {
            FreeLibrary(g_pin);
            g_pin = nullptr;
        }
        return true;
    }

    bool Install(uintptr_t base, size_t size) noexcept
    {
        if (size < kBuilderRva + kPrologueSize || size < kCallerCallRva + 5)
            return false;
        uintptr_t tlsRva = 0;
        __try
        {
            tlsRva = DecodeTlsIndexRva(reinterpret_cast<const uint8_t*>(base + kBuilderRva), kBuilderRva);
            if (!tlsRva || tlsRva + 4 > size ||
                !CallEdgeMatches(reinterpret_cast<const uint8_t*>(base + kCallerCallRva), kCallerCallRva, kBuilderRva))
                return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("decoding Reach's action-test binding faulted");
            return false;
        }
        HMODULE pin = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(base), &pin) ||
            reinterpret_cast<uintptr_t>(pin) != base)
        {
            if (pin)
                FreeLibrary(pin);
            return false;
        }
        g_tlsIndex = reinterpret_cast<const uint32_t*>(base + tlsRva);
        const GatedHook hook = InstallGatedHookWith([](void* t, void* d, void** o)
            { return MCCVR_CreateHookChecked(t, d, o); }, [](void* t) { return MH_EnableHook(t); },
            [](void* t) { return MCCVR_DisableHookForRetirement(t); },
            [](void* t) { return MCCVR_RemoveHookChecked(t); }, g_target, reinterpret_cast<void*>(base + kBuilderRva),
            reinterpret_cast<void*>(&BuilderDetour), reinterpret_cast<void**>(&g_original));
        g_enabled = hook.enabled;
        g_live.store(hook.enabled, std::memory_order_release);
        if (hook.enabled || hook.retained)
        {
            g_pin = pin;
            return hook.enabled;
        }
        g_tlsIndex = nullptr;
        FreeLibrary(pin);
        return false;
    }

    const char* ModeName(RuntimeMode mode) noexcept
    {
        return RuntimeModeName(mode);
    }
}

bool ReachActionTestGuard_Live() noexcept
{
    return g_live.load(std::memory_order_acquire);
}

void ReachActionTestGuard_Tick(uintptr_t base, size_t size, uint32_t generation, bool soleReachTitle, bool levelRunning)
{
    const uint64_t skipped = g_skipped.load(std::memory_order_relaxed);
    if (skipped != g_skippedLogged)
    {
        if (!g_firstSkipLogged)
        {
            g_firstSkipLogged = true;
            const uint64_t last = Input_LastPadDeliveryMs();
            const uint64_t now = GetTickCount64();
            LOG("Reach action-test guard: skipped the builder %llu time(s) because the action-test "
                "state was not allocated (the +0x65BA9 crash); runtime mode %s, pad last delivered "
                "%s%llu ms ago (buttons 0x%04X)",
                static_cast<unsigned long long>(skipped), ModeName(TitleAdapter_GetRuntimeMode()),
                last ? "" : "never; ", static_cast<unsigned long long>(last && now >= last ? now - last : 0),
                Input_LastPadButtons());
        }
        g_skippedLogged = skipped;
    }
    const bool want = g_config.reach_action_test_guard && soleReachTitle && generation && base;
    if (g_target && (!want || generation != g_generation))
    {
        if (Retire())
            LOG("Reach action-test guard: removed (skipped %llu call(s) this session)",
                static_cast<unsigned long long>(g_skipped.load(std::memory_order_relaxed)));
        else
            return; // retried next tick; the pin keeps the module mapped meanwhile
    }
    if (generation != g_generation)
    {
        g_generation = generation;
        g_attempted = false;
    }
    if (!want || g_target || g_attempted || !levelRunning)
        return;
    g_attempted = true;
    const bool installed = Install(base, size);
    LOG("Reach action-test guard: %s",
        installed ? "installed at haloreach.dll+0x657DC (prologue, TLS +0xD8 load and caller edge proven); the "
                    "input update skips its action tests whenever their state is not allocated"
                  : "NOT installed (proof or hook failed); stock builder");
}
