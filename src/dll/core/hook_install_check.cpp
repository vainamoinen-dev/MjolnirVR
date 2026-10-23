// Install-time detour check for every MinHook hook in the DLL; see hook_install_check.h.
// This is the only file that calls the real MinHook create/remove entry points.
#include "hook_install_check.h"
#include <windows.h>
#include <psapi.h>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <mutex>
#include "../../common/log.h"

namespace
{
    struct LiveHook
    {
        LPVOID target;
        LPVOID detour;
    };
    struct ModuleCount
    {
        HMODULE module;
        uintptr_t end;
        unsigned ok, missing, shared, reportedOk, reportedMissing, reportedShared, reportedLive;
    };
    std::mutex lock;
    LiveHook live[256]{};
    ModuleCount counts[16]{};

    bool UnwindCovers(LPVOID detour)
    {
        DWORD64 imageBase = 0;
        const auto* entry = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(detour), &imageBase, nullptr);
        if (!entry || entry->EndAddress <= entry->BeginAddress)
            return false;
        const DWORD64 address = reinterpret_cast<DWORD64>(detour);
        return address >= imageBase + entry->BeginAddress && address < imageBase + entry->EndAddress;
    }
    ModuleCount* CountFor(LPVOID target)
    {
        HMODULE module = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(target), &module);
        for (auto& count : counts)
            if (count.module == module)
                return &count;
        // The last slot is reserved for "other modules" (module stays null).
        for (size_t i = 0; i + 1 < std::size(counts); ++i)
            if (!counts[i].module && !counts[i].ok && !counts[i].missing && !counts[i].shared)
            {
                counts[i].module = module;
                // Remember the image range at first install: once MCC unloads the
                // module its targets no longer resolve, but must still count as live.
                MODULEINFO info{};
                if (module && GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info)))
                    counts[i].end = reinterpret_cast<uintptr_t>(module) + info.SizeOfImage;
                return &counts[i];
            }
        return &counts[std::size(counts) - 1];
    }
    void Describe(HMODULE module, char* out, size_t bytes)
    {
        char path[MAX_PATH] = "";
        if (!module || !GetModuleFileNameA(module, path, MAX_PATH))
        {
            snprintf(out, bytes, "other modules");
            return;
        }
        const char* name = strrchr(path, '\\');
        snprintf(out, bytes, "%s", name ? name + 1 : path);
    }
}

MH_STATUS MCCVR_CreateHookChecked(LPVOID target, LPVOID detour, LPVOID* original)
{
    std::lock_guard<std::mutex> guard(lock);
    ModuleCount* count = CountFor(target);
    if (!UnwindCovers(detour))
    {
        ++count->missing;
        LOG("detour unwind check: detour %p for target %p has no unwind range; hook refused (stock)", detour, target);
        return MH_ERROR_UNSUPPORTED_FUNCTION;
    }
    for (const auto& hook : live)
        if (hook.detour == detour && hook.target && hook.target != target)
        {
            ++count->shared;
            LOG("detour unwind check: detour %p already serves target %p, refused for %p (folded detours; stock)",
                detour, hook.target, target);
            return MH_ERROR_ALREADY_CREATED;
        }
    // A hook that cannot be tracked could not be fold-checked later: refuse it.
    LiveHook* slot = nullptr;
    for (auto& hook : live)
        if (!hook.target)
        {
            slot = &hook;
            break;
        }
    if (!slot)
    {
        ++count->missing;
        LOG("detour unwind check: tracking table full (%zu hooks); hook for target %p refused (stock)", std::size(live),
            target);
        return MH_ERROR_MEMORY_ALLOC;
    }
    const MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status == MH_OK)
    {
        ++count->ok;
        *slot = {target, detour};
    }
    return status;
}

MH_STATUS MCCVR_RemoveHookChecked(LPVOID target)
{
    const MH_STATUS status = MH_RemoveHook(target);
    if (status == MH_OK || status == MH_ERROR_NOT_CREATED)
    {
        std::lock_guard<std::mutex> guard(lock);
        for (auto& hook : live)
            if (hook.target == target)
                hook = {};
    }
    return status;
}

void HookInstallCheck_Report()
{
    std::lock_guard<std::mutex> guard(lock);
    for (auto& count : counts)
    {
        if (!count.ok && !count.missing && !count.shared)
            continue;
        // Live hooks whose target lies in this module's image range as recorded
        // at first install (still counted after MCC unloads the module).
        const uintptr_t begin = reinterpret_cast<uintptr_t>(count.module);
        unsigned liveCount = 0;
        for (const auto& hook : live)
        {
            const uintptr_t target = reinterpret_cast<uintptr_t>(hook.target);
            if (target && count.module && target >= begin && target < count.end)
                ++liveCount;
        }
        // Also report when hooks are removed (title teardown), not only
        // when new installs change the cumulative counts.
        if (count.ok == count.reportedOk && count.missing == count.reportedMissing &&
            count.shared == count.reportedShared && liveCount == count.reportedLive)
            continue;
        char name[96];
        Describe(count.module, name, sizeof(name));
        LOG("detour unwind check: %s %u ok, %u missing, %u folded, %u live", name, count.ok, count.missing,
            count.shared, liveCount);
        count.reportedOk = count.ok;
        count.reportedMissing = count.missing;
        count.reportedShared = count.shared;
        count.reportedLive = liveCount;
    }
}

unsigned HookInstallCheck_LiveTargets(uintptr_t begin, uintptr_t end, uintptr_t* out, unsigned capacity) noexcept
{
    // Diagnostic read without the lock (the faulting thread may hold it):
    // pointer-sized aligned loads; a slot changing mid-scan only misreports
    // that one hook.
    unsigned total = 0;
    for (const auto& hook : live)
    {
        const uintptr_t target = reinterpret_cast<uintptr_t>(*reinterpret_cast<LPVOID const volatile*>(&hook.target));
        if (!target || target < begin || target >= end)
            continue;
        if (out && total < capacity)
            out[total] = target;
        ++total;
    }
    return total;
}
