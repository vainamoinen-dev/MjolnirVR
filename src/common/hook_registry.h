#pragma once
// Bookkeeping for a title's installed MinHook detours and their ordered retirement.
// Used by dll/core/game.cpp and tests/hook_registry_tests.cpp.
//
// A title's installed MinHook detours, recorded as they are enabled and retired
// as one set: every hook is disabled first (all are attempted even after a
// failure), the caller proves its detours are quiescent, then the hooks are
// removed newest first. Every removal is attempted; a hook MinHook cannot
// remove stays recorded so a later retry still owns it - a trampoline that may
// be executing is never freed.
//
// Each hook may carry its trampoline (the original-function pointer MinHook
// returned), so a title can prove its detours are quiescent without a second,
// parallel bookkeeping array.
//
// The MinHook calls are passed in so the bookkeeping is testable on its own.
#include <MinHook.h>
#include <cstddef>

template <size_t Capacity>
class HookRegistry
{
public:
    static constexpr size_t kCapacity = Capacity;

    // Records a target (and its trampoline) once; null and duplicates are
    // ignored, and a full registry refuses the extra target (the title's own
    // install budget is sized so that never happens). Returns whether the target
    // is recorded afterwards.
    bool Remember(void* target, const void* trampoline = nullptr)
    {
        if (!target)
            return false;
        for (size_t i = 0; i < count_; ++i)
            if (hooks_[i] == target)
                return true;
        if (count_ >= Capacity)
            return false;
        trampolines_[count_] = trampoline;
        hooks_[count_++] = target;
        return true;
    }

    // Drops a target the install just rolled back (MinHook already removed it).
    void Forget(void* target)
    {
        for (size_t i = 0; i < count_; ++i)
        {
            if (hooks_[i] != target)
                continue;
            for (size_t j = i + 1; j < count_; ++j)
            {
                hooks_[j - 1] = hooks_[j];
                trampolines_[j - 1] = trampolines_[j];
            }
            --count_;
            hooks_[count_] = nullptr;
            trampolines_[count_] = nullptr;
            return;
        }
    }

    size_t Count() const { return count_; }
    bool Full() const { return count_ >= Capacity; }
    void* At(size_t index) const { return index < count_ ? hooks_[index] : nullptr; }
    const void* TrampolineAt(size_t index) const { return index < count_ ? trampolines_[index] : nullptr; }

    // Disables every recorded hook. MH_OK, MH_ERROR_DISABLED and
    // MH_ERROR_NOT_CREATED count as disabled; anything else is reported through
    // onFailure(target, status) and makes the result false.
    template <class Disable, class OnFailure>
    bool DisableAll(Disable disable, OnFailure onFailure) const
    {
        bool disabled = true;
        for (size_t i = 0; i < count_; ++i)
        {
            if (!hooks_[i])
                continue;
            const MH_STATUS status = disable(hooks_[i]);
            if (status != MH_OK && status != MH_ERROR_DISABLED && status != MH_ERROR_NOT_CREATED)
            {
                onFailure(hooks_[i], status);
                disabled = false;
            }
        }
        return disabled;
    }

    // Removes hooks newest first, attempting every one. MH_OK and
    // MH_ERROR_NOT_CREATED forget the hook; any other status is reported through
    // onFailure(target, status) and keeps that hook recorded (in order) so a
    // later retry still owns it. Returns true when the registry is now empty.
    // (One policy for every title: Reach and ODST always kept going past a
    // failure; Halo 3 used to stop at the first one, which left older,
    // removable hooks in place for no benefit.)
    template <class Remove, class OnFailure>
    bool RemoveAllNewestFirst(Remove remove, OnFailure onFailure)
    {
        bool removedAll = true;
        for (size_t i = count_; i > 0; --i)
        {
            if (!hooks_[i - 1])
                continue;
            const MH_STATUS status = remove(hooks_[i - 1]);
            if (status != MH_OK && status != MH_ERROR_NOT_CREATED)
            {
                onFailure(hooks_[i - 1], status);
                removedAll = false;
                continue;
            }
            hooks_[i - 1] = nullptr;
        }
        size_t kept = 0;
        for (size_t i = 0; i < count_; ++i)
        {
            if (!hooks_[i])
                continue;
            hooks_[kept] = hooks_[i];
            trampolines_[kept] = trampolines_[i];
            ++kept;
        }
        for (size_t i = kept; i < count_; ++i)
        {
            hooks_[i] = nullptr;
            trampolines_[i] = nullptr;
        }
        count_ = kept;
        return removedAll;
    }

private:
    void* hooks_[Capacity]{};
    const void* trampolines_[Capacity]{};
    size_t count_ = 0;
};
