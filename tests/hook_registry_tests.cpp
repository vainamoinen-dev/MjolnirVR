// HookRegistry: the shared installed-hook bookkeeping (Halo 3 first), and
// InstallGatedHookWith: the shared one-hook install/back-out policy (Reach, Halo 4).
#include "../src/common/hook_registry.h"
#include "../src/common/gated_hook.h"
#include <cstdio>
#include <vector>

static int g_failures = 0;
static void Check(bool ok, const char* what)
{
    if (!ok)
    {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

int main()
{
    int a = 0, b = 0, c = 0, d = 0;
    void* const pa = &a;
    void* const pb = &b;
    void* const pc = &c;
    void* const pd = &d;

    {
        HookRegistry<3> r;
        r.Remember(nullptr);
        r.Remember(pa);
        r.Remember(pa);
        r.Remember(pb);
        r.Remember(pc);
        r.Remember(pd);
        Check(r.Count() == 3 && r.At(0) == pa && r.At(1) == pb && r.At(2) == pc && r.At(3) == nullptr,
            "Remember ignores null and duplicates and drops past capacity, keeping order");
    }
    {
        HookRegistry<4> r;
        r.Remember(pa);
        r.Remember(pb);
        r.Remember(pc);
        std::vector<void*> disabled, failed;
        const bool ok = r.DisableAll(
            [&](void* t)
            {
                disabled.push_back(t);
                return t == pb ? MH_ERROR_MEMORY_PROTECT : MH_OK;
            },
            [&](void* t, MH_STATUS) { failed.push_back(t); });
        Check(!ok && disabled.size() == 3 && failed.size() == 1 && failed[0] == pb,
            "DisableAll attempts every hook after a failure and reports only the failing one");
        const bool safe = r.DisableAll([&](void* t)
            { return t == pa ? MH_ERROR_DISABLED : (t == pb ? MH_ERROR_NOT_CREATED : MH_OK); },
            [&](void*, MH_STATUS) { Check(false, "safe statuses are not failures"); });
        Check(safe, "MH_OK, MH_ERROR_DISABLED and MH_ERROR_NOT_CREATED count as disabled");
    }
    {
        HookRegistry<4> r;
        r.Remember(pa);
        r.Remember(pb);
        r.Remember(pc);
        std::vector<void*> order;
        const bool ok = r.RemoveAllNewestFirst(
            [&](void* t)
            {
                order.push_back(t);
                return t == pb ? MH_ERROR_NOT_CREATED : MH_OK;
            },
            [&](void*, MH_STATUS) { Check(false, "no failure expected"); });
        Check(ok && order.size() == 3 && order[0] == pc && order[1] == pb && order[2] == pa && r.Count() == 0,
            "RemoveAllNewestFirst removes newest first, accepts NOT_CREATED and empties the registry");
    }
    {
        HookRegistry<4> r;
        r.Remember(pa);
        r.Remember(pb);
        r.Remember(pc);
        std::vector<void*> order, failed;
        const bool ok = r.RemoveAllNewestFirst(
            [&](void* t)
            {
                order.push_back(t);
                return t == pb ? MH_ERROR_MEMORY_PROTECT : MH_OK;
            },
            [&](void* t, MH_STATUS) { failed.push_back(t); });
        Check(!ok && order.size() == 3 && order[0] == pc && order[1] == pb && order[2] == pa && failed.size() == 1 &&
                  failed[0] == pb,
            "RemoveAllNewestFirst attempts every hook past a failure and reports only the failure");
        Check(r.Count() == 1 && r.At(0) == pb, "only the hook that could not be removed stays recorded for a retry");
        std::vector<void*> retry;
        const bool retried = r.RemoveAllNewestFirst(
            [&](void* t)
            {
                retry.push_back(t);
                return MH_OK;
            },
            [&](void*, MH_STATUS) { Check(false, "retry succeeds"); });
        Check(retried && retry.size() == 1 && retry[0] == pb && r.Count() == 0,
            "a retry removes only what is still recorded");
        r.Remember(pd);
        Check(r.Count() == 1 && r.At(0) == pd, "the registry is reusable after it empties");
    }

    {
        HookRegistry<3> r;
        int ta = 0, tb = 0, tc = 0;
        Check(r.Remember(pa, &ta) && r.Remember(pb, &tb) && r.Remember(pc, &tc) && r.Full() &&
                  !r.Remember(pd, nullptr) && r.Remember(pa, nullptr),
            "Remember reports whether the target is recorded (refuses past capacity)");
        Check(r.TrampolineAt(0) == &ta && r.TrampolineAt(1) == &tb && r.TrampolineAt(2) == &tc &&
                  r.TrampolineAt(3) == nullptr,
            "each hook keeps its own trampoline; a duplicate does not overwrite it");
        r.Forget(pb);
        Check(r.Count() == 2 && r.At(0) == pa && r.At(1) == pc && r.TrampolineAt(0) == &ta &&
                  r.TrampolineAt(1) == &tc && !r.Full(),
            "Forget drops one rolled-back hook and keeps the others paired with their trampolines");
        r.Forget(pd);
        Check(r.Count() == 2, "Forget of an unrecorded target does nothing");
        r.Remember(pb, &tb);
        r.RemoveAllNewestFirst(
            [&](void* t) { return t == pc ? MH_ERROR_MEMORY_PROTECT : MH_OK; }, [&](void*, MH_STATUS) {});
        Check(r.Count() == 1 && r.At(0) == pc && r.TrampolineAt(0) == &tc,
            "a hook kept after a failed removal keeps its trampoline");
    }

    {
        // InstallGatedHookWith: scripted MinHook results, recorded calls.
        struct Script
        {
            MH_STATUS create, enable, disable, remove;
        };
        auto run = [&](Script sc, std::vector<const char*>& calls, void*& target, void*& original)
        {
            int trampoline = 0;
            return InstallGatedHookWith(
                [&](void*, void*, void** o)
                {
                    calls.push_back("create");
                    if (sc.create == MH_OK)
                        *o = &trampoline;
                    return sc.create;
                },
                [&](void*)
                {
                    calls.push_back("enable");
                    return sc.enable;
                },
                [&](void*)
                {
                    calls.push_back("disable");
                    return sc.disable;
                },
                [&](void*)
                {
                    calls.push_back("remove");
                    return sc.remove;
                },
                target, pa, pb, &original);
        };
        {
            std::vector<const char*> calls;
            void* t = nullptr;
            void* o = nullptr;
            const GatedHook h = run({MH_OK, MH_OK, MH_OK, MH_OK}, calls, t, o);
            Check(h.enabled && !h.retained && t == pa && o != nullptr && calls.size() == 2,
                "gated hook: create + enable succeed and keep target and original");
        }
        {
            std::vector<const char*> calls;
            void* t = nullptr;
            void* o = nullptr;
            const GatedHook h = run({MH_ERROR_ALREADY_CREATED, MH_OK, MH_OK, MH_OK}, calls, t, o);
            Check(!h.enabled && !h.retained && h.created == MH_ERROR_ALREADY_CREATED && t == nullptr && o == nullptr &&
                      calls.size() == 1,
                "gated hook: a refused create touches nothing else and clears target");
        }
        {
            std::vector<const char*> calls;
            void* t = nullptr;
            void* o = nullptr;
            const GatedHook h = run({MH_OK, MH_ERROR_MEMORY_PROTECT, MH_ERROR_DISABLED, MH_OK}, calls, t, o);
            Check(!h.enabled && !h.retained && h.created == MH_OK && t == nullptr && o == nullptr && calls.size() == 4,
                "gated hook: a failed enable is disabled and removed, then forgotten");
        }
        {
            std::vector<const char*> calls;
            void* t = nullptr;
            void* o = nullptr;
            const GatedHook h = run({MH_OK, MH_ERROR_MEMORY_PROTECT, MH_ERROR_MEMORY_PROTECT, MH_OK}, calls, t, o);
            Check(!h.enabled && h.retained && t == pa && o != nullptr && calls.size() == 3,
                "gated hook: an unconfirmed disable is never removed; target and original are kept");
        }
        {
            std::vector<const char*> calls;
            void* t = nullptr;
            void* o = nullptr;
            const GatedHook h = run({MH_OK, MH_ERROR_MEMORY_PROTECT, MH_OK, MH_ERROR_MEMORY_ALLOC}, calls, t, o);
            Check(!h.enabled && h.retained && t == pa && o != nullptr,
                "gated hook: a failed removal keeps target and original for teardown");
        }
        {
            std::vector<const char*> calls;
            void* t = nullptr;
            void* o = nullptr;
            const GatedHook h = run({MH_OK, MH_ERROR_MEMORY_PROTECT, MH_OK, MH_ERROR_NOT_CREATED}, calls, t, o);
            Check(!h.enabled && !h.retained && t == nullptr, "gated hook: NOT_CREATED on removal counts as removed");
        }
        Check(HookDisableStatusIsSafe(MH_OK) && HookDisableStatusIsSafe(MH_ERROR_DISABLED) &&
                  HookDisableStatusIsSafe(MH_ERROR_NOT_CREATED) && !HookDisableStatusIsSafe(MH_ERROR_MEMORY_PROTECT),
            "disable statuses that leave the hook unreachable");
    }

    if (g_failures == 0)
        std::printf("hook registry tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
